#pragma once

#include <QLibrary>
#include <QString>

#include "qmlbrowser_canvas.h"

class QImage;

// One canvas library (see canvas/include/qmlbrowser_canvas.h), resolved and
// checked. NativeCanvas draws through it; the smoke test uses it directly.
class CanvasLibrary
{
public:
    // <folder of this executable>/canvas. The only place libraries load from.
    static QString directory();
    // Letters, digits, '-' and '_'. Keeps a page from naming a path.
    static bool isValidName(const QString &name);

    bool load(const QString &name);
    bool isLoaded() const { return m_draw != nullptr; }
    QString errorString() const { return m_error; }

    // `image` must be QImage::Format_ARGB32_Premultiplied.
    void draw(QImage &image, double time) const;

private:
    bool fail(const QString &message);

    QLibrary m_library;
    qmlbrowser_canvas_draw_fn m_draw = nullptr;
    QString m_error;
};
