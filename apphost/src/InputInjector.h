#pragma once

#include <QEvent>
#include <QJsonObject>
#include <QObject>
#include <QPointer>
#include <QPointF>
#include <QString>

#include "InputEvents.h"

QT_BEGIN_NAMESPACE
class QTimer;
class QWidget;
QT_END_NAMESPACE

class WindowCompositor;

// Replays the browser's input in the application.
//
// Pointer, wheel, gesture, key and focus events go through
// QWindowSystemInterface, synchronously, into the window under the pointer or
// the active popup, as if the platform had produced them. Qt then does its
// normal work: double clicks, enter and leave, hover, tooltips, popup
// dismissal, modal blocking and shortcuts. Input-method events go straight to
// the focus object, since the composition itself lives in the browser.
//
// The other direction is reported too: the cursor shape the pointer should
// have, and the focused widget's input-method state.
class InputInjector : public QObject
{
    Q_OBJECT

public:
    explicit InputInjector(WindowCompositor *windows, QObject *parent = nullptr);
    ~InputInjector() override;

    // One "input" message from the browser.
    void deliver(const QJsonObject &message);

    // Re-read the cursor and the input-method state, and emit the signals
    // below when they changed. Widgets move without the focus moving, so the
    // caret is re-read once per frame.
    void updateCursor();
    void updateInputMethod();

signals:
    void cursorChanged(int shape);
    // The "ime" message without its type: serial, enabled, hints, rects in
    // page coordinates, surrounding text, positions.
    void inputMethodChanged(const QJsonObject &state);
    // A trackpad swipe the application left alone: "direction" and
    // "fingers" (shared/InputEvents.h).
    void swiped(const QJsonObject &swipe);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void deliverMouse(QEvent::Type type, const QJsonObject &message);
    void deliverWheel(const QJsonObject &message);
    void deliverGesture(const QJsonObject &message);
    void deliverHover(const QJsonObject &message);
    void deliverKey(QEvent::Type type, const QJsonObject &message);
    void deliverFocus(bool on);
    void deliverInputMethod(const QJsonObject &message);

    QWidget *mouseTarget(const QPointF &global) const;
    QWidget *keyTarget() const;
    void activate(QWidget *window);
    void hoverWindow(QWidget *window, const QPointF &global);
    void onFocusObjectChanged();
    void reassertFocus();

    WindowCompositor *m_windows = nullptr;
    QTimer *m_cursorTimer = nullptr;
    QTimer *m_focusTimer = nullptr;
    // The browser's event times, which a QQuickWidget's Flickable measures
    // scrolling speed from, and QGuiApplication double clicks.
    InputClock m_clock;
    SwipeNavigation m_swipe;

    bool m_focused = false;
    QPointF m_lastGlobal;
    QPointer<QWidget> m_grabWindow;
    QPointer<QWidget> m_pressWidget;
    QPointer<QWidget> m_hoverWindow;
    int m_cursor = -1;

    // Same scheme as the renderer: the serial changes whenever a different
    // widget takes focus, and a preedit left in the old one is committed.
    quint32 m_focusSerial = 0;
    QJsonObject m_inputMethod;
    QPointer<QObject> m_composing;
    QString m_preedit;
};
