#include "TabManager.h"

#include "BrowserTab.h"
#include "IpcChannel.h"
#include "RendererShutdown.h"
#include "WindowFrame.h"

#include <QCoreApplication>
#include <QJsonObject>
#include <QLocalServer>
#include <QRect>
#include <QLocalSocket>
#include <QUuid>

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
}

TabManager::~TabManager()
{
    RendererShutdown::shutdown();
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

void TabManager::setViewport(const QSize &logicalSize, qreal dpr)
{
    if (dpr < 0.5)
        dpr = 1;
    if (m_viewport == logicalSize && qAbs(m_dpr - dpr) < 0.001)
        return;
    m_viewport = logicalSize;
    m_dpr = dpr;
    updateViewport();
}

void TabManager::updateViewport()
{
    for (int i = 0; i < m_tabs.size(); ++i) {
        auto *tab = qobject_cast<BrowserTab *>(m_tabs.at(i));
        if (!tab || !tab->isAttached())
            continue;
        tab->setViewport(i == m_currentIndex, m_viewport, m_dpr);
    }
}

void TabManager::addTab(const QString &url)
{
    auto *tab = new BrowserTab(m_nextTabId++, m_channelName, this);
    connect(tab, &BrowserTab::attached, this, [this] { updateViewport(); });
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
    delete tab;

    if (m_tabs.isEmpty()) {
        m_currentIndex = -1;
        emit tabsChanged();
        emit currentChanged();
        emit lastTabClosed();
        return;
    }

    emit tabsChanged();

    m_currentIndex = qBound(0, m_currentIndex > index ? m_currentIndex - 1 : m_currentIndex,
                            int(m_tabs.size()) - 1);
    emit currentChanged();
    updateViewport();
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
            updateViewport();
        return;
    }

    m_currentIndex = index;
    emit currentChanged();
    updateViewport();
}

void TabManager::onNewConnection()
{
    while (QLocalSocket *socket = m_server->nextPendingConnection()) {
        auto *channel = new IpcChannel(socket, this);
        socket->setParent(channel);

        connect(channel, &IpcChannel::received, this,
                [this, channel](const QJsonObject &message) { onHello(channel, message); });
        connect(channel, &IpcChannel::disconnected, channel, &QObject::deleteLater);
    }
}

void TabManager::onHello(IpcChannel *channel, const QJsonObject &message)
{
    if (message.value(QStringLiteral("type")).toString() != QLatin1String("hello"))
        return;

    disconnect(channel, &IpcChannel::received, this, nullptr);

    BrowserTab *tab = tabById(message.value(QStringLiteral("tab")).toInt());
    if (!tab) {
        channel->deleteLater();
        return;
    }

    tab->attach(channel, qint64(message.value(QStringLiteral("pid")).toDouble()));
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
