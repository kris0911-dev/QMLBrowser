#include "TabManager.h"

#include "BrowserTab.h"
#include "IpcChannel.h"
#include "RendererShutdown.h"
#include "WindowFrame.h"

#include <QCoreApplication>
#include <QGuiApplication>
#include <QJsonObject>
#include <QLocalServer>
#include <QLocalSocket>
#include <QUuid>
#include <QWindow>

#ifdef Q_OS_MACOS
#  include <QAbstractNativeEventFilter>
#  include <QCursor>
#  include <QScreen>
#  include <QTimer>
#  include <functional>
#  include <objc/message.h>
#  include <objc/runtime.h>
#endif

#ifdef Q_OS_WIN
#  define WIN32_LEAN_AND_MEAN
#  define NOMINMAX
#  include <windows.h>
#endif

namespace {

// The renderer reparents itself into the browser window, so from here on the
// child is placed with ordinary window calls rather than over the socket. That
// keeps resizing in step with the frame instead of one IPC round trip behind.
void showChildAt(WId child, const QRect &rect)
{
#ifdef Q_OS_WIN
    if (!child)
        return;
    ::SetWindowPos(reinterpret_cast<HWND>(child), HWND_TOP, rect.x(), rect.y(), rect.width(),
                   rect.height(), SWP_SHOWWINDOW | SWP_NOACTIVATE);
#else
    Q_UNUSED(child)
    Q_UNUSED(rect)
#endif
}

void hideChild(WId child)
{
#ifdef Q_OS_WIN
    if (child)
        ::ShowWindow(reinterpret_cast<HWND>(child), SW_HIDE);
#else
    Q_UNUSED(child)
#endif
}

#ifdef Q_OS_MACOS
// Sees every left button press the browser receives, the title bar included.
// A title bar press never becomes a QMouseEvent, but it is where a drag starts.
// It runs before AppKit hands the press on, so the cursor is still where the
// press happened.
class MouseDownFilter : public QAbstractNativeEventFilter
{
public:
    explicit MouseDownFilter(std::function<void()> onPress)
        : m_onPress(std::move(onPress)) {}

    bool nativeEventFilter(const QByteArray &eventType, void *message, qintptr *) override
    {
        if (eventType != "NSEvent" && eventType != "mac_generic_NSEvent")
            return false;
        const auto type = reinterpret_cast<unsigned long (*)(id, SEL)>(objc_msgSend)(
                static_cast<id>(message), sel_getUid("type"));
        // NSEventTypeLeftMouseDown
        if (type == 1)
            m_onPress();
        return false;
    }

private:
    std::function<void()> m_onPress;
};

bool leftButtonHeld()
{
    const auto buttons = reinterpret_cast<unsigned long (*)(id, SEL)>(objc_msgSend)(
            reinterpret_cast<id>(objc_getClass("NSEvent")), sel_getUid("pressedMouseButtons"));
    return buttons & 1;
}
#endif

} // namespace

TabManager::TabManager(QObject *parent)
    : QObject(parent)
{
    // macOS sockaddr_un::sun_path holds 104 bytes, and QDir::tempPath() is
    // already "/private/var/folders/...". A full UUID makes listen() fail with
    // a name error, the renderer cannot connect, and it exits 1.
    m_channelName = QStringLiteral("qb-%1-%2")
                            .arg(QCoreApplication::applicationPid())
                            .arg(QUuid::createUuid().toString(QUuid::Id128).left(8));

    m_server = new QLocalServer(this);
    m_server->setSocketOptions(QLocalServer::UserAccessOption);
    QLocalServer::removeServer(m_channelName);
    if (!m_server->listen(m_channelName))
        qWarning("Cannot open the renderer channel: %s", qPrintable(m_server->errorString()));

    connect(m_server, &QLocalServer::newConnection, this, &TabManager::onNewConnection);

#ifdef Q_OS_MACOS
    m_dragFollow = new QTimer(this);
    m_dragFollow->setTimerType(Qt::PreciseTimer);
    m_dragFollow->setInterval(8);
    connect(m_dragFollow, &QTimer::timeout, this, &TabManager::followTitleBarDrag);

    // After the button is released AppKit still owes the browser the final
    // position. Until it arrives the predicted one is kept, so the page does
    // not jump back to where the window started. This only bounds that wait.
    m_dropPrediction = new QTimer(this);
    m_dropPrediction->setSingleShot(true);
    m_dropPrediction->setInterval(1000);
    connect(m_dropPrediction, &QTimer::timeout, this, [this] {
        m_predictedFrame.reset();
        updatePlacement();
    });

    m_mouseDownFilter = new MouseDownFilter([this] { beginTitleBarDrag(); });
    QCoreApplication::instance()->installNativeEventFilter(m_mouseDownFilter);
#endif
}

