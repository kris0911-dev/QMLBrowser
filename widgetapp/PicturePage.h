#pragma once

#include <QImage>
#include <QPointF>
#include <QWidget>

QT_BEGIN_NAMESPACE
class QComboBox;
class QLabel;
class QSlider;
QT_END_NAMESPACE

// Shows one raster picture with zoom, rotation and panning. The wheel zooms
// around the pointer, dragging pans, and a double click fits it again.
class PictureView : public QWidget
{
    Q_OBJECT

public:
    explicit PictureView(QWidget *parent = nullptr);

    void setImage(const QImage &image);
    QImage image() const { return m_image; }

    qreal zoom() const { return m_zoom; }
    void setZoom(qreal zoom);
    void setRotation(qreal degrees);
    void setSmooth(bool on);
    void fit();

signals:
    void zoomChanged(qreal zoom);

protected:
    void paintEvent(QPaintEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    qreal fitZoom() const;

    QImage m_image;
    qreal m_zoom = 1;
    qreal m_rotation = 0;
    QPointF m_pan;
    bool m_smooth = true;
    bool m_fitted = true;
    bool m_dragging = false;
    QPointF m_dragFrom;
};

class PicturePage : public QWidget
{
    Q_OBJECT

public:
    explicit PicturePage(QWidget *parent = nullptr);

    void openFile(const QString &path);

signals:
    void message(const QString &text);

private:
    void showGenerated(int index);
    void describe();

    PictureView *m_view = nullptr;
    QComboBox *m_source = nullptr;
    QSlider *m_zoom = nullptr;
    QLabel *m_zoomLabel = nullptr;
    QLabel *m_info = nullptr;
    QString m_fileName;
};
