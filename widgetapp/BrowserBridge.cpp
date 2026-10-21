#include "BrowserBridge.h"

#include "GalleryWindow.h"
#include "IpcChannel.h"

#include <QApplication>
#include <QColor>
#include <QCursor>
#include <QDir>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QInputMethodEvent>
#include <QInputMethodQueryEvent>
#include <QJsonArray>
#include <QLocalSocket>
#include <QPainter>
#include <QPainterPath>
#include <QTextCharFormat>
#include <QTimer>
#include <QWidget>
#include <QWindow>

#include <qpa/qplatformnativeinterface.h>
#include <qpa/qwindowsysteminterface.h>

#include <cstring>

namespace {

constexpr int kMaxPixels = 7680 * 4320;
// Frames are produced when widgets repaint. A spinning progress bar or a
// playing video would otherwise ask for one frame per paint event.
constexpr int kMinFrameIntervalMs = 8;
constexpr int kNoticeMs = 3200;

using Sync = QWindowSystemInterface::SynchronousDelivery;

// The offscreen platform plugin exposes its configuration, including the
// screen list, through the native interface. The functions take the native
// interface as a trailing argument.
using GetConfiguration = QJsonObject (*)(QPlatformNativeInterface *);
using SetConfiguration = void (*)(const QJsonObject &, QPlatformNativeInterface *);

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

void dumpWidget(QString &out, const QWidget *widget, int depth)
{
    if (depth > 14)
        return;

    QString line = QString(depth * 2, QLatin1Char(' '))
            + QLatin1String(widget->metaObject()->className());
    const QString name = widget->objectName();
    if (!name.isEmpty() && !name.startsWith(QLatin1String("qt_")))
        line += QLatin1String(" #") + name;

    const QVariant text = widget->property("text");
    if (text.typeId() == QMetaType::QString && !text.toString().isEmpty()) {
        QString shown = text.toString().simplified();
        if (shown.size() > 40)
            shown = shown.left(39) + QChar(0x2026);
        line += QLatin1String(" \"") + shown + QLatin1Char('"');
    }
    if (widget->isWindow() && !widget->windowTitle().isEmpty())
        line += QLatin1String(" \"") + widget->windowTitle() + QLatin1Char('"');

    const QRect g = widget->geometry();
    line += QStringLiteral("  %1,%2 %3×%4").arg(g.x()).arg(g.y()).arg(g.width()).arg(g.height());
    if (!widget->isVisible())
        line += QLatin1String("  hidden");
    out += line + QLatin1Char('\n');

    for (const QObject *child : widget->children()) {
        if (!child->isWidgetType())
            continue;
        const auto *childWidget = static_cast<const QWidget *>(child);
        if (!childWidget->isWindow())
            dumpWidget(out, childWidget, depth + 1);
    }
}

} // namespace

QString BrowserBridge::pageOf(const QUrl &url)
{
    // app://widgets with no page is the first page, so Back to it returns
    // there rather than staying wherever the app was.
    const QString page = url.path().section(QLatin1Char('/'), 1, 1).toLower();
    return page.isEmpty() ? GalleryWindow::pageNames().constFirst() : page;
}

void BrowserBridge::selectOffscreenPlatform()
{
    // Nothing of this process may appear on screen: the browser window is the
    // only window. Popups and dialogs are top-levels too, so hiding the main
    // window alone would not be enough.
    qputenv("QT_QPA_PLATFORM", "offscreen");

#ifdef Q_OS_WIN
    // On Windows the offscreen plugin uses a FreeType font database that
    // scans QT_QPA_FONTDIR, or <Qt>/lib/fonts, which a Qt install does not
    // have. Without this the app would start with no fonts and draw no text.
    if (qEnvironmentVariableIsEmpty("QT_QPA_FONTDIR"))
        qputenv("QT_QPA_FONTDIR", windowsFontDirectory().toLocal8Bit());
#endif
}

QString BrowserBridge::windowsFontDirectory()
{
    QString windows = qEnvironmentVariable("WINDIR");
    if (windows.isEmpty())
        windows = QStringLiteral("C:/Windows");
    return QDir::fromNativeSeparators(windows) + QStringLiteral("/Fonts");
}

