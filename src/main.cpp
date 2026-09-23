#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QtWebEngineQuick/QtWebEngineQuick>

int main(int argc, char *argv[])
{
    QtWebEngineQuick::initialize();

    QGuiApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("QMLBrowser"));
    app.setOrganizationName(QStringLiteral("QMLBrowser"));

    QQuickStyle::setStyle(QStringLiteral("Fusion"));

    QString startupUrl = QStringLiteral("https://example.com");
    const QStringList args = app.arguments();
    if (args.size() > 1 && !args.at(1).trimmed().isEmpty())
        startupUrl = args.at(1).trimmed();

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("startupUrl"), startupUrl);

    const QUrl url(QStringLiteral("qrc:/qml/Main.qml"));
    QObject::connect(
        &engine, &QQmlApplicationEngine::objectCreated, &app,
        [url](QObject *obj, const QUrl &objUrl) {
            if (!obj && url == objUrl)
                QCoreApplication::exit(-1);
        },
        Qt::QueuedConnection);

    engine.load(url);
    if (engine.rootObjects().isEmpty())
        return -1;

    return app.exec();
}
