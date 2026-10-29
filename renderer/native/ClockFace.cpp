#include "ClockFace.h"

#include <QPainter>
#include <QTimer>
#include <QtMath>

ClockFace::ClockFace(QQuickItem *parent)
    : QQuickPaintedItem(parent)
{
    setAntialiasing(true);
    m_timer = new QTimer(this);
    m_timer->setInterval(33);
    connect(m_timer, &QTimer::timeout, this, &ClockFace::tick);
    m_timer->start();
}

void ClockFace::setAccent(const QColor &accent)
{
    if (accent == m_accent)
        return;
    m_accent = accent;
    emit accentChanged();
    update();
}

void ClockFace::setFace(const QColor &face)
{
    if (face == m_face)
        return;
    m_face = face;
    emit faceChanged();
    update();
}

void ClockFace::setSweep(bool sweep)
{
    if (sweep == m_sweep)
        return;
    m_sweep = sweep;
    m_timer->setInterval(sweep ? 33 : 250);
    emit sweepChanged();
    update();
}

void ClockFace::setOffsetMinutes(int minutes)
{
    if (minutes == m_offsetMinutes)
        return;
    m_offsetMinutes = minutes;
    emit offsetMinutesChanged();
    emit timeTextChanged();
    update();
}

QString ClockFace::timeText() const
{
    return now().toString(QStringLiteral("HH:mm:ss"));
}

QTime ClockFace::now() const
{
    return QTime::currentTime().addSecs(m_offsetMinutes * 60);
}

void ClockFace::tick()
{
    const int second = now().second();
    const bool newSecond = second != m_lastSecond;
    m_lastSecond = second;
    if (newSecond)
        emit timeTextChanged();
    // A ticking hand only moves once a second, so the other polls draw nothing.
    if (m_sweep || newSecond)
        update();
}

void ClockFace::paint(QPainter *painter)
{
    const qreal side = qMin(width(), height());
    if (side <= 0)
        return;

    painter->setRenderHint(QPainter::Antialiasing);
    painter->translate(width() / 2, height() / 2);
    painter->scale(side / 200.0, side / 200.0);

    // Dial: a soft radial fill and a rim in the accent colour.
    QRadialGradient fill(QPointF(0, -30), 120);
    fill.setColorAt(0, m_face.lighter(135));
    fill.setColorAt(1, m_face);
    painter->setPen(QPen(m_accent, 3));
    painter->setBrush(fill);
    painter->drawEllipse(QPointF(0, 0), 96, 96);

    // Minute and hour marks.
    for (int i = 0; i < 60; ++i) {
        const bool hour = i % 5 == 0;
        painter->setPen(QPen(hour ? QColor(0xee, 0xf2, 0xfb) : QColor(0x5a, 0x64, 0x7c),
                             hour ? 3 : 1.2, Qt::SolidLine, Qt::RoundCap));
        painter->drawLine(QPointF(0, hour ? -78 : -84), QPointF(0, -89));
        painter->rotate(6);
    }

    QFont font = painter->font();
    font.setFamily(QStringLiteral("Segoe UI"));
    font.setPixelSize(13);
    font.setBold(true);
    painter->setFont(font);
    painter->setPen(QColor(0x98, 0xa2, 0xb8));
    for (int h = 1; h <= 12; ++h) {
        const qreal angle = qDegreesToRadians(h * 30.0);
        const QPointF centre(qSin(angle) * 64, -qCos(angle) * 64);
        painter->drawText(QRectF(centre.x() - 12, centre.y() - 9, 24, 18), Qt::AlignCenter,
                          QString::number(h));
    }

    const QTime t = now();
    const qreal seconds = t.second() + (m_sweep ? t.msec() / 1000.0 : 0.0);
    const qreal minutes = t.minute() + seconds / 60.0;
    const qreal hours = (t.hour() % 12) + minutes / 60.0;

    const auto hand = [painter](qreal degrees, qreal length, qreal tail, qreal widthPx,
                                const QColor &color) {
        painter->save();
        painter->rotate(degrees);
        painter->setPen(QPen(color, widthPx, Qt::SolidLine, Qt::RoundCap));
        painter->drawLine(QPointF(0, tail), QPointF(0, -length));
        painter->restore();
    };

    hand(hours * 30.0, 48, 10, 6, QColor(0xee, 0xf2, 0xfb));
    hand(minutes * 6.0, 70, 12, 4, QColor(0xdb, 0xe2, 0xf0));
    hand(seconds * 6.0, 80, 18, 1.6, m_accent);

    painter->setPen(Qt::NoPen);
    painter->setBrush(m_accent);
    painter->drawEllipse(QPointF(0, 0), 5, 5);
}
