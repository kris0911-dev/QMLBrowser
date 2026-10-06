#pragma once

#include <QImage>
#include <QJsonObject>
#include <QObject>
#include <QPointer>
#include <QSize>
#include <QUrl>

#include "BrowserHistory.h"
#include "IpcChannel.h"
#include "SharedPixels.h"

QT_BEGIN_NAMESPACE
class QProcess;
QT_END_NAMESPACE

// One tab, backed by its own QmlRenderer.exe.
//
// The renderer has no window. It submits compositor frames through shared
// memory; this object keeps the latest one so the browser window can draw it,
// and forwards input because the page itself cannot be clicked.
class BrowserTab : public QObject
{
    Q_OBJECT

    Q_PROPERTY(int id READ id CONSTANT)
    Q_PROPERTY(QString title READ title NOTIFY stateChanged)
    Q_PROPERTY(QString url READ url NOTIFY stateChanged)
    Q_PROPERTY(int status READ status NOTIFY stateChanged)
    Q_PROPERTY(qreal progress READ progress NOTIFY stateChanged)
    Q_PROPERTY(int httpStatus READ httpStatus NOTIFY stateChanged)
    Q_PROPERTY(int byteCount READ byteCount NOTIFY stateChanged)
    Q_PROPERTY(QString errorString READ errorString NOTIFY stateChanged)
    Q_PROPERTY(QString sourceText READ sourceText NOTIFY sourceTextChanged)
    Q_PROPERTY(bool crashed READ crashed NOTIFY stateChanged)
    Q_PROPERTY(qint64 rendererPid READ rendererPid NOTIFY stateChanged)
    Q_PROPERTY(bool canGoBack READ canGoBack NOTIFY stateChanged)
    Q_PROPERTY(bool canGoForward READ canGoForward NOTIFY stateChanged)

public:
    // Mirrors PageView::Status in the renderer process.
    enum Status { Null, Loading, Ready, Error };
    Q_ENUM(Status)

    BrowserTab(int id, const QString &channelName, QObject *parent = nullptr);
    ~BrowserTab() override;

    int id() const { return m_id; }
    QString title() const;
    QString url() const { return m_history->currentUrl().toString(); }
    int status() const { return m_status; }
    qreal progress() const { return m_progress; }
    int httpStatus() const { return m_httpStatus; }
    int byteCount() const { return m_byteCount; }
    QString errorString() const { return m_errorString; }
    QString sourceText() const { return m_sourceText; }
    bool crashed() const { return m_crashed; }
    qint64 rendererPid() const { return m_rendererPid; }
    bool canGoBack() const { return m_history->canGoBack(); }
    bool canGoForward() const { return m_history->canGoForward(); }

    BrowserHistory *history() const { return m_history; }
    bool isAttached() const { return m_channel != nullptr; }
    QImage frame() const { return m_frame; }
    int cursorShape() const { return m_cursorShape; }

    // Called by TabManager once the renderer has connected back.
    void attach(IpcChannel *channel, qint64 pid);

    // Tells the renderer whether the browser chrome is currently hidden.
    void setChromeFullScreen(bool on);
    void setChromeHidden(bool hidden);

    // Logical pixels of the page area. Only the active tab is visible; the
    // others keep the size so a snapshot can be taken, then stop drawing.
    void setViewport(bool visible, const QSize &logicalSize, qreal dpr);
    void postInput(const QJsonObject &event);

    void navigateTo(const QUrl &url);
    Q_INVOKABLE void navigateToText(const QString &text);
    Q_INVOKABLE void reload();
    Q_INVOKABLE void stop();
    Q_INVOKABLE void back();
    Q_INVOKABLE void forward();
    // Relaunches the renderer after a crash and reloads the last URL.
    Q_INVOKABLE void restart();

signals:
    void stateChanged();
    void sourceTextChanged();
    // Emitted once the renderer is connected and can take a viewport.
    void attached();
    void frameChanged();
    void cursorChanged(int shape);
    // A browser-level key combination pressed while the page had focus.
    void shortcutRequested(const QString &key);
    // The page called browser.setFullScreen().
    void fullScreenRequested(bool on);

private:
    void startProcess();
    // Hangs up on the current renderer and lets it exit in the background.
    void releaseRenderer();
    void onProcessFinished(int exitCode, int status);
    void onMessage(const QJsonObject &message);
    void onFrame(const QJsonObject &message);
    void sendCurrentUrl();
    void sendChromeState();
    void sendViewport();

    int m_id = 0;
    QString m_channelName;

    QProcess *m_process = nullptr;
    QPointer<IpcChannel> m_channel;
    qint64 m_rendererPid = 0;

    BrowserHistory *m_history = nullptr;

    QString m_title;
    int m_status = Null;
    qreal m_progress = 0.0;
    int m_httpStatus = 0;
    int m_byteCount = 0;
    QString m_errorString;
    QString m_sourceText;
    bool m_crashed = false;
    bool m_closing = false;
    bool m_chromeFullScreen = false;
    bool m_chromeHidden = false;

    bool m_viewportValid = false;
    bool m_viewportVisible = false;
    QSize m_viewportSize;
    qreal m_viewportDpr = 1;

    SharedPixels m_pixels;
    QImage m_frame;
    int m_cursorShape = 0;
};
