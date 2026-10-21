#include "PaintPage.h"

#include <QButtonGroup>
#include <QContextMenuEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QSlider>
#include <QToolButton>
#include <QVBoxLayout>

#include <cmath>

namespace {

const QColor kPaper(QStringLiteral("#f7f4ec"));
constexpr double kPi = 3.14159265358979323846;

} // namespace

PaintCanvas::PaintCanvas(QWidget *parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_OpaquePaintEvent);
    setMouseTracking(true);
    setCursor(Qt::CrossCursor);
    setMinimumSize(200, 150);
}

void PaintCanvas::ensureImage(const QSize &size)
{
    // Grows, never shrinks: making the tab smaller and then larger again
    // keeps what was drawn near the edges.
    const QSize wanted = size.expandedTo(m_image.size());
    if (wanted == m_image.size())
        return;
    QImage grown(wanted, QImage::Format_ARGB32_Premultiplied);
    grown.fill(kPaper);
    QPainter painter(&grown);
    painter.drawImage(0, 0, m_image);
    m_image = grown;
}

void PaintCanvas::clear()
{
    m_image.fill(kPaper);
    m_strokes = 0;
    update();
}

void PaintCanvas::fillPattern()
{
    // A spirograph, so there is something to draw over right away.
    QPainter painter(&m_image);
    painter.setRenderHint(QPainter::Antialiasing);
    const QPointF centre(width() / 2.0, height() / 2.0);
    const qreal big = qMin(width(), height()) * 0.42;
    for (int ring = 0; ring < 3; ++ring) {
        const qreal r = big * (0.55 + 0.2 * ring);
        const qreal k = 0.31 + 0.11 * ring;
        QPainterPath path;
        for (int i = 0; i <= 2400; ++i) {
            const qreal t = i * 2 * kPi / 240.0;
            const QPointF p = centre + QPointF((1 - k) * std::cos(t) + 0.7 * k * std::cos((1 - k) / k * t),
                                               (1 - k) * std::sin(t) - 0.7 * k * std::sin((1 - k) / k * t)) * r;
            if (i == 0)
                path.moveTo(p);
            else
                path.lineTo(p);
        }
        painter.setPen(QPen(QColor::fromHsvF(0.58 + ring * 0.12, 0.65, 0.8, 0.8), 1.4));
        painter.drawPath(path);
    }
    update();
}

void PaintCanvas::resizeEvent(QResizeEvent *event)
{
    ensureImage(size());
    QWidget::resizeEvent(event);
}

void PaintCanvas::paintEvent(QPaintEvent *event)
{
    QPainter painter(this);
    painter.drawImage(event->rect(), m_image, event->rect());

    // Brush preview under the pointer, which shows that hover moves arrive.
    if (m_hover.x() >= 0 && !m_drawing) {
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(QPen(QColor(0, 0, 0, 110), 1, Qt::DashLine));
        painter.setBrush(Qt::NoBrush);
        const qreal r = qMax(2.0, m_width / 2.0);
        painter.drawEllipse(m_hover, r, r);
    }
}

void PaintCanvas::drawSegment(const QPointF &from, const QPointF &to)
{
    QPainter painter(&m_image);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(QPen(m_eraser ? kPaper : m_color, m_eraser ? m_width * 3 : m_width,
                        Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.drawLine(from, to);
    const int pad = (m_eraser ? m_width * 3 : m_width) + 2;
    update(QRectF(from, to).normalized().adjusted(-pad, -pad, pad, pad).toAlignedRect());
}

void PaintCanvas::mousePressEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton)
        return;
    m_drawing = true;
    m_last = event->position();
    drawSegment(m_last, m_last + QPointF(0.01, 0));
}

void PaintCanvas::mouseMoveEvent(QMouseEvent *event)
{
    const QPointF previousHover = m_hover;
    m_hover = event->position();
    if (m_drawing && (event->buttons() & Qt::LeftButton)) {
        drawSegment(m_last, event->position());
        m_last = event->position();
        return;
    }
    const int pad = m_width + 4;
    update(QRectF(previousHover, QSizeF(1, 1)).adjusted(-pad, -pad, pad, pad).toAlignedRect());
    update(QRectF(m_hover, QSizeF(1, 1)).adjusted(-pad, -pad, pad, pad).toAlignedRect());
}

