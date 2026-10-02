#pragma once

#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QRect>
#include <QStringList>
#include <QWindow>

#include <optional>

// The currentTab property exposes BrowserTab to the meta-object system, which
// needs the complete type rather than a forward declaration.
#include "BrowserTab.h"

QT_BEGIN_NAMESPACE
class QAbstractNativeEventFilter;
class QLocalServer;
class QTimer;
QT_END_NAMESPACE

class IpcChannel;

// Owns every tab and the local socket the renderers connect back on.
//
// It also does the window management: each renderer draws into its own native
// child window, and this class is what positions, shows and hides them as the
// viewport moves or the user switches tab.
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

    // Set by TabViewport once the browser window has a native handle.
    void setHostWindow(WId window);
    // Viewport rectangle in device pixels, relative to the browser window.
    void setViewportRect(const QRect &rect);
    // Called every frame. On macOS the page is its own window, so a browser
    // move has to be forwarded even when the viewport rectangle is unchanged.
    void syncPlacement();

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
    QWindow *hostWindow() const;
    void updatePlacement(bool force = false);
#ifdef Q_OS_MACOS
    void beginTitleBarDrag();
    void followTitleBarDrag();
    void followPageForward(qint64 pageWindow);
#endif

    QLocalServer *m_server = nullptr;
    QString m_channelName;

    QList<QObject *> m_tabs;
    int m_currentIndex = -1;
    int m_nextTabId = 1;

    WId m_hostWindow = 0;
#ifndef Q_OS_WIN
    // The browser window whose move, screen and activation signals are hooked.
    // The page is a separate top-level window here, so it has to be told when
    // the browser moves; nothing is repainted that would report it otherwise.
    QPointer<QWindow> m_trackedHost;
#endif
#ifdef Q_OS_MACOS
    // AppKit tells the browser about a window drag in batches, a few hundred
    // milliseconds behind the screen. While the title bar is held, the page is
    // placed where the pointer has taken the window instead.
    QAbstractNativeEventFilter *m_mouseDownFilter = nullptr;
    QTimer *m_dragFollow = nullptr;
    QTimer *m_dropPrediction = nullptr;
    QPoint m_dragStartCursor;
    QPoint m_dragStartFrame;
    // Top-left of the browser frame, in global points, while it is ahead of
    // what QWindow reports.
    std::optional<QPoint> m_predictedFrame;
#endif
    QRect m_viewportRect;
    QString m_homeUrl;
    QStringList m_pendingUrls;
    bool m_fullScreen = false;
    bool m_chromeVisible = true;
};
