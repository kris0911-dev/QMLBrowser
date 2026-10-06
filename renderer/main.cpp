#include "IpcChannel.h"
#include "OffscreenPage.h"
#include "PageView.h"

#include <QCommandLineParser>
#include <QGuiApplication>
#include <QJsonObject>
#include <QLocalSocket>
#include <QTimer>

#ifdef Q_OS_MACOS
#  include <objc/message.h>
#  include <objc/runtime.h>
#endif

namespace {

#ifdef Q_OS_MACOS
// Accessory: no Dock icon. A tab is not an application of its own.
void hideFromDock()
{
    using SendId = id (*)(id, SEL);
    auto msg = reinterpret_cast<SendId>(objc_msgSend);
    id nsApp = msg(reinterpret_cast<id>(objc_getClass("NSApplication")),
                   sel_getUid("sharedApplication"));
    if (!nsApp)
        return;
    const long policy = reinterpret_cast<long (*)(id, SEL)>(objc_msgSend)(
            nsApp, sel_getUid("activationPolicy"));
    if (policy == 1L)
        return;
    // NSApplicationActivationPolicyAccessory == 1
    reinterpret_cast<BOOL (*)(id, SEL, long)>(objc_msgSend)(
            nsApp, sel_getUid("setActivationPolicy:"), 1L);
}
#endif

QJsonObject stateMessage(PageView *page)
{
    QJsonObject message;
    message.insert(QStringLiteral("type"), QStringLiteral("state"));
    message.insert(QStringLiteral("status"), int(page->status()));
    message.insert(QStringLiteral("title"), page->pageTitle());
    message.insert(QStringLiteral("url"), page->url().toString());
    message.insert(QStringLiteral("httpStatus"), page->httpStatus());
    message.insert(QStringLiteral("progress"), page->progress());
    message.insert(QStringLiteral("error"), page->errorString());
    message.insert(QStringLiteral("bytes"), page->sourceText().size());
    return message;
}

} // namespace

