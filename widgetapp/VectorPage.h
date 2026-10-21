#pragma once

#include <QGraphicsView>
#include <QWidget>

QT_BEGIN_NAMESPACE
class QGraphicsScene;
class QLabel;
class QTimer;
QT_END_NAMESPACE

class GearItem;

// A QGraphicsView that zooms with the wheel and draws a grid behind the scene.
class VectorView : public QGraphicsView
{
    Q_OBJECT

public:
    explicit VectorView(QGraphicsScene *scene, QWidget *parent = nullptr);

    qreal zoom() const;
    void setZoom(qreal zoom);

signals:
    void zoomChanged(qreal zoom);

protected:
    void wheelEvent(QWheelEvent *event) override;
    void drawBackground(QPainter *painter, const QRectF &rect) override;
};

// Vector graphics: shapes, gradients, Bézier paths, editable text, an SVG
// (when Qt SVG is available) and two animated gears. Shapes can be dragged,
// selected with a rubber band, added and deleted.
class VectorPage : public QWidget
{
    Q_OBJECT

public:
    explicit VectorPage(QWidget *parent = nullptr);

signals:
    void message(const QString &text);

protected:
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;

private:
    void populate();
    void addShape(int kind);
    void deleteSelected();

    QGraphicsScene *m_scene = nullptr;
    VectorView *m_view = nullptr;
    QTimer *m_animation = nullptr;
    GearItem *m_gears[2] = {};
    QLabel *m_zoomLabel = nullptr;
    bool m_animate = true;
    bool m_centred = false;
    int m_added = 0;
};
