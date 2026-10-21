#include "VectorPage.h"

#include <QCheckBox>
#include <QGraphicsEllipseItem>
#include <QGraphicsPathItem>
#include <QGraphicsRectItem>
#include <QGraphicsScene>
#include <QGraphicsTextItem>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QRandomGenerator>
#include <QSlider>
#include <QStyleOptionGraphicsItem>
#include <QTimer>
#include <QVBoxLayout>
#include <QWheelEvent>

#ifdef WIDGETAPP_HAVE_SVG
#  include <QSvgRenderer>
#endif

#include <cmath>

namespace {

constexpr double kPi = 3.14159265358979323846;

QPainterPath starPath(qreal outer, qreal inner, int points)
{
    QPainterPath path;
    for (int i = 0; i < points * 2; ++i) {
        const qreal r = (i % 2) ? inner : outer;
        const qreal a = -kPi / 2 + i * kPi / points;
        const QPointF p(std::cos(a) * r, std::sin(a) * r);
        if (i == 0)
            path.moveTo(p);
        else
            path.lineTo(p);
    }
    path.closeSubpath();
    return path;
}

QPainterPath gearPath(qreal radius, int teeth)
{
    QPainterPath path;
    const qreal depth = radius * 0.16;
    const int steps = teeth * 4;
    for (int i = 0; i <= steps; ++i) {
        const qreal a = i * 2 * kPi / steps;
        const qreal r = (i % 4 == 1 || i % 4 == 2) ? radius : radius - depth;
        const QPointF p(std::cos(a) * r, std::sin(a) * r);
        if (i == 0)
            path.moveTo(p);
        else
            path.lineTo(p);
    }
    path.closeSubpath();
    path.addEllipse(QPointF(), radius * 0.28, radius * 0.28);
    return path;
}

void makeInteractive(QGraphicsItem *item)
{
    item->setFlags(QGraphicsItem::ItemIsMovable | QGraphicsItem::ItemIsSelectable);
    item->setCursor(Qt::SizeAllCursor);
}

#ifdef WIDGETAPP_HAVE_SVG
const char kCompassSvg[] = R"svg(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 200 200">
  <defs>
    <radialGradient id="face" cx="50%" cy="45%" r="60%">
      <stop offset="0" stop-color="#2d3a5c"/><stop offset="1" stop-color="#121725"/>
    </radialGradient>
    <linearGradient id="north" x1="0" y1="0" x2="0" y2="1">
      <stop offset="0" stop-color="#ff8095"/><stop offset="1" stop-color="#c23a52"/>
    </linearGradient>
  </defs>
  <circle cx="100" cy="100" r="94" fill="url(#face)" stroke="#8ab4ff" stroke-width="4"/>
  <g stroke="#8ab4ff" stroke-width="2" opacity="0.6">
    <line x1="100" y1="12" x2="100" y2="26"/><line x1="100" y1="174" x2="100" y2="188"/>
    <line x1="12" y1="100" x2="26" y2="100"/><line x1="174" y1="100" x2="188" y2="100"/>
  </g>
  <polygon points="100,30 114,100 100,92 86,100" fill="url(#north)"/>
  <polygon points="100,170 86,100 100,108 114,100" fill="#d7deee"/>
  <circle cx="100" cy="100" r="8" fill="#f0b775"/>
  <text x="100" y="58" font-size="16" font-family="Helvetica" text-anchor="middle" fill="#ffffff">SVG</text>
</svg>)svg";

// Paints the SVG through QSvgRenderer, so it stays sharp at every zoom.
class SvgItem : public QGraphicsItem
{
public:
    SvgItem() : m_renderer(QByteArray(kCompassSvg)) {}

    QRectF boundingRect() const override { return QRectF(-80, -80, 160, 160); }

    void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *) override
    {
        m_renderer.render(painter, boundingRect());
        if (option->state & QStyle::State_Selected) {
            painter->setPen(QPen(QColor(QStringLiteral("#8ab4ff")), 0, Qt::DashLine));
            painter->setBrush(Qt::NoBrush);
            painter->drawRect(boundingRect());
        }
    }

private:
    QSvgRenderer m_renderer;
};
#endif

} // namespace

class GearItem : public QGraphicsItem
{
public:
    GearItem(qreal radius, int teeth, const QColor &color)
        : m_path(gearPath(radius, teeth))
        , m_radius(radius)
        , m_color(color)
    {
    }

    QRectF boundingRect() const override
    {
        return QRectF(-m_radius - 2, -m_radius - 2, m_radius * 2 + 4, m_radius * 2 + 4);
    }

