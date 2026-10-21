#include "PicturePage.h"

#include <QCheckBox>
#include <QComboBox>
#include <QConicalGradient>
#include <QDial>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QImageReader>
#include <QLabel>
#include <QLinearGradient>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QRadialGradient>
#include <QRandomGenerator>
#include <QSlider>
#include <QVBoxLayout>
#include <QWheelEvent>

#include <cmath>

namespace {

constexpr double kPi = 3.14159265358979323846;

// A painted landscape: gradients, polygons with a seeded random outline,
// and a reflection. Everything is QPainter on a QImage.
QImage landscape()
{
    QImage image(1600, 1000, QImage::Format_ARGB32_Premultiplied);
    QPainter p(&image);
    p.setRenderHint(QPainter::Antialiasing);

    QLinearGradient sky(0, 0, 0, 640);
    sky.setColorAt(0.0, QColor(QStringLiteral("#1b2a5a")));
    sky.setColorAt(0.55, QColor(QStringLiteral("#d9607a")));
    sky.setColorAt(1.0, QColor(QStringLiteral("#ffc27a")));
    p.fillRect(0, 0, 1600, 640, sky);

    QRadialGradient sun(1050, 520, 260);
    sun.setColorAt(0.0, QColor(255, 244, 214, 255));
    sun.setColorAt(0.18, QColor(255, 220, 150, 255));
    sun.setColorAt(0.19, QColor(255, 190, 120, 140));
    sun.setColorAt(1.0, QColor(255, 160, 120, 0));
    p.fillRect(0, 0, 1600, 640, sun);

    QRandomGenerator random(7);
    p.setPen(Qt::NoPen);
    for (int i = 0; i < 90; ++i) {
        p.setBrush(QColor(255, 255, 255, 40 + random.bounded(120)));
        const qreal r = 0.6 + random.generateDouble() * 1.6;
        p.drawEllipse(QPointF(random.bounded(1600), random.bounded(260)), r, r);
    }

    const QList<QColor> ranges = { QColor(QStringLiteral("#6b4a7a")), QColor(QStringLiteral("#4a3561")),
                                   QColor(QStringLiteral("#2c2245")) };
    for (int layer = 0; layer < ranges.size(); ++layer) {
        QPainterPath ridge;
        const qreal base = 520 + layer * 55;
        ridge.moveTo(0, 640);
        qreal y = base - 80;
        for (int x = 0; x <= 1600; x += 40) {
            y = qBound(base - 260.0 + layer * 40, y + (random.generateDouble() - 0.5) * 90, base);
            ridge.lineTo(x, y);
        }
        ridge.lineTo(1600, 640);
        ridge.closeSubpath();
        p.fillPath(ridge, ranges.at(layer));
    }

    QLinearGradient water(0, 640, 0, 1000);
    water.setColorAt(0.0, QColor(QStringLiteral("#e5876f")));
    water.setColorAt(1.0, QColor(QStringLiteral("#162043")));
    p.fillRect(0, 640, 1600, 360, water);
    for (int i = 0; i < 46; ++i) {
        const qreal y = 650 + i * i * 0.17;
        const qreal half = 220 - i * 3.6;
        p.fillRect(QRectF(1050 - half + std::sin(i * 1.7) * 20, y, half * 2, 2.2),
                   QColor(255, 225, 170, 150 - i * 3));
    }

    for (int i = 0; i < 28; ++i) {
        const qreal x = 40 + i * 21 + random.bounded(14);
        const qreal h = 90 + random.bounded(110);
        QPainterPath tree;
        tree.moveTo(x, 650);
        tree.lineTo(x - h * 0.22, 650);
        tree.lineTo(x, 650 - h);
        tree.lineTo(x + h * 0.22, 650);
        tree.closeSubpath();
        p.fillPath(tree, QColor(QStringLiteral("#151229")));
    }
    return image;
}

QImage mandelbrot()
{
    const int w = 1200, h = 800;
    QImage image(w, h, QImage::Format_RGB32);
    const double cx = -0.7453, cy = 0.1127, scale = 0.0065 / h;
    for (int y = 0; y < h; ++y) {
        auto *line = reinterpret_cast<QRgb *>(image.scanLine(y));
        for (int x = 0; x < w; ++x) {
            const double a = cx + (x - w / 2.0) * scale;
            const double b = cy + (y - h / 2.0) * scale;
            double zr = 0, zi = 0;
            int n = 0;
            constexpr int limit = 400;
            while (n < limit && zr * zr + zi * zi < 16) {
                const double t = zr * zr - zi * zi + a;
                zi = 2 * zr * zi + b;
                zr = t;
                ++n;
            }
            if (n == limit) {
                line[x] = qRgb(8, 9, 16);
                continue;
            }
            const double smooth = n + 1 - std::log2(std::log2(zr * zr + zi * zi) / 2);
            const double t = smooth / 64.0;
            line[x] = QColor::fromHsvF(std::fmod(0.62 + t * 0.9, 1.0), 0.75,
                                       qBound(0.0, 0.25 + std::fmod(t * 3, 1.0) * 0.75, 1.0))
                              .rgb();
        }
    }
    return image;
}

QImage colorWheel()
{
    QImage image(1000, 1000, QImage::Format_ARGB32_Premultiplied);
    image.fill(QColor(QStringLiteral("#10131b")));
    QPainter p(&image);
    p.setRenderHint(QPainter::Antialiasing);

    QConicalGradient hue(500, 500, 0);
    for (int i = 0; i <= 12; ++i)
        hue.setColorAt(i / 12.0, QColor::fromHsvF(std::fmod(i / 12.0, 1.0), 1, 1));
    p.setPen(Qt::NoPen);
    p.setBrush(hue);
    p.drawEllipse(QPointF(500, 500), 420, 420);

    QRadialGradient fade(500, 500, 420);
    fade.setColorAt(0, Qt::white);
    fade.setColorAt(1, QColor(255, 255, 255, 0));
    p.setBrush(fade);
    p.drawEllipse(QPointF(500, 500), 420, 420);

    // Thin rings and spokes show how scaling and rotation filter edges.
    p.setBrush(Qt::NoBrush);
    for (int r = 60; r <= 420; r += 60) {
        p.setPen(QPen(QColor(0, 0, 0, 70), 1.2));
        p.drawEllipse(QPointF(500, 500), r, r);
    }
    for (int i = 0; i < 48; ++i) {
        const double a = i * 2 * kPi / 48;
        p.setPen(QPen(QColor(0, 0, 0, i % 4 == 0 ? 110 : 45), 1));
        p.drawLine(QPointF(500, 500), QPointF(500 + std::cos(a) * 440, 500 + std::sin(a) * 440));
    }
    QFont font;
    font.setPixelSize(30);
    font.setBold(true);
    p.setFont(font);
    p.setPen(QColor(QStringLiteral("#e6ecf9")));
    p.drawText(QRectF(0, 930, 1000, 50), Qt::AlignCenter, QStringLiteral("QConicalGradient + QRadialGradient"));
    return image;
}

} // namespace

