#include "WaveShape.h"

#include <QSGGeometryNode>
#include <QSGVertexColorMaterial>
#include <QtMath>

namespace {

// The scene graph blends with premultiplied alpha, and vertex colours are
// used as they are.
void setColored(QSGGeometry::ColoredPoint2D &v, float x, float y, const QColor &c)
{
    const float a = float(c.alphaF());
    v.set(x, y, uchar(c.red() * a), uchar(c.green() * a), uchar(c.blue() * a), uchar(a * 255));
}

QColor mix(const QColor &a, const QColor &b, qreal t)
{
    return QColor::fromRgbF(float(a.redF() + (b.redF() - a.redF()) * t),
                            float(a.greenF() + (b.greenF() - a.greenF()) * t),
                            float(a.blueF() + (b.blueF() - a.blueF()) * t),
                            float(a.alphaF() + (b.alphaF() - a.alphaF()) * t));
}

template <typename T>
bool assign(T &member, const T &value)
{
    if (member == value)
        return false;
    member = value;
    return true;
}

} // namespace

WaveShape::WaveShape(QQuickItem *parent)
    : QQuickItem(parent)
{
    // Without this flag updatePaintNode() is never called.
    setFlag(ItemHasContents, true);
}

void WaveShape::setAmplitude(qreal amplitude)
{
    if (assign(m_amplitude, amplitude)) {
        emit amplitudeChanged();
        update();
    }
}

void WaveShape::setFrequency(qreal frequency)
{
    if (assign(m_frequency, frequency)) {
        emit frequencyChanged();
        update();
    }
}

void WaveShape::setPhase(qreal phase)
{
    if (assign(m_phase, phase)) {
        emit phaseChanged();
        update();
    }
}

void WaveShape::setLineWidth(qreal width)
{
    if (assign(m_lineWidth, width)) {
        emit lineWidthChanged();
        update();
    }
}

void WaveShape::setLineColor(const QColor &color)
{
    if (assign(m_lineColor, color)) {
        emit lineColorChanged();
        update();
    }
}

void WaveShape::setFillTop(const QColor &color)
{
    if (assign(m_fillTop, color)) {
        emit fillTopChanged();
        update();
    }
}

void WaveShape::setFillBottom(const QColor &color)
{
    if (assign(m_fillBottom, color)) {
        emit fillBottomChanged();
        update();
    }
}

void WaveShape::geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry)
{
    QQuickItem::geometryChange(newGeometry, oldGeometry);
    if (newGeometry.size() != oldGeometry.size())
        update();
}

qreal WaveShape::valueAt(qreal x) const
{
    const qreal u = width() > 0 ? x / width() : 0;
    const qreal turn = 2 * M_PI * m_frequency * u;
    // A second, faster partial keeps it from looking like a textbook sine.
    const qreal s = 0.72 * qSin(turn + m_phase) + 0.28 * qSin(2.3 * turn - 1.7 * m_phase);
    const qreal margin = m_lineWidth;
    return height() / 2 - s * m_amplitude * (height() / 2 - margin);
}

