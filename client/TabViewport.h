#pragma once

#include <QPointer>
#include <QQuickItem>

#include "TabManager.h"

// A hole in the browser chrome where the active renderer's window is shown.
//
// This item paints nothing. It reports its position and size, in device pixels
// relative to the browser window, to the TabManager. On Windows that rectangle
// is passed to SetWindowPos on the child HWND. On macOS the same numbers are
// converted back to logical screen coordinates and sent as a `place` message,
// because the renderer's window id is a pointer in the other process.
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

private:
    void publish();

    QPointer<TabManager> m_manager;
};