PictureView::PictureView(QWidget *parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_OpaquePaintEvent);
    setCursor(Qt::OpenHandCursor);
    setMinimumSize(200, 150);
}

void PictureView::setImage(const QImage &image)
{
    m_image = image;
    m_pan = QPointF();
    fit();
}

qreal PictureView::fitZoom() const
{
    if (m_image.isNull() || width() < 1 || height() < 1)
        return 1;
    return qMin((width() - 24.0) / m_image.width(), (height() - 24.0) / m_image.height());
}

void PictureView::fit()
{
    m_fitted = true;
    m_pan = QPointF();
    m_zoom = fitZoom();
    emit zoomChanged(m_zoom);
    update();
}

void PictureView::setZoom(qreal zoom)
{
    zoom = qBound(0.02, zoom, 16.0);
    if (qFuzzyCompare(zoom, m_zoom))
        return;
    m_fitted = false;
    m_zoom = zoom;
    emit zoomChanged(m_zoom);
    update();
}

void PictureView::setRotation(qreal degrees)
{
    m_rotation = degrees;
    update();
}

void PictureView::setSmooth(bool on)
{
    m_smooth = on;
    update();
}

void PictureView::resizeEvent(QResizeEvent *event)
{
    if (m_fitted) {
        m_zoom = fitZoom();
        emit zoomChanged(m_zoom);
    }
    QWidget::resizeEvent(event);
}

