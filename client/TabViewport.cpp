#include "TabViewport.h"

#include "BrowserTab.h"
#include "TabManager.h"

#include <QCursor>
#include <QHoverEvent>
#include <QJsonObject>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QQuickWindow>
#include <QSGSimpleTextureNode>
#include <QWheelEvent>

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
    if (m_tab)
        disconnect(m_tab, nullptr, this, nullptr);

    m_tab = m_manager ? m_manager->currentTab() : nullptr;
    if (!m_tab) {
        adoptFrame();
        return;
    }

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

void TabViewport::focusInEvent(QFocusEvent *event)
{
    BrowserTab *tab = currentTab();
    if (tab) {
        QJsonObject message;
        message.insert(QStringLiteral("kind"), QStringLiteral("focus"));
        message.insert(QStringLiteral("on"), true);
        tab->postInput(message);
    }
    QQuickItem::focusInEvent(event);
}

void TabViewport::focusOutEvent(QFocusEvent *event)
{
    BrowserTab *tab = currentTab();
    if (tab) {
        QJsonObject message;
        message.insert(QStringLiteral("kind"), QStringLiteral("focus"));
        message.insert(QStringLiteral("on"), false);
        tab->postInput(message);
    }
    QQuickItem::focusOutEvent(event);
}
