#include "clear_ink_dialog.hpp"
#include "resources.h"
#include "settings.hpp"
#include <algorithm>
#include <cstdint>
#include <dwmapi.h>
#include <vector>

namespace paper {
namespace {
class FrostedBackdrop {
    HWND window_ = nullptr;
    HWND owner_;
    BITMAPINFO info_{};
    std::vector<uint32_t> pixels_;
    bool captured_ = false;
    int transparency_;
    unsigned tint_;

    void capture(HWND owner, RECT window, RECT bounds) {
        const int width = bounds.right - bounds.left;
        const int height = bounds.bottom - bounds.top;
        auto &header = info_.bmiHeader;
        header.biSize = sizeof(header);
        header.biWidth = std::max(1, width / 8);
        header.biHeight = -std::max(1, height / 8);
        header.biPlanes = 1;
        header.biBitCount = 32;
        header.biCompression = BI_RGB;
        const int w = header.biWidth, h = -header.biHeight;
        pixels_.resize(size_t(w) * h);
        std::vector<uint32_t> scratch(pixels_.size());

        HDC screen = GetDC(owner);
        if (!screen) return;
        HDC snapshot = CreateCompatibleDC(screen), reduced = CreateCompatibleDC(screen);
        HBITMAP full = CreateCompatibleBitmap(screen, window.right - window.left, window.bottom - window.top);
        void *bits = nullptr;
        HBITMAP small = CreateDIBSection(screen, &info_, DIB_RGB_COLORS, &bits, nullptr, 0);
        if (snapshot && reduced && full && small && bits) {
            HGDIOBJ oldFull = SelectObject(snapshot, full);
            HGDIOBJ oldSmall = SelectObject(reduced, small);
            // Capture only the owner, including its title bar and the Direct2D canvas.
            constexpr UINT renderFullContent = 0x00000002;
            if (PrintWindow(owner, snapshot, renderFullContent)) {
                SetStretchBltMode(reduced, HALFTONE);
                SetBrushOrgEx(reduced, 0, 0, nullptr);
                captured_ = StretchBlt(reduced, 0, 0, w, h, snapshot, bounds.left - window.left,
                                       bounds.top - window.top, width, height, SRCCOPY) != FALSE;
                GdiFlush();
                if (captured_)
                    std::copy_n(static_cast<const uint32_t *>(bits), pixels_.size(), pixels_.begin());
            }
            SelectObject(reduced, oldSmall);
            SelectObject(snapshot, oldFull);
        }
        if (small) DeleteObject(small);
        if (full) DeleteObject(full);
        if (reduced) DeleteDC(reduced);
        if (snapshot) DeleteDC(snapshot);
        ReleaseDC(owner, screen);
        if (!captured_) return;

        auto blur = [w, h](const auto &source, auto &dest, bool horizontal) {
            for (int y = 0; y < h; ++y) {
                for (int x = 0; x < w; ++x) {
                    unsigned red = 0, green = 0, blue = 0;
                    for (int offset = -2; offset <= 2; ++offset) {
                        int sx = horizontal ? std::clamp(x + offset, 0, w - 1) : x;
                        int sy = horizontal ? y : std::clamp(y + offset, 0, h - 1);
                        auto pixel = source[size_t(sy) * w + sx];
                        red += (pixel >> 16) & 255;
                        green += (pixel >> 8) & 255;
                        blue += pixel & 255;
                    }
                    dest[size_t(y) * w + x] = ((red / 5) << 16) | ((green / 5) << 8) | (blue / 5);
                }
            }
        };
        blur(pixels_, scratch, true);
        blur(scratch, pixels_, false);
        auto blend = [this](unsigned channel) {
            return (channel * transparency_ + tint_ * (100 - transparency_) + 50) / 100;
        };
        for (auto &pixel : pixels_) {
            unsigned red = blend((pixel >> 16) & 255);
            unsigned green = blend((pixel >> 8) & 255);
            unsigned blue = blend(pixel & 255);
            pixel = (red << 16) | (green << 8) | blue;
        }
    }

    static LRESULT CALLBACK proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
        if (message == WM_NCCREATE) {
            auto *create = reinterpret_cast<CREATESTRUCTW *>(lparam);
            SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
        }
        if (message == WM_ERASEBKGND)
            return 1;
        if (message == WM_MOUSEACTIVATE)
            return MA_NOACTIVATEANDEAT;
        if (message == WM_PAINT || message == WM_PRINTCLIENT) {
            auto *self = reinterpret_cast<FrostedBackdrop *>(GetWindowLongPtrW(window, GWLP_USERDATA));
            PAINTSTRUCT paint{};
            HDC dc = message == WM_PAINT ? BeginPaint(window, &paint) : reinterpret_cast<HDC>(wparam);
            RECT area{};
            GetClientRect(window, &area);
            if (self->captured_) {
                SetStretchBltMode(dc, HALFTONE);
                SetBrushOrgEx(dc, 0, 0, nullptr);
                StretchDIBits(dc, 0, 0, area.right, area.bottom, 0, 0, self->info_.bmiHeader.biWidth,
                              -self->info_.bmiHeader.biHeight, self->pixels_.data(), &self->info_,
                              DIB_RGB_COLORS, SRCCOPY);
            } else {
                HBRUSH brush = CreateSolidBrush(RGB(self->tint_, self->tint_, self->tint_));
                FillRect(dc, &area, brush);
                DeleteObject(brush);
            }
            if (message == WM_PAINT)
                EndPaint(window, &paint);
            return 0;
        }
        return DefWindowProcW(window, message, wparam, lparam);
    }

