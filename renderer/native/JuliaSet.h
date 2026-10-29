#pragma once

#include <QImage>
#include <QQuickItem>

// A Julia set computed pixel by pixel in C++ and shown as a GPU texture.
//
// The third way to draw from C++: fill a QImage with any code you like, then
// hand it to the scene graph as a texture. The image is rebuilt in
// updatePolish(), which runs on the GUI thread before each frame that needs
// it, and updatePaintNode() only uploads it. Keeping the work in polish means
// the item can emit signals (renderTime) safely, whatever render loop is in use.
class JuliaSet : public QQuickItem
{
    Q_OBJECT

    Q_PROPERTY(qreal cx READ cx WRITE setCx NOTIFY cxChanged)
    Q_PROPERTY(qreal cy READ cy WRITE setCy NOTIFY cyChanged)
    Q_PROPERTY(int maxIterations READ maxIterations WRITE setMaxIterations NOTIFY maxIterationsChanged)
    Q_PROPERTY(qreal hue READ hue WRITE setHue NOTIFY hueChanged)
    Q_PROPERTY(qreal renderTime READ renderTime NOTIFY renderTimeChanged)

public:
    explicit JuliaSet(QQuickItem *parent = nullptr);

    // The constant c in z = z² + c.
    qreal cx() const { return m_cx; }
    void setCx(qreal cx);
    qreal cy() const { return m_cy; }
    void setCy(qreal cy);
    int maxIterations() const { return m_maxIterations; }
    void setMaxIterations(int iterations);
    // 0–1, where the palette starts on the colour wheel.
    qreal hue() const { return m_hue; }
    void setHue(qreal hue);
    // Milliseconds the last image took to compute.
    qreal renderTime() const { return m_renderTime; }

    // Maps a point in item coordinates to the complex plane, so a page can
    // let the pointer pick c.
    Q_INVOKABLE QPointF toComplex(qreal x, qreal y) const;

signals:
    void cxChanged();
    void cyChanged();
    void maxIterationsChanged();
    void hueChanged();
    void renderTimeChanged();

protected:
    void updatePolish() override;
    QSGNode *updatePaintNode(QSGNode *oldNode, UpdatePaintNodeData *) override;
    void geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry) override;

private:
    void invalidate();
    // Units of the complex plane per item pixel.
    qreal unitsPerPixel() const;

    qreal m_cx = -0.8;
    qreal m_cy = 0.156;
    int m_maxIterations = 120;
    qreal m_hue = 0.6;
    qreal m_renderTime = 0;
    QImage m_image;
    bool m_imageChanged = false;
};
