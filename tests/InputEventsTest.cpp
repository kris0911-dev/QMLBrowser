#include "InputEventsTest.h"

#include "InputEvents.h"

#include <QGuiApplication>
#include <QNativeGestureEvent>
#include <QPointingDevice>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QWheelEvent>
#include <QtTest>

#include <memory>
#include <vector>

namespace {

using Events = std::vector<std::unique_ptr<QInputEvent>>;

const QPointF kCenter(100, 100);

// What the macOS platform plugin reports a trackpad as.
const QPointingDevice *platformTrackpad()
{
    static const QPointingDevice *const device = new QPointingDevice(
            QStringLiteral("trackpad or magic mouse"), 0x100, QInputDevice::DeviceType::TouchPad,
            QPointingDevice::PointerType::Finger,
            QInputDevice::Capability::Position | QInputDevice::Capability::Scroll
                    | QInputDevice::Capability::PixelScroll,
            5, 0);
    return device;
}

struct Scene
{
    std::unique_ptr<QQuickWindow> window;
    std::unique_ptr<QQuickItem> root;
};

Scene createScene(QQmlEngine &engine, const QByteArray &qml)
{
    Scene scene;
    QQmlComponent component(&engine);
    component.setData(qml, QUrl(QStringLiteral("qrc:/input.qml")));
    scene.root.reset(qobject_cast<QQuickItem *>(component.create()));
    if (!scene.root) {
        qWarning("%s", qPrintable(component.errorString()));
        return scene;
    }
    scene.window = std::make_unique<QQuickWindow>();
    scene.window->resize(200, 200);
    scene.root->setParentItem(scene.window->contentItem());
    scene.window->show();
    return scene;
}

// A two-finger scroll as macOS reports it: pixel deltas, angle deltas at two
// per pixel, scroll phases, synthesized by the system, 8 ms apart. The
// fingers lift and the system's momentum carries on before the end. With
// natural scrolling ("inverted") the deltas follow the fingers.
Events trackpadScroll(QPoint step, int updates = 10, bool inverted = true)
{
    Events events;
    quint64 time = 5000;
    const auto add = [&](Qt::ScrollPhase phase, QPoint delta) {
        auto event = std::make_unique<QWheelEvent>(
                kCenter, kCenter, delta, delta * 2, Qt::NoButton, Qt::NoModifier, phase,
                inverted, Qt::MouseEventSynthesizedBySystem, platformTrackpad());
        event->setTimestamp(time);
        time += 8;
        events.push_back(std::move(event));
    };
    add(Qt::ScrollBegin, QPoint());
    for (int i = 0; i < updates; ++i)
        add(Qt::ScrollUpdate, step);
    for (int i = 0; i < 5; ++i)
        add(Qt::ScrollMomentum, step / 2);
    add(Qt::ScrollEnd, QPoint());
    return events;
}

// Notches of a mouse wheel: no phases, no pixels, not synthesized.
Events mouseWheel(QPoint step, int notches)
{
    Events events;
    quint64 time = 5000;
    for (int i = 0; i < notches; ++i) {
        auto event = std::make_unique<QWheelEvent>(kCenter, kCenter, QPoint(), step,
                                                   Qt::NoButton, Qt::NoModifier,
                                                   Qt::NoScrollPhase, false);
        event->setTimestamp(time);
        time += 40;
        events.push_back(std::move(event));
    }
    return events;
}

// A pinch out with a slight turn, as macOS reports it.
Events trackpadPinch()
{
    Events events;
    quint64 time = 9000;
    const auto add = [&](Qt::NativeGestureType type, qreal value) {
        auto event = std::make_unique<QNativeGestureEvent>(type, platformTrackpad(), 2, kCenter,
                                                           kCenter, kCenter, value, QPointF());
        event->setTimestamp(time);
        time += 8;
        events.push_back(std::move(event));
    };
    add(Qt::BeginNativeGesture, 0);
    for (int i = 0; i < 5; ++i) {
        add(Qt::ZoomNativeGesture, 0.1);
        add(Qt::RotateNativeGesture, 2);
    }
    add(Qt::EndNativeGesture, 0);
    return events;
}

// A three-finger swipe, which macOS sends as one event when Swipe between
// pages is set to three fingers. Qt gives the direction as an angle.
Events trackpadSwipe(qreal angle)
{
    Events events;
    auto event = std::make_unique<QNativeGestureEvent>(Qt::SwipeNativeGesture, platformTrackpad(),
                                                       3, kCenter, kCenter, kCenter, angle,
                                                       QPointF());
    event->setTimestamp(9000);
    events.push_back(std::move(event));
    return events;
}

void sendDirectly(QQuickWindow *window, const Events &events)
{
    for (const auto &event : events)
        QCoreApplication::sendEvent(window, event.get());
}

// Browser to message, message to renderer: every message is read before any
// is delivered, the way a busy renderer drains the pipe. Returns what the
// renderer tells the browser about swipes the page left alone.
QList<QJsonObject> sendForwarded(QQuickWindow *window, const Events &events)
{
    QList<QJsonObject> messages;
    for (const auto &event : events) {
        if (event->type() == QEvent::Wheel)
            messages.append(InputEvents::wheelMessage(static_cast<QWheelEvent *>(event.get())));
        else
            messages.append(InputEvents::gestureMessage(
                    static_cast<QNativeGestureEvent *>(event.get())));
    }

    InputClock clock;
    SwipeNavigation navigation;
    QList<QJsonObject> swipes;
    for (const QJsonObject &message : messages) {
        const quint64 time = clock.time(message);
        std::unique_ptr<QInputEvent> event;
        if (message.value(QStringLiteral("kind")).toString() == QLatin1String("wheel"))
            event = InputEvents::wheelEvent(message, time);
        else
            event = InputEvents::gestureEvent(message, time);
        QCoreApplication::sendEvent(window, event.get());
        const QJsonObject swipe = navigation.observe(message, event->isAccepted());
        if (!swipe.isEmpty())
            swipes.append(swipe);
    }
    return swipes;
}

// A page as most are: a column taller than the window, and something to click.
const QByteArray kPage = "import QtQuick\n"
                         "Flickable {\n"
                         "    width: 200; height: 200\n"
                         "    contentWidth: 200; contentHeight: 5000\n"
                         "    MouseArea { width: 200; height: 5000 }\n"
                         "}\n";

QList<QJsonObject> swipeOverPage(const Events &events)
{
    QQmlEngine engine;
    Scene page = createScene(engine, kPage);
    if (!page.root || !QTest::qWaitForWindowExposed(page.window.get()))
        return { { { QStringLiteral("error"), QStringLiteral("no page") } } };
    return sendForwarded(page.window.get(), events);
}

} // namespace