TabManager::~TabManager()
{
    // One wait for every live and retiring renderer, before any ~QProcess.
    // aboutToQuit normally arrives first; this also covers exiting main()
    // without having entered the event loop.
    RendererShutdown::shutdown();
    qDeleteAll(m_tabs);
    m_tabs.clear();

#ifdef Q_OS_MACOS
    QCoreApplication::instance()->removeNativeEventFilter(m_mouseDownFilter);
    delete m_mouseDownFilter;
#endif
}

BrowserTab *TabManager::currentTab() const
{
    if (m_currentIndex < 0 || m_currentIndex >= m_tabs.size())
        return nullptr;
    return qobject_cast<BrowserTab *>(m_tabs.at(m_currentIndex));
}

void TabManager::setHomeUrl(const QString &url)
{
    if (m_homeUrl == url)
        return;
    m_homeUrl = url;
    emit homeUrlChanged();
}

void TabManager::setFullScreen(bool on)
{
    if (m_fullScreen == on)
        return;

    m_fullScreen = on;

    // Full screen wants the ordinary frame calculation back, and it has to be
    // switched before the window actually changes size.
    WindowFrame::instance()->setEnabled(!on);

    // Every renderer needs to hear about this. With the chrome gone their
    // window covers the entire frame, so Escape has to become an exit key
    // there, and the reminder saying so can only be drawn by the process that
    // owns those pixels.
    for (QObject *object : m_tabs) {
        if (auto *tab = qobject_cast<BrowserTab *>(object))
            tab->setChromeFullScreen(on);
    }

    emit fullScreenChanged();
    WindowFrame::instance()->syncFrame();
}

void TabManager::toggleFullScreen()
{
    setFullScreen(!m_fullScreen);
}

void TabManager::setChromeVisible(bool visible)
{
    if (m_chromeVisible == visible)
        return;

    m_chromeVisible = visible;

    // The renderer draws the reminder, so it has to be told what to say.
    for (QObject *object : m_tabs) {
        if (auto *tab = qobject_cast<BrowserTab *>(object))
            tab->setChromeHidden(!visible);
    }

    emit chromeVisibilityChanged();
}

void TabManager::toggleChrome()
{
    setChromeVisible(!m_chromeVisible);
}

void TabManager::setCaptionDragRegion(qreal x, qreal y, qreal width, qreal height)
{
    WindowFrame::instance()->setDragRegion(
            QRect(qRound(x), qRound(y), qRound(width), qRound(height)));
}

void TabManager::setHostWindow(WId window)
{
    if (m_hostWindow == window)
        return;

    m_hostWindow = window;

    // Tabs requested before the window existed could not be parented yet.
    const QStringList pending = std::move(m_pendingUrls);
    m_pendingUrls.clear();
    for (const QString &url : pending)
        addTab(url);
}

void TabManager::setViewportRect(const QRect &rect)
{
    if (m_viewportRect == rect)
        return;
    m_viewportRect = rect;
    updatePlacement();
}

void TabManager::syncPlacement()
{
#ifndef Q_OS_WIN
    updatePlacement();
#endif
}

