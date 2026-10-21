#include "InputInjector.h"

#include "WindowCompositor.h"

#include <QApplication>
#include <QColor>
#include <QCursor>
#include <QGuiApplication>
#include <QInputMethodEvent>
#include <QInputMethodQueryEvent>
#include <QJsonArray>
#include <QTextCharFormat>
#include <QTimer>
#include <QWidget>
#include <QWindow>

#include <private/qhighdpiscaling_p.h>
#include <qpa/qwindowsysteminterface.h>

namespace {

using Sync = QWindowSystemInterface::SynchronousDelivery;

// QWindowSystemInterface takes positions in native pixels, the way a
// platform plugin reports them, and scales them down to device-independent
// ones itself. The browser sends device-independent positions. On macOS and
// on the offscreen platform the two are the same; on Windows, where Qt does
// the scaling instead of the platform, a 125% display would otherwise move
// every click towards the top-left corner by a fifth.
QPointF nativeLocal(const QPointF &local, const QWindow *window)
{
    return QHighDpi::toNativeLocalPosition(local, window);
}

QPointF nativeGlobal(const QPointF &global, const QWindow *window)
{
    return QHighDpi::toNativeGlobalPosition(global, window);
}

QPointF pointFrom(const QJsonObject &message)
{
    return QPointF(message.value(QStringLiteral("x")).toDouble(),
                   message.value(QStringLiteral("y")).toDouble());
}

Qt::KeyboardModifiers modsFrom(const QJsonObject &message)
{
    return Qt::KeyboardModifiers(message.value(QStringLiteral("mods")).toInt());
}

QJsonArray rectToJson(const QRectF &rect)
{
    return { rect.x(), rect.y(), rect.width(), rect.height() };
}

QColor colorFrom(const QJsonObject &object, const QString &key)
{
    const QString name = object.value(key).toString();
    return name.isEmpty() ? QColor() : QColor::fromString(name);
}

QTextCharFormat formatFrom(const QJsonObject &attribute)
{
    QTextCharFormat format;
    const int underline = attribute.value(QStringLiteral("underline")).toInt();
    if (underline != QTextCharFormat::NoUnderline)
        format.setUnderlineStyle(QTextCharFormat::UnderlineStyle(underline));
    const QColor underlineColor = colorFrom(attribute, QStringLiteral("underlineColor"));
    if (underlineColor.isValid())
        format.setUnderlineColor(underlineColor);
    const QColor foreground = colorFrom(attribute, QStringLiteral("foreground"));
    if (foreground.isValid())
        format.setForeground(foreground);
    const QColor background = colorFrom(attribute, QStringLiteral("background"));
    if (background.isValid())
        format.setBackground(background);
    return format;
}

bool isMenuLike(const QWidget *window)
{
    const Qt::WindowType type = window->windowType();
    return type == Qt::Popup || type == Qt::ToolTip;
}

} // namespace

InputInjector::InputInjector(WindowCompositor *windows, QObject *parent)
    : QObject(parent)
    , m_windows(windows)
{
    m_clock.start();

    m_cursorTimer = new QTimer(this);
    m_cursorTimer->setSingleShot(true);
    m_cursorTimer->setInterval(0);
    connect(m_cursorTimer, &QTimer::timeout, this, &InputInjector::updateCursor);

    // In GPU mode the native platform reports activation too, from what the
    // window system tells this process, which knows nothing of the browser.
    // On Windows it can deactivate the application, or leave it without a
    // focus window, while the tab has the keyboard: keys still arrive, but
    // no focus ring or caret is drawn until the browser window is
    // deactivated and activated again. The browser's focus is the one that
    // counts, so it is put back, on the next turn of the loop, after the
    // platform's own change has been processed.
    m_focusTimer = new QTimer(this);
    m_focusTimer->setSingleShot(true);
    m_focusTimer->setInterval(0);
    connect(m_focusTimer, &QTimer::timeout, this, &InputInjector::reassertFocus);
    connect(qApp, &QGuiApplication::applicationStateChanged, this,
            [this](Qt::ApplicationState state) {
                if (m_focused && state != Qt::ApplicationActive)
                    m_focusTimer->start();
            });
    connect(qApp, &QGuiApplication::focusWindowChanged, this, [this](QWindow *window) {
        if (m_focused && !window)
            m_focusTimer->start();
    });

    qApp->installEventFilter(this);
    connect(qApp, &QGuiApplication::focusObjectChanged, this,
            &InputInjector::onFocusObjectChanged);
}

