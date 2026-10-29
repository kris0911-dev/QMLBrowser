#include "InputEvents.h"

#include <QNativeGestureEvent>
#include <QPointingDevice>
#include <QWheelEvent>
#include <QtGlobal>

namespace {

QPointF pointFrom(const QJsonObject &message)
{
    return QPointF(message.value(QStringLiteral("x")).toDouble(),
                   message.value(QStringLiteral("y")).toDouble());
}

bool fromTouchpad(const QInputEvent *event)
{
    return event->device() && event->device()->type() == QInputDevice::DeviceType::TouchPad;
}

const QPointingDevice *trackpad()
{
    // One for the life of the process, as a platform plugin keeps one per
    // physical device. Pointer handlers keep per-device state.
    static const QPointingDevice *const device = new QPointingDevice(
            QStringLiteral("browser trackpad"), 0x7462, QInputDevice::DeviceType::TouchPad,
            QPointingDevice::PointerType::Finger,
            QInputDevice::Capability::Position | QInputDevice::Capability::Scroll
                    | QInputDevice::Capability::PixelScroll,
            5, 0);
    return device;
}

} // namespace

namespace InputEvents {

void stamp(QJsonObject &message, const QInputEvent *event)
{
    message.insert(QStringLiteral("t"), static_cast<double>(event->timestamp()));
}

QJsonObject wheelMessage(const QWheelEvent *event)
{
    QJsonObject message;
    message.insert(QStringLiteral("kind"), QStringLiteral("wheel"));
    message.insert(QStringLiteral("x"), event->position().x());
    message.insert(QStringLiteral("y"), event->position().y());
    message.insert(QStringLiteral("px"), event->pixelDelta().x());
    message.insert(QStringLiteral("py"), event->pixelDelta().y());
    message.insert(QStringLiteral("ax"), event->angleDelta().x());
    message.insert(QStringLiteral("ay"), event->angleDelta().y());
    message.insert(QStringLiteral("buttons"), int(event->buttons()));
    message.insert(QStringLiteral("mods"), int(event->modifiers()));
    message.insert(QStringLiteral("phase"), int(event->phase()));
    message.insert(QStringLiteral("inverted"), event->inverted());
    message.insert(QStringLiteral("source"), int(event->source()));
    message.insert(QStringLiteral("touchpad"), fromTouchpad(event));
    stamp(message, event);
    return message;
}

QJsonObject gestureMessage(const QNativeGestureEvent *event)
{
    QJsonObject message;
    message.insert(QStringLiteral("kind"), QStringLiteral("gesture"));
    message.insert(QStringLiteral("type"), int(event->gestureType()));
    message.insert(QStringLiteral("x"), event->position().x());
    message.insert(QStringLiteral("y"), event->position().y());
    message.insert(QStringLiteral("value"), event->value());
    message.insert(QStringLiteral("dx"), event->delta().x());
    message.insert(QStringLiteral("dy"), event->delta().y());
    message.insert(QStringLiteral("fingers"), event->fingerCount());
    message.insert(QStringLiteral("touchpad"), fromTouchpad(event));
    stamp(message, event);
    return message;
}

const QPointingDevice *device(const QJsonObject &message)
{
    return message.value(QStringLiteral("touchpad")).toBool()
            ? trackpad()
            : QPointingDevice::primaryPointingDevice();
}

Qt::MouseEventSource source(const QJsonObject &message)
{
    return Qt::MouseEventSource(message.value(QStringLiteral("source")).toInt());
}

std::unique_ptr<QWheelEvent> wheelEvent(const QJsonObject &message, quint64 timestamp)
{
    const QPointF pos = pointFrom(message);
    const QPoint pixel(message.value(QStringLiteral("px")).toInt(),
                       message.value(QStringLiteral("py")).toInt());
    const QPoint angle(message.value(QStringLiteral("ax")).toInt(),
                       message.value(QStringLiteral("ay")).toInt());
    auto event = std::make_unique<QWheelEvent>(
            pos, pos, pixel, angle,
            Qt::MouseButtons(message.value(QStringLiteral("buttons")).toInt()),
            Qt::KeyboardModifiers(message.value(QStringLiteral("mods")).toInt()),
            Qt::ScrollPhase(message.value(QStringLiteral("phase")).toInt()),
            message.value(QStringLiteral("inverted")).toBool(), source(message), device(message));
    event->setTimestamp(timestamp);
    return event;
}

std::unique_ptr<QNativeGestureEvent> gestureEvent(const QJsonObject &message, quint64 timestamp)
{
    const QPointF pos = pointFrom(message);
    const QPointF delta(message.value(QStringLiteral("dx")).toDouble(),
                        message.value(QStringLiteral("dy")).toDouble());
    auto event = std::make_unique<QNativeGestureEvent>(
            Qt::NativeGestureType(message.value(QStringLiteral("type")).toInt()), device(message),
            message.value(QStringLiteral("fingers")).toInt(), pos, pos, pos,
            message.value(QStringLiteral("value")).toDouble(), delta);
    event->setTimestamp(timestamp);
    return event;
}

} // namespace InputEvents

