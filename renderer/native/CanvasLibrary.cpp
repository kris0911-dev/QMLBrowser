#include "CanvasLibrary.h"

#include <QCoreApplication>
#include <QDir>
#include <QImage>
#include <QRegularExpression>

QString CanvasLibrary::directory()
{
    return QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("canvas"));
}

bool CanvasLibrary::isValidName(const QString &name)
{
    static const QRegularExpression pattern(QStringLiteral("^[A-Za-z0-9_-]{1,64}$"));
    return pattern.match(name).hasMatch();
}

bool CanvasLibrary::fail(const QString &message)
{
    m_draw = nullptr;
    m_error = message;
    return false;
}

bool CanvasLibrary::load(const QString &name)
{
    m_draw = nullptr;
    m_error.clear();
    if (m_library.isLoaded())
        m_library.unload();

    if (!isValidName(name))
        return fail(QStringLiteral("\"%1\" is not a canvas library name.").arg(name));

    // No suffix: QLibrary tries .dll, .so or .dylib as the platform expects.
    m_library.setFileName(QDir(directory()).filePath(name));
    if (!m_library.load())
        return fail(QStringLiteral("Could not load canvas library \"%1\": %2")
                            .arg(name, m_library.errorString()));

    const auto abi = reinterpret_cast<qmlbrowser_canvas_abi_fn>(
            m_library.resolve("qmlbrowser_canvas_abi"));
    const auto draw = reinterpret_cast<qmlbrowser_canvas_draw_fn>(
            m_library.resolve("qmlbrowser_canvas_draw"));
    if (!abi || !draw)
        return fail(QStringLiteral("\"%1\" does not export qmlbrowser_canvas_abi and "
                                   "qmlbrowser_canvas_draw.").arg(name));
    const int version = abi();
    if (version != QMLBROWSER_CANVAS_ABI)
        return fail(QStringLiteral("\"%1\" implements canvas ABI %2; this renderer needs %3.")
                            .arg(name).arg(version).arg(QMLBROWSER_CANVAS_ABI));

    m_draw = draw;
    return true;
}

void CanvasLibrary::draw(QImage &image, double time) const
{
    Q_ASSERT(image.format() == QImage::Format_ARGB32_Premultiplied);
    if (!m_draw || image.isNull())
        return;
    m_draw(reinterpret_cast<uint32_t *>(image.bits()), image.width(), image.height(),
           int(image.bytesPerLine()), time);
}
