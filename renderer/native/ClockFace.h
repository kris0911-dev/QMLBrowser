#pragma once

#include <QColor>
#include <QQuickPaintedItem>
#include <QTime>

class QTimer;

// An analog clock drawn with QPainter.
//
// QQuickPaintedItem is the easiest way to put C++ drawing in a page: paint()
// gets an ordinary QPainter, the item rasterizes into an image, and the scene
// graph shows that image like any other texture. The cost is a full repaint
// whenever update() is called, so it suits items that change a few times a
// second rather than every frame of a large surface.
class ClockFace : public QQuickPaintedItem
{
    Q_OBJECT

    Q_PROPERTY(QColor accent READ accent WRITE setAccent NOTIFY accentChanged)
    Q_PROPERTY(QColor face READ face WRITE setFace NOTIFY faceChanged)
    Q_PROPERTY(bool sweep READ sweep WRITE setSweep NOTIFY sweepChanged)
    Q_PROPERTY(int offsetMinutes READ offsetMinutes WRITE setOffsetMinutes NOTIFY offsetMinutesChanged)
    Q_PROPERTY(QString timeText READ timeText NOTIFY timeTextChanged)

public:
    explicit ClockFace(QQuickItem *parent = nullptr);

    QColor accent() const { return m_accent; }
    void setAccent(const QColor &accent);
    QColor face() const { return m_face; }
    void setFace(const QColor &face);
    // A moving second hand repaints 30 times a second; a ticking one, once.
    bool sweep() const { return m_sweep; }
    void setSweep(bool sweep);
    // Added to local time, so a page can show another zone.
    int offsetMinutes() const { return m_offsetMinutes; }
    void setOffsetMinutes(int minutes);
    QString timeText() const;

    void paint(QPainter *painter) override;

signals:
    void accentChanged();
    void faceChanged();
    void sweepChanged();
    void offsetMinutesChanged();
    void timeTextChanged();

private:
    void tick();
    QTime now() const;

    QColor m_accent = QColor(0x8a, 0xb4, 0xff);
    QColor m_face = QColor(0x16, 0x1a, 0x24);
    bool m_sweep = true;
    int m_offsetMinutes = 0;
    int m_lastSecond = -1;
    QTimer *m_timer = nullptr;
};
