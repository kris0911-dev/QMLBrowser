#pragma once

#include <QColor>
#include <QImage>
#include <QPointF>
#include <QWidget>

QT_BEGIN_NAMESPACE
class QLabel;
QT_END_NAMESPACE

// A sheet of paper for freehand drawing. Strokes go into a QImage that grows
// with the widget, so nothing is lost when the tab is resized.
class PaintCanvas : public QWidget
{
    Q_OBJECT

public:
    explicit PaintCanvas(QWidget *parent = nullptr);

    void setPenColor(const QColor &color) { m_color = color; }
    void setPenWidth(int width) { m_width = width; update(); }
    void setEraser(bool on) { m_eraser = on; }
    void clear();
    void fillPattern();

signals:
    void strokeFinished(int strokes);

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;

private:
    void ensureImage(const QSize &size);
    void drawSegment(const QPointF &from, const QPointF &to);

    QImage m_image;
    QColor m_color = QColor(QStringLiteral("#1f4fbf"));
    int m_width = 6;
    bool m_eraser = false;
    bool m_drawing = false;
    QPointF m_last;
    QPointF m_hover = QPointF(-1, -1);
    int m_strokes = 0;
};

class PaintPage : public QWidget
{
    Q_OBJECT

public:
    explicit PaintPage(QWidget *parent = nullptr);

    void clear();

signals:
    void message(const QString &text);

private:
    PaintCanvas *m_canvas = nullptr;
};
