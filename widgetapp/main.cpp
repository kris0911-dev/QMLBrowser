#include "BrowserBridge.h"
#include "GalleryWindow.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QFont>
#include <QPalette>
#include <QStyleFactory>

#include <cstring>

namespace {

// Dark Fusion, close to the browser chrome, so the tab does not flash white.
void applyTheme()
{
    QApplication::setStyle(QStyleFactory::create(QStringLiteral("Fusion")));

    const QColor window(QStringLiteral("#141821"));
    const QColor base(QStringLiteral("#0e1017"));
    const QColor text(QStringLiteral("#e6ecf9"));
    const QColor muted(QStringLiteral("#5a6378"));

    QPalette palette;
    palette.setColor(QPalette::Window, window);
    palette.setColor(QPalette::WindowText, QColor(QStringLiteral("#dbe2f0")));
    palette.setColor(QPalette::Base, base);
    palette.setColor(QPalette::AlternateBase, QColor(QStringLiteral("#161a24")));
    palette.setColor(QPalette::Text, text);
    palette.setColor(QPalette::PlaceholderText, QColor(QStringLiteral("#6b7590")));
    palette.setColor(QPalette::Button, QColor(QStringLiteral("#1d2230")));
    palette.setColor(QPalette::ButtonText, QColor(QStringLiteral("#dbe2f0")));
    palette.setColor(QPalette::BrightText, QColor(QStringLiteral("#ff8095")));
    palette.setColor(QPalette::Highlight, QColor(QStringLiteral("#3f6bd8")));
    palette.setColor(QPalette::HighlightedText, Qt::white);
    palette.setColor(QPalette::Link, QColor(QStringLiteral("#8ab4ff")));
    palette.setColor(QPalette::ToolTipBase, QColor(QStringLiteral("#1d2433")));
    palette.setColor(QPalette::ToolTipText, text);
    palette.setColor(QPalette::Light, QColor(QStringLiteral("#2a3142")));
    palette.setColor(QPalette::Midlight, QColor(QStringLiteral("#232939")));
    palette.setColor(QPalette::Mid, QColor(QStringLiteral("#1b2030")));
    palette.setColor(QPalette::Dark, QColor(QStringLiteral("#0b0d13")));
    palette.setColor(QPalette::Shadow, QColor(QStringLiteral("#05060a")));
    for (const QPalette::ColorRole role : { QPalette::WindowText, QPalette::Text,
                                            QPalette::ButtonText }) {
        palette.setColor(QPalette::Disabled, role, muted);
    }
    palette.setColor(QPalette::Disabled, QPalette::Highlight, QColor(QStringLiteral("#2b3247")));
    QApplication::setPalette(palette);
}

} // namespace

int main(int argc, char *argv[])
{
    // The platform has to be chosen before QApplication exists, so --channel
    // is looked for by hand. Without it this is an ordinary desktop app.
    bool hosted = false;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--channel") == 0)
            hosted = true;
    }
    if (hosted)
        BrowserBridge::selectOffscreenPlatform();

    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("WidgetApp"));
    QApplication::setApplicationDisplayName(QStringLiteral("Qt Widgets Gallery"));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral(
            "A Qt Widgets application. QmlBrowser starts it for app://widgets tabs; "
            "run it without options to see it in a window of its own."));
    parser.addHelpOption();
    QCommandLineOption channelOption(QStringLiteral("channel"),
                                     QStringLiteral("Name of the browser process's local socket."),
                                     QStringLiteral("name"));
    QCommandLineOption tabOption(QStringLiteral("tab"), QStringLiteral("Tab identifier."),
                                 QStringLiteral("id"));
    QCommandLineOption pageOption(QStringLiteral("page"),
                                  QStringLiteral("Page to open first: %1.")
                                          .arg(GalleryWindow::pageNames().join(QStringLiteral(", "))),
                                  QStringLiteral("name"));
    parser.addOption(channelOption);
    parser.addOption(tabOption);
    parser.addOption(pageOption);
    parser.process(app);

    if (hosted) {
        BrowserBridge::resizeScreen(1280, 800);
        BrowserBridge::registerFontCollections();
        // The offscreen platform has no theme, so it would otherwise fall back
        // to a generic font. Use what a native window here would use.
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
    applyTheme();

    GalleryWindow window(hosted);
    if (parser.isSet(pageOption))
        window.showPage(parser.value(pageOption));

    if (!hosted) {
        window.resize(1180, 780);
        window.show();
        return app.exec();
    }

    BrowserBridge bridge(&window);
    if (!bridge.connectToBrowser(parser.value(channelOption), parser.value(tabOption).toInt()))
        return 1;
    return app.exec();
}
