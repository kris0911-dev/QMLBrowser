#include "WindowFrame.h"

#include <QGuiApplication>

#ifdef Q_OS_WIN
#  define WIN32_LEAN_AND_MEAN
#  define NOMINMAX
#  include <windows.h>
#  include <windowsx.h>
#  include <dwmapi.h>
#endif

namespace {

#ifdef Q_OS_WIN

// Present in the Windows 10 SDK; some toolchains hide it behind a higher WINVER.
#ifndef SM_CXPADDEDFRAME
#  define SM_CXPADDEDFRAME 92
#endif

// Win11 attributes. Defined here so an older SDK still compiles.
constexpr DWORD kUseImmersiveDarkMode = 20;
constexpr DWORD kWindowCornerPreference = 33;
constexpr DWORD kBorderColor = 34;
constexpr DWORD kCaptionColor = 35;
constexpr DWORD kCornerRound = 2;

int frameThickness(HWND hwnd, int metric)
{
    const UINT dpi = ::GetDpiForWindow(hwnd);
    return ::GetSystemMetricsForDpi(metric, dpi) + ::GetSystemMetricsForDpi(SM_CXPADDEDFRAME, dpi);
}

#endif

} // namespace

WindowFrame *WindowFrame::instance()
{
    static WindowFrame frame;
    return &frame;
}

void WindowFrame::install()
{
    if (m_installed)
        return;
    m_installed = true;
    QGuiApplication::instance()->installNativeEventFilter(this);
}

void WindowFrame::adopt(WId window)
{
#ifdef Q_OS_WIN
    m_hwnd = reinterpret_cast<HWND>(window);
    applyAppearance();
    syncFrame();
#else
    Q_UNUSED(window)
#endif
}

