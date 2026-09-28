#include "IpcChannel.h"
#include "PageView.h"

#include <QAbstractNativeEventFilter>
#include <QColor>
#include <QCommandLineParser>
#include <QGuiApplication>
#include <QJsonObject>
#include <QKeyEvent>
#include <QLocalSocket>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickView>
#include <QRect>

#ifdef Q_OS_MACOS
#  include <objc/message.h>
#  include <objc/runtime.h>
#  include <unistd.h>
#endif
#include <QTimer>
#include <QWindow>

#ifdef Q_OS_WIN
#  define WIN32_LEAN_AND_MEAN
#  define NOMINMAX
#  include <windows.h>
#endif

namespace {

#ifdef Q_OS_MACOS
// Accessory: no Dock icon and no menu bar. Regular would add one icon per tab.
void hideFromDock()
{
    using SendId = id (*)(id, SEL);
    auto msg = reinterpret_cast<SendId>(objc_msgSend);
    id nsApp = msg(reinterpret_cast<id>(objc_getClass("NSApplication")),
                   sel_getUid("sharedApplication"));
    if (!nsApp)
        return;
    // Already an accessory. Setting the policy again while this process is
    // active resigns it, and the browser stays inactive until its title bar
    // is clicked.
    const long policy = reinterpret_cast<long (*)(id, SEL)>(objc_msgSend)(
            nsApp, sel_getUid("activationPolicy"));
    if (policy == 1L)
        return;
    const BOOL active = reinterpret_cast<BOOL (*)(id, SEL)>(objc_msgSend)(
            nsApp, sel_getUid("isActive"));
    if (active)
        return;
    // NSApplicationActivationPolicyAccessory == 1
    reinterpret_cast<BOOL (*)(id, SEL, long)>(objc_msgSend)(
            nsApp, sel_getUid("setActivationPolicy:"), 1L);
}

id pageNsWindow(QWindow *window)
{
    if (!window)
        return nil;
    window->create();
    using SendId = id (*)(id, SEL);
    auto msg = reinterpret_cast<SendId>(objc_msgSend);
    id nsView = reinterpret_cast<id>(window->winId());
    if (!nsView)
        return nil;
    return msg(nsView, sel_getUid("window"));
}

using CanBecomeKeyFn = BOOL (*)(id, SEL);
CanBecomeKeyFn originalCanBecomeKey = nullptr;

BOOL pageCannotBecomeKey(id, SEL)
{
    return NO;
}

void setPageRefusesKey(bool refuse)
{
    Class panel = objc_getClass("QNSPanel");
    if (!panel)
        return;
    Method method = class_getInstanceMethod(panel, sel_getUid("canBecomeKeyWindow"));
    if (!method)
        return;
    if (!originalCanBecomeKey)
        originalCanBecomeKey = reinterpret_cast<CanBecomeKeyFn>(method_getImplementation(method));
    method_setImplementation(method,
                             refuse ? reinterpret_cast<IMP>(pageCannotBecomeKey)
                                    : reinterpret_cast<IMP>(originalCanBecomeKey));
}

// The page sits above the browser, but it must not become the key window until
// the user actually clicks it. show() and orderFrontRegardless otherwise activate
// this process a moment later, and the browser stays inactive until its title
// bar is clicked.
class TakeKeyOnPress : public QObject
{
public:
    explicit TakeKeyOnPress(QWindow *window)
        : QObject(window), m_window(window) {}

protected:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (event->type() != QEvent::MouseButtonPress)
            return QObject::eventFilter(watched, event);
        setPageRefusesKey(false);
        if (m_window)
            m_window->requestActivate();
        if (watched)
            watched->removeEventFilter(this);
        deleteLater();
        return false;
    }

private:
    QWindow *m_window = nullptr;
};

