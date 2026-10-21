#pragma once

#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QUrl>

#include <functional>

QT_BEGIN_NAMESPACE
class QWidget;
QT_END_NAMESPACE

class InputInjector;
class IpcChannel;
class WindowCompositor;

// The tab side of the browser protocol for a Widgets application.
//
// This is the widget counterpart of the renderer's OffscreenPage and speaks
// the same newline-delimited JSON over a local socket, so the browser treats
// the application like any other tab. WindowCompositor turns the windows
// into frames and InputInjector replays input; this class owns the socket,
// routes messages between the two and the browser, and reports the tab's
// state (title, address, source).
class BrowserBridge : public QObject
{
    Q_OBJECT

public:
    using NavigateHandler = std::function<void(const QString &path)>;

    // `gpu`: the application runs on its native platform with its windows
    // kept off screen (Graphics::Gpu) rather than on the offscreen platform.
    BrowserBridge(QWidget *mainWindow, bool gpu, QObject *parent = nullptr);
    ~BrowserBridge() override;

    bool connectToBrowser(const QString &channelName, int tabId);

    void setNavigateHandler(NavigateHandler handler);
    void setPath(const QString &path);

private:
    static QString pathOf(const QUrl &url);

    void send(const QJsonObject &message);
    void onMessage(const QJsonObject &message);
    void navigate(const QUrl &url);
    void sendFrame(quint32 sequence, int width, int height, int stride, const QString &key);

    void publishState();
    void publishSource();

    QWidget *m_window = nullptr;
    WindowCompositor *m_windows = nullptr;
    InputInjector *m_input = nullptr;
    IpcChannel *m_channel = nullptr;
    QUrl m_url;
    NavigateHandler m_navigateHandler;
    bool m_navigating = false;
};