void InputEventsTest::trackpadScrollMatchesPlatform_data()
{
    QTest::addColumn<QPoint>("step");
    QTest::addColumn<QSize>("content");
    QTest::addColumn<QByteArray>("property");

    // Natural scrolling: fingers up or left move the content up or left.
    QTest::newRow("down") << QPoint(0, -12) << QSize(200, 5000) << QByteArray("contentY");
    QTest::newRow("sideways") << QPoint(-12, 0) << QSize(5000, 200) << QByteArray("contentX");
}

void InputEventsTest::trackpadScrollMatchesPlatform()
{
    QFETCH(QPoint, step);
    QFETCH(QSize, content);
    QFETCH(QByteArray, property);

    const QByteArray qml = "import QtQuick\n"
                           "Flickable {\n"
                           "    width: 200; height: 200\n"
                           "    contentWidth: " + QByteArray::number(content.width())
            + "; contentHeight: " + QByteArray::number(content.height()) + "\n"
            "}\n";
    QQmlEngine engine;
    Scene direct = createScene(engine, qml);
    Scene forwarded = createScene(engine, qml);
    QVERIFY(direct.root && forwarded.root);
    QVERIFY(QTest::qWaitForWindowExposed(direct.window.get()));
    QVERIFY(QTest::qWaitForWindowExposed(forwarded.window.get()));

    const Events events = trackpadScroll(step);
    sendDirectly(direct.window.get(), events);
    const QList<QJsonObject> swipes = sendForwarded(forwarded.window.get(), events);

    // At least the 120 pixels the fingers moved: Flickable dragged the
    // content, as it does for a trackpad, rather than flicking per notch.
    const qreal expected = direct.root->property(property).toReal();
    QVERIFY(expected >= 120);
    QCOMPARE(forwarded.root->property(property).toReal(), expected);
    QVERIFY(swipes.isEmpty());
}

void InputEventsTest::mouseWheelStaysMouseWheel()
{
    QWheelEvent wheel(kCenter, kCenter, QPoint(), QPoint(0, -120), Qt::NoButton, Qt::NoModifier,
                      Qt::NoScrollPhase, false);
    wheel.setTimestamp(42);
    const QJsonObject message = InputEvents::wheelMessage(&wheel);
    const auto rebuilt = InputEvents::wheelEvent(message, 7);

    QCOMPARE(rebuilt->pointingDevice(), QPointingDevice::primaryPointingDevice());
    QCOMPARE(rebuilt->source(), Qt::MouseEventNotSynthesized);
    QCOMPARE(rebuilt->phase(), Qt::NoScrollPhase);
    QCOMPARE(rebuilt->angleDelta(), QPoint(0, -120));
    QCOMPARE(rebuilt->pixelDelta(), QPoint());
    QCOMPARE(rebuilt->timestamp(), quint64(7));
}