InputInjector::~InputInjector()
{
    qApp->removeEventFilter(this);
}

bool InputInjector::eventFilter(QObject *watched, QEvent *event)
{
    if (!watched->isWidgetType())
        return false;
    auto *widget = static_cast<QWidget *>(watched);

    switch (event->type()) {
    case QEvent::Hide:
        if (widget->isWindow()) {
            if (m_hoverWindow == widget)
                m_hoverWindow = nullptr;
            // The pointer may now be over whatever was behind it.
            m_cursorTimer->start();
        }
        break;
    case QEvent::CursorChange:
        m_cursorTimer->start();
        break;
    default:
        break;
    }
    return false;
}

void InputInjector::deliver(const QJsonObject &message)
{
    const QString kind = message.value(QStringLiteral("kind")).toString();
    if (kind == QLatin1String("mouse")) {
        const QString action = message.value(QStringLiteral("action")).toString();
        if (action == QLatin1String("press"))
            deliverMouse(QEvent::MouseButtonPress, message);
        else if (action == QLatin1String("release"))
            deliverMouse(QEvent::MouseButtonRelease, message);
        else if (action == QLatin1String("move"))
            deliverMouse(QEvent::MouseMove, message);
        // "dbl" is dropped: QGuiApplication makes the double click from two
        // presses itself, the same way it does for a platform window.
    } else if (kind == QLatin1String("wheel")) {
        deliverWheel(message);
    } else if (kind == QLatin1String("hover")) {
        deliverHover(message);
    } else if (kind == QLatin1String("key")) {
        const bool release = message.value(QStringLiteral("action")).toString()
                == QLatin1String("release");
        deliverKey(release ? QEvent::KeyRelease : QEvent::KeyPress, message);
    } else if (kind == QLatin1String("focus")) {
        deliverFocus(message.value(QStringLiteral("on")).toBool());
    } else if (kind == QLatin1String("ime")) {
        deliverInputMethod(message);
    }
    updateInputMethod();
}

// ----------------------------------------------------------------- targets

QWidget *InputInjector::mouseTarget(const QPointF &global) const
{
    // While a popup is open it receives every mouse event, wherever it is.
    // That is how a click outside a menu closes it.
    if (QWidget *popup = m_windows->activePopup())
        return popup;
    return m_windows->windowAt(global);
}

QWidget *InputInjector::keyTarget() const
{
    if (QWidget *popup = m_windows->activePopup())
        return popup;
    if (QWidget *active = QApplication::activeWindow())
        return active;
    if (QWidget *modal = m_windows->activeModal())
        return modal;
    return m_windows->mainWindow();
}

void InputInjector::activate(QWidget *window)
{
    if (!window || !window->windowHandle())
        return;
    if (QGuiApplication::focusWindow() == window->windowHandle())
        return;
    QWindowSystemInterface::handleFocusWindowChanged<Sync>(window->windowHandle(),
                                                           Qt::ActiveWindowFocusReason);
}

void InputInjector::hoverWindow(QWidget *window, const QPointF &global)
{
    if (window == m_hoverWindow)
        return;
    if (m_hoverWindow && m_hoverWindow->windowHandle())
        QWindowSystemInterface::handleLeaveEvent<Sync>(m_hoverWindow->windowHandle());
    m_hoverWindow = window;
    if (window && window->windowHandle()) {
        QWindow *handle = window->windowHandle();
        QWindowSystemInterface::handleEnterEvent<Sync>(
                handle, nativeLocal(global - QPointF(window->geometry().topLeft()), handle),
                nativeGlobal(global, handle));
    }
}

// ------------------------------------------------------------------- input

