#pragma once

#include <QElapsedTimer>
#include <QEvent>
#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QPointF>
#include <QUrl>

#include "SharedPixels.h"

QT_BEGIN_NAMESPACE
class QPainter;
class QTimer;
class QWidget;
QT_END_NAMESPACE

class GalleryWindow;
class IpcChannel;

// Puts a Qt Widgets application inside a browser tab.
//
// This is the widget counterpart of the renderer's OffscreenPage, and it
// speaks the same protocol, so the browser treats it like any other tab. The
// application runs on Qt's offscreen platform: no window ever reaches the
// screen, so menus, combo box lists, tooltips and dialogs never show up as
// stray windows outside the browser. Every visible top-level widget is painted,
// in stacking order, into a frame in shared memory, and the browser draws that
// frame. Pointer, wheel, key, focus and input-method events come back over the
// socket and are fed in through QWindowSystemInterface, as if the platform had
// produced them. Double clicks, enter and leave, popups, modality and shortcuts
// then behave the way they do in a normal window.
class BrowserBridge : public QObject
{
    Q_OBJECT

public:
    explicit BrowserBridge(GalleryWindow *window, QObject *parent = nullptr);
    ~BrowserBridge() override;

    // Selects Qt's offscreen platform. Must run before QApplication exists.
    static void selectOffscreenPlatform();
    // Windows only, after QApplication exists: makes the system's font
    // collections (CJK fonts among them) available to the offscreen platform.
    static void registerFontCollections();
    // Makes the offscreen screen exactly the tab's page area, in logical
    // pixels. Called once at startup, before any widget is created, and again
    // on every viewport change.
    static void resizeScreen(int width, int height);

    bool connectToBrowser(const QString &channelName, int tabId);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    static QString pageOf(const QUrl &url);
    static QString windowsFontDirectory();
    void send(const QJsonObject &message);
    void onMessage(const QJsonObject &message);

    void setViewport(bool visible, int width, int height, qreal dpr);
    void setNotice(const QString &notice);

    void raiseWindow(QWidget *window);
    void dropWindow(QWidget *window);
    void scheduleRender();
    void renderFrame();
    void paintDecorations(QPainter &painter, QWidget *window, const QPoint &offset);
    void paintNotice(QPainter &painter);
    int freeSlot() const;
    QString prepareSlot(int index, int bytes);
    void releaseSequence(quint32 sequence);

    void deliverInput(const QJsonObject &message);
    void deliverMouse(QEvent::Type type, const QJsonObject &message);
    void deliverWheel(const QJsonObject &message);
    void deliverHover(const QJsonObject &message);
    void deliverKey(QEvent::Type type, const QJsonObject &message);
    void deliverFocus(bool on);
    void deliverInputMethod(const QJsonObject &message);

    QPointF origin() const;
    QWidget *windowAt(const QPointF &global) const;
    QWidget *mouseTarget(const QPointF &global) const;
    QWidget *keyTarget() const;
    QWidget *widgetAt(const QPointF &global) const;
    void activate(QWidget *window);
    void hoverWindow(QWidget *window, const QPointF &global);
    void closeToolTips();

    void onFocusObjectChanged();
    void publishState();
    void publishSource();
    void publishCursor();
    void publishInputMethod();

    GalleryWindow *m_window = nullptr;
    IpcChannel *m_channel = nullptr;
    QUrl m_url;

    // Top-level widgets in stacking order, bottom first. Qt does not keep
    // a z-order for offscreen windows, so a window moves to the top when it
    // is shown. The main window is always first.
    QList<QPointer<QWidget>> m_stack;

    QTimer *m_renderTimer = nullptr;
    QTimer *m_cursorTimer = nullptr;
    QTimer *m_noticeTimer = nullptr;
    QElapsedTimer m_clock;
    qint64 m_lastFrameAt = -1;

    struct Slot {
        SharedPixels pixels;
        quint32 sequence = 0;
        bool pending = false;
    };
    Slot m_slots[2];
    quint32 m_nextSequence = 1;
    qint64 m_epoch = 0;
    qint64 m_pid = 0;

    bool m_visible = false;
    bool m_sizeDirty = false;
    bool m_blocked = false;
    // Set while widgets paint into a frame. QWidget::render() sends paint
    // events, and those must not schedule the next frame.
    bool m_painting = false;
    int m_width = 0;
    int m_height = 0;
    qreal m_dpr = 1;
    QString m_notice;

    bool m_focused = false;
    QPointF m_lastGlobal;
    QPointer<QWidget> m_grabWindow;
    QPointer<QWidget> m_pressWidget;
    QPointer<QWidget> m_hoverWindow;
    int m_cursor = -1;

    // Same scheme as the renderer: the serial changes whenever a different
    // widget takes focus, and a preedit left in the old one is committed.
    quint32 m_focusSerial = 0;
    QJsonObject m_inputMethod;
    QPointer<QObject> m_composing;
    QString m_preedit;
};
