#include "JuliaSet.h"

#include <QElapsedTimer>
#include <QQuickWindow>
#include <QSGSimpleTextureNode>
#include <QtMath>

namespace {

// The image is computed at no more than this many pixels across and scaled
// up with linear filtering, so dragging c stays interactive in Debug builds.
constexpr int MaxImageWidth = 560;

} // namespace

JuliaSet::JuliaSet(QQuickItem *parent)
    : QQuickItem(parent)
{
    setFlag(ItemHasContents, true);
}

void JuliaSet::setCx(qreal cx)
{
    if (cx == m_cx)
        return;
    m_cx = cx;
    emit cxChanged();
    invalidate();
}

void JuliaSet::setCy(qreal cy)
{
    if (cy == m_cy)
        return;
    m_cy = cy;
    emit cyChanged();
    invalidate();
}

void JuliaSet::setMaxIterations(int iterations)
{
    iterations = qBound(8, iterations, 2000);
    if (iterations == m_maxIterations)
        return;
    m_maxIterations = iterations;
    emit maxIterationsChanged();
    invalidate();
}

void JuliaSet::setHue(qreal hue)
{
    if (hue == m_hue)
        return;
    m_hue = hue;
    emit hueChanged();
    invalidate();
}

void JuliaSet::geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry)
{
    QQuickItem::geometryChange(newGeometry, oldGeometry);
    if (newGeometry.size() != oldGeometry.size())
        invalidate();
}

void JuliaSet::invalidate()
{
    // polish() asks for updatePolish() before the next frame; several
    // property changes in one turn of the event loop compute one image.
    polish();
}

qreal JuliaSet::unitsPerPixel() const
{
    // The set fits in about 3.2 × 2.2 units; show all of it, centred.
    if (width() <= 0 || height() <= 0)
        return 0;
    return qMax(3.2 / width(), 2.2 / height());
}

QPointF JuliaSet::toComplex(qreal x, qreal y) const
{
    const qreal unit = unitsPerPixel();
    return QPointF((x - width() / 2) * unit, (y - height() / 2) * unit);
}

void JuliaSet::updatePolish()
{
    if (width() < 1 || height() < 1)
        return;

    const qreal dpr = window() ? window()->effectiveDevicePixelRatio() : 1.0;
    const qreal scale = qMin(dpr, MaxImageWidth / width());
    const QSize size(qMax(1, qRound(width() * scale)), qMax(1, qRound(height() * scale)));

    QElapsedTimer timer;
    timer.start();

    QImage image(size, QImage::Format_RGB32);
    const qreal unit = unitsPerPixel() / scale;
    const qreal left = -size.width() / 2.0 * unit;
    const qreal top = -size.height() / 2.0 * unit;
    const qreal logTwo = qLn(2.0);

    for (int py = 0; py < size.height(); ++py) {
        auto *line = reinterpret_cast<QRgb *>(image.scanLine(py));
        const qreal zy0 = top + py * unit;
        for (int px = 0; px < size.width(); ++px) {
            qreal zx = left + px * unit;
            qreal zy = zy0;
            int n = 0;
            qreal r2 = zx * zx + zy * zy;
            while (n < m_maxIterations && r2 < 256.0) {
                const qreal x = zx * zx - zy * zy + m_cx;
                zy = 2 * zx * zy + m_cy;
                zx = x;
                r2 = zx * zx + zy * zy;
                ++n;
            }
            if (n >= m_maxIterations) {
                line[px] = qRgb(0x0b, 0x0d, 0x14);
                continue;
            }
            // Smooth escape count, so the bands blend instead of stepping.
            const qreal smooth = n + 1 - qLn(qLn(qSqrt(r2))) / logTwo;
            const qreal t = smooth / m_maxIterations;
            const qreal h = std::fmod(m_hue + smooth * 0.015, 1.0);
            const qreal v = qBound<qreal>(0.0, qPow(t, 0.35) * 1.15, 1.0);
            line[px] = QColor::fromHsvF(float(h), 0.75f, float(v)).rgb();
        }
    }

    m_image = image;
    m_imageChanged = true;
    m_renderTime = timer.nsecsElapsed() / 1e6;
    emit renderTimeChanged();
    update();
}

QSGNode *JuliaSet::updatePaintNode(QSGNode *oldNode, UpdatePaintNodeData *)
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
        // The node owns the texture, so the previous one is deleted here.
        node->setTexture(window()->createTextureFromImage(m_image));
        m_imageChanged = false;
    }
    node->setRect(boundingRect());
    return node;
}
