#pragma once

#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QSize>

#include "BrowserTab.h"

QT_BEGIN_NAMESPACE
class QLocalServer;
QT_END_NAMESPACE

class IpcChannel;

// Owns every tab and the local socket the renderers connect back on.
//
// Each renderer draws into shared memory, not a window. This class tells the
// active one how big the page area is; TabViewport is what puts the frame on
// screen inside the browser window.
class TabManager : public QObject
{
    Q_OBJECT

    Q_PROPERTY(QList<QObject *> tabs READ tabs NOTIFY tabsChanged)
    Q_PROPERTY(int count READ count NOTIFY tabsChanged)
    Q_PROPERTY(int currentIndex READ currentIndex WRITE setCurrentIndex NOTIFY currentChanged)
    Q_PROPERTY(BrowserTab *currentTab READ currentTab NOTIFY currentChanged)
    Q_PROPERTY(QString homeUrl READ homeUrl WRITE setHomeUrl NOTIFY homeUrlChanged)
    Q_PROPERTY(bool fullScreen READ isFullScreen WRITE setFullScreen NOTIFY fullScreenChanged)
    Q_PROPERTY(bool chromeVisible READ isChromeVisible WRITE setChromeVisible
               NOTIFY chromeVisibilityChanged)

public:
    explicit TabManager(QObject *parent = nullptr);
    ~TabManager() override;

    QList<QObject *> tabs() const { return m_tabs; }
    int count() const { return int(m_tabs.size()); }

    int currentIndex() const { return m_currentIndex; }
    void setCurrentIndex(int index);
    BrowserTab *currentTab() const;

    QString homeUrl() const { return m_homeUrl; }
    void setHomeUrl(const QString &url);

    bool isFullScreen() const { return m_fullScreen; }
    void setFullScreen(bool on);

    bool isChromeVisible() const { return m_chromeVisible; }
    void setChromeVisible(bool visible);

    // Page area in logical pixels, plus the browser window's device pixel ratio.
    void setViewport(const QSize &logicalSize, qreal dpr);

    Q_INVOKABLE void addTab(const QString &url = QString());
    Q_INVOKABLE void closeTab(int index);
    Q_INVOKABLE void closeCurrentTab();
    Q_INVOKABLE void selectNextTab();
    Q_INVOKABLE void selectPreviousTab();
    Q_INVOKABLE void toggleFullScreen();
    Q_INVOKABLE void toggleChrome();
    // Where the empty tab strip can be grabbed to move the window, in physical
    // client pixels. An empty rectangle leaves the page clickable.
    Q_INVOKABLE void setCaptionDragRegion(qreal x, qreal y, qreal width, qreal height);
    // Runs a browser-level shortcut, whether it came from the chrome or was
    // forwarded by a renderer that had keyboard focus.
    Q_INVOKABLE void handleShortcut(const QString &key);

signals:
    void tabsChanged();
    void currentChanged();
    void homeUrlChanged();
    void fullScreenChanged();
    void chromeVisibilityChanged();
    void lastTabClosed();
    void focusAddressBarRequested();
    void toggleSourceRequested();

private:
    void onNewConnection();
    void onHello(IpcChannel *channel, const QJsonObject &message);
    BrowserTab *tabById(int id) const;
    void updateViewport();

    QLocalServer *m_server = nullptr;
    QString m_channelName;

    QList<QObject *> m_tabs;
    int m_currentIndex = -1;
    int m_nextTabId = 1;

    QSize m_viewport;
    qreal m_dpr = 1;
    QString m_homeUrl;
    bool m_fullScreen = false;
    bool m_chromeVisible = true;
};