void BrowserBridge::registerFontCollections()
{
#ifdef Q_OS_WIN
    // That database also skips .ttc collections, and the CJK fonts Windows
    // ships are collections: Microsoft YaHei, SimSun, Yu Gothic, Meiryo.
    // Composed Chinese or Japanese text would draw as boxes. Registering a
    // file path does not load the file; FreeType opens it on first use.
    const QDir fonts(qEnvironmentVariable("QT_QPA_FONTDIR", windowsFontDirectory()));
    const QStringList collections = fonts.entryList({ QStringLiteral("*.ttc") }, QDir::Files);
    for (const QString &name : collections)
        QFontDatabase::addApplicationFont(fonts.filePath(name));
#endif
}

BrowserBridge::BrowserBridge(GalleryWindow *window, QObject *parent)
    : QObject(parent)
    , m_window(window)
    , m_url(QStringLiteral("app://widgets"))
    , m_pid(QCoreApplication::applicationPid())
{
    m_clock.start();

    m_renderTimer = new QTimer(this);
    m_renderTimer->setSingleShot(true);
    m_renderTimer->setTimerType(Qt::PreciseTimer);
    connect(m_renderTimer, &QTimer::timeout, this, &BrowserBridge::renderFrame);

    m_cursorTimer = new QTimer(this);
    m_cursorTimer->setSingleShot(true);
    m_cursorTimer->setInterval(0);
    connect(m_cursorTimer, &QTimer::timeout, this, &BrowserBridge::publishCursor);

    m_noticeTimer = new QTimer(this);
    m_noticeTimer->setSingleShot(true);
    m_noticeTimer->setInterval(kNoticeMs);
    connect(m_noticeTimer, &QTimer::timeout, this, [this] { setNotice(QString()); });

    qApp->installEventFilter(this);
    connect(qApp, &QGuiApplication::focusObjectChanged, this,
            &BrowserBridge::onFocusObjectChanged);
    connect(m_window, &GalleryWindow::pageChanged, this, [this](const QString &page) {
        // A page picked inside the app becomes a browser history entry, the
        // way a single-page web app routes, so Back returns to the previous
        // page. When the browser itself asked for this page, m_url already
        // says so and nothing is sent.
        if (page == pageOf(m_url))
            return;
        const QUrl url(QStringLiteral("app://widgets/") + page);
        m_url = url;
        QJsonObject message;
        message.insert(QStringLiteral("type"), QStringLiteral("navigate"));
        message.insert(QStringLiteral("url"), url.toString());
        send(message);
    });

    m_window->setWindowFlag(Qt::FramelessWindowHint);
    m_window->setGeometry(0, 0, 1280, 800);
    m_stack.append(m_window);
    m_window->show();
}

BrowserBridge::~BrowserBridge()
{
    qApp->removeEventFilter(this);
}

bool BrowserBridge::connectToBrowser(const QString &channelName, int tabId)
{
    auto *socket = new QLocalSocket(this);
    m_channel = new IpcChannel(socket, this);

    // Queued, not direct. A click can open a modal dialog or a menu, whose
    // exec() runs a nested event loop inside this handler, and QLocalSocket
    // does not emit readyRead again until the handler returns. Handled
    // directly, the app would stop hearing the browser (input, frame
    // acknowledgements) for as long as the dialog stayed open. Posted events
    // are delivered by the nested loop too.
    connect(m_channel, &IpcChannel::received, this, &BrowserBridge::onMessage,
            Qt::QueuedConnection);
    connect(m_channel, &IpcChannel::disconnected, qApp, [] {
        // The browser closed the tab, or is gone. Never outlive it.
        QCoreApplication::quit();
    });

    socket->connectToServer(channelName);
    if (!socket->waitForConnected(5000)) {
        qWarning("WidgetApp: could not reach the browser process.");
        return false;
    }

    // waitForConnected() runs a nested loop. Say hello on the next turn so the
    // browser's first viewport message is not handled inside it.
    QTimer::singleShot(0, this, [this, tabId] {
        QJsonObject hello;
        hello.insert(QStringLiteral("type"), QStringLiteral("hello"));
        hello.insert(QStringLiteral("tab"), tabId);
        hello.insert(QStringLiteral("pid"), static_cast<double>(m_pid));
        send(hello);
        publishState();
        publishSource();
    });
    return true;
}