void InputInjector::deliverMouse(QEvent::Type type, const QJsonObject &message)
{
    const QPointF global = pointFrom(message) + m_windows->origin();
    const auto buttons = Qt::MouseButtons(message.value(QStringLiteral("buttons")).toInt());
    const auto button = Qt::MouseButton(message.value(QStringLiteral("button")).toInt());

    if (type == QEvent::MouseButtonPress)
        m_windows->closeToolTips();
    // The window under the press keeps the mouse until the last button is
    // released, like an implicit grab on a real platform.
    if (type == QEvent::MouseButtonPress && !m_grabWindow) {
        m_grabWindow = mouseTarget(global);
        m_pressWidget = m_windows->widgetAt(global);
    }
    QWidget *target = m_grabWindow ? m_grabWindow.data() : mouseTarget(global);
    // A popup opened by the press (a tool button menu) owns the mouse from
    // then on, including the release of that same press.
    if (QWidget *popup = m_windows->activePopup())
        target = popup;
    // Ends the grab before the release is delivered, not after. A release
    // that clicks a button can open a modal dialog, and its exec() does not
    // return until the dialog closes: clicks on the dialog arrive in the
    // meantime and must not be sent to the window behind it.
    if (type == QEvent::MouseButtonRelease && !buttons) {
        m_grabWindow = nullptr;
        m_pressWidget = nullptr;
    }
    if (!target || !target->windowHandle())
        return;

    if (type == QEvent::MouseButtonPress && m_focused && !isMenuLike(target)) {
        // Clicking a window activates it. A modal dialog keeps activation;
        // Qt blocks the click on the window behind it by itself.
        QWidget *modal = m_windows->activeModal();
        activate(modal && modal != target ? modal : target);
    }
    if (!buttons || type == QEvent::MouseButtonPress)
        hoverWindow(target, global);

    m_lastGlobal = global;
    QWindow *handle = target->windowHandle();
    const QPointF local = global - QPointF(target->geometry().topLeft());
    QWindowSystemInterface::handleMouseEvent<Sync>(
            handle, ulong(m_clock.elapsed()), nativeLocal(local, handle),
            nativeGlobal(global, handle), buttons, button, type, modsFrom(message));
    updateCursor();
}

void InputInjector::deliverWheel(const QJsonObject &message)
{
    const QPointF global = pointFrom(message) + m_windows->origin();
    QWidget *target = mouseTarget(global);
    if (!target || !target->windowHandle())
        return;
    m_windows->closeToolTips();

    const QPoint pixel(message.value(QStringLiteral("px")).toInt(),
                       message.value(QStringLiteral("py")).toInt());
    const QPoint angle(message.value(QStringLiteral("ax")).toInt(),
                       message.value(QStringLiteral("ay")).toInt());
    QWindow *handle = target->windowHandle();
    QWindowSystemInterface::handleWheelEvent(
            handle, ulong(m_clock.elapsed()),
            nativeLocal(global - QPointF(target->geometry().topLeft()), handle),
            nativeGlobal(global, handle), pixel, angle,
            modsFrom(message), Qt::ScrollPhase(message.value(QStringLiteral("phase")).toInt()),
            Qt::MouseEventNotSynthesized, message.value(QStringLiteral("inverted")).toBool());
    m_lastGlobal = global;
}

void InputInjector::deliverHover(const QJsonObject &message)
{
    if (message.value(QStringLiteral("action")).toString() == QLatin1String("leave")) {
        if (!m_grabWindow)
            hoverWindow(nullptr, m_lastGlobal);
        return;
    }

    // A hover is a mouse move with no button down, which is what widgets
    // track for hover highlights and tooltips.
    QJsonObject move = message;
    move.insert(QStringLiteral("buttons"), 0);
    move.insert(QStringLiteral("button"), 0);
    deliverMouse(QEvent::MouseMove, move);
}

