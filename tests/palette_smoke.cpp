// Run the real popup and assert width updates do not repaint its color controls.
#include "palette.hpp"
#include <atomic>
#include <fstream>
#include <thread>
std::atomic<int> colorPaints{0};
WNDPROC original;
LRESULT CALLBACK monitor(HWND w, UINT m, WPARAM a, LPARAM b) {
    if (m == WM_DRAWITEM)
        ++colorPaints;
    return CallWindowProcW(original, w, m, a, b);
}
int main() {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    HWND owner = CreateWindowW(L"STATIC", L"Palette regression", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 20, 20,
                               600, 600, nullptr, nullptr, nullptr, nullptr);
    HWND anchor = CreateWindowW(L"BUTTON", L"Pen", WS_CHILD | WS_VISIBLE, 20, 20, 80, 44, owner, nullptr,
                                nullptr, nullptr);
    uint32_t color = 0x006bff;
    float width = 2.5f;
    bool passed = false;
    std::thread driver([&] {
        HWND panel = nullptr;
        for (int i = 0; i < 100 && !panel; i++) {
            Sleep(50);
            panel = FindWindowW(L"PDFViewPenPalette", nullptr);
        }
        if (!panel)
            return;
        Sleep(500);
        original = (WNDPROC)SetWindowLongPtrW(panel, GWLP_WNDPROC, (LONG_PTR)monitor);
        HWND slider = FindWindowExW(panel, nullptr, TRACKBAR_CLASSW, nullptr);
        for (int i = 0; i < 220; i++) {
            if (i == 20)
                colorPaints = 0;
            SendMessageW(slider, TBM_SETPOS, TRUE, 5 + i % 116);
            SendMessageW(panel, WM_HSCROLL, TB_THUMBTRACK, (LPARAM)slider);
            UpdateWindow(panel);
            UpdateWindow(slider);
        }
        int paints = colorPaints;
        HWND black = GetDlgItem(panel, 200);
        UpdateWindow(black);
        HDC dc = GetDC(nullptr);
        RECT screen{};
        GetWindowRect(black, &screen);
        int antialiased = 0;
        RECT rect{};
        GetClientRect(black, &rect);
        for (int y = 0; y < rect.bottom; y++)
            for (int x = 0; x < rect.right; x++) {
                auto c = GetPixel(dc, screen.left + x, screen.top + y);
                if (c != CLR_INVALID && c != RGB(0, 0, 0) && c != RGB(153, 153, 153) &&
                    c != RGB(255, 255, 255))
                    ++antialiased;
            }
        ReleaseDC(nullptr, dc);
        SendMessageW(panel, WM_COMMAND, MAKEWPARAM(207, BN_CLICKED), (LPARAM)GetDlgItem(panel, 207));
        UpdateWindow(GetDlgItem(panel, 207));
        passed = slider && antialiased > 20 && paints == 0 && color == 0xe81818 && width > 0.5f;
        std::ofstream out("palette-smoke.txt");
        out << "antialiased_edge_pixels=" << antialiased << "\n";
        out << "width_updates=200\ncolor_repaints_during_drag=" << paints
            << "\ncolor_selection=" << (color == 0xe81818) << "\nRESULT=" << (passed ? "PASS" : "FAIL")
            << "\n";
        out.close();

        PostMessageW(panel, WM_CLOSE, 0, 0);
    });
    paper::PenPalette palette(color, width, false);
    palette.show(owner, anchor);
    driver.join();
    DestroyWindow(owner);
    return passed ? 0 : 1;
}