    void paint(QPainter *painter, const QStyleOptionGraphicsItem *, QWidget *) override
    {
        QRadialGradient fill(QPointF(-m_radius * 0.3, -m_radius * 0.3), m_radius * 1.6);
        fill.setColorAt(0, m_color.lighter(140));
        fill.setColorAt(1, m_color.darker(150));
        painter->setPen(QPen(m_color.darker(220), 1.5));
        painter->setBrush(fill);
        painter->drawPath(m_path);
    }

private:
    QPainterPath m_path;
    qreal m_radius;
    QColor m_color;
};

VectorView::VectorView(QGraphicsScene *scene, QWidget *parent)
    : QGraphicsView(scene, parent)
{
    setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing
                   | QPainter::SmoothPixmapTransform);
    setDragMode(QGraphicsView::RubberBandDrag);
    setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    setViewportUpdateMode(QGraphicsView::SmartViewportUpdate);
    setMinimumSize(200, 150);
}

qreal VectorView::zoom() const
{
    return transform().m11();
}

void VectorView::setZoom(qreal zoom)
{
    zoom = qBound(0.1, zoom, 12.0);
    const qreal factor = zoom / this->zoom();
    if (qFuzzyCompare(factor, 1.0))
        return;
    scale(factor, factor);
    emit zoomChanged(zoom);
}

void VectorView::wheelEvent(QWheelEvent *event)
{
    const qreal steps = event->angleDelta().y() / 120.0
            + (event->angleDelta().y() == 0 ? event->pixelDelta().y() / 60.0 : 0);
    if (qFuzzyIsNull(steps))
        return;
    setZoom(zoom() * std::pow(1.15, steps));
    event->accept();
}

void VectorView::drawBackground(QPainter *painter, const QRectF &rect)
{
    painter->fillRect(rect, QColor(QStringLiteral("#0e1017")));
    const qreal step = 40;
    const qreal left = std::floor(rect.left() / step) * step;
    const qreal top = std::floor(rect.top() / step) * step;
    painter->setPen(QPen(QColor(QStringLiteral("#1a1f2c")), 0));
    for (qreal x = left; x < rect.right(); x += step)
        painter->drawLine(QPointF(x, rect.top()), QPointF(x, rect.bottom()));
    for (qreal y = top; y < rect.bottom(); y += step)
        painter->drawLine(QPointF(rect.left(), y), QPointF(rect.right(), y));
    painter->setPen(QPen(QColor(QStringLiteral("#2b3247")), 0));
    painter->drawLine(QPointF(rect.left(), 0), QPointF(rect.right(), 0));
    painter->drawLine(QPointF(0, rect.top()), QPointF(0, rect.bottom()));
}

