#pragma once

#include <QObject>

// Smoke test for canvas libraries: plain C shared libraries that draw into a
// NativeCanvas item (canvas/include/qmlbrowser_canvas.h).
//
// It loads the sample library, canvas/plasma, from the canvas folder next to
// this executable, the same way QmlRenderer does, and then shows it through
// a real NativeCanvas in a QML scene. Runs headless on the offscreen platform
// with the software scene graph, so it needs no GPU and no browser.
class CanvasSmokeTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();

    void rejectsNamesThatArePaths_data();
    void rejectsNamesThatArePaths();
    void reportsMissingLibrary();
    void loadsAndDraws();
    void drawsInQmlScene();
    void qmlReportsBadLibrary();
};