void PictureView::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    // A checkerboard shows through transparent pictures.
    const int cell = 12;
    for (int y = 0; y < height(); y += cell) {
        for (int x = 0; x < width(); x += cell) {
            p.fillRect(x, y, cell, cell, ((x / cell + y / cell) % 2) ? QColor(QStringLiteral("#141821"))
                                                                    : QColor(QStringLiteral("#191e29")));
        }
    }
    if (m_image.isNull())
        return;

    p.setRenderHint(QPainter::SmoothPixmapTransform, m_smooth);
    p.setRenderHint(QPainter::Antialiasing, m_smooth);
    p.translate(QPointF(width() / 2.0, height() / 2.0) + m_pan);
    p.rotate(m_rotation);
    p.scale(m_zoom, m_zoom);
    const QRectF target(-m_image.width() / 2.0, -m_image.height() / 2.0, m_image.width(),
                        m_image.height());
    p.drawImage(target, m_image);
    p.setPen(QPen(QColor(255, 255, 255, 60), 1 / m_zoom));
    p.setBrush(Qt::NoBrush);
    p.drawRect(target);
}

void PictureView::wheelEvent(QWheelEvent *event)
{
    // Zoom around the pointer: the image point under it stays put.
    const qreal steps = event->angleDelta().y() / 120.0
            + (event->angleDelta().y() == 0 ? event->pixelDelta().y() / 60.0 : 0);
    if (qFuzzyIsNull(steps))
        return;
    const qreal before = m_zoom;
    setZoom(m_zoom * std::pow(1.15, steps));
    const QPointF centre = QPointF(width() / 2.0, height() / 2.0) + m_pan;
    const QPointF fromCentre = event->position() - centre;
    m_pan -= fromCentre * (m_zoom / before - 1);
    update();
    event->accept();
}

void PictureView::mousePressEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton)
        return;
    m_dragging = true;
    m_dragFrom = event->position();
    setCursor(Qt::ClosedHandCursor);
}

void PictureView::mouseMoveEvent(QMouseEvent *event)
{
    if (!m_dragging)
        return;
    m_pan += event->position() - m_dragFrom;
    m_dragFrom = event->position();
    m_fitted = false;
    update();
}

void PictureView::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton)
        return;
    m_dragging = false;
    setCursor(Qt::OpenHandCursor);
}

void PictureView::mouseDoubleClickEvent(QMouseEvent *)
{
    fit();
}

// -------------------------------------------------------------------------

