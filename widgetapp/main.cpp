#include "GalleryWindow.h"

#include <QmlBrowserHost.h>

#include <QApplication>
#include <QCommandLineParser>
#include <QPalette>
#include <QStyleFactory>

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
    // Started by the browser, the app runs in a tab. Without --channel this
    // is an ordinary desktop app. GPU mode, because the 3D page draws with
    // OpenGL; QMLBROWSERHOST_GRAPHICS=software runs it the other way.
    const bool hosted = QmlBrowserHost::prepare(argc, argv, QmlBrowserHost::Graphics::Gpu);

    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("WidgetApp"));
    QApplication::setApplicationDisplayName(QStringLiteral("Qt Widgets Gallery"));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral(
            "A Qt Widgets application. QmlBrowser starts it for app://widgets tabs; "
            "run it without options to see it in a window of its own."));
    parser.addHelpOption();
    QCommandLineOption pageOption(QStringLiteral("page"),
                                  QStringLiteral("Page to open first: %1.")
                                          .arg(GalleryWindow::pageNames().join(QStringLiteral(", "))),
                                  QStringLiteral("name"));
    parser.addOption(pageOption);
    parser.process(app);

    applyTheme();

    GalleryWindow window(hosted);
    if (parser.isSet(pageOption))
        window.showPage(parser.value(pageOption));

    // app://widgets/<page> opens a page, and a page picked in the app becomes
    // a history entry. app://widgets alone is the first page, so Back to it
    // returns there rather than staying wherever the app was.
    QmlBrowserHost::setNavigateHandler([&window](const QString &path) {
        const QString page = path.section(QLatin1Char('/'), 0, 0).toLower();
        window.showPage(page.isEmpty() ? GalleryWindow::pageNames().constFirst() : page);
    });
    QObject::connect(&window, &GalleryWindow::pageChanged, &QmlBrowserHost::setPath);

    if (!QmlBrowserHost::attach(&window)) {
        window.resize(1180, 780);
        window.show();
    }
    return app.exec();
}
