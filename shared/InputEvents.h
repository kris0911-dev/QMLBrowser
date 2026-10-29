#pragma once

#include <QElapsedTimer>
#include <QJsonObject>
#include <QPointF>

#include <memory>

class QInputEvent;
class QNativeGestureEvent;
class QPointingDevice;
class QWheelEvent;

// What a forwarded pointer event carries besides its position.
//
// The page rebuilds every event the browser forwards, and Qt Quick reads more
// of an event than its position and deltas. Flickable scrolls by pixels only
// when the platform marks a wheel event as synthesized, which macOS does for
// a trackpad; any other wheel event is taken for mouse wheel notches, so a
// two-finger scroll jumped and raced. It measures speed from event times and
// ignores the delta of an event that is no later than the one before it. A
// pinch, rotation or smart zoom arrives as native gesture events from the
// trackpad device.
namespace InputEvents {

// Browser side. stamp() adds the time the platform gave the event ("t").
void stamp(QJsonObject &message, const QInputEvent *event);
QJsonObject wheelMessage(const QWheelEvent *event);
QJsonObject gestureMessage(const QNativeGestureEvent *event);

// Page side. The device is this process's stand-in for the browser's
// trackpad, or the primary pointing device for anything else.
const QPointingDevice *device(const QJsonObject &message);
Qt::MouseEventSource source(const QJsonObject &message);
std::unique_ptr<QWheelEvent> wheelEvent(const QJsonObject &message, quint64 timestamp);
std::unique_ptr<QNativeGestureEvent> gestureEvent(const QJsonObject &message, quint64 timestamp);

} // namespace InputEvents

// Puts the browser's event times on this process's clock.
//
// Events read from the pipe in one go would otherwise all get the moment they
// were read, and their real spacing is what a page measures speed from. The
// browser's clock is shifted to start where this one is, and shifted again if
// the two drift more than a second apart: a renderer started in the middle of
// a session, or Windows' 32-bit message clock wrapping. Never 0, which Qt
// takes for no timestamp.
class InputClock
{
public:
    InputClock();

    quint64 time(const QJsonObject &message);

private:
    QElapsedTimer m_clock;
    qint64 m_offset = 0;
    bool m_anchored = false;
};

// Swipe between pages: a sideways trackpad swipe the page did not use.
//
// macOS leaves this gesture to the application, and Qt does not implement
// it. Two fingers arrive as a horizontal scroll, and three fingers, with that
// setting, as one swipe gesture. The page sees every event first, as in
// Safari, so a horizontal list still scrolls. A swipe nothing in the page
// took becomes Back (fingers moving right) or Forward once the fingers lift.
// Mouse wheels never swipe.
class SwipeNavigation
{
public:
    // The input message just delivered and whether the page accepted it.
    // Returns the message for the browser without its "type": "direction"
    // ("back" or "forward") and "fingers", or an empty object.
    QJsonObject observe(const QJsonObject &message, bool taken);

private:
    QPointF m_travel;
    bool m_tracking = false;
    bool m_taken = false;
    bool m_inverted = false;
};
