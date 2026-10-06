#pragma once

#include <QJsonObject>
#include <QObject>
#include <QPointF>
#include <QSize>

#include "PageView.h"
#include "SharedPixels.h"

class QQmlEngine;
class QQuickItem;
class QQuickRenderControl;
class QQuickWindow;
class QRhi;
class QRhiRenderBuffer;
class QRhiRenderPassDescriptor;
class QRhiTexture;
class QRhiTextureRenderTarget;
class QTimer;

// One tab's page, drawn with no native window.
//
// Chromium's renderer does not own an HWND or NSWindow. It produces a
// compositor frame; the browser process is the only window, and it draws
// that frame. QQuickRenderControl is the same split for Qt Quick: the scene
// is rasterized into an offscreen texture, the bytes are published through
// shared memory, and the browser uploads them into its own scene graph.
// Pointer and key events travel back the other way, because this process
// has nothing on screen to click.
class OffscreenPage : public QObject
{
    Q_OBJECT

public:
    explicit OffscreenPage(QObject *parent = nullptr);
    ~OffscreenPage() override;

    bool load();

    PageView *page() const { return m_page; }
    QQuickItem *rootItem() const { return m_root; }

    // Logical pixels. A hidden tab is snapshotted when its size changes and
    // otherwise does not draw, so a background tab does not spend frames.
    void setViewport(bool visible, int width, int height, qreal dpr);
    void deliverInput(const QJsonObject &message);
    void releaseSequence(quint32 sequence);

signals:
    void frameReady(quint32 sequence, int width, int height, int stride, int format,
                    const QString &key);
    void cursorChanged(int shape);

private:
    void scheduleRender();
    void renderFrame();
    bool ensureTarget();
    void releaseTarget();
    int freeSlot() const;
    QString prepareSlot(int index, int bytes);
    void ensureActive();
    void publishCursor();
    void deliverMouse(QEvent::Type type, const QJsonObject &message);
    void deliverWheel(const QJsonObject &message);
    void deliverHover(const QJsonObject &message);
    void deliverKey(QEvent::Type type, const QJsonObject &message);
    void deliverFocus(bool on);

    QQuickRenderControl *m_renderControl = nullptr;
    QQuickWindow *m_window = nullptr;
    QQmlEngine *m_engine = nullptr;
    QQuickItem *m_root = nullptr;
    PageView *m_page = nullptr;
    QRhi *m_rhi = nullptr;
    QRhiTexture *m_color = nullptr;
    QRhiRenderBuffer *m_depth = nullptr;
    QRhiTextureRenderTarget *m_target = nullptr;
    QRhiRenderPassDescriptor *m_pass = nullptr;
    QTimer *m_renderTimer = nullptr;

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
    bool m_rendering = false;
    bool m_activateFailed = false;
    int m_width = 0;
    int m_height = 0;
    qreal m_dpr = 1;
    int m_cursor = -1;
    QPointF m_lastHover;
};