void WindowFrame::syncFrame()
{
#ifdef Q_OS_WIN
    if (!m_hwnd)
        return;
    ::SetWindowPos(static_cast<HWND>(m_hwnd), nullptr, 0, 0, 0, 0,
                   SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
#endif
}

void WindowFrame::setEnabled(bool on)
{
    m_enabled = on;
}

void WindowFrame::setDragRegion(const QRect &physicalClientRect)
{
    m_drag = physicalClientRect;
}

void WindowFrame::applyAppearance()
{
#ifdef Q_OS_WIN
    auto *hwnd = static_cast<HWND>(m_hwnd);
    if (!hwnd)
        return;

    // Matches the tab strip, so nothing the desktop window manager still paints
    // (the border, a sliver of caption) reads as a light classic frame.
    const COLORREF chrome = RGB(0x0e, 0x11, 0x19);
    const BOOL dark = TRUE;
    ::DwmSetWindowAttribute(hwnd, kUseImmersiveDarkMode, &dark, sizeof(dark));
    ::DwmSetWindowAttribute(hwnd, kWindowCornerPreference, &kCornerRound, sizeof(kCornerRound));
    ::DwmSetWindowAttribute(hwnd, kBorderColor, &chrome, sizeof(chrome));
    ::DwmSetWindowAttribute(hwnd, kCaptionColor, &chrome, sizeof(chrome));
#endif
}

bool WindowFrame::nativeEventFilter(const QByteArray &eventType, void *message, qintptr *result)
{
#ifdef Q_OS_WIN
    if (eventType != "windows_generic_MSG" || !m_enabled)
        return false;

    auto *msg = static_cast<MSG *>(message);

    // Adopt the browser window the first time Windows asks about its frame.
    // Renderers live in other processes, so the only captioned top-level
    // window in this one is the browser.
    if (!m_hwnd) {
        if (::GetAncestor(msg->hwnd, GA_ROOT) != msg->hwnd)
            return false;
        if (!(::GetWindowLong(msg->hwnd, GWL_STYLE) & WS_CAPTION))
            return false;
        wchar_t cls[32] = {};
        ::GetClassName(msg->hwnd, cls, 32);
        if (wcsncmp(cls, L"Qt", 2) != 0)
            return false;
        m_hwnd = msg->hwnd;
        applyAppearance();
    }

    if (msg->hwnd != static_cast<HWND>(m_hwnd))
        return false;

    switch (msg->message) {
    case WM_ERASEBKGND: {
        // Qt drops this and paints nothing, so a region just uncovered by a
        // tab's child window flashes the default white until the next frame.
        RECT rect = {};
        ::GetClientRect(msg->hwnd, &rect);
        static const HBRUSH brush = ::CreateSolidBrush(RGB(0x0b, 0x0d, 0x13));
        ::FillRect(reinterpret_cast<HDC>(msg->wParam), &rect, brush);
        *result = 1;
        return true;
    }
    case WM_NCCALCSIZE:
        return handleNcCalcSize(msg, result);
    case WM_NCHITTEST:
        return handleNcHitTest(msg, result);
    case WM_NCACTIVATE:
        // -1 keeps DefWindowProc from repainting the stock title bar on focus.
        *result = ::DefWindowProc(msg->hwnd, WM_NCACTIVATE, msg->wParam, -1);
        return true;
    default:
        return false;
    }
#else
    Q_UNUSED(eventType)
    Q_UNUSED(message)
    Q_UNUSED(result)
    return false;
#endif
}

bool WindowFrame::handleNcCalcSize(void *message, qintptr *result)
{
#ifdef Q_OS_WIN
    auto *msg = static_cast<MSG *>(message);
    if (msg->wParam != TRUE)
        return false;

    auto *hwnd = msg->hwnd;
    auto *params = reinterpret_cast<NCCALCSIZE_PARAMS *>(msg->lParam);

    if (::IsZoomed(hwnd)) {
        // A maximised window is laid out bigger than the monitor by one frame,
        // so the client area has to be pulled back onto the work area. Using
        // the work area also keeps it clear of the taskbar.
        MONITORINFO monitor = {};
        monitor.cbSize = sizeof(monitor);
        if (::GetMonitorInfo(::MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &monitor))
            params->rgrc[0] = monitor.rcWork;
    } else {
        // Inset by the resize border only. The caption height is what makes a
        // window look classical, and that part becomes client area for the tabs.
        const int borderX = frameThickness(hwnd, SM_CXFRAME);
        const int borderY = frameThickness(hwnd, SM_CYFRAME);
        params->rgrc[0].left += borderX;
        params->rgrc[0].right -= borderX;
        params->rgrc[0].top += borderY;
        params->rgrc[0].bottom -= borderY;
    }

    *result = 0;
    return true;
#else
    Q_UNUSED(message)
    Q_UNUSED(result)
    return false;
#endif
}

bool WindowFrame::handleNcHitTest(void *message, qintptr *result)
{
#ifdef Q_OS_WIN
    auto *msg = static_cast<MSG *>(message);
    auto *hwnd = msg->hwnd;

    // Classify every point ourselves. Falling through to DefWindowProc makes it
    // treat the old caption band as a resize border, so the pointer stays a
    // size cursor across the top of the window once that band is client area.
    POINT screen = { GET_X_LPARAM(msg->lParam), GET_Y_LPARAM(msg->lParam) };
    RECT window = {};
    ::GetWindowRect(hwnd, &window);

    RECT client = {};
    ::GetClientRect(hwnd, &client);
    ::MapWindowPoints(hwnd, nullptr, reinterpret_cast<POINT *>(&client), 2);

    const bool zoomed = ::IsZoomed(hwnd) != FALSE;
    const bool left = !zoomed && screen.x < client.left && screen.x >= window.left;
    const bool right = !zoomed && screen.x >= client.right && screen.x < window.right;
    const bool top = !zoomed && screen.y < client.top && screen.y >= window.top;
    const bool bottom = !zoomed && screen.y >= client.bottom && screen.y < window.bottom;

    if (left || right || top || bottom) {
        if (top && left)
            *result = HTTOPLEFT;
        else if (top && right)
            *result = HTTOPRIGHT;
        else if (bottom && left)
            *result = HTBOTTOMLEFT;
        else if (bottom && right)
            *result = HTBOTTOMRIGHT;
        else if (left)
            *result = HTLEFT;
        else if (right)
            *result = HTRIGHT;
        else if (top)
            *result = HTTOP;
        else
            *result = HTBOTTOM;
        return true;
    }

    POINT local = screen;
    ::ScreenToClient(hwnd, &local);
    if (!m_drag.isEmpty() && m_drag.contains(local.x, local.y)) {
        // HTCAPTION is what makes the empty tab strip drag the window and
        // maximise it on a double click, the same as a title bar would.
        *result = HTCAPTION;
        return true;
    }

    *result = HTCLIENT;
    return true;
#else
    Q_UNUSED(message)
    Q_UNUSED(result)
    return false;
#endif
}