PicturePage::PicturePage(QWidget *parent)
    : QWidget(parent)
{
    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(16, 12, 16, 16);
    layout->setSpacing(16);

    m_view = new PictureView;
    layout->addWidget(m_view, 1);

    auto *panel = new QWidget;
    panel->setFixedWidth(260);
    auto *form = new QFormLayout(panel);
    form->setContentsMargins(0, 0, 0, 0);
    form->setRowWrapPolicy(QFormLayout::WrapAllRows);

    m_source = new QComboBox;
    m_source->addItems({ QStringLiteral("Painted landscape"), QStringLiteral("Mandelbrot set"),
                         QStringLiteral("Color wheel") });
    connect(m_source, &QComboBox::activated, this, &PicturePage::showGenerated);
    form->addRow(QStringLiteral("Picture"), m_source);

    auto *zoomRow = new QHBoxLayout;
    m_zoom = new QSlider(Qt::Horizontal);
    m_zoom->setRange(2, 1600);
    m_zoomLabel = new QLabel;
    m_zoomLabel->setMinimumWidth(48);
    zoomRow->addWidget(m_zoom);
    zoomRow->addWidget(m_zoomLabel);
    form->addRow(QStringLiteral("Zoom"), zoomRow);
    connect(m_zoom, &QSlider::valueChanged, this, [this](int percent) {
        if (qRound(m_view->zoom() * 100) != percent)
            m_view->setZoom(percent / 100.0);
    });
    connect(m_view, &PictureView::zoomChanged, this, [this](qreal zoom) {
        QSignalBlocker block(m_zoom);
        m_zoom->setValue(qRound(zoom * 100));
        m_zoomLabel->setText(QStringLiteral("%1%").arg(qRound(zoom * 100)));
    });

    auto *dialRow = new QHBoxLayout;
    auto *dial = new QDial;
    dial->setRange(-180, 180);
    dial->setWrapping(true);
    dial->setNotchesVisible(true);
    dial->setFixedSize(76, 76);
    auto *angle = new QLabel(QStringLiteral("0°"));
    connect(dial, &QDial::valueChanged, this, [this, angle](int degrees) {
        m_view->setRotation(degrees);
        angle->setText(QStringLiteral("%1°").arg(degrees));
    });
    dialRow->addWidget(dial);
    dialRow->addWidget(angle);
    dialRow->addStretch();
    form->addRow(QStringLiteral("Rotation"), dialRow);

    auto *smooth = new QCheckBox(QStringLiteral("Smooth scaling"));
    smooth->setChecked(true);
    connect(smooth, &QCheckBox::toggled, m_view, &PictureView::setSmooth);
    form->addRow(smooth);

    auto *fit = new QPushButton(QStringLiteral("Fit to view"));
    connect(fit, &QPushButton::clicked, m_view, &PictureView::fit);
    form->addRow(fit);

    m_info = new QLabel;
    m_info->setWordWrap(true);
    m_info->setForegroundRole(QPalette::PlaceholderText);
    form->addRow(m_info);

    auto *hint = new QLabel(QStringLiteral("Wheel zooms around the pointer, drag pans, "
                                           "double-click fits. File › Open picture loads "
                                           "your own image."));
    hint->setWordWrap(true);
    hint->setForegroundRole(QPalette::PlaceholderText);
    form->addRow(hint);

    layout->addWidget(panel);
    showGenerated(0);
}

void PicturePage::showGenerated(int index)
{
    QImage image;
    switch (index) {
    case 0: image = landscape(); break;
    case 1: image = mandelbrot(); break;
    case 2: image = colorWheel(); break;
    default:
        return;
    }
    m_fileName.clear();
    m_view->setImage(image);
    describe();
    emit message(QStringLiteral("Showing ") + m_source->itemText(index));
}

void PicturePage::openFile(const QString &path)
{
    QImageReader reader(path);
    reader.setAutoTransform(true);
    const QImage image = reader.read();
    if (image.isNull()) {
        emit message(QStringLiteral("Could not open %1: %2").arg(QFileInfo(path).fileName(),
                                                                 reader.errorString()));
        return;
    }
    m_fileName = QFileInfo(path).fileName();
    if (m_source->count() == 3)
        m_source->addItem(m_fileName);
    else
        m_source->setItemText(3, m_fileName);
    m_source->setCurrentIndex(3);
    m_view->setImage(image);
    describe();
    emit message(QStringLiteral("Opened ") + m_fileName);
}

void PicturePage::describe()
{
    const QImage image = m_view->image();
    m_info->setText(QStringLiteral("%1 × %2 pixels, %3-bit")
                            .arg(image.width())
                            .arg(image.height())
                            .arg(image.depth()));
}
