#include "OffscreenPlatform.h"

#include <QApplication>
#include <QDir>
#include <QFont>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonObject>

#include <qpa/qplatformnativeinterface.h>

namespace {

// The offscreen platform plugin exposes its configuration, including the
// screen list, through the native interface. The functions take the native
// interface as a trailing argument.
using GetConfiguration = QJsonObject (*)(QPlatformNativeInterface *);
using SetConfiguration = void (*)(const QJsonObject &, QPlatformNativeInterface *);

#ifdef Q_OS_WIN
QString windowsFontDirectory()
{
    QString windows = qEnvironmentVariable("WINDIR");
    if (windows.isEmpty())
        windows = QStringLiteral("C:/Windows");
    return QDir::fromNativeSeparators(windows) + QStringLiteral("/Fonts");
}
#endif

} // namespace

void OffscreenPlatform::select()
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

void OffscreenPlatform::registerFontCollections()
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

void OffscreenPlatform::applyInterfaceFont()
{
#if defined(Q_OS_MACOS)
    QFont font(QStringLiteral(".AppleSystemUIFont"));
    font.setPointSizeF(13);
    QApplication::setFont(font);
#elif defined(Q_OS_WIN)
    QFont font(QStringLiteral("Segoe UI"));
    font.setPointSizeF(9);
    QApplication::setFont(font);
#endif
}

void OffscreenPlatform::resizeScreen(int width, int height)
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
            qWarning("QmlBrowserHost: the offscreen screen cannot be resized; popups may be misplaced.");
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
