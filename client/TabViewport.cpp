#include "TabViewport.h"

#include "BrowserTab.h"
#include "TabManager.h"

#include <QCursor>
#include <QGuiApplication>
#include <QHoverEvent>
#include <QInputMethod>
#include <QInputMethodEvent>
#include <QJsonArray>
#include <QJsonObject>
#include <QKeyEvent>
#include <QTextCharFormat>
#include <QMouseEvent>
#include <QQuickWindow>
#include <QSGSimpleTextureNode>
#include <QWheelEvent>

namespace {

QRectF rectFrom(const QJsonValue &value)
{
    const QJsonArray rect = value.toArray();
    if (rect.size() != 4)
        return {};
    return QRectF(rect.at(0).toDouble(), rect.at(1).toDouble(), rect.at(2).toDouble(),
                  rect.at(3).toDouble());
}

QString colorName(const QBrush &brush)
{
    return brush.style() == Qt::NoBrush ? QString() : brush.color().name(QColor::HexArgb);
}

} // namespace

TabViewport::TabViewport(QQuickItem *parent)
    : QQuickItem(parent)
{
    setFlag(ItemHasContents, true);
    setAcceptedMouseButtons(Qt::AllButtons);
    setAcceptHoverEvents(true);
    setFlag(ItemIsFocusScope, true);
}

void TabViewport::setManager(TabManager *manager)
{
    if (m_manager == manager)
        return;

    if (m_manager)
        disconnect(m_manager, nullptr, this, nullptr);

    m_manager = manager;
    emit managerChanged();

    if (m_manager)
        connect(m_manager, &TabManager::currentChanged, this, &TabViewport::bindCurrentTab);

    bindCurrentTab();
    publish();
}

void TabViewport::geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry)
{
    QQuickItem::geometryChange(newGeometry, oldGeometry);
    publish();
    update();
}

void TabViewport::itemChange(ItemChange change, const ItemChangeData &value)
{
    QQuickItem::itemChange(change, value);

    if (change == ItemSceneChange && value.window) {
        // Anchors resolve after construction, and there is no signal for an
        // ancestor moving this item. Once per frame covers that, plus resizes
        // and DPI changes. An unchanged rectangle is not sent again.
        connect(value.window, &QQuickWindow::afterAnimating, this, &TabViewport::publish,
                Qt::UniqueConnection);
        publish();
    }
}

void TabViewport::publish()
{
    QQuickWindow *host = window();
    if (!m_manager || !host)
        return;

    m_manager->setViewport(QSize(qRound(width()), qRound(height())), host->devicePixelRatio());
}

void TabViewport::bindCurrentTab()
{
    if (m_tab) {
        disconnect(m_tab, nullptr, this, nullptr);
        // The tab going to the background loses the keyboard with it.
        if (hasActiveFocus())
            postFocus(m_tab, false);
    }

    m_tab = m_manager ? m_manager->currentTab() : nullptr;
    syncInputMethod(int(Qt::ImQueryAll), true);
    if (!m_tab) {
        adoptFrame();
        return;
    }

    // Focus is only reported when it changes here, so a process that was not
    // listening at that moment must be told. That is a tab brought to the
    // front while the page area has the keyboard, and a process that starts
    // behind a focused page area: the first renderer, a reload, or a link to
    // an app:// address, which replaces the process without the viewport
    // losing focus. Without it the app stays inactive: keys reach it, but it
    // draws no focus ring or caret until the window is deactivated and
    // activated again.
    if (hasActiveFocus())
        postFocus(m_tab, true);
    connect(m_tab, &BrowserTab::attached, this, [this] {
        if (hasActiveFocus())
            postFocus(m_tab, true);
    });

    connect(m_tab, &BrowserTab::inputMethodChanged, this, &TabViewport::syncInputMethod);

    connect(m_tab, &BrowserTab::frameChanged, this, &TabViewport::adoptFrame);
    connect(m_tab, &BrowserTab::cursorChanged, this, [this](int shape) {
        setCursor(Qt::CursorShape(shape));
    });
    setCursor(Qt::CursorShape(m_tab->cursorShape()));
    adoptFrame();
}

void TabViewport::adoptFrame()
{
    QImage frame = m_tab ? m_tab->frame() : QImage();
    {
        QMutexLocker lock(&m_mutex);
        m_frame = frame;
    }
    update();
}

BrowserTab *TabViewport::currentTab() const
{
    return m_tab;
}