InputClock::InputClock()
{
    m_clock.start();
}

quint64 InputClock::time(const QJsonObject &message)
{
    const qint64 now = m_clock.elapsed() + 1;
    const qint64 sent = qint64(message.value(QStringLiteral("t")).toDouble());
    if (sent <= 0)
        return quint64(now);
    if (!m_anchored || qAbs(sent + m_offset - now) > 1000) {
        m_offset = now - sent;
        m_anchored = true;
    }
    return quint64(qMax<qint64>(sent + m_offset, 1));
}

namespace {

// How far the fingers must travel, mostly sideways, in pixels.
constexpr qreal kSwipeDistance = 80;

QJsonObject swipe(bool back, int fingers)
{
    QJsonObject message;
    message.insert(QStringLiteral("direction"),
                   back ? QStringLiteral("back") : QStringLiteral("forward"));
    message.insert(QStringLiteral("fingers"), fingers);
    return message;
}

} // namespace

QJsonObject SwipeNavigation::observe(const QJsonObject &message, bool taken)
{
    const QString kind = message.value(QStringLiteral("kind")).toString();
    if (kind == QLatin1String("gesture")) {
        if (taken
            || message.value(QStringLiteral("type")).toInt() != int(Qt::SwipeNativeGesture))
            return {};
        // Qt turns AppKit's deltaX into an angle: 180 for what AppKit calls a
        // swipe left, which Chrome and Firefox take as Back; 0 the other way.
        const int angle = qRound(message.value(QStringLiteral("value")).toDouble());
        const int fingers = message.value(QStringLiteral("fingers")).toInt();
        if (angle == 180)
            return swipe(true, fingers);
        if (angle == 0)
            return swipe(false, fingers);
        return {};
    }

    // Only a trackpad (or Magic Mouse) scroll has phases and is synthesized.
    if (kind != QLatin1String("wheel")
        || InputEvents::source(message) != Qt::MouseEventSynthesizedBySystem)
        return {};

    const QPointF delta(message.value(QStringLiteral("px")).toDouble(),
                        message.value(QStringLiteral("py")).toDouble());
    switch (Qt::ScrollPhase(message.value(QStringLiteral("phase")).toInt())) {
    case Qt::ScrollBegin:
        m_tracking = true;
        m_taken = false;
        m_travel = QPointF();
        Q_FALLTHROUGH();
    case Qt::ScrollUpdate:
        // macOS reports the start of momentum as an update too.
        if (m_tracking) {
            m_travel += delta;
            m_taken = m_taken || taken;
            m_inverted = message.value(QStringLiteral("inverted")).toBool();
        }
        return {};
    case Qt::ScrollMomentum:
    case Qt::ScrollEnd: {
        // The fingers have lifted: decide once.
        if (!m_tracking)
            return {};
        m_tracking = false;
        if (m_taken || qAbs(m_travel.x()) < kSwipeDistance
            || qAbs(m_travel.x()) < 2 * qAbs(m_travel.y()))
            return {};
        // With natural scrolling the deltas follow the fingers; without, they
        // are the other way round.
        const qreal fingersX = m_inverted ? m_travel.x() : -m_travel.x();
        return swipe(fingersX > 0, 2);
    }
    case Qt::NoScrollPhase:
        break;
    }
    return {};
}
