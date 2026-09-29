#include "BrowserTab.h"

#include "IpcChannel.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonObject>
#include <QProcess>
#include <QThread>
#include <QTimer>

namespace {

// How long a renderer gets to shut itself down before it is killed outright.
constexpr int kExitGraceMs = 2000;

// Lets a renderer wind down without the browser's UI thread waiting on it.
// Closing the channel is the request to exit; this only escalates if the
// process is somehow still around afterwards.
void retireProcess(QProcess *process)
{
    if (!process)
        return;

    process->disconnect();
    process->setParent(QCoreApplication::instance());

    const auto stopNow = [process] {
        if (process->state() == QProcess::NotRunning)
            return;
        // The channel is already closed, which is the renderer's cue to quit.
        // Kill it if that has not happened, so ~QProcess never runs while the
        // child is still alive.
        if (!process->waitForFinished(200)) {
            process->kill();
            process->waitForFinished(kExitGraceMs);
        }
    };

    // Tabs are also torn down after the event loop has stopped, on the way out
    // of main(). Nothing can deliver finished() or fire the grace timer by
    // then, and ~QProcess would kill and wait without a timeout, so do it here.
    const bool synchronous = QThread::currentThread()->loopLevel() == 0
            || !QCoreApplication::instance()
            || QCoreApplication::closingDown();
    if (synchronous) {
        stopNow();
        delete process;
        return;
    }

    if (process->state() == QProcess::NotRunning) {
        process->deleteLater();
        return;
    }

    QObject::connect(process, &QProcess::finished, process, &QObject::deleteLater);
    // Closing the last tab quits the process while this QProcess is still a
    // child of the application. aboutToQuit runs before that child is
    // destroyed, which is the last chance to wait for the renderer. Drop the
    // finished handler first: waitForFinished() runs a nested loop, and
    // deleteLater from finished() would free this object mid-call.
    QObject::connect(QCoreApplication::instance(), &QCoreApplication::aboutToQuit,
                     process, [process] {
        QObject::disconnect(process, &QProcess::finished, nullptr, nullptr);
        if (process->state() == QProcess::NotRunning)
            return;
        if (!process->waitForFinished(200)) {
            process->kill();
            process->waitForFinished(kExitGraceMs);
        }
    });
    QTimer::singleShot(kExitGraceMs, process, [process] {
        if (process->state() != QProcess::NotRunning)
            process->kill();
    });
}

} // namespace

BrowserTab::BrowserTab(int id, const QString &channelName, WId hostWindow, QObject *parent)
    : QObject(parent)
    , m_id(id)
    , m_channelName(channelName)
    , m_hostWindow(hostWindow)
    , m_history(new BrowserHistory(this))
{
    connect(m_history, &BrowserHistory::currentUrlChanged, this, &BrowserTab::sendCurrentUrl);
    connect(m_history, &BrowserHistory::changed, this, &BrowserTab::stateChanged);

    startProcess();
}

BrowserTab::~BrowserTab()
{
    m_closing = true;
    releaseRenderer();
}

void BrowserTab::releaseRenderer()
{
    // Dropping the channel is what tells the renderer to go: it quits as soon
    // as the socket closes. QProcess::terminate() cannot deliver that message,
    // because on Windows it posts WM_CLOSE to top-level windows only, and this
    // renderer's window is a child of the browser's.
    if (m_channel) {
        m_channel->disconnect(this);
        m_channel->close();
        m_channel = nullptr;
    }

    m_childWindow = 0;
    m_rendererPid = 0;

    retireProcess(m_process);
    m_process = nullptr;
}

QString BrowserTab::title() const
{
    if (m_crashed)
        return QStringLiteral("Tab crashed");
    if (!m_title.isEmpty())
        return m_title;
    const QUrl current = m_history->currentUrl();
    if (current.isValid() && !current.isEmpty())
        return current.host();
    return QStringLiteral("New tab");
}

namespace {

// Next to this executable on Windows. On macOS the CMake build puts each
// program in its own bundle, side by side in the build directory.
QString rendererExecutable()
{
    const QDir dir(QCoreApplication::applicationDirPath());
#if defined(Q_OS_WIN)
    return dir.absoluteFilePath(QStringLiteral("QmlRenderer.exe"));
#elif defined(Q_OS_MACOS)
    const QString beside = dir.absoluteFilePath(QStringLiteral("QmlRenderer"));
    if (QFileInfo::exists(beside))
        return beside;

    QDir root(dir);
    if (root.cdUp() && root.cdUp() && root.cdUp()) {
        const QString bundled = root.absoluteFilePath(
                QStringLiteral("QmlRenderer.app/Contents/MacOS/QmlRenderer"));
        if (QFileInfo::exists(bundled))
            return bundled;
    }
    return beside;
#else
    return dir.absoluteFilePath(QStringLiteral("QmlRenderer"));
#endif
}

} // namespace

void BrowserTab::startProcess()
{
    m_crashed = false;
    m_childWindow = 0;
    m_rendererPid = 0;
    m_placedVisible = false;
    m_placedRect = QRect();
    m_aboveWindow = 0;

    const QString program = rendererExecutable();

    m_process = new QProcess(this);
    m_process->setProgram(program);
    m_process->setArguments({ QStringLiteral("--channel"), m_channelName,
                              QStringLiteral("--tab"), QString::number(m_id),
                              QStringLiteral("--parent"), QString::number(quint64(m_hostWindow)) });
    // Renderer diagnostics belong in the browser's console, like chrome's stderr.
    m_process->setProcessChannelMode(QProcess::ForwardedErrorChannel);

    connect(m_process, &QProcess::finished, this, &BrowserTab::onProcessFinished);

    m_process->start();
    emit stateChanged();
}