QSGNode *TabViewport::updatePaintNode(QSGNode *oldNode, UpdatePaintNodeData *)
{
    QImage frame;
    {
        QMutexLocker lock(&m_mutex);
        frame = m_frame;
    }

    if (frame.isNull() || width() < 1 || height() < 1) {
        delete oldNode;
        return nullptr;
    }

    auto *node = static_cast<QSGSimpleTextureNode *>(oldNode);
    if (!node) {
        node = new QSGSimpleTextureNode;
        node->setOwnsTexture(true);
        node->setFiltering(QSGTexture::Linear);
    }

    QSGTexture *texture = window()->createTextureFromImage(
            frame, QQuickWindow::TextureHasAlphaChannel);
    node->setTexture(texture);
    node->setRect(0, 0, width(), height());
    return node;
}

void TabViewport::postMouse(const QString &action, QMouseEvent *event)
{
    BrowserTab *tab = currentTab();
    if (!tab)
        return;

    QJsonObject message;
    message.insert(QStringLiteral("kind"), QStringLiteral("mouse"));
    message.insert(QStringLiteral("action"), action);
    message.insert(QStringLiteral("x"), event->position().x());
    message.insert(QStringLiteral("y"), event->position().y());
    message.insert(QStringLiteral("button"), int(event->button()));
    message.insert(QStringLiteral("buttons"), int(event->buttons()));
    message.insert(QStringLiteral("mods"), int(event->modifiers()));
    tab->postInput(message);
    event->accept();
}

void TabViewport::postKey(const QString &action, QKeyEvent *event)
{
    BrowserTab *tab = currentTab();
    if (!tab)
        return;

    QJsonObject message;
    message.insert(QStringLiteral("kind"), QStringLiteral("key"));
    message.insert(QStringLiteral("action"), action);
    message.insert(QStringLiteral("key"), event->key());
    message.insert(QStringLiteral("mods"), int(event->modifiers()));
    message.insert(QStringLiteral("text"), event->text());
    message.insert(QStringLiteral("repeat"), event->isAutoRepeat());
    message.insert(QStringLiteral("scan"), static_cast<double>(event->nativeScanCode()));
    message.insert(QStringLiteral("vk"), static_cast<double>(event->nativeVirtualKey()));
    message.insert(QStringLiteral("nativeMods"), static_cast<double>(event->nativeModifiers()));
    tab->postInput(message);
    event->accept();
}

void TabViewport::syncInputMethod(int queries, bool focusMoved)
{
    // Only claim input-method support while the page's focused item takes
    // text. macOS sends key presses through the input method only when the
    // focus object answers Qt::ImEnabled, and password or button focus must
    // not open a candidate window.
    const QJsonObject state = m_tab ? m_tab->inputMethodState() : QJsonObject();
    setFlag(ItemAcceptsInputMethod, state.value(QStringLiteral("enabled")).toBool());

    if (!hasActiveFocus())
        return;

    if (focusMoved) {
        // The renderer has already committed the old field's preedit.
        m_discardInputMethod = true;
        QGuiApplication::inputMethod()->reset();
        m_discardInputMethod = false;
    }
    QGuiApplication::inputMethod()->update(Qt::InputMethodQueries(queries));
}

void TabViewport::inputMethodEvent(QInputMethodEvent *event)
{
    BrowserTab *tab = currentTab();
    if (!tab || m_discardInputMethod) {
        event->accept();
        return;
    }

    QJsonArray attributes;
    for (const QInputMethodEvent::Attribute &attribute : event->attributes()) {
        QJsonObject item;
        item.insert(QStringLiteral("type"), int(attribute.type));
        item.insert(QStringLiteral("start"), attribute.start);
        item.insert(QStringLiteral("length"), attribute.length);
        if (attribute.type == QInputMethodEvent::TextFormat) {
            const QTextCharFormat format =
                    qvariant_cast<QTextFormat>(attribute.value).toCharFormat();
            item.insert(QStringLiteral("underline"), int(format.underlineStyle()));
            if (format.underlineColor().isValid())
                item.insert(QStringLiteral("underlineColor"),
                            format.underlineColor().name(QColor::HexArgb));
            item.insert(QStringLiteral("foreground"), colorName(format.foreground()));
            item.insert(QStringLiteral("background"), colorName(format.background()));
        } else if (attribute.type != QInputMethodEvent::Cursor
                   && attribute.type != QInputMethodEvent::Selection) {
            continue;
        }
        attributes.append(item);
    }

    QJsonObject message;
    message.insert(QStringLiteral("kind"), QStringLiteral("ime"));
    message.insert(QStringLiteral("preedit"), event->preeditString());
    message.insert(QStringLiteral("commit"), event->commitString());
    message.insert(QStringLiteral("replaceStart"), event->replacementStart());
    message.insert(QStringLiteral("replaceLength"), event->replacementLength());
    message.insert(QStringLiteral("attributes"), attributes);
    tab->postInput(message);
    event->accept();
}