// show() on a Qt::Tool NSPanel calls makeKeyAndOrderFront, which makes this
// process the active app. The browser then looks inactive until its title bar
// is clicked. becomesKeyOnlyIfNeeded makes that show use orderFront instead.
void showWithoutTakingKey(QWindow *window)
{
    id nsWindow = pageNsWindow(window);
    setPageRefusesKey(true);
    SEL setKey = sel_getUid("setBecomesKeyOnlyIfNeeded:");
    const bool panel = nsWindow && reinterpret_cast<BOOL (*)(id, SEL, SEL)>(objc_msgSend)(
            nsWindow, sel_getUid("respondsToSelector:"), setKey);
    if (panel) {
        reinterpret_cast<void (*)(id, SEL, BOOL)>(objc_msgSend)(
                nsWindow, sel_getUid("setHidesOnDeactivate:"), NO);
        reinterpret_cast<void (*)(id, SEL, BOOL)>(objc_msgSend)(nsWindow, setKey, YES);
    }
    window->show();
    if (panel)
        reinterpret_cast<void (*)(id, SEL, BOOL)>(objc_msgSend)(nsWindow, setKey, NO);
    window->installEventFilter(new TakeKeyOnPress(window));
}

// Qt's raise() calls orderFront, which a background process cannot use to
// cover the active browser. The page then sits behind the window that asked
// for it, and the tab looks empty until a later hide/show.
// orderFrontRegardless is what actually covers the browser, and it also makes
// AppKit activate this process a moment later. Pass false when only the level
// has to be restored after giving activation back.
void stackPage(QWindow *window, int aboveWindowNumber, bool forceFront)
{
    id nsWindow = pageNsWindow(window);
    if (!nsWindow)
        return;

    using SendVoidLong = void (*)(id, SEL, long);
    // NSFloatingWindowLevel. At normal level the page is covered by the browser.
    reinterpret_cast<SendVoidLong>(objc_msgSend)(nsWindow, sel_getUid("setLevel:"), 3L);
    // The renderer process is not the active app. A tool window that hides on
    // deactivate never appears over the browser that opened the tab.
    reinterpret_cast<void (*)(id, SEL, BOOL)>(objc_msgSend)(
            nsWindow, sel_getUid("setHidesOnDeactivate:"), NO);
    if (aboveWindowNumber > 0) {
        // NSWindowAbove == 1.
        reinterpret_cast<void (*)(id, SEL, long, long)>(objc_msgSend)(
                nsWindow, sel_getUid("orderWindow:relativeTo:"), 1L, long(aboveWindowNumber));
    }
    if (forceFront) {
        reinterpret_cast<void (*)(id, SEL, id)>(objc_msgSend)(
                nsWindow, sel_getUid("orderFrontRegardless"), nil);
    }
    // Showing a window promotes the process back to a regular app, which puts
    // another icon in the Dock. Put the accessory policy back afterwards.
    hideFromDock();
}

void orderPageAbove(QWindow *window, int aboveWindowNumber)
{
    stackPage(window, aboveWindowNumber, true);
}

void activateBrowser()
{
    id runningApp = reinterpret_cast<id>(objc_getClass("NSRunningApplication"));
    id browser = runningApp ? reinterpret_cast<id (*)(id, SEL, int)>(objc_msgSend)(
            runningApp, sel_getUid("runningApplicationWithProcessIdentifier:"),
            int(getppid())) : nil;
    if (browser) {
        // NSApplicationActivateIgnoringOtherApps == 1 << 1
        reinterpret_cast<BOOL (*)(id, SEL, unsigned long)>(objc_msgSend)(
                browser, sel_getUid("activateWithOptions:"), 2UL);
    }
}

// orderFrontRegardless makes this process frontmost a moment after show, even
// when the page window cannot become key. Put the browser back in front.
// Limited to the show itself; a later click on the page is allowed to take over
// because these timers have already finished.
void keepBrowserInFront(QWindow *window, int aboveWindowNumber)
{
    const auto kick = [window, aboveWindowNumber] {
        activateBrowser();
        stackPage(window, aboveWindowNumber, false);
    };
    QTimer::singleShot(0, window, kick);
    QTimer::singleShot(40, window, kick);
    QTimer::singleShot(120, window, kick);
    QTimer::singleShot(250, window, kick);
}
#endif

// Browser-level keys are owned by the browser process, but while the user is
// interacting with a page the keyboard focus lives here. Catch that handful of
// combinations and hand them back rather than swallowing them.
class ShortcutForwarder : public QObject
{
public:
    ShortcutForwarder(IpcChannel *channel, QObject *parent)
        : QObject(parent), m_channel(channel) {}

