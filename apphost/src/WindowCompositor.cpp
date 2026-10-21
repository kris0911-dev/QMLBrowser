#include "WindowCompositor.h"

#include "NativePlatform.h"
#include "OffscreenPlatform.h"

#include <QApplication>
#include <QColor>
#include <QEvent>
#include <QImage>
#include <QPainter>
#include <QPainterPath>
#include <QTimer>
#include <QWidget>

namespace {

constexpr int kMaxPixels = 7680 * 4320;
// Frames are produced when widgets repaint. A spinning progress bar or a
// playing video would otherwise ask for one frame per paint event.
constexpr int kMinFrameIntervalMs = 8;
constexpr int kNoticeMs = 3200;

bool isMenuLike(const QWidget *window)
{
    const Qt::WindowType type = window->windowType();
    return type == Qt::Popup || type == Qt::ToolTip;
}

} // namespace

WindowCompositor::WindowCompositor(QWidget *mainWindow, bool gpu, QObject *parent)
    : QObject(parent)
    , m_window(mainWindow)
    , m_gpu(gpu)
    , m_pid(QCoreApplication::applicationPid())
{
    m_clock.start();

    m_renderTimer = new QTimer(this);
    m_renderTimer->setSingleShot(true);
    m_renderTimer->setTimerType(Qt::PreciseTimer);
    connect(m_renderTimer, &QTimer::timeout, this, &WindowCompositor::renderFrame);

    m_noticeTimer = new QTimer(this);
    m_noticeTimer->setSingleShot(true);
    m_noticeTimer->setInterval(kNoticeMs);
    connect(m_noticeTimer, &QTimer::timeout, this, [this] { setNotice(QString()); });

    qApp->installEventFilter(this);

    // The tab is the window frame. Until the first viewport arrives the
    // window gets the size the screen starts with.
    m_window->setWindowFlag(Qt::FramelessWindowHint);
    m_window->setGeometry(0, 0, 1280, 800);
    m_stack.append(m_window);
    // In GPU mode the Show event below keeps it off screen.
    m_window->show();
}

WindowCompositor::~WindowCompositor()
{
    qApp->removeEventFilter(this);
}

// ---------------------------------------------------------------- viewport

void WindowCompositor::setViewport(bool visible, int width, int height, qreal dpr)
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
        if (width > 0 && height > 0) {
            // In GPU mode the screen is a real one, larger than the tab.
            // Popups are kept inside the tab by keepInsideTab() instead.
            if (!m_gpu)
                OffscreenPlatform::resizeScreen(width, height);
            m_window->setGeometry(0, 0, width, height);
        }
    }
    scheduleRender();
}

void WindowCompositor::setNotice(const QString &notice)
{
    if (notice == m_notice)
        return;
    m_notice = notice;
    if (!notice.isEmpty())
        m_noticeTimer->start();
    scheduleRender();
}

// ----------------------------------------------------------------- windows

bool WindowCompositor::eventFilter(QObject *watched, QEvent *event)
{
    if (!watched->isWidgetType())
        return false;
    auto *widget = static_cast<QWidget *>(watched);

    switch (event->type()) {
    case QEvent::Show:
        // Sent before the window is mapped, so the window is in the stack
        // by the time its first paint asks for a frame, and there is still
        // time to keep it off screen.
        if (widget->isWindow()) {
            if (m_gpu) {
                NativePlatform::keepOffScreen(widget);
                keepInsideTab(widget);
            }
            raiseWindow(widget);
            scheduleRender();
        }
        break;
    case QEvent::Hide:
        if (widget->isWindow()) {
            dropWindow(widget);
            scheduleRender();
        }
        break;
    case QEvent::Paint:
        if (!m_painting)
            scheduleRender();
        break;
    case QEvent::Move:
    case QEvent::Resize:
        if (widget->isWindow()) {
            if (m_gpu && widget->isVisible())
                keepInsideTab(widget);
            scheduleRender();
        }
        break;
    default:
        break;
    }
    return false;
}

void WindowCompositor::raiseWindow(QWidget *window)
{
    if (window == m_window)
        return;
    dropWindow(window);
    m_stack.append(window);
}

