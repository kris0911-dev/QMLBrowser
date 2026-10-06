#pragma once

#include <QImage>
#include <QMutex>
#include <QPointer>
#include <QQuickItem>

#include "TabManager.h"

// The page area inside the browser window.
//
// The renderer process has no window of its own. It submits a frame, and this
// item draws that frame in the browser's scene graph, which is why the page
// moves with the window. Pointer and key events are forwarded to the renderer
// because there is nothing else on screen to receive them.
class TabViewport : public QQuickItem
{
    Q_OBJECT

    Q_PROPERTY(TabManager *manager READ manager WRITE setManager NOTIFY managerChanged)

public:
    explicit TabViewport(QQuickItem *parent = nullptr);

    TabManager *manager() const { return m_manager; }
    void setManager(TabManager *manager);

signals:
    void managerChanged();

protected:
    void geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry) override;
    void itemChange(ItemChange change, const ItemChangeData &value) override;
    QSGNode *updatePaintNode(QSGNode *oldNode, UpdatePaintNodeData *data) override;

    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void hoverMoveEvent(QHoverEvent *event) override;
    void hoverLeaveEvent(QHoverEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void keyReleaseEvent(QKeyEvent *event) override;
    void focusInEvent(QFocusEvent *event) override;
    void focusOutEvent(QFocusEvent *event) override;

private:
    void publish();
    void bindCurrentTab();
    void adoptFrame();
    void postMouse(const QString &action, QMouseEvent *event);
    void postKey(const QString &action, QKeyEvent *event);
    BrowserTab *currentTab() const;

    QPointer<TabManager> m_manager;
    QPointer<BrowserTab> m_tab;
    QImage m_frame;
    QMutex m_mutex;
};
