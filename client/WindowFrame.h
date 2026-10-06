#pragma once

#include <QAbstractNativeEventFilter>
#include <QByteArray>
#include <QRect>
#include <QtGui/qwindowdefs.h>

// Turns the browser window into a Chrome-style frame.
//
// The stock title bar is removed, but the thick frame stays, so Windows keeps
// resizing, snapping, the drop shadow and the rounded corners. The tab strip
// occupies the reclaimed caption area and hosts the window buttons itself.
//
// Qt.FramelessWindowHint is deliberately not used: it also throws away the
// resize borders, and those have to live outside the client area so the tab
// strip can occupy the caption without giving up snapping and the shadow.
class WindowFrame : public QAbstractNativeEventFilter
{
public:
    static WindowFrame *instance();

    void install();
    // The browser window, once it exists. Safe to call more than once.
    void adopt(WId window);
    // Recomputes the non-client area after a style change such as full screen.
    void syncFrame();

    void setEnabled(bool on);
    // Empty rectangle disables dragging. Coordinates are physical client pixels.
    void setDragRegion(const QRect &physicalClientRect);

    bool nativeEventFilter(const QByteArray &eventType, void *message,
                           qintptr *result) override;

private:
    void applyAppearance();
    bool handleNcCalcSize(void *message, qintptr *result);
    bool handleNcHitTest(void *message, qintptr *result);

    void *m_hwnd = nullptr;
    QRect m_drag;
    bool m_enabled = true;
    bool m_installed = false;
};
