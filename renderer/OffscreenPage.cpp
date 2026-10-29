#include "OffscreenPage.h"

#include <QColor>
#include <QCoreApplication>
#include <QHoverEvent>
#include <QInputMethodEvent>
#include <QInputMethodQueryEvent>
#include <QJsonArray>
#include <QJsonObject>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickRenderControl>
#include <QQuickRenderTarget>
#include <QQuickWindow>
#include <QTextCharFormat>
#include <QTimer>

#include <rhi/qrhi.h>

#include <cstring>

namespace {

constexpr int kMaxPixels = 7680 * 4320;

QPointF pointFrom(const QJsonObject &message)
{
    return QPointF(message.value(QStringLiteral("x")).toDouble(),
                   message.value(QStringLiteral("y")).toDouble());
}

Qt::KeyboardModifiers modsFrom(const QJsonObject &message)
{
    return Qt::KeyboardModifiers(message.value(QStringLiteral("mods")).toInt());
}

Qt::MouseButton buttonFrom(const QJsonObject &message)
{
    return Qt::MouseButton(message.value(QStringLiteral("button")).toInt());
}

Qt::MouseButtons buttonsFrom(const QJsonObject &message)
{
    return Qt::MouseButtons(message.value(QStringLiteral("buttons")).toInt());
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

// The browser sends the preedit decoration the platform input method asked
// for: the clause underlines and the highlighted candidate segment.
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

} // namespace

OffscreenPage::OffscreenPage(QObject *parent)
    : QObject(parent)
    , m_pid(QCoreApplication::applicationPid())
{
    m_renderTimer = new QTimer(this);
    m_renderTimer->setSingleShot(true);
    m_renderTimer->setInterval(0);
    m_renderTimer->setTimerType(Qt::PreciseTimer);
    connect(m_renderTimer, &QTimer::timeout, this, &OffscreenPage::renderFrame);
}

OffscreenPage::~OffscreenPage()
{
    if (m_window)
        disconnect(m_window, nullptr, this, nullptr);
    releaseTarget();
    if (m_engine)
        m_engine->setIncubationController(nullptr);
    delete m_window;
    m_window = nullptr;
    delete m_renderControl;
    m_renderControl = nullptr;
    delete m_engine;
    m_engine = nullptr;
}

bool OffscreenPage::load()
{
    m_renderControl = new QQuickRenderControl;
    m_window = new QQuickWindow(m_renderControl);
    m_window->setColor(QColor(QStringLiteral("#0e1017")));
    m_window->setGeometry(0, 0, 1, 1);

    m_engine = new QQmlEngine;
    m_engine->setIncubationController(m_window->incubationController());

    QQmlComponent component(m_engine, QUrl(QStringLiteral("qrc:/ui/Renderer.qml")));
    if (component.isError()) {
        qWarning("Renderer UI failed to load: %s", qPrintable(component.errorString()));
        return false;
    }

    QObject *created = component.create();
    m_root = qobject_cast<QQuickItem *>(created);
    if (!m_root) {
        qWarning("Renderer UI has no root item.");
        delete created;
        return false;
    }
    m_root->setParentItem(m_window->contentItem());
    m_root->setSize(QSizeF(1, 1));

    m_page = m_root->findChild<PageView *>(QStringLiteral("pageView"));
    if (!m_page) {
        qWarning("Renderer UI has no PageView.");
        return false;
    }

    if (!m_renderControl->initialize() || !m_renderControl->rhi()) {
        qWarning("Offscreen Qt Quick rendering failed to initialize.");
        return false;
    }
    m_rhi = m_renderControl->rhi();

    connect(m_renderControl, &QQuickRenderControl::renderRequested, this,
            &OffscreenPage::scheduleRender);
    connect(m_renderControl, &QQuickRenderControl::sceneChanged, this,
            &OffscreenPage::scheduleRender);
    connect(m_window, &QQuickWindow::activeFocusItemChanged, this,
            &OffscreenPage::onActiveFocusItemChanged);
    return true;
}

void OffscreenPage::setViewport(bool visible, int width, int height, qreal dpr)
{
    if (dpr < 0.5)
        dpr = 1;

    const bool sizeChanged = width != m_width || height != m_height || qAbs(dpr - m_dpr) > 0.001;
    if (!sizeChanged && visible == m_visible)
        return;

    m_visible = visible;
    if (sizeChanged) {
        m_width = width;
        m_height = height;
        m_dpr = dpr;
        m_sizeDirty = true;
    }
    scheduleRender();
}

void OffscreenPage::releaseSequence(quint32 sequence)
{
    for (Slot &slot : m_slots) {
        if (slot.pending && slot.sequence == sequence) {
            slot.pending = false;
            break;
        }
    }
    if (!m_blocked)
        return;
    m_blocked = false;
    scheduleRender();
}

void OffscreenPage::scheduleRender()
{
    if (m_width < 1 || m_height < 1)
        return;
    if (!m_visible && !m_sizeDirty)
        return;
    m_renderTimer->start();
}

int OffscreenPage::freeSlot() const
{
    for (int i = 0; i < 2; ++i) {
        if (!m_slots[i].pending)
            return i;
    }
    return -1;
}

QString OffscreenPage::prepareSlot(int index, int bytes)
{
    Slot &slot = m_slots[index];
    if (slot.pixels.isAttached() && slot.pixels.size() == bytes)
        return slot.pixels.key();

    const QString key = QStringLiteral("qbpx-%1-%2-%3").arg(m_pid).arg(index).arg(++m_epoch);
    if (!slot.pixels.create(key, bytes)) {
        qWarning("Renderer: could not create the frame buffer: %s",
                 qPrintable(slot.pixels.errorString()));
        return {};
    }
    return key;
}

void OffscreenPage::releaseTarget()
{
    delete m_target;
    m_target = nullptr;
    delete m_pass;
    m_pass = nullptr;
    delete m_color;
    m_color = nullptr;
    delete m_depth;
    m_depth = nullptr;
}

bool OffscreenPage::ensureTarget()
{
    const int px = qRound(m_width * m_dpr);
    const int py = qRound(m_height * m_dpr);
    if (px < 1 || py < 1 || qint64(px) * py > kMaxPixels) {
        qWarning("Renderer: viewport %dx%d is not drawable.", px, py);
        return false;
    }

    const QSize pixels(px, py);
    if (m_color && m_color->pixelSize() == pixels)
        return true;

    releaseTarget();

    m_color = m_rhi->newTexture(QRhiTexture::RGBA8, pixels, 1,
                                QRhiTexture::RenderTarget | QRhiTexture::UsedAsTransferSource);
    m_depth = m_rhi->newRenderBuffer(QRhiRenderBuffer::DepthStencil, pixels, 1);
    if (!m_color->create() || !m_depth->create()) {
        releaseTarget();
        return false;
    }

    QRhiTextureRenderTargetDescription desc{QRhiColorAttachment(m_color), m_depth};
    m_target = m_rhi->newTextureRenderTarget(desc);
    m_pass = m_target->newCompatibleRenderPassDescriptor();
    m_target->setRenderPassDescriptor(m_pass);
    if (!m_target->create()) {
        releaseTarget();
        return false;
    }

    QQuickRenderTarget quickTarget = QQuickRenderTarget::fromRhiRenderTarget(m_target);
    quickTarget.setDevicePixelRatio(m_dpr);
    // OpenGL framebuffers are bottom-up. Mirroring makes a top-left readback
    // match what the page drew. Direct3D and Metal are already top-down.
    quickTarget.setMirrorVertically(m_rhi->isYUpInFramebuffer());
    m_window->setRenderTarget(quickTarget);
    return true;
}

void OffscreenPage::renderFrame()
{
    if (m_rendering || !m_rhi)
        return;
    if (m_width < 1 || m_height < 1)
        return;
    if (!m_visible && !m_sizeDirty)
        return;

    const int slotIndex = freeSlot();
    if (slotIndex < 0) {
        m_blocked = true;
        return;
    }

    m_rendering = true;

    m_window->setGeometry(0, 0, m_width, m_height);
    if (m_root)
        m_root->setSize(QSizeF(m_width, m_height));

    if (!ensureTarget()) {
        m_rendering = false;
        return;
    }

    m_renderControl->polishItems();
    // Scrolling moves the focused field without touching its own cursor
    // rectangle, so the scene position is re-read once per frame.
    publishInputMethod();
    m_renderControl->beginFrame();
    if (!m_rhi->isRecordingFrame() || !m_renderControl->commandBuffer()) {
        m_rendering = false;
        return;
    }

    m_renderControl->sync();
    m_renderControl->render();

    QRhiReadbackResult readback;
    bool completed = false;
    readback.completed = [&completed] { completed = true; };
    QRhiResourceUpdateBatch *batch = m_rhi->nextResourceUpdateBatch();
    batch->readBackTexture(QRhiReadbackDescription(m_color), &readback);
    m_renderControl->commandBuffer()->resourceUpdate(batch);
    m_renderControl->endFrame();
    if (!completed)
        m_rhi->finish();

    m_rendering = false;

    if (!completed || readback.data.isEmpty())
        return;

    const int width = readback.pixelSize.width();
    const int height = readback.pixelSize.height();
    if (width < 1 || height < 1)
        return;

    const int stride = width * 4;
    const int bytes = stride * height;
    if (readback.data.size() < bytes)
        return;

    const QString key = prepareSlot(slotIndex, bytes);
    if (key.isEmpty())
        return;

    Slot &slot = m_slots[slotIndex];
    if (!slot.pixels.lock())
        return;

    uchar *destination = slot.pixels.data();
    const char *source = readback.data.constData();
    if (readback.data.size() == bytes) {
        memcpy(destination, source, bytes);
    } else {
        const int sourceStride = readback.data.size() / height;
        for (int y = 0; y < height; ++y)
            memcpy(destination + y * stride, source + y * sourceStride, stride);
    }
    slot.pixels.unlock();

    const quint32 sequence = m_nextSequence++;
    slot.sequence = sequence;
    slot.pending = true;
    m_sizeDirty = false;

    const int format = readback.format == QRhiTexture::BGRA8 ? 2 : 1;
    emit frameReady(sequence, width, height, stride, format, key);
}

void OffscreenPage::ensureActive()
{
    if (!m_window || m_window->isActive())
        return;

    // The page has no platform window, so it cannot become the key window.
    // Marking it active is what lets a clicked item take key focus inside the
    // offscreen scene. requestActivate() would create a real window, which is
    // the thing this process is not allowed to have.
    QEvent activate(QEvent::WindowActivate);
    QCoreApplication::sendEvent(m_window, &activate);
    if (!m_window->isActive() && !m_activateFailed) {
        m_activateFailed = true;
        qWarning("Renderer: the offscreen page did not accept focus.");
    }
}

void OffscreenPage::publishCursor()
{
    if (!m_window)
        return;
    const int shape = int(m_window->cursor().shape());
    if (shape == m_cursor)
        return;
    m_cursor = shape;
    emit cursorChanged(shape);
}

void OffscreenPage::deliverInput(const QJsonObject &message)
{
    const QString kind = message.value(QStringLiteral("kind")).toString();
    if (kind == QLatin1String("mouse")) {
        const QString action = message.value(QStringLiteral("action")).toString();
        QEvent::Type type = QEvent::MouseMove;
        if (action == QLatin1String("press"))
            type = QEvent::MouseButtonPress;
        else if (action == QLatin1String("release"))
            type = QEvent::MouseButtonRelease;
        else if (action == QLatin1String("dbl"))
            type = QEvent::MouseButtonDblClick;
        deliverMouse(type, message);
    } else if (kind == QLatin1String("wheel")) {
        deliverWheel(message);
    } else if (kind == QLatin1String("gesture")) {
        deliverGesture(message);
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
    // A click or key can move the text cursor; tell the browser right away
    // rather than waiting for the next frame.
    publishInputMethod();
}

void OffscreenPage::deliverMouse(QEvent::Type type, const QJsonObject &message)
{
    if (!m_window)
        return;
    ensureActive();
    const QPointF pos = pointFrom(message);
    QMouseEvent event(type, pos, pos, pos, buttonFrom(message), buttonsFrom(message),
                      modsFrom(message));
    event.setTimestamp(m_inputClock.time(message));
    QCoreApplication::sendEvent(m_window, &event);
    m_lastHover = pos;
    publishCursor();
}

void OffscreenPage::deliverWheel(const QJsonObject &message)
{
    if (!m_window)
        return;
    const auto event = InputEvents::wheelEvent(message, m_inputClock.time(message));
    QCoreApplication::sendEvent(m_window, event.get());
    const QJsonObject swipe = m_swipe.observe(message, event->isAccepted());
    if (!swipe.isEmpty())
        emit swiped(swipe);
}

void OffscreenPage::deliverGesture(const QJsonObject &message)
{
    if (!m_window)
        return;
    // A trackpad pinch, rotation or smart zoom, which a PinchHandler follows,
    // or a three-finger swipe.
    const auto event = InputEvents::gestureEvent(message, m_inputClock.time(message));
    QCoreApplication::sendEvent(m_window, event.get());
    const QJsonObject swipe = m_swipe.observe(message, event->isAccepted());
    if (!swipe.isEmpty())
        emit swiped(swipe);
}

void OffscreenPage::deliverHover(const QJsonObject &message)
{
    if (!m_window)
        return;
    const bool leave = message.value(QStringLiteral("action")).toString() == QLatin1String("leave");
    const QPointF pos = leave ? m_lastHover : pointFrom(message);
    const QEvent::Type type = leave ? QEvent::HoverLeave : QEvent::HoverMove;
    QHoverEvent event(type, pos, pos, m_lastHover, modsFrom(message));
    event.setTimestamp(m_inputClock.time(message));
    QCoreApplication::sendEvent(m_window, &event);
    if (!leave)
        m_lastHover = pos;
    publishCursor();
}

void OffscreenPage::deliverKey(QEvent::Type type, const QJsonObject &message)
{
    if (!m_window)
        return;
    ensureActive();
    if (type == QEvent::KeyPress && m_window->contentItem() && !m_window->activeFocusItem())
        m_window->contentItem()->setFocus(true);

    QKeyEvent event(type, message.value(QStringLiteral("key")).toInt(), modsFrom(message),
                    quint32(message.value(QStringLiteral("scan")).toDouble()),
                    quint32(message.value(QStringLiteral("vk")).toDouble()),
                    quint32(message.value(QStringLiteral("nativeMods")).toDouble()),
                    message.value(QStringLiteral("text")).toString(),
                    message.value(QStringLiteral("repeat")).toBool());
    event.setTimestamp(m_inputClock.time(message));
    QCoreApplication::sendEvent(m_window, &event);
}

void OffscreenPage::deliverFocus(bool on)
{
    if (!m_window)
        return;
    if (on) {
        ensureActive();
        if (m_window->contentItem() && !m_window->activeFocusItem())
            m_window->contentItem()->setFocus(true);
        return;
    }
    QEvent deactivate(QEvent::WindowDeactivate);
    QCoreApplication::sendEvent(m_window, &deactivate);
}

void OffscreenPage::deliverInputMethod(const QJsonObject &message)
{
    if (!m_window)
        return;
    QQuickItem *item = m_window->activeFocusItem();
    if (!item)
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
    QCoreApplication::sendEvent(item, &event);

    m_composingItem = preedit.isEmpty() ? nullptr : item;
    m_preedit = preedit;
}

void OffscreenPage::onActiveFocusItemChanged()
{
    // Qt Quick asks the platform input method to commit when focus moves, but
    // the composition lives in the browser process, so the field that lost
    // focus would keep a dangling preedit. Commit it here instead; the browser
    // discards its own copy when it sees the new focus serial.
    if (m_composingItem && !m_preedit.isEmpty() && m_composingItem != m_window->activeFocusItem()) {
        QInputMethodEvent commit;
        commit.setCommitString(m_preedit);
        QCoreApplication::sendEvent(m_composingItem, &commit);
    }
    m_composingItem = nullptr;
    m_preedit.clear();

    ++m_focusSerial;
    publishInputMethod();
}

void OffscreenPage::publishInputMethod()
{
    if (!m_window)
        return;

    QJsonObject state;
    state.insert(QStringLiteral("serial"), static_cast<double>(m_focusSerial));

    QQuickItem *item = m_window->activeFocusItem();
    QInputMethodQueryEvent query(Qt::ImEnabled | Qt::ImHints | Qt::ImReadOnly
                                 | Qt::ImCursorRectangle | Qt::ImAnchorRectangle
                                 | Qt::ImInputItemClipRectangle | Qt::ImSurroundingText
                                 | Qt::ImCurrentSelection | Qt::ImCursorPosition
                                 | Qt::ImAnchorPosition | Qt::ImAbsolutePosition
                                 | Qt::ImTextBeforeCursor | Qt::ImTextAfterCursor);
    if (item)
        QCoreApplication::sendEvent(item, &query);

    const bool enabled = item && query.value(Qt::ImEnabled).toBool();
    state.insert(QStringLiteral("enabled"), enabled);
    if (enabled) {
        // Item coordinates to scene coordinates. The scene is exactly the
        // browser's viewport item, so the browser can use these unchanged.
        const auto sceneRect = [item, &query](Qt::InputMethodQuery q) {
            return rectToJson(item->mapRectToScene(query.value(q).toRectF()));
        };
        state.insert(QStringLiteral("hints"), query.value(Qt::ImHints).toInt());
        state.insert(QStringLiteral("readOnly"), query.value(Qt::ImReadOnly).toBool());
        state.insert(QStringLiteral("cursorRect"), sceneRect(Qt::ImCursorRectangle));
        state.insert(QStringLiteral("anchorRect"), sceneRect(Qt::ImAnchorRectangle));
        state.insert(QStringLiteral("clipRect"), sceneRect(Qt::ImInputItemClipRectangle));
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
