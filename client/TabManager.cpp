#include "TabManager.h"

#include "BrowserTab.h"
#include "IpcChannel.h"
#include "WindowFrame.h"

#include <QCoreApplication>
#include <QGuiApplication>
#include <QJsonObject>
#include <QLocalServer>
#include <QLocalSocket>
#include <QUuid>
#include <QWindow>

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

} // namespace

TabManager::TabManager(QObject *parent)
    : QObject(parent)
{
    m_channelName = QStringLiteral("qmlbrowser-%1-%2")
                            .arg(QCoreApplication::applicationPid())
                            .arg(QUuid::createUuid().toString(QUuid::Id128));

    m_server = new QLocalServer(this);
    m_server->setSocketOptions(QLocalServer::UserAccessOption);
    if (!m_server->listen(m_channelName))
        qWarning("Cannot open the renderer channel: %s", qPrintable(m_server->errorString()));

    connect(m_server, &QLocalServer::newConnection, this, &TabManager::onNewConnection);
}

TabManager::~TabManager()
{
    qDeleteAll(m_tabs);
    m_tabs.clear();
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
    connect(tab, &BrowserTab::attached, this, &TabManager::updatePlacement);
    connect(tab, &BrowserTab::shortcutRequested, this, &TabManager::handleShortcut);
    connect(tab, &BrowserTab::fullScreenRequested, this, &TabManager::setFullScreen);
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

void TabManager::updatePlacement()
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
    QWindow *host = nullptr;
    const QList<QWindow *> windows = QGuiApplication::allWindows();
    for (QWindow *window : windows) {
        if (window->winId() == m_hostWindow) {
            host = window;
            break;
        }
    }

    const qreal ratio = host ? host->devicePixelRatio() : 1.0;
    const QPoint origin = host ? host->mapToGlobal(QPoint(0, 0)) : QPoint();
    const bool showCurrent = host && m_viewportRect.isValid();
    const QRect screen(origin.x() + qRound(m_viewportRect.x() / ratio),
                       origin.y() + qRound(m_viewportRect.y() / ratio),
                       qRound(m_viewportRect.width() / ratio),
                       qRound(m_viewportRect.height() / ratio));

    for (int i = 0; i < m_tabs.size(); ++i) {
        auto *tab = qobject_cast<BrowserTab *>(m_tabs.at(i));
        if (!tab || !tab->isAttached())
            continue;
        tab->place(showCurrent && i == m_currentIndex, screen);
    }
#endif
}

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
