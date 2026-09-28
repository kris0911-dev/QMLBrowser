#pragma once

#include <QJsonObject>
#include <QObject>
#include <QPointer>
#include <QRect>
#include <QUrl>
#include <QWindow>

#include "BrowserHistory.h"

QT_BEGIN_NAMESPACE
class QProcess;
QT_END_NAMESPACE

class IpcChannel;

// One tab, backed by its own QmlRenderer.exe.
//
// This object never renders anything. It owns the renderer process, the history
// stack for that tab, and a mirror of the page state the renderer reports, which
// is what the tab strip and address bar bind to.
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

    BrowserTab(int id, const QString &channelName, WId hostWindow, QObject *parent = nullptr);
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
    WId childWindow() const { return m_childWindow; }
    bool isAttached() const { return m_channel != nullptr && m_childWindow != 0; }

    // Called by TabManager once the renderer has connected back.
    void attach(IpcChannel *channel, WId childWindow, qint64 pid);

    // Tells the renderer whether the browser chrome is currently hidden.
    void setChromeFullScreen(bool on);
    void setChromeHidden(bool hidden);

    // macOS cannot SetWindowPos a window that belongs to another process, so the
    // renderer moves its own window. screenRect is in global logical pixels.
    void place(bool visible, const QRect &screenRect);

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
    // Emitted once the renderer's window exists and can be placed.
    void attached();
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
    void sendCurrentUrl();
    void sendChromeState();

    int m_id = 0;
    QString m_channelName;
    WId m_hostWindow = 0;

    QProcess *m_process = nullptr;
    QPointer<IpcChannel> m_channel;
    WId m_childWindow = 0;
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
    bool m_placedVisible = false;
    QRect m_placedRect;
};