void TabManager::addTab(const QString &url)
{
    // An empty URL is a fresh tab: no page, and the address bar stays blank
    // until the user types one. The startup tab passes the home page explicitly.
    if (!m_hostWindow) {
        m_pendingUrls.append(url);
        return;
    }

    auto *tab = new BrowserTab(m_nextTabId++, m_channelName, m_hostWindow, this);
    connect(tab, &BrowserTab::attached, this, [this] { updatePlacement(); });
    connect(tab, &BrowserTab::shortcutRequested, this, &TabManager::handleShortcut);
    connect(tab, &BrowserTab::fullScreenRequested, this, &TabManager::setFullScreen);
#ifdef Q_OS_MACOS
    connect(tab, &BrowserTab::pagePressed, this, &TabManager::followPageForward);
#endif
    tab->setChromeFullScreen(m_fullScreen);
    tab->setChromeHidden(!m_chromeVisible);

    m_tabs.append(tab);
    emit tabsChanged();

    if (!url.isEmpty())
        tab->navigateToText(url);

    setCurrentIndex(int(m_tabs.size()) - 1);
    if (url.isEmpty())
        emit focusAddressBarRequested();
}

void TabManager::closeTab(int index)
{
    if (index < 0 || index >= m_tabs.size())
        return;

    QObject *tab = m_tabs.takeAt(index);
    hideChild(qobject_cast<BrowserTab *>(tab)->childWindow());
    delete tab;

    if (m_tabs.isEmpty()) {
        m_currentIndex = -1;
        emit tabsChanged();
        emit currentChanged();
        emit lastTabClosed();
        return;
    }

    emit tabsChanged();

    // Keep the neighbour selected, the way every other browser does.
    m_currentIndex = qBound(0, m_currentIndex > index ? m_currentIndex - 1 : m_currentIndex,
                            int(m_tabs.size()) - 1);
    emit currentChanged();
    updatePlacement();
}

void TabManager::closeCurrentTab()
{
    closeTab(m_currentIndex);
}

void TabManager::selectNextTab()
{
    if (m_tabs.size() > 1)
        setCurrentIndex((m_currentIndex + 1) % int(m_tabs.size()));
}

void TabManager::selectPreviousTab()
{
    if (m_tabs.size() > 1)
        setCurrentIndex((m_currentIndex - 1 + int(m_tabs.size())) % int(m_tabs.size()));
}

void TabManager::handleShortcut(const QString &key)
{
    BrowserTab *tab = currentTab();

    if (key == QLatin1String("Ctrl+T")) {
        addTab();
    } else if (key == QLatin1String("Ctrl+W")) {
        closeCurrentTab();
    } else if (key == QLatin1String("Ctrl+Tab")) {
        selectNextTab();
    } else if (key == QLatin1String("Ctrl+Shift+Tab")) {
        selectPreviousTab();
    } else if (key == QLatin1String("Ctrl+L")) {
        emit focusAddressBarRequested();
    } else if (key == QLatin1String("Ctrl+U")) {
        emit toggleSourceRequested();
    } else if (key == QLatin1String("F11")) {
        toggleFullScreen();
    } else if (key == QLatin1String("Escape")) {
        // Full screen wins when both are on, since it is the more committed
        // state; the next Escape brings the chrome back.
        if (m_fullScreen)
            setFullScreen(false);
        else
            setChromeVisible(true);
    } else if (key == QLatin1String("Ctrl+Shift+F")) {
        toggleChrome();
    } else if (tab) {
        if (key == QLatin1String("F5") || key == QLatin1String("Ctrl+R"))
            tab->reload();
        else if (key == QLatin1String("Alt+Left"))
            tab->back();
        else if (key == QLatin1String("Alt+Right"))
            tab->forward();
    }
}

void TabManager::setCurrentIndex(int index)
{
    if (index < 0 || index >= m_tabs.size() || index == m_currentIndex) {
        if (index == m_currentIndex)
            updatePlacement();
        return;
    }

    m_currentIndex = index;
    emit currentChanged();
    updatePlacement();
}