void InputInjector::deliverKey(QEvent::Type type, const QJsonObject &message)
{
    QWidget *target = keyTarget();
    if (!target || !target->windowHandle())
        return;
    if (type == QEvent::KeyPress) {
        m_windows->closeToolTips();
        if (!QGuiApplication::focusWindow())
            activate(target);
    }

    const ulong timestamp = ulong(m_clock.elapsed());
    const int key = message.value(QStringLiteral("key")).toInt();
    const Qt::KeyboardModifiers mods = modsFrom(message);
    const auto scan = quint32(message.value(QStringLiteral("scan")).toDouble());
    const auto vk = quint32(message.value(QStringLiteral("vk")).toDouble());
    const auto nativeMods = quint32(message.value(QStringLiteral("nativeMods")).toDouble());
    const QString text = message.value(QStringLiteral("text")).toString();
    const bool repeat = message.value(QStringLiteral("repeat")).toBool();

    // A platform plugin offers every key press to the shortcut map first,
    // and handleExtendedKeyEvent() does not: without this, QAction shortcuts
    // would never fire, and neither would the ShortcutOverride a combo box
    // list closes on when Return is pressed. The focus widget sees the
    // override first and can claim the key; a shortcut that matches
    // consumes it.
    if (type == QEvent::KeyPress
            && QWindowSystemInterface::handleShortcutEvent(target->windowHandle(), timestamp, key,
                                                           mods, scan, vk, nativeMods, text,
                                                           repeat)) {
        return;
    }
    QWindowSystemInterface::handleExtendedKeyEvent(target->windowHandle(), timestamp, type, key,
                                                   mods, scan, vk, nativeMods, text, repeat);
}

void InputInjector::deliverFocus(bool on)
{
    m_focused = on;
    // The application is active while its tab has focus, as the browser is.
    // In GPU mode the process itself never becomes the active application,
    // and the native platform treats a popup opened by an inactive
    // application as one shown over another program: on macOS it then closes
    // the popup on any real mouse press, which is every click the user makes
    // in the browser, before the forwarded click can reach the popup.
    QWindowSystemInterface::handleApplicationStateChanged<Sync>(
            on ? Qt::ApplicationActive : Qt::ApplicationInactive);
    if (on) {
        // The page area took keyboard focus: the app becomes the active
        // application, so the caret blinks and selections turn blue.
        QWidget *modal = m_windows->activeModal();
        QWidget *active = QApplication::activeWindow();
        activate(modal ? modal : (active ? active : m_windows->mainWindow()));
        return;
    }
    if (QGuiApplication::focusWindow())
        QWindowSystemInterface::handleFocusWindowChanged<Sync>(nullptr, Qt::ActiveWindowFocusReason);
}

void InputInjector::reassertFocus()
{
    if (!m_focused)
        return;
    if (QGuiApplication::applicationState() != Qt::ApplicationActive)
        QWindowSystemInterface::handleApplicationStateChanged<Sync>(Qt::ApplicationActive);
    if (!QGuiApplication::focusWindow()) {
        QWidget *modal = m_windows->activeModal();
        activate(modal ? modal : m_windows->mainWindow());
    }
}

void InputInjector::deliverInputMethod(const QJsonObject &message)
{
    QObject *target = QGuiApplication::focusObject();
    if (!target)
        return;

    QList<QInputMethodEvent::Attribute> attributes;
    const QJsonArray list = message.value(QStringLiteral("attributes")).toArray();
    for (const QJsonValue &value : list) {
        const QJsonObject attribute = value.toObject();
        const auto type = QInputMethodEvent::AttributeType(
                attribute.value(QStringLiteral("type")).toInt());
        const int start = attribute.value(QStringLiteral("start")).toInt();
        const int length = attribute.value(QStringLiteral("length")).toInt();
        if (type == QInputMethodEvent::TextFormat)
            attributes.append({ type, start, length, formatFrom(attribute) });
        else if (type == QInputMethodEvent::Cursor || type == QInputMethodEvent::Selection)
            attributes.append({ type, start, length });
    }

    const QString preedit = message.value(QStringLiteral("preedit")).toString();
    QInputMethodEvent event(preedit, attributes);
    event.setCommitString(message.value(QStringLiteral("commit")).toString(),
                          message.value(QStringLiteral("replaceStart")).toInt(),
                          message.value(QStringLiteral("replaceLength")).toInt());
    QCoreApplication::sendEvent(target, &event);

    m_composing = preedit.isEmpty() ? nullptr : target;
    m_preedit = preedit;
}

