#pragma once

#include <QObject>

// Test for forwarded pointer input (shared/InputEvents.h).
//
// Sends trackpad events the way the macOS platform plugin makes them to a
// Qt Quick scene twice: once directly, as a page in a real window would get
// them, and once through the message the browser sends and the event the
// renderer rebuilds from it, read in one batch as from the pipe. The two
// scenes must end up the same. Then the swipes between pages: what the page
// leaves alone becomes Back or Forward. Runs headless on the offscreen
// platform.
class InputEventsTest : public QObject
{
    Q_OBJECT

private slots:
    void trackpadScrollMatchesPlatform_data();
    void trackpadScrollMatchesPlatform();
    void mouseWheelStaysMouseWheel();
    void pinchMatchesPlatform();
    void gesturesSurviveForwarding_data();
    void gesturesSurviveForwarding();
    void swipeNavigates_data();
    void swipeNavigates();
    void swipeThePageTakesStaysInPage();
    void scrollingDoesNotNavigate_data();
    void scrollingDoesNotNavigate();
    void threeFingerSwipeNavigates_data();
    void threeFingerSwipeNavigates();
    void clockKeepsSpacing();
    void clockReanchors();
};
