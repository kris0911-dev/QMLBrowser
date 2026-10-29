#include "CanvasSmokeTest.h"

#include "CanvasLibrary.h"
#include "NativeCanvas.h"

#include <QGuiApplication>
#include <QImage>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QRegularExpression>
#include <QSet>
#include <QtTest>

#include <memory>

namespace {

int distinctColours(const QImage &image)
{
    QSet<QRgb> colours;
    for (int y = 0; y < image.height(); ++y)
        for (int x = 0; x < image.width(); ++x)
            colours.insert(image.pixel(x, y));
    return int(colours.size());
}

std::unique_ptr<QQuickItem> createScene(QQmlEngine &engine, const QByteArray &library)
{
    QQmlComponent component(&engine);
    component.setData("import QtQuick\n"
                      "import QmlBrowser.Native\n"
                      "Rectangle {\n"
                      "    width: 160; height: 120; color: \"black\"\n"
                      "    NativeCanvas {\n"
                      "        objectName: \"canvas\"\n"
                      "        anchors.fill: parent\n"
                      "        library: \"" + library + "\"\n"
                      "        time: 1.5\n"
                      "    }\n"
                      "}\n",
                      QUrl(QStringLiteral("qrc:/smoke.qml")));
    std::unique_ptr<QQuickItem> root(qobject_cast<QQuickItem *>(component.create()));
    if (!root)
        qWarning("%s", qPrintable(component.errorString()));
    return root;
}

} // namespace

void CanvasSmokeTest::initTestCase()
{
    qmlRegisterType<NativeCanvas>("QmlBrowser.Native", 1, 0, "NativeCanvas");
    qInfo("Canvas libraries load from %s", qPrintable(CanvasLibrary::directory()));
}

void CanvasSmokeTest::rejectsNamesThatArePaths_data()
{
    QTest::addColumn<QString>("name");
    QTest::newRow("empty") << QString();
    QTest::newRow("parent folder") << QStringLiteral("../plasma");
    QTest::newRow("subfolder") << QStringLiteral("canvas/plasma");
    QTest::newRow("backslash") << QStringLiteral("..\\plasma");
    QTest::newRow("absolute Windows") << QStringLiteral("C:\\Windows\\System32\\kernel32");
    QTest::newRow("absolute Unix") << QStringLiteral("/usr/lib/libc");
    QTest::newRow("suffix") << QStringLiteral("plasma.dll");
    QTest::newRow("space") << QStringLiteral("plas ma");
}

void CanvasSmokeTest::rejectsNamesThatArePaths()
{
    QFETCH(QString, name);
    QVERIFY(!CanvasLibrary::isValidName(name));
    CanvasLibrary library;
    QVERIFY(!library.load(name));
    QVERIFY(!library.isLoaded());
    QVERIFY(library.errorString().contains(QLatin1String("not a canvas library name")));
}

void CanvasSmokeTest::reportsMissingLibrary()
{
    CanvasLibrary library;
    QVERIFY(!library.load(QStringLiteral("no-such-canvas")));
    QVERIFY(!library.isLoaded());
    QVERIFY(library.errorString().contains(QLatin1String("Could not load")));
}

void CanvasSmokeTest::loadsAndDraws()
{
    CanvasLibrary library;
    QVERIFY2(library.load(QStringLiteral("plasma")), qPrintable(library.errorString()));

    QImage first(64, 48, QImage::Format_ARGB32_Premultiplied);
    first.fill(Qt::transparent);
    library.draw(first, 0.0);

    // Every pixel written, opaque, and an actual picture rather than a fill.
    for (int y = 0; y < first.height(); ++y)
        for (int x = 0; x < first.width(); ++x)
            QCOMPARE(qAlpha(first.pixel(x, y)), 255);
    QVERIFY(distinctColours(first) > 16);

    // `time` reaches the library.
    QImage later(first.size(), first.format());
    library.draw(later, 2.0);
    QVERIFY(later != first);

    // A stride wider than the row: the library must honour it and leave the
    // padding alone.
    QImage wide(100, 20, QImage::Format_ARGB32_Premultiplied);
    wide.fill(Qt::transparent);
    QImage view(wide.bits(), 64, 20, wide.bytesPerLine(), QImage::Format_ARGB32_Premultiplied);
    library.draw(view, 0.5);
    QCOMPARE(qAlpha(wide.pixel(63, 19)), 255);
    QCOMPARE(qAlpha(wide.pixel(64, 19)), 0);
}

void CanvasSmokeTest::drawsInQmlScene()
{
    QQmlEngine engine;
    QQuickWindow window;
    window.resize(160, 120);

    std::unique_ptr<QQuickItem> root = createScene(engine, "plasma");
    QVERIFY(root);
    root->setParentItem(window.contentItem());

    auto *canvas = root->findChild<NativeCanvas *>(QStringLiteral("canvas"));
    QVERIFY(canvas);
    QVERIFY2(canvas->status() == NativeCanvas::Ready, qPrintable(canvas->errorString()));

    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));

    const QImage frame = window.grabWindow();
    QVERIFY(!frame.isNull());
    // The background is plain black; the plasma covers it with colour.
    QVERIFY(distinctColours(frame) > 16);
    QVERIFY(frame.pixelColor(frame.width() / 2, frame.height() / 2) != QColor(Qt::black));
    QVERIFY(canvas->renderTime() > 0);
}

void CanvasSmokeTest::qmlReportsBadLibrary()
{
    QTest::ignoreMessage(QtWarningMsg,
                         QRegularExpression(QStringLiteral("NativeCanvas: .*not a canvas library")));
    QQmlEngine engine;
    std::unique_ptr<QQuickItem> root = createScene(engine, "../plasma");
    QVERIFY(root);
    auto *canvas = root->findChild<NativeCanvas *>(QStringLiteral("canvas"));
    QVERIFY(canvas);
    QCOMPARE(canvas->status(), NativeCanvas::Error);
    QVERIFY(!canvas->errorString().isEmpty());
}

int main(int argc, char *argv[])
{
    // Headless unless the caller chose a platform, and no GPU needed.
    if (!qEnvironmentVariableIsSet("QT_QPA_PLATFORM"))
        qputenv("QT_QPA_PLATFORM", "offscreen");
    QQuickWindow::setGraphicsApi(QSGRendererInterface::Software);

    QGuiApplication app(argc, argv);
    CanvasSmokeTest test;
    return QTest::qExec(&test, argc, argv);
}