void InputInjector::onFocusObjectChanged()
{
    // Widgets ask the platform input method to commit when focus moves, but
    // this process has none: the composition lives in the browser. Commit the
    // preedit into the widget that lost focus, as the renderer does, and the
    // browser drops its copy when it sees the new serial.
    QObject *now = QGuiApplication::focusObject();
    if (m_composing && !m_preedit.isEmpty() && m_composing != now) {
        QInputMethodEvent commit;
        commit.setCommitString(m_preedit);
        QCoreApplication::sendEvent(m_composing, &commit);
    }
    m_composing = nullptr;
    m_preedit.clear();

    ++m_focusSerial;
    updateInputMethod();
}

// ----------------------------------------------------------------- reports

void InputInjector::updateCursor()
{
    int shape = Qt::ArrowCursor;
    if (const QCursor *override = QGuiApplication::overrideCursor()) {
        shape = override->shape();
    } else {
        // During a drag the cursor belongs to the widget that was pressed,
        // the way a splitter handle keeps its arrows past its own edge.
        QWidget *widget = m_pressWidget ? m_pressWidget.data() : m_windows->widgetAt(m_lastGlobal);
        if (widget)
            shape = widget->cursor().shape();
    }
    if (shape == Qt::BitmapCursor)
        shape = Qt::ArrowCursor;
    if (shape == m_cursor)
        return;
    m_cursor = shape;
    emit cursorChanged(shape);
}

void InputInjector::updateInputMethod()
{
    QJsonObject state;
    state.insert(QStringLiteral("serial"), static_cast<double>(m_focusSerial));

    QWidget *widget = QApplication::focusWidget();
    QInputMethodQueryEvent query(Qt::ImEnabled | Qt::ImHints | Qt::ImReadOnly
                                 | Qt::ImCursorRectangle | Qt::ImAnchorRectangle
                                 | Qt::ImInputItemClipRectangle | Qt::ImSurroundingText
                                 | Qt::ImCurrentSelection | Qt::ImCursorPosition
                                 | Qt::ImAnchorPosition | Qt::ImAbsolutePosition
                                 | Qt::ImTextBeforeCursor | Qt::ImTextAfterCursor);
    if (widget && widget->isVisible())
        QCoreApplication::sendEvent(widget, &query);
    else
        widget = nullptr;

    const bool enabled = widget && query.value(Qt::ImEnabled).toBool();
    state.insert(QStringLiteral("enabled"), enabled);
    if (enabled) {
        // Widget coordinates to the tab's page coordinates, which are the
        // browser viewport's item coordinates.
        const QPointF offset = QPointF(widget->mapToGlobal(QPoint(0, 0))) - m_windows->origin();
        const auto pageRect = [&query, offset](Qt::InputMethodQuery q) {
            return rectToJson(query.value(q).toRectF().translated(offset));
        };
        QRectF clip = query.value(Qt::ImInputItemClipRectangle).toRectF();
        if (!clip.isValid())
            clip = QRectF(widget->rect());
        state.insert(QStringLiteral("hints"), query.value(Qt::ImHints).toInt());
        state.insert(QStringLiteral("readOnly"), query.value(Qt::ImReadOnly).toBool());
        state.insert(QStringLiteral("cursorRect"), pageRect(Qt::ImCursorRectangle));
        state.insert(QStringLiteral("anchorRect"), pageRect(Qt::ImAnchorRectangle));
        state.insert(QStringLiteral("clipRect"), rectToJson(clip.translated(offset)));
        state.insert(QStringLiteral("text"), query.value(Qt::ImSurroundingText).toString());
        state.insert(QStringLiteral("selection"), query.value(Qt::ImCurrentSelection).toString());
        state.insert(QStringLiteral("cursor"), query.value(Qt::ImCursorPosition).toInt());
        state.insert(QStringLiteral("anchor"), query.value(Qt::ImAnchorPosition).toInt());
        state.insert(QStringLiteral("absolute"), query.value(Qt::ImAbsolutePosition).toInt());
        state.insert(QStringLiteral("before"), query.value(Qt::ImTextBeforeCursor).toString());
        state.insert(QStringLiteral("after"), query.value(Qt::ImTextAfterCursor).toString());
    }

    if (state == m_inputMethod)
        return;
    m_inputMethod = state;
    emit inputMethodChanged(state);
}