void WindowCompositor::keepInsideTab(QWidget *window)
{
    // On the offscreen platform the screen is the tab, so Qt places menus,
    // lists and dialogs inside it by itself. A real screen is larger: a menu
    // near the bottom of the tab would open below it, out of the frame.
    if (window == m_window || m_width < 1 || m_height < 1)
        return;
    const QRect tab(origin().toPoint(), QSize(m_width, m_height));
    QRect rect = window->geometry();
    if (tab.contains(rect))
        return;
    if (rect.right() > tab.right())
        rect.moveRight(tab.right());
    if (rect.bottom() > tab.bottom())
        rect.moveBottom(tab.bottom());
    if (rect.left() < tab.left())
        rect.moveLeft(tab.left());
    if (rect.top() < tab.top())
        rect.moveTop(tab.top());
    if (rect.topLeft() != window->pos())
        window->move(rect.topLeft());
}

QWidget *WindowCompositor::activePopup() const
{
    for (qsizetype i = m_stack.size() - 1; i >= 0; --i) {
        QWidget *window = m_stack.at(i);
        if (window && window->isVisible() && window->windowType() == Qt::Popup)
            return window;
    }
    return nullptr;
}

QWidget *WindowCompositor::activeModal() const
{
    for (qsizetype i = m_stack.size() - 1; i >= 0; --i) {
        QWidget *window = m_stack.at(i);
        if (window && window->isVisible() && window->isModal())
            return window;
    }
    return nullptr;
}

void WindowCompositor::dropWindow(QWidget *window)
{
    if (window == m_window)
        return;
    m_stack.removeAll(window);
    m_stack.removeAll(QPointer<QWidget>());
}

QPointF WindowCompositor::origin() const
{
    return m_window->geometry().topLeft();
}

QWidget *WindowCompositor::windowAt(const QPointF &global) const
{
    for (qsizetype i = m_stack.size() - 1; i >= 0; --i) {
        QWidget *window = m_stack.at(i);
        if (window && window->isVisible() && window->geometry().contains(global.toPoint()))
            return window;
    }
    return m_window;
}

QWidget *WindowCompositor::widgetAt(const QPointF &global) const
{
    QWidget *window = windowAt(global);
    if (!window)
        return nullptr;
    QWidget *child = window->childAt(window->mapFromGlobal(global.toPoint()));
    return child ? child : window;
}

void WindowCompositor::closeToolTips()
{
    // A tooltip hides itself when it sees a press, a wheel turn or a key, but
    // it does so from inside the delivery of that event. On the offscreen
    // platform, hiding a window mid-delivery re-targets the event and the
    // press lands at the wrong position, so the button under it never sees
    // the click. Hiding the tooltip first, which is what would happen anyway,
    // avoids that.
    const QList<QPointer<QWidget>> stack = m_stack;
    for (const QPointer<QWidget> &window : stack) {
        if (window && window->isVisible() && window->windowType() == Qt::ToolTip)
            window->hide();
    }
}

// ------------------------------------------------------------------ frames

void WindowCompositor::scheduleRender()
{
    if (m_width < 1 || m_height < 1)
        return;
    if (!m_visible && !m_sizeDirty)
        return;
    if (m_renderTimer->isActive())
        return;

    int wait = 0;
    if (m_lastFrameAt >= 0) {
        const qint64 since = m_clock.elapsed() - m_lastFrameAt;
        if (since < kMinFrameIntervalMs)
            wait = int(kMinFrameIntervalMs - since);
    }
    m_renderTimer->start(wait);
}

int WindowCompositor::freeSlot() const
{
    for (int i = 0; i < 2; ++i) {
        if (!m_slots[i].pending)
            return i;
    }
    return -1;
}

QString WindowCompositor::prepareSlot(int index, int bytes)
{
    Slot &slot = m_slots[index];
    if (slot.pixels.isAttached() && slot.pixels.size() == bytes)
        return slot.pixels.key();

    const QString key = QStringLiteral("qbwx-%1-%2-%3").arg(m_pid).arg(index).arg(++m_epoch);
    if (!slot.pixels.create(key, bytes)) {
        qWarning("QmlBrowserHost: could not create the frame buffer: %s",
                 qPrintable(slot.pixels.errorString()));
        return {};
    }
    return key;
}

void WindowCompositor::releaseFrame(quint32 sequence)
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