    // Escape only leaves the page alone while the chrome is on screen; once the
    // bars or the window frame are gone it is the way back, so the browser
    // claims it.
    void setFullScreen(bool on) { m_fullScreen = on; }
    void setChromeHidden(bool hidden) { m_chromeHidden = hidden; }

protected:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (event->type() != QEvent::KeyPress)
            return QObject::eventFilter(watched, event);

        auto *key = static_cast<QKeyEvent *>(event);
        const Qt::KeyboardModifiers mods =
                key->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::ShiftModifier);

        QString name;
        if (mods == Qt::ControlModifier) {
            switch (key->key()) {
            case Qt::Key_T: name = QStringLiteral("Ctrl+T"); break;
            case Qt::Key_W: name = QStringLiteral("Ctrl+W"); break;
            case Qt::Key_L: name = QStringLiteral("Ctrl+L"); break;
            case Qt::Key_U: name = QStringLiteral("Ctrl+U"); break;
            case Qt::Key_R: name = QStringLiteral("Ctrl+R"); break;
            case Qt::Key_Tab: name = QStringLiteral("Ctrl+Tab"); break;
            default: break;
            }
        } else if (mods == (Qt::ControlModifier | Qt::ShiftModifier)) {
            if (key->key() == Qt::Key_Tab || key->key() == Qt::Key_Backtab)
                name = QStringLiteral("Ctrl+Shift+Tab");
            else if (key->key() == Qt::Key_F)
                name = QStringLiteral("Ctrl+Shift+F");
        } else if (mods == Qt::AltModifier) {
            if (key->key() == Qt::Key_Left)
                name = QStringLiteral("Alt+Left");
            else if (key->key() == Qt::Key_Right)
                name = QStringLiteral("Alt+Right");
        } else if (mods == Qt::NoModifier) {
            if (key->key() == Qt::Key_F5)
                name = QStringLiteral("F5");
            else if (key->key() == Qt::Key_F11)
                name = QStringLiteral("F11");
            else if (key->key() == Qt::Key_Escape && (m_fullScreen || m_chromeHidden))
                name = QStringLiteral("Escape");
        }

        if (name.isEmpty())
            return QObject::eventFilter(watched, event);

        QJsonObject message;
        message.insert(QStringLiteral("type"), QStringLiteral("shortcut"));
        message.insert(QStringLiteral("key"), name);
        m_channel->send(message);
        return true;
    }

private:
    IpcChannel *m_channel = nullptr;
    bool m_fullScreen = false;
    bool m_chromeHidden = false;
};

// The page is a child of the browser window. Over that child Windows forwards
// WM_SETCURSOR to the browser, and the browser's null class cursor leaves
// whatever was set last — the size cursor from the frame border — in place.
// Once the bars hide, the page meets that border, so crossing it paints a
// resize pointer that never returns to an arrow. Claim the client hit here
// and put the arrow back, unless the page itself has asked for a cursor.
class ClientCursorReset : public QAbstractNativeEventFilter
{
public:
    explicit ClientCursorReset(QWindow *window)
        : m_window(window) {}

    bool nativeEventFilter(const QByteArray &eventType, void *message, qintptr *result) override
    {
#ifdef Q_OS_WIN
        if (eventType != "windows_generic_MSG" || !m_window)
            return false;

        auto *msg = static_cast<MSG *>(message);
        if (!m_window->handle())
            return false;
        if (msg->hwnd != reinterpret_cast<HWND>(m_window->winId()))
            return false;

        if (msg->message == WM_ERASEBKGND) {
            // Same white flash as the browser frame: the first show of a new
            // tab clears the child window before Qt has drawn the page color.
            RECT rect = {};
            ::GetClientRect(msg->hwnd, &rect);
            static const HBRUSH brush = ::CreateSolidBrush(RGB(0x0e, 0x10, 0x17));
            ::FillRect(reinterpret_cast<HDC>(msg->wParam), &rect, brush);
            *result = 1;
            return true;
        }

        if (msg->message != WM_SETCURSOR)
            return false;
        // Real frame borders still want the size cursors. So does a page item
        // that asked for one (the slider). An unset window cursor reports the
        // arrow, which is the case this exists to restore.
        if (LOWORD(msg->lParam) != HTCLIENT || m_window->cursor().shape() != Qt::ArrowCursor)
            return false;

        ::SetCursor(::LoadCursor(nullptr, IDC_ARROW));
        *result = TRUE;
        return true;
#else
        Q_UNUSED(eventType)
        Q_UNUSED(message)
        Q_UNUSED(result)
        return false;
#endif
    }

private:
    QWindow *m_window = nullptr;
};