void InputEventsTest::pinchMatchesPlatform()
{
    const QByteArray qml = "import QtQuick\n"
                           "Item {\n"
                           "    width: 200; height: 200\n"
                           "    Rectangle {\n"
                           "        objectName: \"box\"\n"
                           "        x: 50; y: 50; width: 100; height: 100\n"
                           "        PinchHandler { }\n"
                           "    }\n"
                           "}\n";
    QQmlEngine engine;
    Scene direct = createScene(engine, qml);
    Scene forwarded = createScene(engine, qml);
    QVERIFY(direct.root && forwarded.root);
    QVERIFY(QTest::qWaitForWindowExposed(direct.window.get()));
    QVERIFY(QTest::qWaitForWindowExposed(forwarded.window.get()));

    const Events events = trackpadPinch();
    sendDirectly(direct.window.get(), events);
    sendForwarded(forwarded.window.get(), events);

    auto *directBox = direct.root->findChild<QQuickItem *>(QStringLiteral("box"));
    auto *forwardedBox = forwarded.root->findChild<QQuickItem *>(QStringLiteral("box"));
    QVERIFY(directBox && forwardedBox);
    QVERIFY(directBox->scale() > 1.4);
    QVERIFY(directBox->rotation() != 0);
    QCOMPARE(forwardedBox->scale(), directBox->scale());
    QCOMPARE(forwardedBox->rotation(), directBox->rotation());
}

void InputEventsTest::gesturesSurviveForwarding_data()
{
    QTest::addColumn<Qt::NativeGestureType>("type");
    QTest::addColumn<qreal>("value");
    QTest::addColumn<QPointF>("delta");
    QTest::addColumn<int>("fingers");

    QTest::newRow("begin") << Qt::BeginNativeGesture << 0.0 << QPointF() << 2;
    QTest::newRow("end") << Qt::EndNativeGesture << 0.0 << QPointF() << 2;
    QTest::newRow("pinch") << Qt::ZoomNativeGesture << 0.25 << QPointF() << 2;
    QTest::newRow("smart zoom") << Qt::SmartZoomNativeGesture << 1.0 << QPointF() << 2;
    QTest::newRow("rotate") << Qt::RotateNativeGesture << -15.0 << QPointF() << 2;
    QTest::newRow("swipe") << Qt::SwipeNativeGesture << 180.0 << QPointF() << 3;
    QTest::newRow("pan") << Qt::PanNativeGesture << 0.0 << QPointF(3, -4) << 2;
}

void InputEventsTest::gesturesSurviveForwarding()
{
    QFETCH(Qt::NativeGestureType, type);
    QFETCH(qreal, value);
    QFETCH(QPointF, delta);
    QFETCH(int, fingers);

    QNativeGestureEvent gesture(type, platformTrackpad(), fingers, kCenter, kCenter, kCenter,
                                value, delta);
    gesture.setTimestamp(42);
    const auto rebuilt = InputEvents::gestureEvent(InputEvents::gestureMessage(&gesture), 7);

    QCOMPARE(rebuilt->gestureType(), type);
    QCOMPARE(rebuilt->value(), value);
    QCOMPARE(rebuilt->delta(), delta);
    QCOMPARE(rebuilt->fingerCount(), fingers);
    QCOMPARE(rebuilt->position(), kCenter);
    QCOMPARE(rebuilt->device()->type(), QInputDevice::DeviceType::TouchPad);
    QCOMPARE(rebuilt->timestamp(), quint64(7));
}

void InputEventsTest::swipeNavigates_data()
{
    QTest::addColumn<QPoint>("step");
    QTest::addColumn<bool>("inverted");
    QTest::addColumn<QString>("direction");

    // Natural scrolling: the deltas follow the fingers. Classic: they don't.
    QTest::newRow("fingers right, natural") << QPoint(12, 0) << true << QStringLiteral("back");
    QTest::newRow("fingers left, natural") << QPoint(-12, 0) << true << QStringLiteral("forward");
    QTest::newRow("fingers right, classic") << QPoint(-12, 0) << false << QStringLiteral("back");
    QTest::newRow("fingers left, classic") << QPoint(12, 0) << false << QStringLiteral("forward");
}