VectorPage::VectorPage(QWidget *parent)
    : QWidget(parent)
{
    m_scene = new QGraphicsScene(this);
    m_scene->setSceneRect(-700, -450, 1400, 900);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(16, 12, 16, 16);

    auto *tools = new QHBoxLayout;
    const QStringList kinds = { QStringLiteral("Rectangle"), QStringLiteral("Ellipse"),
                                QStringLiteral("Star"), QStringLiteral("Curve") };
    for (int i = 0; i < kinds.size(); ++i) {
        auto *button = new QPushButton(QStringLiteral("+ ") + kinds.at(i));
        connect(button, &QPushButton::clicked, this, [this, i] { addShape(i); });
        tools->addWidget(button);
    }
    auto *remove = new QPushButton(QStringLiteral("Delete selected"));
    connect(remove, &QPushButton::clicked, this, &VectorPage::deleteSelected);
    tools->addWidget(remove);
    tools->addSpacing(12);

    auto *animate = new QCheckBox(QStringLiteral("Animate"));
    animate->setChecked(true);
    connect(animate, &QCheckBox::toggled, this, [this](bool on) {
        m_animate = on;
        if (on && isVisible())
            m_animation->start();
        else
            m_animation->stop();
    });
    tools->addWidget(animate);

    auto *antialias = new QCheckBox(QStringLiteral("Antialiasing"));
    antialias->setChecked(true);
    connect(antialias, &QCheckBox::toggled, this, [this](bool on) {
        m_view->setRenderHint(QPainter::Antialiasing, on);
        m_view->viewport()->update();
    });
    tools->addWidget(antialias);
    tools->addStretch();

    auto *zoom = new QSlider(Qt::Horizontal);
    zoom->setRange(10, 600);
    zoom->setValue(100);
    zoom->setFixedWidth(140);
    m_zoomLabel = new QLabel(QStringLiteral("100%"));
    m_zoomLabel->setMinimumWidth(44);
    auto *reset = new QPushButton(QStringLiteral("Reset view"));
    tools->addWidget(new QLabel(QStringLiteral("Zoom")));
    tools->addWidget(zoom);
    tools->addWidget(m_zoomLabel);
    tools->addWidget(reset);
    layout->addLayout(tools);

    m_view = new VectorView(m_scene);
    layout->addWidget(m_view, 1);

    connect(zoom, &QSlider::valueChanged, this,
            [this](int percent) { m_view->setZoom(percent / 100.0); });
    connect(m_view, &VectorView::zoomChanged, this, [this, zoom](qreal value) {
        QSignalBlocker block(zoom);
        zoom->setValue(qRound(value * 100));
        m_zoomLabel->setText(QStringLiteral("%1%").arg(qRound(value * 100)));
    });
    connect(reset, &QPushButton::clicked, this, [this] {
        m_view->resetTransform();
        m_view->centerOn(0, 0);
        emit m_view->zoomChanged(1.0);
    });
    connect(m_scene, &QGraphicsScene::selectionChanged, this, [this] {
        const int count = int(m_scene->selectedItems().size());
        if (count > 0)
            emit message(QStringLiteral("%1 item(s) selected").arg(count));
    });

    auto *hint = new QLabel(QStringLiteral("Drag shapes to move them, drag the background to "
                                           "select, wheel to zoom. Click the text to edit it."));
    hint->setForegroundRole(QPalette::PlaceholderText);
    layout->addWidget(hint);

    m_animation = new QTimer(this);
    m_animation->setInterval(33);
    connect(m_animation, &QTimer::timeout, this, [this] {
        m_gears[0]->setRotation(m_gears[0]->rotation() + 1.5);
        // 18 teeth against 12: the small gear turns 1.5 times as fast.
        m_gears[1]->setRotation(m_gears[1]->rotation() - 2.25);
    });

    populate();
}

