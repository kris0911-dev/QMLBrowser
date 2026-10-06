#include "BrowserTab.h"

#include "IpcChannel.h"
#include "RendererShutdown.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonObject>
#include <QProcess>

#include <cstring>

BrowserTab::BrowserTab(int id, const QString &channelName, QObject *parent)
    : QObject(parent)
    , m_id(id)
    , m_channelName(channelName)
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
    // as the socket closes. There is no window to post WM_CLOSE to.
    if (m_channel) {
        m_channel->disconnect(this);
        m_channel->close();
        m_channel = nullptr;
    }

    m_rendererPid = 0;
    m_pixels.detach();

    QProcess *process = m_process;
    m_process = nullptr;
    RendererShutdown::retire(process);
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
    m_rendererPid = 0;
    m_viewportValid = false;

    const QString program = rendererExecutable();

    m_process = new QProcess(this);
    RendererShutdown::watch(m_process);
    m_process->setProgram(program);
    m_process->setArguments({ QStringLiteral("--channel"), m_channelName,
                              QStringLiteral("--tab"), QString::number(m_id) });
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
    m_rendererPid = 0;
    m_pixels.detach();
    m_crashed = true;
    m_status = Error;
    m_progress = 1.0;
    m_errorString = status == QProcess::CrashExit
            ? QStringLiteral("The renderer process for this tab terminated unexpectedly.")
            : QStringLiteral("The renderer process for this tab exited with code %1.").arg(exitCode);

    emit stateChanged();
}

void BrowserTab::attach(IpcChannel *channel, qint64 pid)
{
    m_channel = channel;
    RendererShutdown::bindChannel(m_process, channel);
    m_rendererPid = pid;
    m_crashed = false;

    connect(channel, &IpcChannel::received, this, &BrowserTab::onMessage);

    emit attached();
    emit stateChanged();

    sendChromeState();
    sendViewport();

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

void BrowserTab::setViewport(bool visible, const QSize &logicalSize, qreal dpr)
{
    if (dpr < 0.5)
        dpr = 1;
    if (logicalSize.width() < 1 || logicalSize.height() < 1)
        visible = false;

    if (m_viewportValid && visible == m_viewportVisible && logicalSize == m_viewportSize
            && qAbs(dpr - m_viewportDpr) < 0.001)
        return;

    m_viewportValid = logicalSize.width() > 0 && logicalSize.height() > 0;
    m_viewportVisible = visible;
    m_viewportSize = logicalSize;
    m_viewportDpr = dpr;
    sendViewport();
}

void BrowserTab::sendViewport()
{
    if (!m_channel || !m_viewportValid)
        return;

    QJsonObject message;
    message.insert(QStringLiteral("type"), QStringLiteral("viewport"));
    message.insert(QStringLiteral("visible"), m_viewportVisible);
    message.insert(QStringLiteral("width"), m_viewportSize.width());
    message.insert(QStringLiteral("height"), m_viewportSize.height());
    message.insert(QStringLiteral("dpr"), m_viewportDpr);
    m_channel->send(message);
}

void BrowserTab::postInput(const QJsonObject &event)
{
    if (!m_channel)
        return;

    QJsonObject message = event;
    message.insert(QStringLiteral("type"), QStringLiteral("input"));
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

void BrowserTab::onFrame(const QJsonObject &message)
{
    const int width = message.value(QStringLiteral("width")).toInt();
    const int height = message.value(QStringLiteral("height")).toInt();
    const int stride = message.value(QStringLiteral("stride")).toInt();
    const int format = message.value(QStringLiteral("format")).toInt();
    const quint32 sequence = quint32(message.value(QStringLiteral("seq")).toDouble());
    const QString key = message.value(QStringLiteral("shm")).toString();

    const auto acknowledge = [this, sequence] {
        if (!m_channel)
            return;
        QJsonObject ack;
        ack.insert(QStringLiteral("type"), QStringLiteral("frameAck"));
        ack.insert(QStringLiteral("seq"), static_cast<double>(sequence));
        m_channel->send(ack);
    };

    const qint64 bytes = qint64(stride) * height;
    if (width < 1 || height < 1 || stride < width * 4 || key.isEmpty() || !m_pixels.attach(key)
            || bytes > m_pixels.size() || !m_pixels.lock()) {
        acknowledge();
        return;
    }

    const QImage::Format imageFormat = format == 2 ? QImage::Format_ARGB32_Premultiplied
                                                   : QImage::Format_RGBA8888_Premultiplied;
    QImage image(width, height, imageFormat);
    const uchar *source = m_pixels.data();
    const int rowBytes = width * 4;
    if (image.bytesPerLine() == stride) {
        memcpy(image.bits(), source, size_t(stride) * size_t(height));
    } else {
        for (int y = 0; y < height; ++y)
            memcpy(image.scanLine(y), source + y * stride, size_t(rowBytes));
    }
    m_pixels.unlock();

    m_frame = image;
    emit frameChanged();
    acknowledge();
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
    } else if (type == QLatin1String("frame")) {
        onFrame(message);
    } else if (type == QLatin1String("cursor")) {
        const int shape = message.value(QStringLiteral("shape")).toInt();
        if (shape == m_cursorShape)
            return;
        m_cursorShape = shape;
        emit cursorChanged(shape);
    } else if (type == QLatin1String("navigate")) {
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
    m_frame = QImage();
    emit frameChanged();
    startProcess();
}
