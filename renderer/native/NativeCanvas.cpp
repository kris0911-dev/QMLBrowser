#include "NativeCanvas.h"

#include <QElapsedTimer>
#include <QQuickWindow>
#include <QSGSimpleTextureNode>

namespace {

// As in JuliaSet: drawn at no more than this width and scaled up, so a slow
// library cannot stall the tab on a large viewport.
constexpr int MaxImageWidth = 960;

} // namespace

NativeCanvas::NativeCanvas(QQuickItem *parent)
    : QQuickItem(parent)
{
    setFlag(ItemHasContents, true);
}

void NativeCanvas::setLibrary(const QString &name)
{
    if (name == m_libraryName)
        return;
    m_libraryName = name;
    const Status status = name.isEmpty() ? Null : (m_library.load(name) ? Ready : Error);
    if (status == Error)
        qWarning("NativeCanvas: %s", qPrintable(m_library.errorString()));
    emit libraryChanged();
    if (status != m_status || status == Error) {
        m_status = status;
        emit statusChanged();
    }
    polish();
}

void NativeCanvas::setTime(qreal time)
{
    if (time == m_time)
        return;
    m_time = time;
    emit timeChanged();
    polish();
}

void NativeCanvas::geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry)
{
    QQuickItem::geometryChange(newGeometry, oldGeometry);
    if (newGeometry.size() != oldGeometry.size())
        polish();
}

void NativeCanvas::updatePolish()
{
    if (m_status != Ready || width() < 1 || height() < 1) {
        if (!m_image.isNull()) {
            m_image = QImage();
            update();
        }
        return;
    }

    const qreal dpr = window() ? window()->effectiveDevicePixelRatio() : 1.0;
    const qreal scale = qMin(dpr, MaxImageWidth / width());
    const QSize size(qMax(1, qRound(width() * scale)), qMax(1, qRound(height() * scale)));
    if (m_image.size() != size)
        m_image = QImage(size, QImage::Format_ARGB32_Premultiplied);

    QElapsedTimer timer;
    timer.start();
    m_library.draw(m_image, m_time);
    m_renderTime = timer.nsecsElapsed() / 1e6;
    emit renderTimeChanged();

    m_imageChanged = true;
    update();
}

QSGNode *NativeCanvas::updatePaintNode(QSGNode *oldNode, UpdatePaintNodeData *)
{
    if (m_image.isNull() || !window()) {
        delete oldNode;
        return nullptr;
    }

    auto *node = static_cast<QSGSimpleTextureNode *>(oldNode);
    if (!node) {
        node = new QSGSimpleTextureNode;
        node->setFiltering(QSGTexture::Linear);
        node->setOwnsTexture(true);
    }
    if (m_imageChanged || !node->texture()) {
        node->setTexture(window()->createTextureFromImage(m_image));
        m_imageChanged = false;
    }
    node->setRect(boundingRect());
    return node;
}
