#pragma once

#include <QElapsedTimer>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QPointF>
#include <QString>

#include "SharedPixels.h"

QT_BEGIN_NAMESPACE
class QPainter;
class QTimer;
class QWidget;
QT_END_NAMESPACE

// Paints every visible top-level widget of the application, in stacking
// order, into a frame in shared memory.
//
// Qt keeps no z-order for windows that are not on screen, so this class
// keeps one: a window moves to the top when it is shown, and the main window
// is always at the bottom. It also answers which popup and which modal
// window are open, from that list rather than from Qt's own: since Qt 6.8 a
// popup whose native window is not mapped does not count as open for Qt.
//
// Frames are produced when widgets repaint, at most one per
// kMinFrameIntervalMs, into one of two buffers. A buffer is reused once the
// browser acknowledges the frame in it.
class WindowCompositor : public QObject
{
    Q_OBJECT

public:
    // `gpu`: windows are kept off screen on the native platform, see
    // NativePlatform. Otherwise the application is on the offscreen platform.
    WindowCompositor(QWidget *mainWindow, bool gpu, QObject *parent = nullptr);
    ~WindowCompositor() override;

    QWidget *mainWindow() const { return m_window; }

    // Global position of the page's top-left corner: page coordinates plus
    // this are global coordinates.
    QPointF origin() const;
    // The topmost visible window at a global position, or the main window.
    QWidget *windowAt(const QPointF &global) const;
    // The deepest widget at a global position, or its window.
    QWidget *widgetAt(const QPointF &global) const;
    // Hides every visible tooltip.
    void closeToolTips();
    // The topmost open popup (menu, combo box list), or null.
    QWidget *activePopup() const;
    // The topmost open modal window, or null.
    QWidget *activeModal() const;

    void setViewport(bool visible, int width, int height, qreal dpr);
    // A line drawn over the top of the page for a few seconds. Empty hides it.
    void setNotice(const QString &notice);
    // The browser is done with the frame `sequence`; its buffer may be reused.
    void releaseFrame(quint32 sequence);

signals:
    // A frame is in shared memory under `key`. The bytes are
    // QImage::Format_ARGB32_Premultiplied, `width` by `height` device pixels.
    void frameReady(quint32 sequence, int width, int height, int stride, const QString &key);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void raiseWindow(QWidget *window);
    void keepInsideTab(QWidget *window);
    void dropWindow(QWidget *window);
    void scheduleRender();
    void renderFrame();
    void paintDecorations(QPainter &painter, QWidget *window, const QPoint &offset);
    void paintNotice(QPainter &painter);
    int freeSlot() const;
    QString prepareSlot(int index, int bytes);

    QWidget *m_window = nullptr;
    bool m_gpu = false;
    // Top-level widgets in stacking order, bottom first.
    QList<QPointer<QWidget>> m_stack;

    QTimer *m_renderTimer = nullptr;
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
};
