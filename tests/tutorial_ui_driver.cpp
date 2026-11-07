// Captures the running Windows application after opening the real settings tutorial entry.
#include <windows.h>

#include <algorithm>
#include <commctrl.h>
#include <cstdio>
#include <gdiplus.h>
#include <string>
std::wstring text(HWND window) {
    wchar_t value[256]{};
    SendMessageW(window, WM_GETTEXT, 256, (LPARAM)value);
    return value;
}
bool capture(HWND app, const std::wstring &file) {
    SetCursorPos(0, 0);
    RedrawWindow(app, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_UPDATENOW);
    Sleep(1200);
    RECT bounds{};
    GetWindowRect(app, &bounds);
    HDC screen = GetDC(nullptr), dc = CreateCompatibleDC(screen);
    HBITMAP bitmap = CreateCompatibleBitmap(screen, bounds.right - bounds.left, bounds.bottom - bounds.top);
    auto old = SelectObject(dc, bitmap);
    bool ok = BitBlt(dc, 0, 0, bounds.right - bounds.left, bounds.bottom - bounds.top, screen, bounds.left,
                     bounds.top, SRCCOPY | CAPTUREBLT) != 0;
    Gdiplus::GdiplusStartupInput input;
    ULONG_PTR token;
    Gdiplus::GdiplusStartup(&token, &input, nullptr);
    CLSID png{0x557cf406, 0x1a04, 0x11d3, {0x9a, 0x73, 0x00, 0x00, 0xf8, 0x1e, 0xf3, 0x2e}};
    {
        Gdiplus::Bitmap image(bitmap, nullptr);
        ok = ok && image.Save(file.c_str(), &png, nullptr) == Gdiplus::Ok;
    }
    Gdiplus::GdiplusShutdown(token);
    SelectObject(dc, old);
    DeleteObject(bitmap);
    DeleteDC(dc);
    ReleaseDC(nullptr, screen);
    return ok;
}
int main() {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    HWND app = nullptr;
    for (int i = 0; i < 200; ++i) {
        Sleep(50);
        app = FindWindowW(L"PaperViewMain", nullptr);
        if (app && IsWindowVisible(app))
            break;
    }
    if (!app || !IsWindowVisible(app))
        return 1;
    DEVMODEW mode{};
    mode.dmSize = sizeof(mode);
    EnumDisplaySettingsW(nullptr, ENUM_CURRENT_SETTINGS, &mode);
    mode.dmPelsWidth = 1920;
    mode.dmPelsHeight = 1080;
    mode.dmFields = DM_PELSWIDTH | DM_PELSHEIGHT;
    ChangeDisplaySettingsW(&mode, 0);
    Sleep(1000);
    SetWindowPos(app, HWND_TOP, 24, 20, std::min(1540, GetSystemMetrics(SM_CXSCREEN) - 40),
                 std::min(950, GetSystemMetrics(SM_CYSCREEN) - 48), SWP_SHOWWINDOW);
    SetForegroundWindow(app);
    PostMessageW(app, WM_COMMAND, 115, 0);
    HWND settings = nullptr;
    for (int i = 0; i < 200; ++i) {
        Sleep(50);
        settings = FindWindowW(L"PDFViewSettings", nullptr);
        if (settings && GetDlgItem(settings, 31))
            break;
    }
    if (!settings || !GetDlgItem(settings, 31)) {
        std::fprintf(stderr, "Tutorial settings button did not become ready\n");
        return 2;
    }
    PostMessageW(settings, WM_COMMAND, 31, 0);
    PostMessageW(settings, WM_NULL, 0, 0);
    HWND tree = nullptr;
    for (int i = 0; i < 200; ++i) {
        Sleep(50);
        tree = FindWindowExW(app, nullptr, WC_TREEVIEWW, nullptr);
        if (!FindWindowW(L"PDFViewSettings", nullptr) && tree && TreeView_GetCount(tree) == 6 &&
            IsWindowEnabled(GetDlgItem(app, 125)))
            break;
    }
    if (!tree || TreeView_GetCount(tree) != 6 || !IsWindowEnabled(GetDlgItem(app, 125)))
        return 3;
    SendMessageW(app, WM_COMMAND, 110, 0);
    const wchar_t *names[] = {L"artifacts/01-math.png", L"artifacts/02-physics.png",
                              L"artifacts/03-chemistry.png"};
    for (int page = 4; page <= 6; ++page) {
        auto item = TreeView_GetRoot(tree);
        for (int i = 1; i < page && item; ++i)
            item = TreeView_GetNextSibling(tree, item);
        TreeView_SelectItem(tree, item);
        Sleep(1500);
        if (text(GetDlgItem(app, 116)) != std::to_wstring(page))
            return 4;
        if (!capture(app, names[page - 4]))
            return 5;
        printf("PASS settings tutorial entry, practice page %d and native Windows screenshot\n", page);
    }
    PostMessageW(app, WM_CLOSE, 0, 0);
    return 0;
}
