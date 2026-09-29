#include "TabViewport.h"

#include "TabManager.h"

#include <QQuickWindow>

TabViewport::TabViewport(QQuickItem *parent)
    : QQuickItem(parent)
{
    setFlag(ItemHasContents, false);
}

void TabViewport::setManager(TabManager *manager)
{
    if (m_manager == manager)
        return;

    m_manager = manager;
    emit managerChanged();
    publish();
}

void TabViewport::geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry)
{
    QQuickItem::geometryChange(newGeometry, oldGeometry);
    publish();
}

void TabViewport::itemChange(ItemChange change, const ItemChangeData &value)
{
    QQuickItem::itemChange(change, value);

    if (change == ItemSceneChange && value.window) {
        // QQuickItem has no notification for an ancestor moving it, and this item
        // is positioned by anchors that resolve after construction. Recomputing
        // once per frame covers that as well as window resizes and DPI changes;
        // the manager ignores a rectangle that has not actually moved.
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

    // Creates the native handle if it does not exist yet, which is what the
    // renderer processes are parented to.
    m_manager->setHostWindow(host->winId());

    const QPointF topLeft = mapToScene(QPointF(0, 0));
    const qreal ratio = host->devicePixelRatio();

    m_manager->setViewportRect(QRect(qRound(topLeft.x() * ratio), qRound(topLeft.y() * ratio),
                                     qRound(width() * ratio), qRound(height() * ratio)));
    m_manager->syncPlacement();
}