// Collapses the page state into the single message the browser process needs
// in order to paint the tab strip, address bar and status line.
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
    QCommandLineOption parentOption(QStringLiteral("parent"),
                                    QStringLiteral("Native window handle to embed into."),
                                    QStringLiteral("winid"));
    parser.addOption(channelOption);
    parser.addOption(tabOption);
    parser.addOption(parentOption);
    parser.process(app);

    if (!parser.isSet(channelOption)) {
        qWarning("QmlRenderer is started by QmlBrowser.exe; run that instead.");
        return 2;
    }

    qmlRegisterType<PageView>("QmlRenderer", 1, 0, "PageView");

    QQuickView view;
    view.setResizeMode(QQuickView::SizeRootObjectToView);
    view.setColor(QColor(QStringLiteral("#0e1017")));
#ifdef Q_OS_WIN
    view.setFlags(Qt::FramelessWindowHint);
#else
    // No cross-process child windows here. The browser sends a place message
    // and this window follows the viewport in screen coordinates.
    view.setFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint);
#endif
    view.setSource(QUrl(QStringLiteral("qrc:/ui/Renderer.qml")));

    if (view.status() != QQuickView::Ready) {
        qWarning("Renderer UI failed to load.");
        return 1;
    }

    auto *page = view.rootObject()->findChild<PageView *>(QStringLiteral("pageView"));
    if (!page) {
        qWarning("Renderer UI has no PageView.");
        return 1;
    }

#ifdef Q_OS_WIN
    // Become a native child of the browser window before showing anything, so
    // the tab never flashes as a separate top-level window. The parent id is a
    // Windows handle, valid in this process. On macOS it would be a pointer
    // into the browser process, so it is not used there.
    if (parser.isSet(parentOption)) {
        const WId parentId = static_cast<WId>(parser.value(parentOption).toULongLong());
        if (parentId) {
            if (QWindow *host = QWindow::fromWinId(parentId)) {
                host->setObjectName(QStringLiteral("browserHost"));
                view.setParent(host);
            }
        }
    }
#else
    Q_UNUSED(parentOption)