void BrowserBridge::send(const QJsonObject &message)
{
    if (m_channel)
        m_channel->send(message);
}

void BrowserBridge::onMessage(const QJsonObject &message)
{
    const QString type = message.value(QStringLiteral("type")).toString();

    if (type == QLatin1String("viewport")) {
        setViewport(message.value(QStringLiteral("visible")).toBool(),
                    message.value(QStringLiteral("width")).toInt(),
                    message.value(QStringLiteral("height")).toInt(),
                    message.value(QStringLiteral("dpr")).toDouble());
    } else if (type == QLatin1String("input")) {
        deliverInput(message);
    } else if (type == QLatin1String("frameAck")) {
        releaseSequence(quint32(message.value(QStringLiteral("seq")).toDouble()));
    } else if (type == QLatin1String("navigate")) {
        const QUrl url(message.value(QStringLiteral("url")).toString());
        // app://widgets/video opens the video page. The browser keeps the
        // history; this process only shows what the address says.
        m_url = url;
        m_window->showPage(pageOf(url));
        publishState();
    } else if (type == QLatin1String("chrome")) {
        // Same wording as the QML renderer. The reminder is drawn into the
        // frame, so it stays above whatever the app shows.
        QString notice;
        if (message.value(QStringLiteral("fullScreen")).toBool())
            notice = QStringLiteral("Press F11 or Esc to leave full screen");
        else if (message.value(QStringLiteral("chromeHidden")).toBool())
            notice = QStringLiteral("Press Ctrl+Shift+F or Esc to show the bars");
        setNotice(notice);
    }
    // "reload" and "stop" need no handling: the browser restarts this process
    // to reload an app tab, and there is never a load in flight to stop.
}

// ---------------------------------------------------------------- viewport

void BrowserBridge::resizeScreen(int width, int height)
{
    QPlatformNativeInterface *native = QGuiApplication::platformNativeInterface();
    if (!native)
        return;
    auto get = reinterpret_cast<GetConfiguration>(
            native->nativeResourceForIntegration("configuration"));
    auto set = reinterpret_cast<SetConfiguration>(
            native->nativeResourceForIntegration("setConfiguration"));
    if (!get || !set) {
        // Not the offscreen plugin. Fall back to whatever it provides.
        static bool warned = false;
        if (!warned) {
            warned = true;
            qWarning("WidgetApp: the offscreen screen cannot be resized; popups may be misplaced.");
        }
        return;
    }

    QJsonObject configuration = get(native);
    QJsonArray screens = configuration.value(QStringLiteral("screens")).toArray();
    QJsonObject screen = screens.isEmpty() ? QJsonObject() : screens.first().toObject();
    if (screen.value(QStringLiteral("width")).toInt() == width
            && screen.value(QStringLiteral("height")).toInt() == height
            && configuration.value(QStringLiteral("synchronousWindowSystemEvents")).toBool()) {
        return;
    }

    // The screen is exactly the tab's page area. Combo box lists, menus and
    // dialogs are kept on screen by Qt, so this is what makes a list near the
    // bottom edge open upwards, as it would in a real window.
    screen.insert(QStringLiteral("name"), QStringLiteral("tab"));
    screen.insert(QStringLiteral("x"), 0);
    screen.insert(QStringLiteral("y"), 0);
    screen.insert(QStringLiteral("width"), width);
    screen.insert(QStringLiteral("height"), height);
#ifdef Q_OS_MACOS
    // Point sizes match a native macOS application.
    screen.insert(QStringLiteral("logicalDpi"), 72);
    screen.insert(QStringLiteral("logicalBaseDpi"), 72);
#endif
    screens = QJsonArray{ screen };
    // Synchronous delivery makes each injected event finish before the next
    // message is read, so a click is fully handled before the frame after it.
    // Frame margins are off because nothing draws a frame around a tab.
    configuration.insert(QStringLiteral("screens"), screens);
    configuration.insert(QStringLiteral("synchronousWindowSystemEvents"), true);
    configuration.insert(QStringLiteral("windowFrameMargins"), false);
    set(configuration, native);
}

void BrowserBridge::setViewport(bool visible, int width, int height, qreal dpr)
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
            resizeScreen(width, height);
            m_window->setGeometry(0, 0, width, height);
        }
    }
    scheduleRender();
}

