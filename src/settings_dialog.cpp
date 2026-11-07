#include "settings_dialog.hpp"
#include <commctrl.h>
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
    bool done_ = false, accepted_ = false;
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
    }
    void fill() {
        SendMessageW(controls_[Sidebar], BM_SETCHECK, draft_.openSidebar ? BST_CHECKED : BST_UNCHECKED, 0);
        SendMessageW(controls_[Restore], BM_SETCHECK, draft_.restoreSession ? BST_CHECKED : BST_UNCHECKED, 0);
        SendMessageW(controls_[Blur], CB_SETCURSEL, draft_.blurLevel, 0);
        SendMessageW(controls_[Scroll], TBM_SETPOS, TRUE, (int)std::lround(draft_.scrollSensitivity * 100));
        SendMessageW(controls_[Engine], CB_SETCURSEL, (int)draft_.officeEngine, 0);
        SendMessageW(controls_[Mode], CB_SETCURSEL, draft_.officePdfMode ? 1 : 0, 0);
        listActions();
    }
    void save() {
        draft_.openSidebar = SendMessageW(controls_[Sidebar], BM_GETCHECK, 0, 0) == BST_CHECKED;
        draft_.restoreSession = SendMessageW(controls_[Restore], BM_GETCHECK, 0, 0) == BST_CHECKED;
        draft_.blurLevel = (int)SendMessageW(controls_[Blur], CB_GETCURSEL, 0, 0);
        draft_.scrollSensitivity = SendMessageW(controls_[Scroll], TBM_GETPOS, 0, 0) / 100.;
        draft_.officeEngine = (OfficeEngine)SendMessageW(controls_[Engine], CB_GETCURSEL, 0, 0);
        draft_.officePdfMode = SendMessageW(controls_[Mode], CB_GETCURSEL, 0, 0) == 1;
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
        if (m == WM_NOTIFY && ((NMHDR *)b)->code == TCN_SELCHANGE) {
            self->page();
            return 0;
        }
        if (m == WM_COMMAND) {
            int id = LOWORD(a);
            if (id == Save)
                self->save();
            else if (id == Cancel)
                self->done_ = true;
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
    bool show(AppSettings &out) {
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
        for (auto name : {L"阅读与性能", L"Word / WPS", L"快捷键"}) {
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
        label(L"DOC / DOCX 排版引擎", 28, 78, 560, 28, 1);
        control(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_TABSTOP, Engine, 28, 120, 560, 220, 1);
        for (auto text : {L"自动（优先 Microsoft Word，其次 WPS）", L"Microsoft Word", L"WPS Office"})
            SendMessageW(controls_[Engine], CB_ADDSTRING, 0, (LPARAM)text);
        label(L"Word 文档打开方式（下次打开生效）", 28, 174, 560, 28, 1);
        control(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_TABSTOP, Mode, 28, 210, 560, 180, 1);
        SendMessageW(controls_[Mode], CB_ADDSTRING, 0, (LPARAM)L"原生阅读（默认，不生成 PDF，可用现有画笔）");
        SendMessageW(controls_[Mode], CB_ADDSTRING, 0, (LPARAM)L"PDF 兼容阅读（本机转换，可用 PDF 画笔）");
        label(L"两种方式均在软件内打开，并支持现有画笔、荧光笔和橡皮。需要安装桌面 Word / "
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
        return accepted_;
    }
};
} // namespace
bool showSettingsDialog(HWND owner, AppSettings &settings) {
    return SettingsDialog(owner, settings).show(settings);
}
} // namespace paper
