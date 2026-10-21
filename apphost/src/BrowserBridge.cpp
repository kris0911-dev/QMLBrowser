#include "BrowserBridge.h"

#include "InputInjector.h"
#include "IpcChannel.h"
#include "WindowCompositor.h"

#include <QCoreApplication>
#include <QLocalSocket>
#include <QTimer>
#include <QVariant>
#include <QWidget>

namespace {

void dumpWidget(QString &out, const QWidget *widget, int depth)
{
    if (depth > 14)
        return;

    QString line = QString(depth * 2, QLatin1Char(' '))
            + QLatin1String(widget->metaObject()->className());
    const QString name = widget->objectName();
    if (!name.isEmpty() && !name.startsWith(QLatin1String("qt_")))
        line += QLatin1String(" #") + name;

    const QVariant text = widget->property("text");
    if (text.typeId() == QMetaType::QString && !text.toString().isEmpty()) {
        QString shown = text.toString().simplified();
        if (shown.size() > 40)
            shown = shown.left(39) + QChar(0x2026);
        line += QLatin1String(" \"") + shown + QLatin1Char('"');
    }
    if (widget->isWindow() && !widget->windowTitle().isEmpty())
        line += QLatin1String(" \"") + widget->windowTitle() + QLatin1Char('"');

    const QRect g = widget->geometry();
    line += QStringLiteral("  %1,%2 %3×%4").arg(g.x()).arg(g.y()).arg(g.width()).arg(g.height());
    if (!widget->isVisible())
        line += QLatin1String("  hidden");
    out += line + QLatin1Char('\n');

    for (const QObject *child : widget->children()) {
        if (!child->isWidgetType())
            continue;
        const auto *childWidget = static_cast<const QWidget *>(child);
        if (!childWidget->isWindow())
            dumpWidget(out, childWidget, depth + 1);
    }
}

} // namespace

BrowserBridge::BrowserBridge(QWidget *mainWindow, bool gpu, QObject *parent)
    : QObject(parent)
    , m_window(mainWindow)
{
    m_windows = new WindowCompositor(mainWindow, gpu, this);
    m_input = new InputInjector(m_windows, this);

    connect(m_windows, &WindowCompositor::frameReady, this, &BrowserBridge::sendFrame);
    connect(m_input, &InputInjector::cursorChanged, this, [this](int shape) {
        QJsonObject message;
        message.insert(QStringLiteral("type"), QStringLiteral("cursor"));
        message.insert(QStringLiteral("shape"), shape);
        send(message);
    });
    connect(m_input, &InputInjector::inputMethodChanged, this, [this](const QJsonObject &state) {
        QJsonObject message = state;
        message.insert(QStringLiteral("type"), QStringLiteral("ime"));
        send(message);
    });
    connect(m_window, &QWidget::windowTitleChanged, this, &BrowserBridge::publishState);
}

BrowserBridge::~BrowserBridge() = default;

bool BrowserBridge::connectToBrowser(const QString &channelName, int tabId)
{
    auto *socket = new QLocalSocket(this);
    m_channel = new IpcChannel(socket, this);

    // Queued, not direct. A click can open a modal dialog or a menu, whose
    // exec() runs a nested event loop inside this handler, and QLocalSocket
    // does not emit readyRead again until the handler returns. Handled
    // directly, the app would stop hearing the browser (input, frame
    // acknowledgements) for as long as the dialog stayed open. Posted events
    // are delivered by the nested loop too.
    connect(m_channel, &IpcChannel::received, this, &BrowserBridge::onMessage,
            Qt::QueuedConnection);
    connect(m_channel, &IpcChannel::disconnected, qApp, [] {
        // The browser closed the tab, or is gone. Never outlive it.
        QCoreApplication::quit();
    });

    socket->connectToServer(channelName);
    if (!socket->waitForConnected(5000)) {
        qWarning("QmlBrowserHost: could not reach the browser process.");
        return false;
    }

    // waitForConnected() runs a nested loop. Say hello on the next turn so the
    // browser's first viewport message is not handled inside it.
    QTimer::singleShot(0, this, [this, tabId] {
        QJsonObject hello;
        hello.insert(QStringLiteral("type"), QStringLiteral("hello"));
        hello.insert(QStringLiteral("tab"), tabId);
        hello.insert(QStringLiteral("pid"), static_cast<double>(QCoreApplication::applicationPid()));
        send(hello);
        publishState();
        publishSource();
    });
    return true;
}

void BrowserBridge::send(const QJsonObject &message)
{
    if (m_channel)
        m_channel->send(message);
}