void BrowserTab::onProcessFinished(int exitCode, int status)
{
    if (m_closing)
        return;

    m_channel = nullptr;
    m_childWindow = 0;
    m_rendererPid = 0;
    m_crashed = true;
    m_status = Error;
    m_progress = 1.0;
    m_errorString = status == QProcess::CrashExit
            ? QStringLiteral("The renderer process for this tab terminated unexpectedly.")
            : QStringLiteral("The renderer process for this tab exited with code %1.").arg(exitCode);

    emit stateChanged();
}

bool BrowserTab::isAttached() const
{
    return m_channel != nullptr && m_childWindow != 0;
}

void BrowserTab::attach(IpcChannel *channel, WId childWindow, qint64 pid)
{
    m_channel = channel;
    m_childWindow = childWindow;
    m_rendererPid = pid;
    m_crashed = false;

    connect(channel, &IpcChannel::received, this, &BrowserTab::onMessage);

    emit attached();
    emit stateChanged();

    // A renderer that has just started knows nothing about the window it landed
    // in, so tell it before handing over the URL.
    sendChromeState();

    // The renderer came up after the URL was chosen, so deliver it now.
    if (m_history->currentUrl().isValid())
        sendCurrentUrl();
}

void BrowserTab::setChromeFullScreen(bool on)
{
    if (m_chromeFullScreen == on)
        return;
    m_chromeFullScreen = on;
    sendChromeState();
}

void BrowserTab::setChromeHidden(bool hidden)
{
    if (m_chromeHidden == hidden)
        return;
    m_chromeHidden = hidden;
    sendChromeState();
}

void BrowserTab::place(bool visible, const QRect &screenRect, qint64 aboveWindow, bool force,
                       bool raisePage)
{
    if (!m_channel)
        return;
    if (!force && visible == m_placedVisible && screenRect == m_placedRect
            && aboveWindow == m_aboveWindow && raisePage == m_raisePage)
        return;

    m_placedVisible = visible;
    m_placedRect = screenRect;
    m_aboveWindow = aboveWindow;
    m_raisePage = raisePage;

    QJsonObject message;
    message.insert(QStringLiteral("type"), QStringLiteral("place"));
    message.insert(QStringLiteral("visible"), visible);
    message.insert(QStringLiteral("x"), screenRect.x());
    message.insert(QStringLiteral("y"), screenRect.y());
    message.insert(QStringLiteral("width"), screenRect.width());
    message.insert(QStringLiteral("height"), screenRect.height());
    // A double keeps an X11 window id intact. JSON integers are 32-bit, and a
    // window id can have the high bit set. macOS window numbers still fit.
    message.insert(QStringLiteral("above"), static_cast<double>(aboveWindow));
    message.insert(QStringLiteral("raise"), raisePage);
    m_channel->send(message);
}

void BrowserTab::sendChromeState()
{
    if (!m_channel)
        return;

    QJsonObject message;
    message.insert(QStringLiteral("type"), QStringLiteral("chrome"));
    message.insert(QStringLiteral("fullScreen"), m_chromeFullScreen);
    message.insert(QStringLiteral("chromeHidden"), m_chromeHidden);
    m_channel->send(message);
}

void BrowserTab::onMessage(const QJsonObject &message)
{
    const QString type = message.value(QStringLiteral("type")).toString();

    if (type == QLatin1String("state")) {
        m_status = message.value(QStringLiteral("status")).toInt();
        m_title = message.value(QStringLiteral("title")).toString();
        m_httpStatus = message.value(QStringLiteral("httpStatus")).toInt();
        m_progress = message.value(QStringLiteral("progress")).toDouble();
        m_errorString = message.value(QStringLiteral("error")).toString();
        m_byteCount = message.value(QStringLiteral("bytes")).toInt();
        emit stateChanged();
    } else if (type == QLatin1String("navigate")) {
        // A link inside the page. History lives here, not in the renderer.
        navigateTo(QUrl(message.value(QStringLiteral("url")).toString()));
    } else if (type == QLatin1String("source")) {
        m_sourceText = message.value(QStringLiteral("text")).toString();
        emit sourceTextChanged();
    } else if (type == QLatin1String("shortcut")) {
        emit shortcutRequested(message.value(QStringLiteral("key")).toString());
    } else if (type == QLatin1String("fullscreen")) {
        emit fullScreenRequested(message.value(QStringLiteral("on")).toBool());
    }
}

void BrowserTab::sendCurrentUrl()
{
    emit stateChanged();

    if (!m_channel)
        return;

    QJsonObject message;
    message.insert(QStringLiteral("type"), QStringLiteral("navigate"));
    message.insert(QStringLiteral("url"), m_history->currentUrl().toString());
    m_channel->send(message);
}

void BrowserTab::navigateTo(const QUrl &url)
{
    if (m_crashed) {
        // Reviving the tab picks the URL up once the new renderer connects.
        m_history->navigateTo(url);
        restart();
        return;
    }

    if (url == m_history->currentUrl())
        reload();
    else
        m_history->navigateTo(url);
}

void BrowserTab::navigateToText(const QString &text)
{
    navigateTo(m_history->resolve(text));
}

void BrowserTab::reload()
{
    if (m_crashed) {
        restart();
        return;
    }
    if (m_channel)
        m_channel->send({ { QStringLiteral("type"), QStringLiteral("reload") } });
}

void BrowserTab::stop()
{
    if (m_channel)
        m_channel->send({ { QStringLiteral("type"), QStringLiteral("stop") } });
}

void BrowserTab::back()
{
    m_history->back();
}

void BrowserTab::forward()
{
    m_history->forward();
}

void BrowserTab::restart()
{
    releaseRenderer();

    m_status = Null;
    m_errorString.clear();
    startProcess();
}
