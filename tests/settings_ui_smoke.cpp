#include "settings_dialog.hpp"
#include <commctrl.h>
#include <iostream>
#include <thread>
using namespace paper;
void waitDialog(bool save) {
    HWND panel = nullptr;
    for (int i = 0; i < 150 && !panel; i++) {
        Sleep(40);
        panel = FindWindowW(L"PDFViewSettings", nullptr);
    }
    if (!panel)
        return;
    Sleep(300);
    if (save) {
        SendMessageW(GetDlgItem(panel, 20), BM_SETCHECK, BST_UNCHECKED, 0);
        SendMessageW(GetDlgItem(panel, 21), BM_SETCHECK, BST_UNCHECKED, 0);
        SendMessageW(GetDlgItem(panel, 22), CB_SETCURSEL, 3, 0);
        SendMessageW(GetDlgItem(panel, 23), TBM_SETPOS, TRUE, 225);
        SendMessageW(GetDlgItem(panel, 24), CB_SETCURSEL, 1, 0);
        SendMessageW(GetDlgItem(panel, 25), CB_SETCURSEL, 1, 0);
        HWND tabs = GetDlgItem(panel, 10);
        TabCtrl_SetCurSel(tabs, 1);
        NMHDR changed{tabs, 10, TCN_SELCHANGE};
        SendMessageW(panel, WM_NOTIFY, 10, (LPARAM)&changed);
        Sleep(300);
        SendMessageW(panel, WM_COMMAND, 1, 0);
        PostMessageW(panel, WM_NULL, 0, 0);
    } else {
        SendMessageW(panel, WM_COMMAND, 2, 0);
        PostMessageW(panel, WM_NULL, 0, 0);
    }
}
int main() {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    HWND owner = CreateWindowW(L"STATIC", L"Settings UI smoke", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 0, 0, 900,
                               700, nullptr, nullptr, nullptr, nullptr);
    AppSettings settings;
    std::thread driver(waitDialog, true);
    bool saved = showSettingsDialog(owner, settings);
    driver.join();
    bool valid = saved && !settings.openSidebar && !settings.restoreSession && settings.blurLevel == 3 &&
                 settings.scrollSensitivity == 2.25 && settings.officeEngine == OfficeEngine::Word &&
                 settings.officePdfMode;
    std::cout << (valid ? "PASS" : "FAIL") << " settings controls apply all values and both document modes\n";
    auto before = settings;
    std::thread cancel(waitDialog, false);
    bool cancelled = !showSettingsDialog(owner, settings);
    cancel.join();
    valid = valid && cancelled && settings.bindings == before.bindings &&
            settings.scrollSensitivity == before.scrollSensitivity;
    DestroyWindow(owner);
    std::cout << (valid ? "SETTINGS_UI=PASS" : "SETTINGS_UI=FAIL") << '\n';
    return valid ? 0 : 1;
}