void BrowserBridge::setNotice(const QString &notice)
{
    if (notice == m_notice)
        return;
    m_notice = notice;
    if (!notice.isEmpty())
        m_noticeTimer->start();
    scheduleRender();
}

// ------------------------------------------------------------------ frames

bool BrowserBridge::eventFilter(QObject *watched, QEvent *event)
{
    if (!watched->isWidgetType())
        return false;
    auto *widget = static_cast<QWidget *>(watched);

    switch (event->type()) {
    case QEvent::Show:
        // Sent before the window is mapped, so the window is in the stack
        // by the time its first paint asks for a frame.
        if (widget->isWindow()) {
            raiseWindow(widget);
            scheduleRender();
        }
        break;
    case QEvent::Hide:
        if (widget->isWindow()) {
            dropWindow(widget);
            scheduleRender();
            m_cursorTimer->start();
        }
        break;
    case QEvent::Paint:
        if (!m_painting)
            scheduleRender();
        break;
    case QEvent::Move:
    case QEvent::Resize:
        if (widget->isWindow())
            scheduleRender();
        break;
    case QEvent::WindowTitleChange:
        if (widget == m_window)
            publishState();
        break;
    case QEvent::CursorChange:
        m_cursorTimer->start();
        break;
    default:
        break;
    }
    return false;
}

void BrowserBridge::raiseWindow(QWidget *window)
{
    if (window == m_window)
        return;
    dropWindow(window);
    m_stack.append(window);
}

void BrowserBridge::dropWindow(QWidget *window)
{
    if (window == m_window)
        return;
    m_stack.removeAll(window);
    m_stack.removeAll(QPointer<QWidget>());
    if (m_hoverWindow == window)
        m_hoverWindow = nullptr;
}

void BrowserBridge::scheduleRender()
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

int BrowserBridge::freeSlot() const
{
    for (int i = 0; i < 2; ++i) {
        if (!m_slots[i].pending)
            return i;
    }
    return -1;
}

QString BrowserBridge::prepareSlot(int index, int bytes)
{
    Slot &slot = m_slots[index];
    if (slot.pixels.isAttached() && slot.pixels.size() == bytes)
        return slot.pixels.key();

    const QString key = QStringLiteral("qbwx-%1-%2-%3").arg(m_pid).arg(index).arg(++m_epoch);
    if (!slot.pixels.create(key, bytes)) {
        qWarning("WidgetApp: could not create the frame buffer: %s",
                 qPrintable(slot.pixels.errorString()));
        return {};
    }
    return key;
}

void BrowserBridge::releaseSequence(quint32 sequence)
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

QPointF BrowserBridge::origin() const
{
    return m_window->geometry().topLeft();
}

void BrowserBridge::renderFrame()
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
        qWarning("WidgetApp: viewport %dx%d is not drawable.", width, height);
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
        // Widgets paint straight into the shared buffer. Format 2 tells the
        // browser the bytes are QImage::Format_ARGB32_Premultiplied.
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

    QJsonObject message;
    message.insert(QStringLiteral("type"), QStringLiteral("frame"));
    message.insert(QStringLiteral("seq"), static_cast<double>(sequence));
    message.insert(QStringLiteral("width"), width);
    message.insert(QStringLiteral("height"), height);
    message.insert(QStringLiteral("stride"), stride);
    message.insert(QStringLiteral("format"), 2);
    message.insert(QStringLiteral("shm"), key);
    send(message);

    // Widgets move while the user scrolls or a layout settles, without the
    // focus moving. Re-read the caret once per frame.
    publishInputMethod();
    publishCursor();
}

void BrowserBridge::paintDecorations(QPainter &painter, QWidget *window, const QPoint &offset)
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

void BrowserBridge::paintNotice(QPainter &painter)
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

// ------------------------------------------------------------------- input

void BrowserBridge::deliverInput(const QJsonObject &message)
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
    publishInputMethod();
}

QWidget *BrowserBridge::windowAt(const QPointF &global) const
{
    for (qsizetype i = m_stack.size() - 1; i >= 0; --i) {
        QWidget *window = m_stack.at(i);
        if (window && window->isVisible() && window->geometry().contains(global.toPoint()))
            return window;
    }
    return m_window;
}