#endif

    auto *socket = new QLocalSocket(&app);
    auto *channel = new IpcChannel(socket, &app);
    const int tabId = parser.value(tabOption).toInt();

    auto *shortcuts = new ShortcutForwarder(channel, &app);
    view.installEventFilter(shortcuts);
    app.installNativeEventFilter(new ClientCursorReset(&view));

    QObject::connect(channel, &IpcChannel::disconnected, &app, [] {
        // The browser process is gone; never outlive it.
        QCoreApplication::quit();
    });

    // Push page state to the browser process, coalesced so a burst of property
    // changes during a load turns into one message per event loop pass.
    auto *pump = new QTimer(&app);
    pump->setSingleShot(true);
    pump->setInterval(0);
    QObject::connect(pump, &QTimer::timeout, &app,
                     [channel, page] { channel->send(stateMessage(page)); });

    const auto scheduleUpdate = [pump] { pump->start(); };
    QObject::connect(page, &PageView::statusChanged, &app, scheduleUpdate);
    QObject::connect(page, &PageView::pageTitleChanged, &app, scheduleUpdate);
    QObject::connect(page, &PageView::urlChanged, &app, scheduleUpdate);
    QObject::connect(page, &PageView::httpStatusChanged, &app, scheduleUpdate);
    QObject::connect(page, &PageView::progressChanged, &app, scheduleUpdate);
    QObject::connect(page, &PageView::errorStringChanged, &app, scheduleUpdate);

    QObject::connect(page, &PageView::navigationRequested, &app, [channel](const QUrl &url) {
        QJsonObject message;
        message.insert(QStringLiteral("type"), QStringLiteral("navigate"));
        message.insert(QStringLiteral("url"), url.toString());
        channel->send(message);
    });

    QObject::connect(page, &PageView::fullScreenRequested, &app, [channel](bool on) {
        QJsonObject message;
        message.insert(QStringLiteral("type"), QStringLiteral("fullscreen"));
        message.insert(QStringLiteral("on"), on);
        channel->send(message);
    });

    QObject::connect(page, &PageView::sourceTextChanged, &app, [channel, page] {
        QJsonObject message;
        message.insert(QStringLiteral("type"), QStringLiteral("source"));
        message.insert(QStringLiteral("text"), page->sourceText());
        channel->send(message);
    });

    // Placement and visibility are driven natively by the browser process via
    // the window handle sent in `hello`, so resizing the browser window does not
    // wait on a round trip through this socket.
    QQuickItem *root = view.rootObject();
    QObject::connect(channel, &IpcChannel::received, &app,
                     [page, root, shortcuts, &view](const QJsonObject &message) {
                         const QString type = message.value(QStringLiteral("type")).toString();
                         if (type == QLatin1String("place")) {
                             if (!message.value(QStringLiteral("visible")).toBool()) {
                                 view.hide();
                                 return;
                             }
                             const QRect rect(message.value(QStringLiteral("x")).toInt(),
                                              message.value(QStringLiteral("y")).toInt(),
                                              message.value(QStringLiteral("width")).toInt(),
                                              message.value(QStringLiteral("height")).toInt());
                             const bool wasHidden = !view.isVisible();
                             if (wasHidden) {
                                 view.setGeometry(rect);
#ifdef Q_OS_MACOS
                                 showWithoutTakingKey(&view);
#else
                                 view.show();
#endif
                             }
                             view.setGeometry(rect);
                             if (root)
                                 root->setSize(rect.size());
                             view.requestUpdate();
#ifdef Q_OS_MACOS
                             const int above = message.value(QStringLiteral("above")).toInt();
                             orderPageAbove(&view, above);
                             if (wasHidden) {
                                 keepBrowserInFront(&view, above);
                                 QTimer::singleShot(0, &view, [&view, above] {
                                     orderPageAbove(&view, above);
                                     activateBrowser();
                                     view.requestUpdate();
                                 });
                             }
#else
                             view.raise();
#endif
                         } else if (type == QLatin1String("navigate")) {
                             page->setUrl(QUrl(message.value(QStringLiteral("url")).toString()));
                         } else if (type == QLatin1String("reload")) {
                             page->reload();
                         } else if (type == QLatin1String("stop")) {
                             page->stop();
                         } else if (type == QLatin1String("chrome")) {
                             // Whenever the chrome is off screen this window
                             // covers the browser's own, so the reminder has to
                             // be drawn here.
                             const bool fullScreen =
                                     message.value(QStringLiteral("fullScreen")).toBool();
                             const bool chromeHidden =
                                     message.value(QStringLiteral("chromeHidden")).toBool();
                             shortcuts->setFullScreen(fullScreen);
                             shortcuts->setChromeHidden(chromeHidden);

                             QString notice;
                             if (fullScreen)
                                 notice = QStringLiteral("Press F11 or Esc to leave full screen");
                             else if (chromeHidden)
                                 notice = QStringLiteral("Press Ctrl+Shift+F or Esc to show "
                                                         "the bars");
                             root->setProperty("notice", notice);
                         }
                     });

    socket->connectToServer(parser.value(channelOption));
    if (!socket->waitForConnected(5000)) {
        qWarning("Could not reach the browser process.");
        return 1;
    }

    // waitForConnected() pumps a nested event loop. The browser answers hello
    // immediately with place, and showing the window from that nested loop marks
    // it visible without AppKit mapping it. A later place with the same rectangle
    // is then skipped, so the tab stays blank until it is hidden and shown again.
    QTimer::singleShot(0, &app, [channel, tabId, &view] {
        QJsonObject hello;
        hello.insert(QStringLiteral("type"), QStringLiteral("hello"));
        hello.insert(QStringLiteral("tab"), tabId);
        hello.insert(QStringLiteral("winId"), double(view.winId()));
        hello.insert(QStringLiteral("pid"), QCoreApplication::applicationPid());
        channel->send(hello);
    });

    return app.exec();
}