void BrowserBridge::onMessage(const QJsonObject &message)
{
    const QString type = message.value(QStringLiteral("type")).toString();

    if (type == QLatin1String("viewport")) {
        m_windows->setViewport(message.value(QStringLiteral("visible")).toBool(),
                               message.value(QStringLiteral("width")).toInt(),
                               message.value(QStringLiteral("height")).toInt(),
                               message.value(QStringLiteral("dpr")).toDouble());
    } else if (type == QLatin1String("input")) {
        m_input->deliver(message);
    } else if (type == QLatin1String("frameAck")) {
        m_windows->releaseFrame(quint32(message.value(QStringLiteral("seq")).toDouble()));
    } else if (type == QLatin1String("navigate")) {
        navigate(QUrl(message.value(QStringLiteral("url")).toString()));
    } else if (type == QLatin1String("chrome")) {
        // Same wording as the QML renderer. The reminder is drawn into the
        // frame, so it stays above whatever the app shows.
        QString notice;
        if (message.value(QStringLiteral("fullScreen")).toBool())
            notice = QStringLiteral("Press F11 or Esc to leave full screen");
        else if (message.value(QStringLiteral("chromeHidden")).toBool())
            notice = QStringLiteral("Press Ctrl+Shift+F or Esc to show the bars");
        m_windows->setNotice(notice);
    }
    // "reload" and "stop" need no handling: the browser restarts this process
    // to reload an app tab, and there is never a load in flight to stop.
}

void BrowserBridge::sendFrame(quint32 sequence, int width, int height, int stride,
                              const QString &key)
{
    QJsonObject message;
    message.insert(QStringLiteral("type"), QStringLiteral("frame"));
    message.insert(QStringLiteral("seq"), static_cast<double>(sequence));
    message.insert(QStringLiteral("width"), width);
    message.insert(QStringLiteral("height"), height);
    message.insert(QStringLiteral("stride"), stride);
    // Format 2: QImage::Format_ARGB32_Premultiplied.
    message.insert(QStringLiteral("format"), 2);
    message.insert(QStringLiteral("shm"), key);
    send(message);

    m_input->updateInputMethod();
    m_input->updateCursor();
}

// ----------------------------------------------------------------- routing

QString BrowserBridge::pathOf(const QUrl &url)
{
    QString path = url.path();
    while (path.startsWith(QLatin1Char('/')))
        path.remove(0, 1);
    return path;
}

void BrowserBridge::setNavigateHandler(NavigateHandler handler)
{
    m_navigateHandler = std::move(handler);
}

void BrowserBridge::navigate(const QUrl &url)
{
    // The browser keeps the history; this process only shows what the
    // address says. The application's own report of the switch is ignored
    // while it makes it, so it does not come back as a new history entry.
    m_url = url;
    if (m_navigateHandler) {
        m_navigating = true;
        m_navigateHandler(pathOf(url));
        m_navigating = false;
    }
    publishState();
}

void BrowserBridge::setPath(const QString &path)
{
    if (m_navigating || !m_url.isValid() || m_url.isEmpty())
        return;

    QString clean = path;
    while (clean.startsWith(QLatin1Char('/')))
        clean.remove(0, 1);
    if (clean == pathOf(m_url))
        return;

    // A place picked inside the app becomes a browser history entry, so Back
    // returns to the previous one.
    QUrl url = m_url;
    url.setPath(clean.isEmpty() ? QString() : QLatin1Char('/') + clean);
    url.setQuery(QString());
    url.setFragment(QString());
    m_url = url;

    QJsonObject message;
    message.insert(QStringLiteral("type"), QStringLiteral("navigate"));
    message.insert(QStringLiteral("url"), url.toString());
    send(message);
}

// ----------------------------------------------------------------- publish

void BrowserBridge::publishState()
{
    QJsonObject message;
    message.insert(QStringLiteral("type"), QStringLiteral("state"));
    message.insert(QStringLiteral("status"), 2); // Ready: there is nothing to load
    message.insert(QStringLiteral("title"), m_window->windowTitle());
    message.insert(QStringLiteral("url"), m_url.toString());
    message.insert(QStringLiteral("httpStatus"), 0);
    message.insert(QStringLiteral("progress"), 1.0);
    message.insert(QStringLiteral("error"), QString());
    message.insert(QStringLiteral("bytes"), 0);
    send(message);
}

void BrowserBridge::publishSource()
{
    // There is no document behind this tab. View source shows the live
    // widget tree instead, which is the closest thing to one.
    QString text = QStringLiteral(
            "// This tab is a native Qt Widgets process, not a QML document.\n"
            "// It runs on Qt's offscreen platform and paints every top-level\n"
            "// widget into the frame this tab shows. Its widget tree at startup:\n\n");
    dumpWidget(text, m_window, 0);

    QJsonObject message;
    message.insert(QStringLiteral("type"), QStringLiteral("source"));
    message.insert(QStringLiteral("text"), text);
    send(message);
}