QWidget *BrowserBridge::mouseTarget(const QPointF &global) const
{
    // While a popup is open it receives every mouse event, wherever it is.
    // That is how a click outside a menu closes it.
    if (QWidget *popup = QApplication::activePopupWidget())
        return popup;
    return windowAt(global);
}

QWidget *BrowserBridge::keyTarget() const
{
    if (QWidget *popup = QApplication::activePopupWidget())
        return popup;
    if (QWidget *active = QApplication::activeWindow())
        return active;
    if (QWidget *modal = QApplication::activeModalWidget())
        return modal;
    return m_window;
}

QWidget *BrowserBridge::widgetAt(const QPointF &global) const
{
    QWidget *window = windowAt(global);
    if (!window)
        return nullptr;
    QWidget *child = window->childAt(window->mapFromGlobal(global.toPoint()));
    return child ? child : window;
}

void BrowserBridge::activate(QWidget *window)
{
    if (!window || !window->windowHandle())
        return;
    if (QGuiApplication::focusWindow() == window->windowHandle())
        return;
    QWindowSystemInterface::handleFocusWindowChanged<Sync>(window->windowHandle(),
                                                           Qt::ActiveWindowFocusReason);
}

void BrowserBridge::closeToolTips()
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

void BrowserBridge::hoverWindow(QWidget *window, const QPointF &global)
{
    if (window == m_hoverWindow)
        return;
    if (m_hoverWindow && m_hoverWindow->windowHandle())
        QWindowSystemInterface::handleLeaveEvent<Sync>(m_hoverWindow->windowHandle());
    m_hoverWindow = window;
    if (window && window->windowHandle()) {
        QWindowSystemInterface::handleEnterEvent<Sync>(
                window->windowHandle(), global - QPointF(window->geometry().topLeft()), global);
    }
}

void BrowserBridge::deliverMouse(QEvent::Type type, const QJsonObject &message)
{
    const QPointF global = pointFrom(message) + origin();
    const auto buttons = Qt::MouseButtons(message.value(QStringLiteral("buttons")).toInt());
    const auto button = Qt::MouseButton(message.value(QStringLiteral("button")).toInt());

    if (type == QEvent::MouseButtonPress)
        closeToolTips();
    // The window under the press keeps the mouse until the last button is
    // released, like an implicit grab on a real platform.
    if (type == QEvent::MouseButtonPress && !m_grabWindow) {
        m_grabWindow = mouseTarget(global);
        m_pressWidget = widgetAt(global);
    }
    QWidget *target = m_grabWindow ? m_grabWindow.data() : mouseTarget(global);
    // A popup opened by the press (a tool button menu) owns the mouse from
    // then on, including the release of that same press.
    if (QWidget *popup = QApplication::activePopupWidget())
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
        QWidget *modal = QApplication::activeModalWidget();
        activate(modal && modal != target ? modal : target);
    }
    if (!buttons || type == QEvent::MouseButtonPress)
        hoverWindow(target, global);

    m_lastGlobal = global;
    const QPointF local = global - QPointF(target->geometry().topLeft());
    QWindowSystemInterface::handleMouseEvent<Sync>(
            target->windowHandle(), ulong(m_clock.elapsed()), local, global, buttons, button,
            type, modsFrom(message));
    publishCursor();
}

void BrowserBridge::deliverWheel(const QJsonObject &message)
{
    const QPointF global = pointFrom(message) + origin();
    QWidget *target = mouseTarget(global);
    if (!target || !target->windowHandle())
        return;
    closeToolTips();

    const QPoint pixel(message.value(QStringLiteral("px")).toInt(),
                       message.value(QStringLiteral("py")).toInt());
    const QPoint angle(message.value(QStringLiteral("ax")).toInt(),
                       message.value(QStringLiteral("ay")).toInt());
    QWindowSystemInterface::handleWheelEvent(
            target->windowHandle(), ulong(m_clock.elapsed()),
            global - QPointF(target->geometry().topLeft()), global, pixel, angle,
            modsFrom(message), Qt::ScrollPhase(message.value(QStringLiteral("phase")).toInt()),
            Qt::MouseEventNotSynthesized, message.value(QStringLiteral("inverted")).toBool());
    m_lastGlobal = global;
}

