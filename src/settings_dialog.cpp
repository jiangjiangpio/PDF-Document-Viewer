#include "settings_dialog.hpp"
#include "display.hpp"
#include <charconv>
#include <commctrl.h>
#include <cmath>
#include <cwctype>
#include <map>
#include <windowsx.h>
namespace paper {
std::wstring shortcutLabel(KeyChord chord) {
    if (!chord.key)
        return L"未绑定";
    std::wstring s;
    if (chord.modifiers & 1)
        s += L"Ctrl+";
    if (chord.modifiers & 2)
        s += L"Shift+";
    if (chord.modifiers & 4)
        s += L"Alt+";
    wchar_t name[80]{};
    UINT scan = MapVirtualKeyW(chord.key, MAPVK_VK_TO_VSC);
    GetKeyNameTextW(scan << 16, name, 80);
    return s + (name[0] ? std::wstring(name) : std::to_wstring(chord.key));
}
namespace {
std::wstring textOf(HWND w) {
    int n = GetWindowTextLengthW(w);
    std::wstring value(n + 1, 0);
    GetWindowTextW(w, value.data(), n + 1);
    value.resize(n);
    return value;
}
struct CalibrationState {
    HWND window = nullptr, edit = nullptr;
    UINT dpi = 96;
    bool done = false, ok = false;
    double pixelsPerMm = 0;
    std::string screenKey;
};
LRESULT CALLBACK calibrationProc(HWND w, UINT m, WPARAM a, LPARAM b) {
    auto *state = reinterpret_cast<CalibrationState *>(GetWindowLongPtrW(w, GWLP_USERDATA));
    if (m == WM_NCCREATE) {
        state = static_cast<CalibrationState *>(reinterpret_cast<CREATESTRUCTW *>(b)->lpCreateParams);
        state->window = w;
        SetWindowLongPtrW(w, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
    }
    if (!state)
        return DefWindowProcW(w, m, a, b);
    if (m == WM_PAINT) {
        PAINTSTRUCT ps{};
        BeginPaint(w, &ps);
        auto p = [&](int value) { return MulDiv(value, state->dpi, 96); };
        HPEN pen = CreatePen(PS_SOLID, std::max(1, p(2)), RGB(37, 99, 235));
        auto old = SelectObject(ps.hdc, pen);
        MoveToEx(ps.hdc, p(32), p(82), nullptr);
        LineTo(ps.hdc, p(332), p(82));
        for (int x : {32, 332}) {
            MoveToEx(ps.hdc, p(x), p(73), nullptr);
            LineTo(ps.hdc, p(x), p(91));
        }
        SelectObject(ps.hdc, old);
        DeleteObject(pen);
        EndPaint(w, &ps);
        return 0;
    }
    if (m == WM_COMMAND && (LOWORD(a) == IDOK || LOWORD(a) == 13)) {
        bool useDiagonal = LOWORD(a) == 13;
        auto metrics = screenMetrics(w,AppSettings{});
        auto value = textOf(useDiagonal ? GetDlgItem(w,12) : state->edit);
        wchar_t *end = nullptr;
        double measured = wcstod(value.c_str(), &end);
        while (end && iswspace(*end)) ++end;
        double density = (MulDiv(332, state->dpi, 96) - MulDiv(32, state->dpi, 96)) / measured;
        if (useDiagonal) {
            int preset = (int)SendMessageW(GetDlgItem(w,11),CB_GETCURSEL,0,0);
            const int widths[]{metrics.width,1920,2560,3840}, heights[]{metrics.height,1080,1440,2160};
            if (preset<0 || preset>3) return 0;
            int width=widths[preset],height=heights[preset];
            if (preset && metrics.width<metrics.height) std::swap(width,height);
            if (width != metrics.width || height != metrics.height) {
                MessageBoxW(w,L"所选预设与当前显示分辨率不同，请选择当前分辨率。",L"屏幕尺寸",MB_ICONINFORMATION);
                return 0;
            }
            density = std::hypot(double(width),double(height))/(25.4*measured);
        }
        if (end == value.c_str() || (end && *end) || !std::isfinite(density) || density < .5 || density > 20) {
            MessageBoxW(w, useDiagonal ? L"请输入有效的屏幕对角线英寸数。" : L"请输入尺子测得的横线长度，例如 79.5。", L"屏幕校准",
                        MB_ICONWARNING);
            return 0;
        }
        if (screenMetrics(w, AppSettings{}).key != state->screenKey) {
            MessageBoxW(w, L"校准窗口所在的显示器已改变。请取消后，在需要校准的显示器上重新打开。",
                        L"屏幕校准", MB_ICONINFORMATION);
            return 0;
        }
        state->pixelsPerMm = density;
        state->ok = true;
        state->done = true;
        return 0;
    }
    if (m == WM_COMMAND && LOWORD(a) == IDCANCEL) {
        state->done = true;
        return 0;
    }
    if (m == WM_CLOSE) {
        state->done = true;
        return 0;
    }
    return DefWindowProcW(w, m, a, b);
}
std::optional<std::pair<std::string, double>> showCalibrationDialog(HWND owner) {
    static bool registered = false;
    if (!registered) {
        WNDCLASSW wc{};
        wc.lpfnWndProc = calibrationProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
        wc.lpszClassName = L"PDFViewCalibration";
        RegisterClassW(&wc);
        registered = true;
    }
    CalibrationState state;
    state.dpi = GetDpiForWindow(owner);
    auto p = [&](int value) { return MulDiv(value, state.dpi, 96); };
    RECT parent{};
    GetWindowRect(owner, &parent);
    int width = p(430), height = p(388);
    int x = parent.left + ((parent.right - parent.left) - width) / 2;
    int y = parent.top + ((parent.bottom - parent.top) - height) / 2;
    HWND window = CreateWindowExW(WS_EX_DLGMODALFRAME, L"PDFViewCalibration", L"校准屏幕尺寸",
                                 WS_CAPTION | WS_SYSMENU | WS_CLIPCHILDREN, x, y, width, height, owner,
                                 nullptr, GetModuleHandleW(nullptr), &state);
    state.screenKey = screenMetrics(window, AppSettings{}).key;
    HFONT font = CreateFontW(-p(14), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, 0, 0,
                             CLEARTYPE_QUALITY, 0, L"Microsoft YaHei UI");
    auto control = [&](const wchar_t *cls, const wchar_t *label, DWORD style, int id, int cx, int cy,
                       int cw, int ch) {
        HWND c = CreateWindowExW(0, cls, label, WS_CHILD | WS_VISIBLE | style, p(cx), p(cy), p(cw), p(ch),
                                 window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), nullptr, nullptr);
        SendMessageW(c, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        return c;
    };
    control(L"STATIC", L"用实体尺测量两端刻度之间的长度，填入毫米数：", 0, 0, 24, 18, 380, 42);
    control(L"EDIT", L"", WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL, 10, 24, 118, 310, 34);
    control(L"STATIC", L"毫米", 0, 0, 344, 122, 60, 30);
    control(L"BUTTON", L"使用尺测值", WS_TABSTOP | BS_DEFPUSHBUTTON, IDOK, 218, 164, 110, 34);
    control(L"STATIC", L"或按分辨率与屏幕对角线计算", 0, 0, 24, 210, 365, 24);
    auto presets=control(L"COMBOBOX",L"",CBS_DROPDOWNLIST|WS_TABSTOP,11,24,242,220,180);
    auto metrics = screenMetrics(window,AppSettings{});
    auto current = L"当前 "+std::to_wstring(metrics.width)+L" × "+std::to_wstring(metrics.height);
    for (auto label : {current.c_str(),L"1K / FHD  1920 × 1080",L"2K / QHD  2560 × 1440",L"4K / UHD  3840 × 2160"})
        SendMessageW(presets,CB_ADDSTRING,0,(LPARAM)label);
    SendMessageW(presets,CB_SETCURSEL,0,0);
    control(L"EDIT",L"",WS_BORDER|WS_TABSTOP|ES_AUTOHSCROLL,12,252,242,62,30);
    control(L"STATIC",L"英寸",0,0,322,245,60,28);
    control(L"BUTTON",L"使用计算值",WS_TABSTOP,13,218,290,110,36);
    control(L"BUTTON", L"取消", WS_TABSTOP, IDCANCEL, 334, 290, 68, 36);
    EnableWindow(owner, FALSE);
    ShowWindow(window, SW_SHOW);
    state.edit = GetDlgItem(window, 10);
    SetFocus(state.edit);
    SendMessageW(state.edit, EM_SETSEL, 0, -1);
    MSG msg{};
    while (!state.done && GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (!IsDialogMessageW(window, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    DestroyWindow(window);
    DeleteObject(font);
    EnableWindow(owner, TRUE);
    SetForegroundWindow(owner);
    if (state.ok)
        return std::make_pair(state.screenKey, state.pixelsPerMm);
    return std::nullopt;
}
class SettingsDialog {
    enum {
        Tabs = 10,
        Sidebar = 20,
        Restore,
        Blur,
        Scroll,
        Engine,
        Mode,
        Actions,
        Capture,
        Assign,
        Clear,
        Defaults,
        OpenTutorial,
        AutoWidth,
        Calibrate,
        CalibrationInfo,
        DailyPenWidth,
        FrostTransparency,
        FrostTintChoice,
        FrostPercentage,
        Save = 1,
        Cancel = 2
    };
    HWND window_ = nullptr, owner_;
    HFONT font_ = nullptr;
    UINT dpi_;
    std::map<int, HWND> controls_;
    std::vector<std::pair<HWND, int>> pages_;
    AppSettings draft_;
    KeyChord captured_{};
    bool done_ = false, accepted_ = false, tutorial_ = false;
    int p(int n) const {
        return MulDiv(n, dpi_, 96);
    }
    HWND control(const wchar_t *cls, const wchar_t *text, DWORD style, int id, int x, int y, int w, int h,
                 int page = -1) {
        HWND c = CreateWindowExW(0, cls, text, WS_CHILD | WS_VISIBLE | style, p(x), p(y), p(w), p(h), window_,
                                 (HMENU)(INT_PTR)id, nullptr, nullptr);
        SendMessageW(c, WM_SETFONT, (WPARAM)font_, TRUE);
        if (id)
            controls_[id] = c;
        if (wcscmp(cls, L"COMBOBOX") == 0) {
            SendMessageW(c, CB_SETITEMHEIGHT, (WPARAM)-1, p(32));
            SendMessageW(c, CB_SETITEMHEIGHT, 0, p(32));
        }
        if (page >= 0)
            pages_.push_back({c, page});
        return c;
    }
    void label(const wchar_t *text, int x, int y, int w, int h, int page) {
        control(L"STATIC", text, 0, 0, x, y, w, h, page);
    }
    int selectedAction() const {
        return (int)SendMessageW(controls_.at(Actions), LB_GETCURSEL, 0, 0);
    }
    void listActions(int selected = 0) {
        HWND list = controls_[Actions];
        SendMessageW(list, LB_RESETCONTENT, 0, 0);
        for (size_t i = 0; i < draft_.bindings.size(); ++i) {
            auto s = AppSettings::actionName((Action)i) + L"    " + shortcutLabel(draft_.bindings[i]);
            SendMessageW(list, LB_ADDSTRING, 0, (LPARAM)s.c_str());
        }
        SendMessageW(list, LB_SETCURSEL, selected, 0);
        selectAction();
    }
    void selectAction() {
        int i = selectedAction();
        if (i < 0)
            return;
        captured_ = draft_.bindings[i];
        SetWindowTextW(controls_[Capture], shortcutLabel(captured_).c_str());
    }
    void page() {
        int i = TabCtrl_GetCurSel(controls_[Tabs]);
        for (auto [w, page] : pages_)
            ShowWindow(w, i == page ? SW_SHOW : SW_HIDE);
        if (i == 4)
            refreshCalibration();
    }
    void fill() {
        SendMessageW(controls_[Sidebar], BM_SETCHECK, draft_.openSidebar ? BST_CHECKED : BST_UNCHECKED, 0);
        SendMessageW(controls_[Restore], BM_SETCHECK, draft_.restoreSession ? BST_CHECKED : BST_UNCHECKED, 0);
        SendMessageW(controls_[Blur], CB_SETCURSEL, draft_.blurLevel, 0);
        SendMessageW(controls_[Scroll], TBM_SETPOS, TRUE, (int)std::lround(draft_.scrollSensitivity * 100));
        SendMessageW(controls_[FrostTransparency], TBM_SETPOS, TRUE, draft_.frostTransparency);
        SendMessageW(controls_[FrostTintChoice], CB_SETCURSEL, static_cast<int>(draft_.frostTint), 0);
        showFrostPercentage();
        SendMessageW(controls_[Engine], CB_SETCURSEL, (int)draft_.officeEngine, 0);
        SendMessageW(controls_[Mode], CB_SETCURSEL, draft_.officePdfMode ? 1 : 0, 0);
        SendMessageW(controls_[AutoWidth], BM_SETCHECK, draft_.autoPenWidth ? BST_CHECKED : BST_UNCHECKED, 0);
        char width[64];
        auto result = std::to_chars(width, width + sizeof(width), draft_.dailyPenWidthMm);
        std::wstring widthText(width, result.ptr);
        SetWindowTextW(controls_[DailyPenWidth], widthText.c_str());
        refreshCalibration();
        listActions();
    }
    void refreshCalibration() {
        auto screen = screenMetrics(window_, draft_);
        auto text = screen.name + (screen.calibrated ? L"：已校准" : (screen.physical ? L"：已读取屏幕物理尺寸" : L"：缺少物理尺寸，当前按 DPI 估算"));
        SetWindowTextW(controls_[CalibrationInfo], text.c_str());
    }
    int frostTransparency() const {
        return static_cast<int>(SendMessageW(controls_.at(FrostTransparency), TBM_GETPOS, 0, 0));
    }
    void showFrostPercentage() {
        auto text = std::to_wstring(frostTransparency()) + L"%";
        SetWindowTextW(controls_.at(FrostPercentage), text.c_str());
    }
    void save() {
        auto widthText = textOf(controls_[DailyPenWidth]);
        wchar_t *end = nullptr;
        double width = std::wcstod(widthText.c_str(), &end);
        if (end != widthText.c_str())
            while (std::iswspace(*end)) ++end;
        if (end == widthText.c_str() || *end || !std::isfinite(width) ||
            width < AppSettings::minScreenPenWidthMm || width > AppSettings::maxScreenPenWidthMm) {
            MessageBoxW(window_, L"每日重置线宽请输入 0.1 到 10 mm 之间的数值。", L"画笔与校准",
                        MB_OK | MB_ICONWARNING);
            TabCtrl_SetCurSel(controls_[Tabs], 4);
            page();
            SetFocus(controls_[DailyPenWidth]);
            SendMessageW(controls_[DailyPenWidth], EM_SETSEL, 0, -1);
            return;
        }
        draft_.dailyPenWidthMm = width;
        draft_.openSidebar = SendMessageW(controls_[Sidebar], BM_GETCHECK, 0, 0) == BST_CHECKED;
        draft_.restoreSession = SendMessageW(controls_[Restore], BM_GETCHECK, 0, 0) == BST_CHECKED;
        draft_.blurLevel = (int)SendMessageW(controls_[Blur], CB_GETCURSEL, 0, 0);
        draft_.frostTransparency = frostTransparency();
        draft_.frostTint = static_cast<FrostTint>(SendMessageW(controls_[FrostTintChoice], CB_GETCURSEL, 0, 0));
        draft_.scrollSensitivity = SendMessageW(controls_[Scroll], TBM_GETPOS, 0, 0) / 100.;
        draft_.officeEngine = (OfficeEngine)SendMessageW(controls_[Engine], CB_GETCURSEL, 0, 0);
        draft_.officePdfMode = SendMessageW(controls_[Mode], CB_GETCURSEL, 0, 0) == 1;
        draft_.autoPenWidth = SendMessageW(controls_[AutoWidth], BM_GETCHECK, 0, 0) == BST_CHECKED;
        try {
            draft_.validate();
            accepted_ = true;
            done_ = true;
        } catch (...) {
            MessageBoxW(window_, L"设置无效，请检查快捷键是否重复或使用了系统保留按键。", L"设置",
                        MB_ICONWARNING);
        }
    }
    static LRESULT CALLBACK captureProc(HWND w, UINT m, WPARAM a, LPARAM b, UINT_PTR, DWORD_PTR data) {
        auto self = (SettingsDialog *)data;
        if (m == WM_GETDLGCODE)
            return DLGC_WANTALLKEYS;
        if (m == WM_KEYDOWN || m == WM_SYSKEYDOWN) {
            if (a == VK_ESCAPE) {
                self->done_ = true;
                return 0;
            }
            if (a == VK_CONTROL || a == VK_SHIFT || a == VK_MENU || a == VK_LWIN || a == VK_RWIN)
                return 0;
            self->captured_ = {(unsigned)a, (unsigned)((GetKeyState(VK_CONTROL) < 0 ? 1 : 0) |
                                                       (GetKeyState(VK_SHIFT) < 0 ? 2 : 0) |
                                                       (GetKeyState(VK_MENU) < 0 ? 4 : 0))};
            SetWindowTextW(w, shortcutLabel(self->captured_).c_str());
            return 0;
        }
        if (m == WM_CHAR || m == WM_SYSCHAR)
            return 0;
        if (m == WM_NCDESTROY)
            RemoveWindowSubclass(w, captureProc, 1);
        return DefSubclassProc(w, m, a, b);
    }
    static LRESULT CALLBACK proc(HWND w, UINT m, WPARAM a, LPARAM b) {
        auto self = (SettingsDialog *)GetWindowLongPtrW(w, GWLP_USERDATA);
        if (m == WM_NCCREATE) {
            self = (SettingsDialog *)((CREATESTRUCTW *)b)->lpCreateParams;
            self->window_ = w;
            SetWindowLongPtrW(w, GWLP_USERDATA, (LONG_PTR)self);
        }
        if (!self)
            return DefWindowProcW(w, m, a, b);
        if (m == WM_CTLCOLORSTATIC || m == WM_CTLCOLORBTN) {
            SetBkColor((HDC)a, RGB(255, 255, 255));
            SetTextColor((HDC)a, RGB(48, 52, 58));
            return (LRESULT)GetStockObject(WHITE_BRUSH);
        }
        if (m == WM_CLOSE) {
            self->done_ = true;
            return 0;
        }
        if (m == WM_HSCROLL && GetDlgCtrlID(reinterpret_cast<HWND>(b)) == FrostTransparency) {
            self->showFrostPercentage();
            return 0;
        }
        if (m == WM_NOTIFY && ((NMHDR *)b)->code == TCN_SELCHANGE) {
            self->page();
            return 0;
        }
        if (m == WM_COMMAND) {
            int id = LOWORD(a);
            if (id == OpenTutorial) {
                self->save();
                self->tutorial_ = self->accepted_;
            } else if (id == Save)
                self->save();
            else if (id == Cancel)
                self->done_ = true;
            else if (id == Calibrate) {
                if (auto calibration = showCalibrationDialog(w)) {
                    self->draft_.screenCalibrations[calibration->first] = calibration->second;
                    self->refreshCalibration();
                }
            }
            else if (id == Defaults) {
                self->draft_ = AppSettings();
                self->fill();
            } else if (id == Actions && HIWORD(a) == LBN_SELCHANGE)
                self->selectAction();
            else if (id == Assign || id == Clear) {
                int i = self->selectedAction();
                if (i < 0)
                    return 0;
                auto old = self->draft_.bindings[i];
                self->draft_.bindings[i] = id == Clear ? KeyChord{} : self->captured_;
                try {
                    self->draft_.validate();
                    self->listActions(i);
                } catch (...) {
                    self->draft_.bindings[i] = old;
                    MessageBoxW(w, L"快捷键已被占用或属于保留按键。请使用 Ctrl / Shift / Alt 组合或功能键。",
                                L"快捷键冲突", MB_ICONWARNING);
                }
            }
            return 0;
        }
        return DefWindowProcW(w, m, a, b);
    }

  public:
    SettingsDialog(HWND owner, const AppSettings &settings)
        : owner_(owner), dpi_(GetDpiForWindow(owner)), draft_(settings) {}
    bool show(AppSettings &out, bool *openTutorial) {
        if (openTutorial)
            *openTutorial = false;
        INITCOMMONCONTROLSEX init{sizeof(init), ICC_TAB_CLASSES | ICC_BAR_CLASSES};
        InitCommonControlsEx(&init);
        WNDCLASSW wc{};
        wc.lpfnWndProc = proc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = L"PDFViewSettings";
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
        RegisterClassW(&wc);
        RECT parent{};
        GetWindowRect(owner_, &parent);
        MONITORINFO monitor{sizeof(monitor)};
        GetMonitorInfoW(MonitorFromWindow(owner_, MONITOR_DEFAULTTONEAREST), &monitor);
        int width = p(650), height = p(530);
        int x = std::max((int)monitor.rcWork.left,
                         (int)parent.left + ((int)(parent.right - parent.left) - width) / 2);
        int y = std::max((int)monitor.rcWork.top,
                         (int)parent.top + ((int)(parent.bottom - parent.top) - height) / 2);
        window_ = CreateWindowExW(WS_EX_DLGMODALFRAME, wc.lpszClassName, L"PDF view 设置",
                                  WS_CAPTION | WS_SYSMENU | WS_CLIPCHILDREN, x, y, width, height, owner_,
                                  nullptr, wc.hInstance, this);
        font_ = CreateFontW(-p(14), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, 0, 0,
                            CLEARTYPE_QUALITY, 0, L"Microsoft YaHei UI");
        control(WC_TABCONTROLW, L"", WS_TABSTOP, Tabs, 16, 12, 610, 42);
        for (auto name : {L"阅读与性能", L"Word / WPS", L"快捷键", L"功能教程", L"画笔与校准", L"界面外观"}) {
            TCITEMW tab{};
            tab.mask = TCIF_TEXT;
            tab.pszText = (LPWSTR)name;
            TabCtrl_InsertItem(controls_[Tabs], TabCtrl_GetItemCount(controls_[Tabs]), &tab);
        }
        control(L"BUTTON", L"打开文档时自动打开侧边栏", BS_AUTOCHECKBOX | WS_TABSTOP, Sidebar, 28, 70, 570,
                42, 0);
        control(L"BUTTON", L"启动时恢复上次打开的文档", BS_AUTOCHECKBOX | WS_TABSTOP, Restore, 28, 116, 570,
                42, 0);
        label(L"滚动时虚化等级", 28, 176, 200, 28, 0);
        control(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_TABSTOP | WS_VSCROLL, Blur, 260, 172, 330, 220, 0);
        for (auto text :
             {L"关闭（优先清晰）", L"轻微（默认）", L"中等（优先流畅）", L"较高（最低预览清晰度）"})
            SendMessageW(controls_[Blur], CB_ADDSTRING, 0, (LPARAM)text);
        label(L"滑动灵敏度：0.25× — 3×", 28, 236, 560, 28, 0);
        control(TRACKBAR_CLASSW, L"滑动灵敏度", TBS_HORZ | TBS_NOTICKS | WS_TABSTOP, Scroll, 28, 270, 560, 44,
                0);
        SendMessageW(controls_[Scroll], TBM_SETRANGE, TRUE, MAKELPARAM(25, 300));
        label(L"影响滚轮与阅读拖动，默认 1×；停止交互后恢复高清。", 28, 332, 570, 55, 0);
        control(L"BUTTON", L"新写笔迹保持屏幕线宽",
                BS_AUTOCHECKBOX | WS_TABSTOP, AutoWidth, 28, 76, 570, 42, 4);
        label(L"开启时，手动粗细当天优先，北京时间次日第一笔恢复每日重置线宽。\r\n"
              L"关闭时，使用记忆的文档笔宽。",
              28, 129, 560, 64, 4);
        label(L"每日重置线宽", 28, 216, 146, 30, 4);
        control(L"EDIT", L"", WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL,
                DailyPenWidth, 176, 209, 95, 32, 4);
        label(L"mm（0.1–10）", 282, 216, 190, 30, 4);
        control(L"BUTTON", L"校准屏幕尺寸", WS_TABSTOP, Calibrate, 28, 266, 185, 44, 4);
        control(L"STATIC", L"", 0, CalibrationInfo, 28, 326, 560, 42, 4);
        label(L"用实体尺校准后才能对应实际毫米；每台显示器与分辨率分别保存。", 28, 376, 560, 42, 4);
        label(L"DOC / DOCX 排版引擎", 28, 78, 560, 28, 1);
        control(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_TABSTOP, Engine, 28, 120, 560, 220, 1);
        for (auto text : {L"自动（优先 Microsoft Word，其次 WPS）", L"Microsoft Word", L"WPS Office"})
            SendMessageW(controls_[Engine], CB_ADDSTRING, 0, (LPARAM)text);
        label(L"Word 文档打开方式（下次打开生效）", 28, 174, 560, 28, 1);
        control(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_TABSTOP, Mode, 28, 210, 560, 180, 1);
        SendMessageW(controls_[Mode], CB_ADDSTRING, 0, (LPARAM)L"原生阅读（默认，不生成 PDF，可用现有画笔）");
        SendMessageW(controls_[Mode], CB_ADDSTRING, 0, (LPARAM)L"PDF 兼容阅读（本机转换，可用 PDF 画笔）");
        label(L"两种方式均在软件内打开，并支持现有画笔、旧批注和橡皮。需要安装桌面 Word / "
              L"WPS。\r\n\r\n原生阅读由 Office "
              L"显示原文档，不生成 PDF；PDF 兼容阅读保留为可选项。\r\n\r\n公式和字体依赖原 Office "
              L"排版和本机字体。关闭文档后重新打开可切换方式。",
              28, 274, 560, 146, 1);
        control(L"LISTBOX", L"", LBS_NOTIFY | WS_VSCROLL | WS_BORDER | WS_TABSTOP, Actions, 28, 72, 300, 328,
                2);
        label(L"选择左侧功能，点击下框并按下快捷键：", 344, 76, 254, 55, 2);
        control(L"EDIT", L"", ES_READONLY | WS_BORDER | WS_TABSTOP, Capture, 344, 140, 250, 44, 2);
        SetWindowSubclass(controls_[Capture], captureProc, 1, (DWORD_PTR)this);
        control(L"BUTTON", L"绑定", WS_TABSTOP, Assign, 344, 200, 118, 44, 2);
        control(L"BUTTON", L"清除", WS_TABSTOP, Clear, 476, 200, 118, 44, 2);
        label(L"冲突时会提示。Esc、Enter、Alt+F4 与普通 Tab 保留用于窗口操作。", 344, 270, 250, 100, 2);
        label(L"用数理化示例，边看边练", 28, 76, 560, 32, 3);
        label(L"内置离线教程，不需要 Word。打开后使用顶部现有工具练习，用 Ctrl+S 保存批注。", 28, 114, 560, 52,
              3);
        label(L"阅读与触控：多标签、目录、缩略图、页码、缩放、查找与全屏。", 28, 180, 560, 52, 3);
        label(L"画笔与批注：画笔开关、颜色粗细、旧批注显示、橡皮、撤销与重做。", 28, 238, 560, 52, 3);
        label(L"实战练习：数学方程与函数图、物理受力图、化学方程式与分子结构。", 28, 296, 560, 52, 3);
        control(L"BUTTON", L"打开教程并练习", WS_TABSTOP, OpenTutorial, 28, 358, 270, 44, 3);
        label(L"清除弹窗的毛玻璃", 28, 76, 560, 28, 5);
        label(L"透明度", 28, 133, 128, 28, 5);
        control(TRACKBAR_CLASSW, L"毛玻璃透明度", TBS_HORZ | TBS_NOTICKS | WS_TABSTOP,
                FrostTransparency, 158, 120, 350, 48, 5);
        SendMessageW(controls_[FrostTransparency], TBM_SETRANGE, TRUE, MAKELPARAM(0, 100));
        control(L"STATIC", L"", SS_RIGHT, FrostPercentage, 525, 133, 56, 28, 5);
        label(L"透明度越高，模糊后的背景越明显。", 158, 173, 426, 32, 5);
        label(L"色调", 28, 232, 128, 28, 5);
        control(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_TABSTOP, FrostTintChoice, 158, 224, 240, 144, 5);
        for (auto text : {L"偏白", L"偏灰"})
            SendMessageW(controls_[FrostTintChoice], CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(text));
        control(L"BUTTON", L"恢复默认", WS_TABSTOP, Defaults, 28, 434, 130, 44);
        control(L"BUTTON", L"保存", WS_TABSTOP | BS_DEFPUSHBUTTON, Save, 374, 434, 108, 44);
        control(L"BUTTON", L"取消", WS_TABSTOP, Cancel, 492, 434, 108, 44);
        fill();
        page();
        EnableWindow(owner_, FALSE);
        ShowWindow(window_, SW_SHOW);
        SetFocus(controls_[Tabs]);
        MSG msg{};
        while (!done_) {
            int result = GetMessageW(&msg, nullptr, 0, 0);
            if (result <= 0) {
                if (!result)
                    PostQuitMessage((int)msg.wParam);
                break;
            }
            if (GetFocus() != controls_[Capture] && msg.message == WM_KEYDOWN && msg.wParam == VK_ESCAPE) {
                done_ = true;
                continue;
            }
            if (GetFocus() == controls_[Capture]) {
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            } else if (!IsDialogMessageW(window_, &msg)) {
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }
        }
        DestroyWindow(window_);
        DeleteObject(font_);
        EnableWindow(owner_, TRUE);
        SetForegroundWindow(owner_);
        if (accepted_)
            out = draft_;
        if (openTutorial)
            *openTutorial = tutorial_;
        return accepted_;
    }
};
} // namespace
bool showSettingsDialog(HWND owner, AppSettings &settings, bool *openTutorial) {
    return SettingsDialog(owner, settings).show(settings, openTutorial);
}
} // namespace paper