int main(int argc, char *argv[])
{
#ifdef Q_OS_MACOS
    // Qt activates a Gui application from applicationDidFinishLaunching unless
    // this is set. A tab's renderer would then become the front process, and
    // the browser window stays inactive until its title bar is clicked.
    qputenv("QT_MAC_DISABLE_FOREGROUND_APPLICATION_TRANSFORM", "1");
#endif
    QGuiApplication app(argc, argv);
    QGuiApplication::setApplicationName(QStringLiteral("QmlRenderer"));
#ifdef Q_OS_MACOS
    hideFromDock();
#endif

    QCommandLineParser parser;
    parser.setApplicationDescription(
            QStringLiteral("Renders one browser tab. Launched by QmlBrowser.exe."));
    parser.addHelpOption();

    QCommandLineOption channelOption(QStringLiteral("channel"),
                                     QStringLiteral("Name of the browser process's local socket."),
                                     QStringLiteral("name"));
    QCommandLineOption tabOption(QStringLiteral("tab"), QStringLiteral("Tab identifier."),
                                 QStringLiteral("id"));
    parser.addOption(channelOption);
    parser.addOption(tabOption);
    parser.process(app);

    if (!parser.isSet(channelOption)) {
        qWarning("QmlRenderer is started by QmlBrowser.exe; run that instead.");
        return 2;
    }

    qmlRegisterType<PageView>("QmlRenderer", 1, 0, "PageView");

    OffscreenPage page;
    if (!page.load())
        return 1;

    auto *socket = new QLocalSocket(&app);
    auto *channel = new IpcChannel(socket, &app);
    const int tabId = parser.value(tabOption).toInt();

    QObject::connect(channel, &IpcChannel::disconnected, &app, [] {
        // The browser process is gone; never outlive it.
        QCoreApplication::quit();
    });

    auto *pump = new QTimer(&app);
    pump->setSingleShot(true);
    pump->setInterval(0);
    QObject::connect(pump, &QTimer::timeout, &app,
                     [channel, &page] { channel->send(stateMessage(page.page())); });

    const auto scheduleUpdate = [pump] { pump->start(); };
    PageView *document = page.page();
    QObject::connect(document, &PageView::statusChanged, &app, scheduleUpdate);
    QObject::connect(document, &PageView::pageTitleChanged, &app, scheduleUpdate);
    QObject::connect(document, &PageView::urlChanged, &app, scheduleUpdate);
    QObject::connect(document, &PageView::httpStatusChanged, &app, scheduleUpdate);
    QObject::connect(document, &PageView::progressChanged, &app, scheduleUpdate);
    QObject::connect(document, &PageView::errorStringChanged, &app, scheduleUpdate);

    QObject::connect(document, &PageView::navigationRequested, &app, [channel](const QUrl &url) {
        QJsonObject message;
        message.insert(QStringLiteral("type"), QStringLiteral("navigate"));
        message.insert(QStringLiteral("url"), url.toString());
        channel->send(message);
    });

    QObject::connect(document, &PageView::fullScreenRequested, &app, [channel](bool on) {
        QJsonObject message;
        message.insert(QStringLiteral("type"), QStringLiteral("fullscreen"));
        message.insert(QStringLiteral("on"), on);
        channel->send(message);
    });

    QObject::connect(document, &PageView::sourceTextChanged, &app, [channel, document] {
        QJsonObject message;
        message.insert(QStringLiteral("type"), QStringLiteral("source"));
        message.insert(QStringLiteral("text"), document->sourceText());
        channel->send(message);
    });

    QObject::connect(&page, &OffscreenPage::frameReady, &app,
                     [channel](quint32 sequence, int width, int height, int stride, int format,
                               const QString &key) {
                         QJsonObject message;
                         message.insert(QStringLiteral("type"), QStringLiteral("frame"));
                         message.insert(QStringLiteral("seq"), static_cast<double>(sequence));
                         message.insert(QStringLiteral("width"), width);
                         message.insert(QStringLiteral("height"), height);
                         message.insert(QStringLiteral("stride"), stride);
                         message.insert(QStringLiteral("format"), format);
                         message.insert(QStringLiteral("shm"), key);
                         channel->send(message);
                     });

    QObject::connect(&page, &OffscreenPage::cursorChanged, &app, [channel](int shape) {
        QJsonObject message;
        message.insert(QStringLiteral("type"), QStringLiteral("cursor"));
        message.insert(QStringLiteral("shape"), shape);
        channel->send(message);
    });

    QQuickItem *root = page.rootItem();
    QObject::connect(channel, &IpcChannel::received, &app,
                     [&page, root](const QJsonObject &message) {
                         const QString type = message.value(QStringLiteral("type")).toString();
                         if (type == QLatin1String("viewport")) {
                             page.setViewport(message.value(QStringLiteral("visible")).toBool(),
                                              message.value(QStringLiteral("width")).toInt(),
                                              message.value(QStringLiteral("height")).toInt(),
                                              message.value(QStringLiteral("dpr")).toDouble());
                         } else if (type == QLatin1String("input")) {
                             page.deliverInput(message);
                         } else if (type == QLatin1String("frameAck")) {
                             page.releaseSequence(
                                     quint32(message.value(QStringLiteral("seq")).toDouble()));
                         } else if (type == QLatin1String("navigate")) {
                             page.page()->setUrl(QUrl(message.value(QStringLiteral("url")).toString()));
                         } else if (type == QLatin1String("reload")) {
                             page.page()->reload();
                         } else if (type == QLatin1String("stop")) {
                             page.page()->stop();
                         } else if (type == QLatin1String("chrome")) {
                             // The notice is part of the page frame. The browser
                             // composites that frame, so this is how the reminder
                             // stays on top of the document.
                             const bool fullScreen =
                                     message.value(QStringLiteral("fullScreen")).toBool();
                             const bool chromeHidden =
                                     message.value(QStringLiteral("chromeHidden")).toBool();
                             QString notice;
                             if (fullScreen)
                                 notice = QStringLiteral("Press F11 or Esc to leave full screen");
                             else if (chromeHidden)
                                 notice = QStringLiteral("Press Ctrl+Shift+F or Esc to show "
                                                         "the bars");
                             if (root)
                                 root->setProperty("notice", notice);
                         }
                     });

    socket->connectToServer(parser.value(channelOption));
    if (!socket->waitForConnected(5000)) {
        qWarning("Could not reach the browser process.");
        return 1;
    }

    // waitForConnected() pumps a nested event loop. Announce the tab on the
    // next turn so the browser's first viewport message is not handled inside
    // that loop.
    QTimer::singleShot(0, &app, [channel, tabId] {
        QJsonObject hello;
        hello.insert(QStringLiteral("type"), QStringLiteral("hello"));
        hello.insert(QStringLiteral("tab"), tabId);
        hello.insert(QStringLiteral("pid"), QCoreApplication::applicationPid());
        channel->send(hello);
    });

    return app.exec();
}
