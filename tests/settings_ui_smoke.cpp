#include "settings_dialog.hpp"
#include <commctrl.h>
#include <iostream>
#include <thread>
using namespace paper;
HWND readyDialog(const wchar_t *name) {
    HWND prior=nullptr;
    while ((prior=FindWindowExW(nullptr,prior,name,nullptr))) {
        DWORD pid=0; GetWindowThreadProcessId(prior,&pid);
        if (pid==GetCurrentProcessId() && IsWindowVisible(prior)) return prior;
    }
    return nullptr;
}
void waitDialog(int action) {
    HWND panel = nullptr;
    for (int i = 0; i < 150 && !panel; i++) {
        Sleep(40);
        panel = readyDialog(L"PDFViewSettings");
    }
    if (!panel)
        return;
    Sleep(300);
    if (action == 3 || action == 4 || action == 5) {
        HWND tabs = GetDlgItem(panel, 10);
        TabCtrl_SetCurSel(tabs, 4);
        NMHDR changed{tabs, 10, TCN_SELCHANGE};
        SendMessageW(panel, WM_NOTIFY, 10, (LPARAM)&changed);
        SendMessageW(GetDlgItem(panel, 32), BM_SETCHECK, BST_UNCHECKED, 0);
        PostMessageW(panel, WM_COMMAND, 33, 0);
        HWND calibration = nullptr;
        for (int i = 0; i < 100 && !calibration; ++i) {
            Sleep(30);
            calibration = readyDialog(L"PDFViewCalibration");
        }
        if (!calibration) {
            PostMessageW(panel, WM_COMMAND, 2, 0);
            return;
        }
        SetWindowTextW(GetDlgItem(calibration, 10), L"75.5");
        if (action == 5) SetWindowTextW(GetDlgItem(calibration,12),L"24.0");
        SendMessageW(calibration, WM_COMMAND, action == 5 ? 13 : action == 3 ? IDOK : IDCANCEL, 0);
        PostMessageW(calibration, WM_NULL, 0, 0);
        for (int i = 0; i < 100 && IsWindow(calibration); ++i)
            Sleep(20);
        SendMessageW(panel, WM_COMMAND, 1, 0);
        PostMessageW(panel, WM_NULL, 0, 0);
    } else if (action == 2) {
        HWND tabs = GetDlgItem(panel, 10);
        TabCtrl_SetCurSel(tabs, 3);
        NMHDR changed{tabs, 10, TCN_SELCHANGE};
        SendMessageW(panel, WM_NOTIFY, 10, (LPARAM)&changed);
        HWND tutorial = GetDlgItem(panel, 31);
        RECT bounds{};
        GetWindowRect(tutorial, &bounds);
        if (!IsWindowVisible(tutorial) || bounds.bottom - bounds.top < 44)
            SendMessageW(panel, WM_COMMAND, 2, 0);
        else
            SendMessageW(panel, WM_COMMAND, 31, 0);
        PostMessageW(panel, WM_NULL, 0, 0);
    } else if (action == 1) {
        SendMessageW(GetDlgItem(panel, 20), BM_SETCHECK, BST_UNCHECKED, 0);
        SendMessageW(GetDlgItem(panel, 21), BM_SETCHECK, BST_UNCHECKED, 0);
        SendMessageW(GetDlgItem(panel, 22), CB_SETCURSEL, 3, 0);
        SendMessageW(GetDlgItem(panel, 23), TBM_SETPOS, TRUE, 225);
        SendMessageW(GetDlgItem(panel, 24), CB_SETCURSEL, 1, 0);
        SendMessageW(GetDlgItem(panel, 25), CB_SETCURSEL, 1, 0);
        SetWindowTextW(GetDlgItem(panel, 35), L"0.85");
        SendMessageW(GetDlgItem(panel, 36), TBM_SETPOS, TRUE, 37);
        SendMessageW(GetDlgItem(panel, 37), CB_SETCURSEL, 0, 0);
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
                 settings.officePdfMode && settings.dailyPenWidthMm == 0.85 &&
                 settings.frostTransparency == 37 && settings.frostTint == FrostTint::White;
    std::cout << (valid ? "PASS" : "FAIL") << " settings controls apply all values and both document modes\n";
    auto before = settings;
    std::thread cancel(waitDialog, false);
    bool cancelled = !showSettingsDialog(owner, settings);
    cancel.join();
    valid = valid && cancelled && settings.bindings == before.bindings &&
            settings.scrollSensitivity == before.scrollSensitivity;
    bool openTutorial = false;
    std::thread tutorial(waitDialog, 2);
    bool opened = showSettingsDialog(owner, settings, &openTutorial);
    tutorial.join();
    valid = valid && opened && openTutorial;
    std::cout << (opened && openTutorial ? "PASS" : "FAIL")
              << " tutorial tab exposes a touch-sized entry and returns an open request\n";
    std::thread calibrate(waitDialog, 3);
    bool calibrated = showSettingsDialog(owner, settings);
    calibrate.join();
    double sample = MulDiv(332, GetDpiForWindow(owner), 96) - MulDiv(32, GetDpiForWindow(owner), 96);
    bool calibrationOk = calibrated && !settings.autoPenWidth && settings.screenCalibrations.size() == 1 &&
                         std::abs(settings.screenCalibrations.begin()->second - sample / 75.5) < 1e-10;
    valid = valid && calibrationOk;
    std::cout << (calibrationOk ? "PASS" : "FAIL") << " optional physical ruler calibration applies and retains other edits\n";
    auto savedCalibration = settings.screenCalibrations;
    std::thread cancelCalibration(waitDialog, 4);
    bool closed = showSettingsDialog(owner, settings);
    cancelCalibration.join();
    valid = valid && closed && settings.screenCalibrations == savedCalibration;
    std::thread diagonal(waitDialog,5);
    bool usedDiagonal=showSettingsDialog(owner,settings);
    diagonal.join();
    MONITORINFO monitor{sizeof(monitor)};
    GetMonitorInfoW(MonitorFromWindow(owner,MONITOR_DEFAULTTONEAREST),&monitor);
    double expected=std::hypot(monitor.rcMonitor.right-monitor.rcMonitor.left,monitor.rcMonitor.bottom-monitor.rcMonitor.top)/(24*25.4);
    valid=valid && usedDiagonal && !settings.screenCalibrations.empty() &&
          std::abs(settings.screenCalibrations.begin()->second-expected)<1e-10;
    DestroyWindow(owner);
    std::cout << (valid ? "SETTINGS_UI=PASS" : "SETTINGS_UI=FAIL") << '\n';
    return valid ? 0 : 1;
}