  public:
    FrostedBackdrop(HWND owner, HINSTANCE instance, const AppSettings &settings)
        : owner_(owner), transparency_(settings.frostTransparency),
          tint_(settings.frostTint == FrostTint::White ? 255u : 140u) {
        RECT window{};
        if (!IsWindowVisible(owner) || IsIconic(owner) || !GetWindowRect(owner, &window))
            return;
        RECT bounds = window;
        RECT frame{};
        if (SUCCEEDED(DwmGetWindowAttribute(owner, DWMWA_EXTENDED_FRAME_BOUNDS, &frame, sizeof(frame))) &&
            frame.left >= window.left && frame.top >= window.top &&
            frame.right <= window.right && frame.bottom <= window.bottom &&
            frame.right > frame.left && frame.bottom > frame.top)
            bounds = frame;
        if (bounds.right <= bounds.left || bounds.bottom <= bounds.top)
            return;
        RedrawWindow(owner, nullptr, nullptr, RDW_UPDATENOW | RDW_ALLCHILDREN);
        if (transparency_)
            capture(owner, window, bounds);
        WNDCLASSW cls{};
        cls.lpfnWndProc = proc;
        cls.hInstance = instance;
        cls.lpszClassName = L"PaperViewFrostedBackdrop";
        RegisterClassW(&cls);
        // An owned popup is drawn above the entire owner, including all child HWNDs.
        window_ = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, cls.lpszClassName, nullptr, WS_POPUP,
                                  bounds.left, bounds.top, bounds.right - bounds.left, bounds.bottom - bounds.top,
                                  owner, nullptr, instance, this);
        if (window_) {
            SetWindowPos(window_, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
            UpdateWindow(window_);
        }
    }
    ~FrostedBackdrop() {
        if (window_) {
            DestroyWindow(window_);
            RedrawWindow(owner_, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN);
        }
    }
    HWND handle() const { return window_; }
};

struct ClearDialogWindows {
    HWND owner;
    HWND backdrop;
};

INT_PTR CALLBACK dialogProc(HWND dialog, UINT message, WPARAM wparam, LPARAM lparam) {
    switch (message) {
    case WM_INITDIALOG: {
        SendDlgItemMessageW(dialog, IDC_CLEAR_PROMPT_ICON, STM_SETICON,
                           reinterpret_cast<WPARAM>(LoadIconW(nullptr, IDI_QUESTION)), 0);
        auto &windows = *reinterpret_cast<ClearDialogWindows *>(lparam);
        auto owner = windows.owner;
        auto ownerZOrder = GetWindowLongPtrW(owner, GWL_EXSTYLE) & WS_EX_TOPMOST ? HWND_TOPMOST : HWND_TOP;
        RECT parent{}, bounds{};
        GetWindowRect(owner, &parent);
        GetWindowRect(dialog, &bounds);
        MONITORINFO monitor{sizeof(monitor)};
        GetMonitorInfoW(MonitorFromWindow(owner, MONITOR_DEFAULTTONEAREST), &monitor);
        LONG width = bounds.right - bounds.left, height = bounds.bottom - bounds.top;
        LONG x = (parent.left + parent.right - width) / 2, y = (parent.top + parent.bottom - height) / 2;
        auto area = monitor.rcWork;
        x = std::clamp(x, area.left, std::max(area.left, area.right - width));
        y = std::clamp(y, area.top, std::max(area.top, area.bottom - height));
        SetWindowPos(dialog, ownerZOrder, x, y, 0, 0, SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOOWNERZORDER);
        if (windows.backdrop)
            SetWindowPos(windows.backdrop, dialog, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        SetFocus(GetDlgItem(dialog, IDCANCEL));
        return FALSE;
    }
    case WM_ERASEBKGND: {
        auto dc = reinterpret_cast<HDC>(wparam);
        RECT area{}, footerStart{0, 0, 0, 65};
        GetClientRect(dialog, &area);
        MapDialogRect(dialog, &footerStart);
        RECT body{0, 0, area.right, footerStart.bottom};
        RECT footer{0, body.bottom, area.right, area.bottom};
        FillRect(dc, &body, GetSysColorBrush(COLOR_WINDOW));
        FillRect(dc, &footer, GetSysColorBrush(COLOR_3DFACE));
        return TRUE;
    }
    case WM_CTLCOLORDLG:
        return reinterpret_cast<INT_PTR>(GetStockObject(NULL_BRUSH));
    case WM_CTLCOLORSTATIC: {
        auto dc = reinterpret_cast<HDC>(wparam);
        SetTextColor(dc, GetSysColor(COLOR_WINDOWTEXT));
        SetBkColor(dc, GetSysColor(COLOR_WINDOW));
        return reinterpret_cast<INT_PTR>(GetSysColorBrush(COLOR_WINDOW));
    }
    case WM_COMMAND:
        switch (LOWORD(wparam)) {
        case IDC_CLEAR_ENTIRE_DOCUMENT: EndDialog(dialog, 1); return TRUE;
        case IDC_CLEAR_CURRENT_PAGE: EndDialog(dialog, 2); return TRUE;
        case IDC_CLEAR_CURRENT_VIEW: EndDialog(dialog, 3); return TRUE;
        case IDCANCEL: EndDialog(dialog, 0); return TRUE;
        }
        break;
    case WM_CLOSE:
        EndDialog(dialog, 0);
        return TRUE;
    }
    return FALSE;
}
} // namespace

int showClearInkDialog(HWND owner, const AppSettings &settings) {
    auto instance = GetModuleHandleW(nullptr);
    FrostedBackdrop backdrop(owner, instance, settings);
    ClearDialogWindows windows{owner, backdrop.handle()};
    return static_cast<int>(DialogBoxParamW(instance, MAKEINTRESOURCEW(IDD_CLEAR_INK), owner,
                                            dialogProc, reinterpret_cast<LPARAM>(&windows)));
}
} // namespace paper