void PaintCanvas::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton || !m_drawing)
        return;
    m_drawing = false;
    emit strokeFinished(++m_strokes);
}

void PaintCanvas::leaveEvent(QEvent *event)
{
    m_hover = QPointF(-1, -1);
    update();
    QWidget::leaveEvent(event);
}

void PaintCanvas::contextMenuEvent(QContextMenuEvent *event)
{
    QMenu menu(this);
    menu.addAction(QStringLiteral("Clear"), this, &PaintCanvas::clear);
    menu.addAction(QStringLiteral("Draw a spirograph"), this, &PaintCanvas::fillPattern);
    menu.exec(event->globalPos());
}

// -------------------------------------------------------------------------

PaintPage::PaintPage(QWidget *parent)
    : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(16, 12, 16, 16);

    auto *tools = new QHBoxLayout;
    tools->addWidget(new QLabel(QStringLiteral("Color")));

    m_canvas = new PaintCanvas;

    auto *colors = new QButtonGroup(this);
    const QList<QColor> palette = { QColor(QStringLiteral("#1f4fbf")), QColor(QStringLiteral("#d23c4f")),
                                    QColor(QStringLiteral("#1f9e6e")), QColor(QStringLiteral("#e39b2d")),
                                    QColor(QStringLiteral("#7a4bd1")), QColor(QStringLiteral("#20242e")) };
    for (int i = 0; i < palette.size(); ++i) {
        const QColor color = palette.at(i);
        auto *button = new QToolButton;
        button->setCheckable(true);
        button->setChecked(i == 0);
        button->setFixedSize(28, 28);
        button->setStyleSheet(QStringLiteral(
                "QToolButton { background: %1; border: 2px solid #2b3247; border-radius: 6px; }"
                "QToolButton:checked { border: 2px solid #ffffff; }").arg(color.name()));
        button->setToolTip(color.name());
        colors->addButton(button, i);
        tools->addWidget(button);
        connect(button, &QToolButton::clicked, this, [this, color] {
            m_canvas->setPenColor(color);
            m_canvas->setEraser(false);
        });
    }

    auto *eraser = new QToolButton;
    eraser->setText(QStringLiteral("Eraser"));
    eraser->setCheckable(true);
    colors->addButton(eraser, int(palette.size()));
    connect(eraser, &QToolButton::toggled, this, [this](bool on) { m_canvas->setEraser(on); });
    tools->addSpacing(8);
    tools->addWidget(eraser);

    tools->addSpacing(16);
    tools->addWidget(new QLabel(QStringLiteral("Width")));
    auto *width = new QSlider(Qt::Horizontal);
    width->setRange(1, 40);
    width->setValue(6);
    width->setFixedWidth(140);
    auto *widthLabel = new QLabel(QStringLiteral("6 px"));
    widthLabel->setMinimumWidth(40);
    connect(width, &QSlider::valueChanged, this, [this, widthLabel](int value) {
        m_canvas->setPenWidth(value);
        widthLabel->setText(QStringLiteral("%1 px").arg(value));
    });
    tools->addWidget(width);
    tools->addWidget(widthLabel);
    tools->addStretch();

    auto *pattern = new QPushButton(QStringLiteral("Spirograph"));
    connect(pattern, &QPushButton::clicked, m_canvas, &PaintCanvas::fillPattern);
    tools->addWidget(pattern);
    auto *clearButton = new QPushButton(QStringLiteral("Clear"));
    connect(clearButton, &QPushButton::clicked, this, &PaintPage::clear);
    tools->addWidget(clearButton);

    layout->addLayout(tools);
    layout->addWidget(m_canvas, 1);

    auto *hint = new QLabel(QStringLiteral("Drag to draw. Right-click the paper for a context menu."));
    hint->setForegroundRole(QPalette::PlaceholderText);
    layout->addWidget(hint);

    connect(m_canvas, &PaintCanvas::strokeFinished, this, [this](int strokes) {
        emit message(QStringLiteral("%1 stroke(s) on the canvas").arg(strokes));
    });
}

void PaintPage::clear()
{
    m_canvas->clear();
    emit message(QStringLiteral("Canvas cleared"));
}