void BrowserBridge::deliverHover(const QJsonObject &message)
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

void BrowserBridge::deliverKey(QEvent::Type type, const QJsonObject &message)
{
    QWidget *target = keyTarget();
    if (!target || !target->windowHandle())
        return;
    if (type == QEvent::KeyPress) {
        closeToolTips();
        if (!QGuiApplication::focusWindow())
            activate(target);
    }

    // Shortcuts are matched inside this call, before the key reaches the
    // focus widget, exactly as for a key from the platform.
    QWindowSystemInterface::handleExtendedKeyEvent(
            target->windowHandle(), ulong(m_clock.elapsed()), type,
            message.value(QStringLiteral("key")).toInt(), modsFrom(message),
            quint32(message.value(QStringLiteral("scan")).toDouble()),
            quint32(message.value(QStringLiteral("vk")).toDouble()),
            quint32(message.value(QStringLiteral("nativeMods")).toDouble()),
            message.value(QStringLiteral("text")).toString(),
            message.value(QStringLiteral("repeat")).toBool());
}

void BrowserBridge::deliverFocus(bool on)
{
    m_focused = on;
    if (on) {
        // The page area took keyboard focus: the app becomes the active
        // application, so the caret blinks and selections turn blue.
        QWidget *modal = QApplication::activeModalWidget();
        activate(modal ? modal : (QApplication::activeWindow() ? QApplication::activeWindow()
                                                               : m_window));
        return;
    }
    if (QGuiApplication::focusWindow())
        QWindowSystemInterface::handleFocusWindowChanged<Sync>(nullptr, Qt::ActiveWindowFocusReason);
}

void BrowserBridge::deliverInputMethod(const QJsonObject &message)
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

void BrowserBridge::onFocusObjectChanged()
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
    publishInputMethod();
}

// ----------------------------------------------------------------- publish

void BrowserBridge::publishState()
{
    QJsonObject message;
    message.insert(QStringLiteral("type"), QStringLiteral("state"));
    message.insert(QStringLiteral("status"), 2); // Ready: there is nothing to load
    message.insert(QStringLiteral("title"), m_window->windowTitle());
    message.insert(QStringLiteral("url"), m_url.toString());
    message.insert(QStringLiteral("httpStatus"), 0);
    message.insert(QStringLiteral("progress"), 1.0);
    message.insert(QStringLiteral("error"), QString());
    message.insert(QStringLiteral("bytes"), 0);
    send(message);
}

void BrowserBridge::publishSource()
{
    // There is no document behind this tab. View source shows the live
    // widget tree instead, which is the closest thing to one.
    QString text = QStringLiteral(
            "// app://widgets is a native Qt Widgets process, not a QML document.\n"
            "// It runs on Qt's offscreen platform and paints every top-level\n"
            "// widget into the frame this tab shows. Its widget tree at startup:\n\n");
    dumpWidget(text, m_window, 0);

    QJsonObject message;
    message.insert(QStringLiteral("type"), QStringLiteral("source"));
    message.insert(QStringLiteral("text"), text);
    send(message);
}

void BrowserBridge::publishCursor()
{
    int shape = Qt::ArrowCursor;
    if (const QCursor *override = QGuiApplication::overrideCursor()) {
        shape = override->shape();
    } else {
        // During a drag the cursor belongs to the widget that was pressed,
        // the way a splitter handle keeps its arrows past its own edge.
        QWidget *widget = m_pressWidget ? m_pressWidget.data() : widgetAt(m_lastGlobal);
        if (widget)
            shape = widget->cursor().shape();
    }
    if (shape == Qt::BitmapCursor)
        shape = Qt::ArrowCursor;
    if (shape == m_cursor)
        return;
    m_cursor = shape;

    QJsonObject message;
    message.insert(QStringLiteral("type"), QStringLiteral("cursor"));
    message.insert(QStringLiteral("shape"), shape);
    send(message);
}

void BrowserBridge::publishInputMethod()
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
        const QPointF offset = QPointF(widget->mapToGlobal(QPoint(0, 0))) - origin();
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

    QJsonObject message = state;
    message.insert(QStringLiteral("type"), QStringLiteral("ime"));
    send(message);
}
