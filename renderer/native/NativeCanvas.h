#pragma once

#include <QImage>
#include <QQuickItem>

#include "CanvasLibrary.h"

// Shows pixels drawn by a plain C shared library from the renderer's canvas
// folder (see canvas/include/qmlbrowser_canvas.h).
//
// The library fills a QImage in updatePolish() and updatePaintNode() uploads
// it, as JuliaSet does. Only the library name comes from the page, and it
// cannot contain a path.
class NativeCanvas : public QQuickItem
{
    Q_OBJECT

    Q_PROPERTY(QString library READ library WRITE setLibrary NOTIFY libraryChanged)
    Q_PROPERTY(qreal time READ time WRITE setTime NOTIFY timeChanged)
    Q_PROPERTY(Status status READ status NOTIFY statusChanged)
    Q_PROPERTY(QString errorString READ errorString NOTIFY statusChanged)
    Q_PROPERTY(qreal renderTime READ renderTime NOTIFY renderTimeChanged)

public:
    enum Status { Null, Ready, Error };
    Q_ENUM(Status)

    explicit NativeCanvas(QQuickItem *parent = nullptr);

    QString library() const { return m_libraryName; }
    void setLibrary(const QString &name);
    qreal time() const { return m_time; }
    void setTime(qreal time);
    Status status() const { return m_status; }
    QString errorString() const { return m_library.errorString(); }
    // Milliseconds the library took to draw the last image.
    qreal renderTime() const { return m_renderTime; }

signals:
    void libraryChanged();
    void timeChanged();
    void statusChanged();
    void renderTimeChanged();

protected:
    void updatePolish() override;
    QSGNode *updatePaintNode(QSGNode *oldNode, UpdatePaintNodeData *) override;
    void geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry) override;

private:
    QString m_libraryName;
    CanvasLibrary m_library;
    Status m_status = Null;
    qreal m_time = 0;
    qreal m_renderTime = 0;
    QImage m_image;
    bool m_imageChanged = false;
};