namespace {

int hostWindowNumber(QWindow *host)
{
#ifdef Q_OS_MACOS
    if (!host)
        return 0;
    using SendId = id (*)(id, SEL);
    auto msg = reinterpret_cast<SendId>(objc_msgSend);
    id nsView = reinterpret_cast<id>(host->winId());
    id nsWindow = nsView ? msg(nsView, sel_getUid("window")) : nil;
    if (!nsWindow)
        return 0;
    return int(reinterpret_cast<long (*)(id, SEL)>(objc_msgSend)(
            nsWindow, sel_getUid("windowNumber")));
#else
    Q_UNUSED(host)
    return 0;
#endif
}

} // namespace

void TabManager::updatePlacement(bool force)
{
#ifdef Q_OS_WIN
    for (int i = 0; i < m_tabs.size(); ++i) {
        auto *tab = qobject_cast<BrowserTab *>(m_tabs.at(i));
        if (!tab || !tab->childWindow())
            continue;

        if (i == m_currentIndex && m_viewportRect.isValid())
            showChildAt(tab->childWindow(), m_viewportRect);
        else
            hideChild(tab->childWindow());
    }
#else
    // A window id from the renderer is a pointer in that process. Move the
    // page by telling the renderer its screen rectangle instead.
    QWindow *host = hostWindow();

    const qreal ratio = host ? host->devicePixelRatio() : 1.0;
    QPoint origin = host ? host->mapToGlobal(QPoint(0, 0)) : QPoint();
#ifdef Q_OS_MACOS
    if (host && m_predictedFrame) {
        // Once AppKit reports the same position the prediction has done its job.
        if (!m_dragFollow->isActive() && host->framePosition() == *m_predictedFrame) {
            m_predictedFrame.reset();
            m_dropPrediction->stop();
        } else {
            const QMargins margins = host->frameMargins();
            origin = *m_predictedFrame + QPoint(margins.left(), margins.top());
        }
    }
#endif
    const bool showCurrent = host && m_viewportRect.isValid();
    const QRect screen(origin.x() + qRound(m_viewportRect.x() / ratio),
                       origin.y() + qRound(m_viewportRect.y() / ratio),
                       qRound(m_viewportRect.width() / ratio),
                       qRound(m_viewportRect.height() / ratio));
    qint64 above = hostWindowNumber(host);

    if (host && host != m_trackedHost) {
        if (m_trackedHost)
            m_trackedHost->disconnect(this);
        m_trackedHost = host;
        // Moving the window renders no frame, so the per-frame sync in
        // TabViewport never sees it. Follow the window's own position instead.
        const auto follow = [this] { updatePlacement(); };
        connect(host, &QWindow::xChanged, this, follow);
        connect(host, &QWindow::yChanged, this, follow);
        connect(host, &QWindow::screenChanged, this, follow);
        // Restack the page when the browser is activated. On macOS the page
        // also has to stop floating above other apps once it is not.
        connect(host, &QWindow::activeChanged, this,
                [this, host] { updatePlacement(host->isActive()); });
    }

#if defined(Q_OS_LINUX)
    // X11 window ids are not macOS window numbers. The renderer uses this id
    // as the transient owner so a window manager keeps the page with the browser.
    if (host && QGuiApplication::platformName() == QLatin1String("xcb"))
        above = static_cast<qint64>(static_cast<qulonglong>(host->winId()));
#endif
    // Whether the page may sit above everything else: only while the browser
    // is the active window. Otherwise other apps must be able to cover it.
    const bool raisePage = host && host->isActive();

    for (int i = 0; i < m_tabs.size(); ++i) {
        auto *tab = qobject_cast<BrowserTab *>(m_tabs.at(i));
        if (!tab || !tab->isAttached())
            continue;
        tab->place(showCurrent && i == m_currentIndex, screen, above,
                   force && i == m_currentIndex, raisePage && i == m_currentIndex);
    }
#endif
}

QWindow *TabManager::hostWindow() const
{
    const QList<QWindow *> windows = QGuiApplication::allWindows();
    for (QWindow *window : windows) {
        if (window->winId() == m_hostWindow)
            return window;
    }
    return nullptr;
}