QSGNode *WaveShape::updatePaintNode(QSGNode *oldNode, UpdatePaintNodeData *)
{
    if (width() <= 0 || height() <= 0) {
        delete oldNode;
        return nullptr;
    }

    // One parent with two children: the gradient fill, then the line on top.
    QSGNode *root = oldNode;
    QSGGeometryNode *fill = nullptr;
    QSGGeometryNode *line = nullptr;
    if (!root) {
        root = new QSGNode;

        fill = new QSGGeometryNode;
        auto *fillGeometry =
                new QSGGeometry(QSGGeometry::defaultAttributes_ColoredPoint2D(), 0);
        fillGeometry->setDrawingMode(QSGGeometry::DrawTriangleStrip);
        fill->setGeometry(fillGeometry);
        fill->setMaterial(new QSGVertexColorMaterial);
        fill->setFlags(QSGNode::OwnsGeometry | QSGNode::OwnsMaterial);
        root->appendChildNode(fill);

        // Indexed triangles: a solid core with a one-pixel feather on each
        // side, which is how the line gets smooth edges without multisampling.
        line = new QSGGeometryNode;
        auto *lineGeometry = new QSGGeometry(QSGGeometry::defaultAttributes_ColoredPoint2D(),
                                             0, 0, QSGGeometry::UnsignedShortType);
        lineGeometry->setDrawingMode(QSGGeometry::DrawTriangles);
        line->setGeometry(lineGeometry);
        line->setMaterial(new QSGVertexColorMaterial);
        line->setFlags(QSGNode::OwnsGeometry | QSGNode::OwnsMaterial);
        root->appendChildNode(line);
    } else {
        fill = static_cast<QSGGeometryNode *>(root->childAtIndex(0));
        line = static_cast<QSGGeometryNode *>(root->childAtIndex(1));
    }

    const int samples = qBound(16, int(width() / 2), 1024);
    QVector<QPointF> curve(samples);
    for (int i = 0; i < samples; ++i) {
        const qreal x = width() * i / (samples - 1);
        curve[i] = QPointF(x, valueAt(x));
    }

    // Fill: a strip from each curve point straight down to the bottom edge,
    // coloured by height so the gradient stays put while the wave moves.
    QSGGeometry *fillGeometry = fill->geometry();
    fillGeometry->allocate(samples * 2);
    QSGGeometry::ColoredPoint2D *fv = fillGeometry->vertexDataAsColoredPoint2D();
    const float bottom = float(height());
    for (int i = 0; i < samples; ++i) {
        const QPointF &p = curve[i];
        setColored(fv[i * 2], float(p.x()), float(p.y()),
                   mix(m_fillTop, m_fillBottom, p.y() / height()));
        setColored(fv[i * 2 + 1], float(p.x()), bottom, m_fillBottom);
    }
    fill->markDirty(QSGNode::DirtyGeometry);

    // Line: four vertices per sample across the normal, from the outer edge
    // of one feather to the outer edge of the other.
    const qreal core = qMax<qreal>(0.0, m_lineWidth / 2 - 0.5);
    const qreal outer = core + 1.0;
    QColor clear = m_lineColor;
    clear.setAlpha(0);

    QSGGeometry *lineGeometry = line->geometry();
    lineGeometry->allocate(samples * 4, (samples - 1) * 18);
    QSGGeometry::ColoredPoint2D *lv = lineGeometry->vertexDataAsColoredPoint2D();
    quint16 *index = lineGeometry->indexDataAsUShort();
    for (int i = 0; i < samples; ++i) {
        const QPointF before = curve[qMax(0, i - 1)];
        const QPointF after = curve[qMin(samples - 1, i + 1)];
        QPointF normal(before.y() - after.y(), after.x() - before.x());
        const qreal length = qSqrt(QPointF::dotProduct(normal, normal));
        if (length > 0)
            normal /= length;
        const QPointF &p = curve[i];
        const qreal offsets[4] = { -outer, -core, core, outer };
        for (int k = 0; k < 4; ++k) {
            const QPointF v = p + normal * offsets[k];
            setColored(lv[i * 4 + k], float(v.x()), float(v.y()),
                       (k == 0 || k == 3) ? clear : m_lineColor);
        }
    }
    for (int i = 0; i < samples - 1; ++i) {
        const quint16 a = quint16(i * 4);
        const quint16 b = quint16(a + 4);
        // Three quads per segment: feather, core, feather.
        for (int k = 0; k < 3; ++k) {
            *index++ = quint16(a + k);
            *index++ = quint16(b + k);
            *index++ = quint16(a + k + 1);
            *index++ = quint16(a + k + 1);
            *index++ = quint16(b + k);
            *index++ = quint16(b + k + 1);
        }
    }
    line->markDirty(QSGNode::DirtyGeometry);

    return root;
}