void VectorPage::populate()
{
    // Rounded rectangle with a linear gradient.
    auto *card = new QGraphicsRectItem(-130, -70, 260, 140);
    QLinearGradient cardFill(-130, -70, 130, 70);
    cardFill.setColorAt(0, QColor(QStringLiteral("#3f6bd8")));
    cardFill.setColorAt(1, QColor(QStringLiteral("#7ce3b0")));
    card->setBrush(cardFill);
    card->setPen(QPen(QColor(QStringLiteral("#dce6ff")), 2));
    card->setPos(-380, -200);
    card->setRotation(-6);
    makeInteractive(card);
    m_scene->addItem(card);
    auto *cardText = new QGraphicsSimpleTextItem(QStringLiteral("QGraphicsRectItem"), card);
    QFont bold;
    bold.setPixelSize(18);
    bold.setBold(true);
    cardText->setFont(bold);
    cardText->setBrush(Qt::white);
    cardText->setPos(-cardText->boundingRect().width() / 2, -12);

    // Ellipse with a radial gradient.
    auto *orb = new QGraphicsEllipseItem(-90, -90, 180, 180);
    QRadialGradient orbFill(QPointF(-30, -35), 150);
    orbFill.setColorAt(0, QColor(QStringLiteral("#ffe3b0")));
    orbFill.setColorAt(0.45, QColor(QStringLiteral("#f0b775")));
    orbFill.setColorAt(1, QColor(QStringLiteral("#8a4a1f")));
    orb->setBrush(orbFill);
    orb->setPen(Qt::NoPen);
    orb->setPos(-60, -190);
    makeInteractive(orb);
    m_scene->addItem(orb);

    // Star polygon.
    auto *star = new QGraphicsPathItem(starPath(95, 42, 5));
    star->setBrush(QColor(QStringLiteral("#ff8095")));
    star->setPen(QPen(QColor(QStringLiteral("#ffd1d9")), 3, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    star->setPos(240, -190);
    makeInteractive(star);
    m_scene->addItem(star);

    // Cubic Bézier wave with a gradient pen and its control polygon.
    QPainterPath wave(QPointF(-560, 150));
    for (int i = 0; i < 4; ++i) {
        const qreal x = -560 + i * 280;
        wave.cubicTo(QPointF(x + 90, 20), QPointF(x + 190, 280), QPointF(x + 280, 150));
    }
    QLinearGradient penFill(-560, 0, 560, 0);
    penFill.setColorAt(0, QColor(QStringLiteral("#8ab4ff")));
    penFill.setColorAt(0.5, QColor(QStringLiteral("#c9a2ff")));
    penFill.setColorAt(1, QColor(QStringLiteral("#7ce3b0")));
    auto *curve = new QGraphicsPathItem(wave);
    curve->setPen(QPen(QBrush(penFill), 7, Qt::SolidLine, Qt::RoundCap));
    makeInteractive(curve);
    m_scene->addItem(curve);
    auto *dashed = new QGraphicsPathItem(wave);
    dashed->setPen(QPen(QColor(255, 255, 255, 120), 1.5, Qt::DashLine));
    dashed->setPos(0, 40);
    m_scene->addItem(dashed);

    // Editable rich text, which takes keyboard and input-method events
    // through the scene.
    auto *text = new QGraphicsTextItem;
    text->setHtml(QStringLiteral("<div style='color:#e6ecf9; font-size:20px'>"
                                 "<b>Editable vector text.</b> Click and type, IME included."
                                 "</div>"));
    text->setTextWidth(320);
    text->setTextInteractionFlags(Qt::TextEditorInteraction);
    text->setFlag(QGraphicsItem::ItemIsMovable);
    text->setPos(-560, 260);
    m_scene->addItem(text);

    m_gears[0] = new GearItem(90, 18, QColor(QStringLiteral("#7c8fb8")));
    m_gears[0]->setPos(200, 290);
    m_gears[1] = new GearItem(60, 12, QColor(QStringLiteral("#c9a2ff")));
    m_gears[1]->setPos(200 + 90 + 60 - 14, 290);
    m_gears[1]->setRotation(15);
    for (GearItem *gear : m_gears)
        m_scene->addItem(gear);

#ifdef WIDGETAPP_HAVE_SVG
    auto *svg = new SvgItem;
    svg->setPos(520, 40);
    makeInteractive(svg);
    m_scene->addItem(svg);
#else
    auto *note = new QGraphicsSimpleTextItem(QStringLiteral("(Built without Qt SVG)"));
    note->setBrush(QColor(QStringLiteral("#5f6a80")));
    note->setPos(450, 30);
    m_scene->addItem(note);
#endif
}

void VectorPage::addShape(int kind)
{
    QRandomGenerator *random = QRandomGenerator::global();
    const QColor color = QColor::fromHsvF(random->generateDouble(), 0.55, 0.95);
    QAbstractGraphicsShapeItem *item = nullptr;
    switch (kind) {
    case 0:
        item = new QGraphicsRectItem(-60, -40, 120, 80);
        break;
    case 1:
        item = new QGraphicsEllipseItem(-55, -55, 110, 110);
        break;
    case 2:
        item = new QGraphicsPathItem(starPath(60, 26, 5 + random->bounded(4)));
        break;
    default: {
        QPainterPath path(QPointF(-80, 0));
        path.cubicTo(QPointF(-30, -90), QPointF(30, 90), QPointF(80, 0));
        auto *curve = new QGraphicsPathItem(path);
        curve->setPen(QPen(color, 6, Qt::SolidLine, Qt::RoundCap));
        item = curve;
        break;
    }
    }
    if (kind != 3) {
        item->setBrush(color);
        item->setPen(QPen(color.lighter(150), 2));
    }
    // New shapes land in the middle of what is visible.
    const QPointF centre = m_view->mapToScene(m_view->viewport()->rect().center());
    item->setPos(centre + QPointF(random->bounded(160) - 80, random->bounded(120) - 60));
    makeInteractive(item);
    m_scene->addItem(item);
    m_scene->clearSelection();
    item->setSelected(true);
    emit message(QStringLiteral("Added shape %1").arg(++m_added));
}

void VectorPage::deleteSelected()
{
    const QList<QGraphicsItem *> selected = m_scene->selectedItems();
    for (QGraphicsItem *item : selected) {
        if (item == m_gears[0] || item == m_gears[1])
            continue;
        m_scene->removeItem(item);
        delete item;
    }
    emit message(QStringLiteral("Deleted %1 item(s)").arg(selected.size()));
}

void VectorPage::showEvent(QShowEvent *event)
{
    // Only animate while the page is on screen; a hidden page should not
    // keep the frame pump busy.
    if (m_animate)
        m_animation->start();
    QWidget::showEvent(event);

    if (!m_centred) {
        // The view has its real size only once it is shown.
        m_centred = true;
        QTimer::singleShot(0, this, [this] { m_view->centerOn(0, 40); });
    }
}

void VectorPage::hideEvent(QHideEvent *event)
{
    m_animation->stop();
    QWidget::hideEvent(event);
}