void InputEventsTest::swipeNavigates()
{
    QFETCH(QPoint, step);
    QFETCH(bool, inverted);
    QFETCH(QString, direction);

    // The page scrolls only up and down, so a sideways swipe is not its.
    const QList<QJsonObject> swipes = swipeOverPage(trackpadScroll(step, 10, inverted));
    QCOMPARE(swipes.size(), 1);
    QCOMPARE(swipes.first().value(QStringLiteral("direction")).toString(), direction);
    QCOMPARE(swipes.first().value(QStringLiteral("fingers")).toInt(), 2);
}

void InputEventsTest::swipeThePageTakesStaysInPage()
{
    const QByteArray qml = "import QtQuick\n"
                           "Flickable {\n"
                           "    width: 200; height: 200\n"
                           "    contentWidth: 5000; contentHeight: 200\n"
                           "}\n";
    QQmlEngine engine;
    Scene page = createScene(engine, qml);
    QVERIFY(page.root);
    QVERIFY(QTest::qWaitForWindowExposed(page.window.get()));

    const QList<QJsonObject> swipes =
            sendForwarded(page.window.get(), trackpadScroll(QPoint(-12, 0)));
    QVERIFY(page.root->property("contentX").toReal() > 0);
    QVERIFY(swipes.isEmpty());
}

void InputEventsTest::scrollingDoesNotNavigate_data()
{
    QTest::addColumn<QPoint>("step");
    QTest::addColumn<int>("updates");
    QTest::addColumn<bool>("trackpad");

    QTest::newRow("up and down") << QPoint(0, -12) << 10 << true;
    QTest::newRow("short sideways") << QPoint(12, 0) << 4 << true;
    QTest::newRow("diagonal") << QPoint(12, 9) << 10 << true;
    QTest::newRow("mouse wheel sideways") << QPoint(120, 0) << 10 << false;
}

void InputEventsTest::scrollingDoesNotNavigate()
{
    QFETCH(QPoint, step);
    QFETCH(int, updates);
    QFETCH(bool, trackpad);

    const Events events = trackpad ? trackpadScroll(step, updates) : mouseWheel(step, updates);
    QVERIFY(swipeOverPage(events).isEmpty());
}

void InputEventsTest::threeFingerSwipeNavigates_data()
{
    QTest::addColumn<qreal>("angle");
    QTest::addColumn<QString>("direction");

    QTest::newRow("right") << 180.0 << QStringLiteral("back");
    QTest::newRow("left") << 0.0 << QStringLiteral("forward");
    QTest::newRow("up") << 90.0 << QString();
    QTest::newRow("down") << 270.0 << QString();
}

void InputEventsTest::threeFingerSwipeNavigates()
{
    QFETCH(qreal, angle);
    QFETCH(QString, direction);

    const QList<QJsonObject> swipes = swipeOverPage(trackpadSwipe(angle));
    if (direction.isEmpty()) {
        QVERIFY(swipes.isEmpty());
        return;
    }
    QCOMPARE(swipes.size(), 1);
    QCOMPARE(swipes.first().value(QStringLiteral("direction")).toString(), direction);
    QCOMPARE(swipes.first().value(QStringLiteral("fingers")).toInt(), 3);
}

void InputEventsTest::clockKeepsSpacing()
{
    InputClock clock;
    const quint64 first = clock.time({ { QStringLiteral("t"), 1000 } });
    const quint64 second = clock.time({ { QStringLiteral("t"), 1008 } });
    const quint64 third = clock.time({ { QStringLiteral("t"), 1016 } });
    QVERIFY(first > 0);
    QCOMPARE(second - first, quint64(8));
    QCOMPARE(third - second, quint64(8));

    // A message without a time gets this process's clock, never 0.
    QVERIFY(clock.time({}) > 0);
}

void InputEventsTest::clockReanchors()
{
    // Windows' message clock wraps every 49.7 days.
    InputClock clock;
    const quint64 before = clock.time({ { QStringLiteral("t"), 4294967000.0 } });
    const quint64 after = clock.time({ { QStringLiteral("t"), 300 } });
    QVERIFY(after >= before);
    QCOMPARE(clock.time({ { QStringLiteral("t"), 316 } }) - after, quint64(16));
}

int main(int argc, char *argv[])
{
    // Headless unless the caller chose a platform, and no GPU needed.
    if (!qEnvironmentVariableIsSet("QT_QPA_PLATFORM"))
        qputenv("QT_QPA_PLATFORM", "offscreen");
    QQuickWindow::setGraphicsApi(QSGRendererInterface::Software);

    QGuiApplication app(argc, argv);
    InputEventsTest test;
    return QTest::qExec(&test, argc, argv);
}
