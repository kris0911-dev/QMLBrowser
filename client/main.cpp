#include "BrowserHistory.h"
#include "BrowserTab.h"
#include "TabManager.h"
#include "TabViewport.h"
#include "WindowFrame.h"

#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QWindow>
#include <QQmlContext>
#include <QtQml/qqml.h>

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    QGuiApplication::setApplicationName(QStringLiteral("QmlBrowser"));
    QGuiApplication::setApplicationVersion(QStringLiteral("1.0"));
    QGuiApplication::setOrganizationName(QStringLiteral("QmlBrowser"));

    qmlRegisterType<TabManager>("QmlBrowser", 1, 0, "TabManager");
    qmlRegisterType<TabViewport>("QmlBrowser", 1, 0, "TabViewport");
    // A tab created from QML would have no renderer process and no socket.
    // TabManager is the only place that starts QmlRenderer.
    qmlRegisterUncreatableType<BrowserTab>("QmlBrowser", 1, 0, "BrowserTab",
                                           QStringLiteral("Tabs are created by TabManager."));
    qmlRegisterUncreatableType<BrowserHistory>("QmlBrowser", 1, 0, "BrowserHistory",
                                               QStringLiteral("Each tab owns its history."));

    // First positional argument is the page to open, otherwise the local server.
    QString startUrl = QStringLiteral("http://127.0.0.1:8080/index.qml");
    const QStringList arguments = QGuiApplication::arguments();
    if (arguments.size() > 1 && !arguments.at(1).startsWith(QLatin1Char('-')))
        startUrl = arguments.at(1);

    // Installed first so the window is born without a classical title bar.
    WindowFrame::instance()->install();

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("initialUrl"), startUrl);
    engine.load(QUrl(QStringLiteral("qrc:/ui/Browser.qml")));

    if (engine.rootObjects().isEmpty())
        return 1;

    if (auto *window = qobject_cast<QWindow *>(engine.rootObjects().constFirst()))
        WindowFrame::instance()->adopt(window->winId());

    return app.exec();
}