#ifdef Q_OS_MACOS
void TabManager::beginTitleBarDrag()
{
    QWindow *host = hostWindow();
    if (!host || host->visibility() == QWindow::FullScreen)
        return;

    // A drag that starts right after the last one ends begins from where that
    // one left the window, which AppKit may not have reported yet.
    const QPoint frame = m_predictedFrame.value_or(host->framePosition());
    const QPoint cursor = QCursor::pos();
    const int titleBar = host->frameMargins().top();
    // Only the title bar moves the window. The first 80 points hold the close,
    // minimise and zoom buttons, which do not.
    if (titleBar <= 0 || cursor.y() < frame.y() || cursor.y() >= frame.y() + titleBar
            || cursor.x() < frame.x() + 80 || cursor.x() >= frame.x() + host->width())
        return;

    m_dragStartCursor = cursor;
    m_dragStartFrame = frame;
    m_predictedFrame = frame;
    m_dropPrediction->stop();
    m_dragFollow->start();
}

void TabManager::followPageForward(qint64 pageWindow)
{
    // Ordering its own window just below the page brings the browser forward
    // with it, without activating the browser and taking the click's keyboard
    // focus away from the page.
    QWindow *host = hostWindow();
    id nsView = host ? reinterpret_cast<id>(host->winId()) : nil;
    id nsWindow = nsView ? reinterpret_cast<id (*)(id, SEL)>(objc_msgSend)(
            nsView, sel_getUid("window")) : nil;
    if (!nsWindow || pageWindow <= 0)
        return;
    // NSWindowBelow == -1.
    reinterpret_cast<void (*)(id, SEL, long, long)>(objc_msgSend)(
            nsWindow, sel_getUid("orderWindow:relativeTo:"), -1L, long(pageWindow));
}

void TabManager::followTitleBarDrag()
{
    // The window server moves a title bar drag by exactly the distance the
    // pointer travels, keeping the title bar below the menu bar. Reading the
    // cursor costs nothing, where asking the window server for the window's
    // position blocks for as long as it is busy compositing.
    const QPoint cursor = QCursor::pos();
    QPoint frame = m_dragStartFrame + (cursor - m_dragStartCursor);
    if (QScreen *screen = QGuiApplication::screenAt(cursor))
        frame.setY(qMax(frame.y(), screen->availableGeometry().top()));
    m_predictedFrame = frame;

    if (!leftButtonHeld()) {
        m_dragFollow->stop();
        // A click or double click that never moved the pointer has nothing to
        // predict, and a zoom on double click must not be held back by it.
        if (frame == m_dragStartFrame)
            m_predictedFrame.reset();
        else
            m_dropPrediction->start();
    }
    updatePlacement();
}
#endif

void TabManager::onNewConnection()
{
    while (QLocalSocket *socket = m_server->nextPendingConnection()) {
        auto *channel = new IpcChannel(socket, this);
        socket->setParent(channel);

        // The first message identifies which tab this renderer belongs to.
        connect(channel, &IpcChannel::received, this,
                [this, channel](const QJsonObject &message) { onHello(channel, message); });
        connect(channel, &IpcChannel::disconnected, channel, &QObject::deleteLater);
    }
}

void TabManager::onHello(IpcChannel *channel, const QJsonObject &message)
{
    if (message.value(QStringLiteral("type")).toString() != QLatin1String("hello"))
        return;

    // Only the handshake is handled here; the tab takes the channel from now on.
    disconnect(channel, &IpcChannel::received, this, nullptr);

    BrowserTab *tab = tabById(message.value(QStringLiteral("tab")).toInt());
    if (!tab) {
        channel->deleteLater();
        return;
    }

    const auto childWindow = static_cast<WId>(message.value(QStringLiteral("winId")).toDouble());
    tab->attach(channel, childWindow, qint64(message.value(QStringLiteral("pid")).toDouble()));
}

BrowserTab *TabManager::tabById(int id) const
{
    for (QObject *object : m_tabs) {
        auto *tab = qobject_cast<BrowserTab *>(object);
        if (tab && tab->id() == id)
            return tab;
    }
    return nullptr;
}