void WindowCompositor::renderFrame()
{
    if (m_painting || m_width < 1 || m_height < 1)
        return;
    if (!m_visible && !m_sizeDirty)
        return;

    const int slotIndex = freeSlot();
    if (slotIndex < 0) {
        // Both buffers are still on the browser side. Its acknowledgement
        // schedules the next frame.
        m_blocked = true;
        return;
    }

    // A resize posts a layout request. Draw the new layout, not the old one.
    QCoreApplication::sendPostedEvents(nullptr, QEvent::LayoutRequest);

    const int width = qRound(m_width * m_dpr);
    const int height = qRound(m_height * m_dpr);
    if (width < 1 || height < 1 || qint64(width) * height > kMaxPixels) {
        qWarning("QmlBrowserHost: viewport %dx%d is not drawable.", width, height);
        return;
    }

    const int stride = width * 4;
    const int bytes = stride * height;
    const QString key = prepareSlot(slotIndex, bytes);
    if (key.isEmpty())
        return;

    Slot &slot = m_slots[slotIndex];
    if (!slot.pixels.lock())
        return;

    {
        // Widgets paint straight into the shared buffer.
        QImage image(slot.pixels.data(), width, height, stride,
                     QImage::Format_ARGB32_Premultiplied);
        image.setDevicePixelRatio(m_dpr);
        image.fill(m_window->palette().color(QPalette::Window));

        QPainter painter(&image);
        m_painting = true;
        const QPoint base = origin().toPoint();
        const QList<QPointer<QWidget>> stack = m_stack;
        for (const QPointer<QWidget> &window : stack) {
            if (!window || !window->isVisible())
                continue;
            const QPoint offset = window->geometry().topLeft() - base;
            paintDecorations(painter, window, offset);
            painter.save();
            window->render(&painter, offset, QRegion(),
                           QWidget::DrawWindowBackground | QWidget::DrawChildren);
            painter.restore();
        }
        paintNotice(painter);
        m_painting = false;
    }
    slot.pixels.unlock();

    const quint32 sequence = m_nextSequence++;
    slot.sequence = sequence;
    slot.pending = true;
    m_sizeDirty = false;
    m_lastFrameAt = m_clock.elapsed();

    emit frameReady(sequence, width, height, stride, key);
}

void WindowCompositor::paintDecorations(QPainter &painter, QWidget *window, const QPoint &offset)
{
    // Offscreen windows have no frame. Menus and tooltips draw their own
    // border; a dialog gets a shadow and a title strip, so it reads as a
    // window above the page rather than part of it.
    if (window == m_window || isMenuLike(window))
        return;

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing);

    if (window->isModal()) {
        painter.fillRect(QRectF(0, 0, m_width, m_height), QColor(0, 0, 0, 110));
    }

    constexpr int titleHeight = 26;
    const QRectF body(offset, window->size());
    const QRectF frame = body.adjusted(-1, -titleHeight, 1, 1);

    for (int i = 1; i <= 8; ++i) {
        QPainterPath shadow;
        shadow.addRoundedRect(frame.adjusted(-i, -i + 3, i, i + 3), 8 + i, 8 + i);
        painter.fillPath(shadow, QColor(0, 0, 0, 14));
    }

    QPainterPath outline;
    outline.addRoundedRect(frame, 8, 8);
    painter.fillPath(outline, QColor(QStringLiteral("#1b2130")));
    painter.setPen(QColor(QStringLiteral("#313a50")));
    painter.drawPath(outline);

    painter.setPen(QColor(QStringLiteral("#c5cedf")));
    QFont font = window->font();
    font.setBold(true);
    painter.setFont(font);
    const QRectF title(frame.left() + 12, frame.top(), frame.width() - 24, titleHeight);
    painter.drawText(title, Qt::AlignVCenter | Qt::AlignLeft,
                     painter.fontMetrics().elidedText(window->windowTitle(), Qt::ElideRight,
                                                      int(title.width())));
    painter.restore();
}

void WindowCompositor::paintNotice(QPainter &painter)
{
    if (m_notice.isEmpty())
        return;

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing);
    QFont font = m_window->font();
    font.setPixelSize(14);
    painter.setFont(font);
    const int textWidth = painter.fontMetrics().horizontalAdvance(m_notice);
    const QRectF pill((m_width - textWidth - 36) / 2.0, 18, textWidth + 36, 36);
    QPainterPath path;
    path.addRoundedRect(pill, 18, 18);
    painter.fillPath(path, QColor(20, 24, 34, 235));
    painter.setPen(QColor(QStringLiteral("#3a445c")));
    painter.drawPath(path);
    painter.setPen(QColor(QStringLiteral("#e6ecf9")));
    painter.drawText(pill, Qt::AlignCenter, m_notice);
    painter.restore();
}
