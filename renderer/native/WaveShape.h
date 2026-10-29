#pragma once

#include <QColor>
#include <QQuickItem>

// A filled waveform built directly from scene graph geometry.
//
// No painter and no image: updatePaintNode() writes vertices into
// QSGGeometry and the GPU draws them as triangles. Nothing is rasterized on
// the CPU, so QML can animate `phase` every frame and only the vertex buffer
// changes. This is how Qt Quick's own Rectangle and Shape are drawn.
class WaveShape : public QQuickItem
{
    Q_OBJECT

    Q_PROPERTY(qreal amplitude READ amplitude WRITE setAmplitude NOTIFY amplitudeChanged)
    Q_PROPERTY(qreal frequency READ frequency WRITE setFrequency NOTIFY frequencyChanged)
    Q_PROPERTY(qreal phase READ phase WRITE setPhase NOTIFY phaseChanged)
    Q_PROPERTY(qreal lineWidth READ lineWidth WRITE setLineWidth NOTIFY lineWidthChanged)
    Q_PROPERTY(QColor lineColor READ lineColor WRITE setLineColor NOTIFY lineColorChanged)
    Q_PROPERTY(QColor fillTop READ fillTop WRITE setFillTop NOTIFY fillTopChanged)
    Q_PROPERTY(QColor fillBottom READ fillBottom WRITE setFillBottom NOTIFY fillBottomChanged)

public:
    explicit WaveShape(QQuickItem *parent = nullptr);

    // 0 is a flat line, 1 reaches the top and bottom edges.
    qreal amplitude() const { return m_amplitude; }
    void setAmplitude(qreal amplitude);
    // Cycles across the item's width.
    qreal frequency() const { return m_frequency; }
    void setFrequency(qreal frequency);
    qreal phase() const { return m_phase; }
    void setPhase(qreal phase);
    qreal lineWidth() const { return m_lineWidth; }
    void setLineWidth(qreal width);
    QColor lineColor() const { return m_lineColor; }
    void setLineColor(const QColor &color);
    QColor fillTop() const { return m_fillTop; }
    void setFillTop(const QColor &color);
    QColor fillBottom() const { return m_fillBottom; }
    void setFillBottom(const QColor &color);

    // The curve's y, in item coordinates, at item x. Lets QML put things on it.
    Q_INVOKABLE qreal valueAt(qreal x) const;

signals:
    void amplitudeChanged();
    void frequencyChanged();
    void phaseChanged();
    void lineWidthChanged();
    void lineColorChanged();
    void fillTopChanged();
    void fillBottomChanged();

protected:
    QSGNode *updatePaintNode(QSGNode *oldNode, UpdatePaintNodeData *) override;
    void geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry) override;

private:
    qreal m_amplitude = 0.6;
    qreal m_frequency = 2.0;
    qreal m_phase = 0.0;
    qreal m_lineWidth = 3.0;
    QColor m_lineColor = QColor(0x7c, 0xe3, 0xb0);
    QColor m_fillTop = QColor(0x7c, 0xe3, 0xb0, 0x90);
    QColor m_fillBottom = QColor(0x7c, 0xe3, 0xb0, 0x00);
};