QVariant TabViewport::inputMethodQuery(Qt::InputMethodQuery query) const
{
    // Answered from the renderer's last report: the platform asks
    // synchronously, and the page lives in another process. Rectangles are in
    // the page's scene coordinates, which are this item's coordinates.
    const QJsonObject state = m_tab ? m_tab->inputMethodState() : QJsonObject();
    const auto value = [&state](const char *key) { return state.value(QLatin1String(key)); };

    switch (query) {
    case Qt::ImEnabled:
        return value("enabled").toBool();
    case Qt::ImHints:
        return value("hints").toInt();
    case Qt::ImReadOnly:
        return value("readOnly").toBool();
    case Qt::ImCursorRectangle:
        return rectFrom(value("cursorRect"));
    case Qt::ImAnchorRectangle:
        return rectFrom(value("anchorRect"));
    case Qt::ImInputItemClipRectangle: {
        const QRectF clip = rectFrom(value("clipRect"));
        return clip.isValid() ? QVariant(clip) : QQuickItem::inputMethodQuery(query);
    }
    case Qt::ImSurroundingText:
        return value("text").toString();
    case Qt::ImCurrentSelection:
        return value("selection").toString();
    case Qt::ImCursorPosition:
        return value("cursor").toInt();
    case Qt::ImAnchorPosition:
        return value("anchor").toInt();
    case Qt::ImAbsolutePosition:
        return value("absolute").toInt();
    case Qt::ImTextBeforeCursor:
        return value("before").toString();
    case Qt::ImTextAfterCursor:
        return value("after").toString();
    default:
        return QQuickItem::inputMethodQuery(query);
    }
}

void TabViewport::mousePressEvent(QMouseEvent *event)
{
    forceActiveFocus();
    postMouse(QStringLiteral("press"), event);
}

void TabViewport::mouseMoveEvent(QMouseEvent *event)
{
    postMouse(QStringLiteral("move"), event);
}

void TabViewport::mouseReleaseEvent(QMouseEvent *event)
{
    postMouse(QStringLiteral("release"), event);
}

void TabViewport::mouseDoubleClickEvent(QMouseEvent *event)
{
    postMouse(QStringLiteral("dbl"), event);
}

void TabViewport::wheelEvent(QWheelEvent *event)
{
    BrowserTab *tab = currentTab();
    if (!tab)
        return;

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
    tab->postInput(message);
    event->accept();
}

void TabViewport::hoverMoveEvent(QHoverEvent *event)
{
    BrowserTab *tab = currentTab();
    if (!tab)
        return;

    QJsonObject message;
    message.insert(QStringLiteral("kind"), QStringLiteral("hover"));
    message.insert(QStringLiteral("action"), QStringLiteral("move"));
    message.insert(QStringLiteral("x"), event->position().x());
    message.insert(QStringLiteral("y"), event->position().y());
    message.insert(QStringLiteral("mods"), int(event->modifiers()));
    tab->postInput(message);
    event->accept();
}

void TabViewport::hoverLeaveEvent(QHoverEvent *event)
{
    BrowserTab *tab = currentTab();
    if (tab) {
        QJsonObject message;
        message.insert(QStringLiteral("kind"), QStringLiteral("hover"));
        message.insert(QStringLiteral("action"), QStringLiteral("leave"));
        tab->postInput(message);
    }
    event->accept();
}

void TabViewport::keyPressEvent(QKeyEvent *event)
{
    postKey(QStringLiteral("press"), event);
}

void TabViewport::keyReleaseEvent(QKeyEvent *event)
{
    postKey(QStringLiteral("release"), event);
}

void TabViewport::postFocus(BrowserTab *tab, bool on)
{
    if (!tab)
        return;
    QJsonObject message;
    message.insert(QStringLiteral("kind"), QStringLiteral("focus"));
    message.insert(QStringLiteral("on"), on);
    tab->postInput(message);
}

void TabViewport::focusInEvent(QFocusEvent *event)
{
    postFocus(currentTab(), true);
    QQuickItem::focusInEvent(event);
}

void TabViewport::focusOutEvent(QFocusEvent *event)
{
    postFocus(currentTab(), false);
    QQuickItem::focusOutEvent(event);
}
