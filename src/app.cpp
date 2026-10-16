#include "app.hpp"
#include <commdlg.h>
#include <fstream>
#include <shellapi.h>
#include <sstream>
#include <uxtheme.h>
#include <windowsx.h>
namespace paper {
namespace {
constexpr uint32_t colors[] = {0x111827, 0x2563eb, 0xdc2626, 0x16a34a, 0x9333ea, 0xfacc15, 0xf97316};
constexpr const wchar_t *colorNames[] = {L"黑色", L"蓝色", L"红色", L"绿色", L"紫色", L"黄色", L"橙色"};
constexpr float penWidths[] = {1.2f, 2.5f, 4.f, 7.f};
constexpr float markerWidths[] = {8.f, 14.f, 20.f, 28.f};
std::wstring windowText(HWND w) {
    int n = GetWindowTextLengthW(w);
    std::wstring s(n + 1, 0);
    GetWindowTextW(w, s.data(), n + 1);
    s.resize(n);
    return s;
}
void clipboard(HWND w, const std::wstring &text) {
    if (!OpenClipboard(w))
        return;
    HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, (text.size() + 1) * sizeof(wchar_t));
    if (mem) {
        void *p = GlobalLock(mem);
        if (p) {
            memcpy(p, text.c_str(), (text.size() + 1) * sizeof(wchar_t));
            GlobalUnlock(mem);
            EmptyClipboard();
            if (!SetClipboardData(CF_UNICODETEXT, mem))
                GlobalFree(mem);
        } else
            GlobalFree(mem);
    }
    CloseClipboard();
}
struct PromptState {
    HWND edit = nullptr;
    std::wstring value;
    bool done = false, ok = false, password = false;
};
LRESULT CALLBACK promptProc(HWND w, UINT m, WPARAM a, LPARAM b) {
    auto *s = reinterpret_cast<PromptState *>(GetWindowLongPtrW(w, GWLP_USERDATA));
    if (m == WM_NCCREATE) {
        s = static_cast<PromptState *>(reinterpret_cast<CREATESTRUCTW *>(b)->lpCreateParams);
        SetWindowLongPtrW(w, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(s));
    }
    if (s && m == WM_COMMAND && (LOWORD(a) == IDOK || LOWORD(a) == IDCANCEL)) {
        s->ok = LOWORD(a) == IDOK;
        s->value = windowText(s->edit);
        s->done = true;
        return 0;
    }
    if (s && m == WM_CLOSE) {
        s->done = true;
        return 0;
    }
    return DefWindowProcW(w, m, a, b);
}
std::optional<std::wstring> prompt(HWND parent, const std::wstring &title, const std::wstring &label,
                                   bool password) {
    static bool registered = false;
    if (!registered) {
        WNDCLASSW wc{};
        wc.lpfnWndProc = promptProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
        wc.lpszClassName = L"PaperViewPrompt";
        RegisterClassW(&wc);
        registered = true;
    }
    PromptState state;
    state.password = password;
    RECT pr{};
    GetWindowRect(parent, &pr);
    int scale = GetDpiForWindow(parent);
    auto p = [&](int x) { return MulDiv(x, scale, 96); };
    HWND w = CreateWindowExW(WS_EX_DLGMODALFRAME, L"PaperViewPrompt", title.c_str(), WS_CAPTION | WS_SYSMENU,
                             pr.left + (pr.right - pr.left - p(420)) / 2,
                             pr.top + (pr.bottom - pr.top - p(200)) / 2, p(420), p(210), parent, nullptr,
                             GetModuleHandleW(nullptr), &state);
    HFONT font = CreateFontW(-p(15), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, 0, 0,
                             CLEARTYPE_QUALITY, 0, L"Microsoft YaHei UI");
    auto ctl = [&](const wchar_t *cls, const wchar_t *text, DWORD style, int x, int y, int width, int height,
                   int id) {
        HWND c = CreateWindowExW(0, cls, text, WS_CHILD | WS_VISIBLE | style, p(x), p(y), p(width), p(height),
                                 w, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                                 GetModuleHandleW(nullptr), nullptr);
        SendMessageW(c, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        return c;
    };
    ctl(L"STATIC", label.c_str(), 0, 20, 15, 375, 28, 0);
    state.edit = ctl(L"EDIT", L"", WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL | (password ? ES_PASSWORD : 0), 20,
                     48, 375, 34, 10);
    ctl(L"BUTTON", L"确定", WS_TABSTOP | BS_DEFPUSHBUTTON, 190, 105, 98, 42, IDOK);
    ctl(L"BUTTON", L"取消", WS_TABSTOP, 298, 105, 98, 42, IDCANCEL);
    EnableWindow(parent, FALSE);
    ShowWindow(w, SW_SHOW);
    SetFocus(state.edit);
    MSG msg{};
    while (!state.done && GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (msg.message == WM_KEYDOWN && msg.wParam == VK_RETURN)
            SendMessageW(w, WM_COMMAND, IDOK, 0);
        else if (msg.message == WM_KEYDOWN && msg.wParam == VK_ESCAPE)
            SendMessageW(w, WM_COMMAND, IDCANCEL, 0);
        else if (!IsDialogMessageW(w, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    DestroyWindow(w);
    DeleteObject(font);
    EnableWindow(parent, TRUE);
    SetForegroundWindow(parent);
    return state.ok ? std::optional<std::wstring>(state.value) : std::nullopt;
}
} // namespace
AppWindow::AppWindow()
    : input_(InputCallbacks{[this](Point p) { beginStroke(p); }, [this](Point p) { appendStroke(p); },
                            [this] { commitStroke(); }, [this] { cancelStroke(); },
                            [this](Point d) {
                                if (auto *s = active()) {
                                    s->view.pan(d);
                                    refresh(true);
                                    viewChanged();
                                }
                            },
                            [this](Point anchor, double factor, Point delta) {
                                if (auto *s = active()) {
                                    s->view.setZoom(s->view.zoom * factor, anchor);
                                    s->view.pan(delta);
                                    refresh(true);
                                    viewChanged();
                                }
                            }}) {}
AppWindow::~AppWindow() {
    scheduler_.reset();
    saver_.reset();
    renderer_.reset();
    if (font_)
        DeleteObject(font_);
}
DocumentSession *AppWindow::active() {
    return active_ >= 0 && active_ < static_cast<int>(documents_.size()) ? documents_[active_].get()
                                                                         : nullptr;
}
DocumentSession *AppWindow::byId(int id) {
    for (auto &s : documents_)
        if (s->id == id)
            return s.get();
    return nullptr;
}
int AppWindow::run(HINSTANCE instance, int show, const std::vector<std::wstring> &paths) {
    INITCOMMONCONTROLSEX cc{sizeof(cc), ICC_TAB_CLASSES | ICC_TREEVIEW_CLASSES};
    InitCommonControlsEx(&cc);
    WNDCLASSEXW wc{sizeof(wc)};
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(1));
    wc.hIconSm = wc.hIcon;
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    wc.lpszClassName = L"PaperViewMain";
    RegisterClassExW(&wc);
    WNDCLASSEXW cv{sizeof(cv)};
    cv.lpfnWndProc = CanvasProc;
    cv.hInstance = instance;
    cv.hCursor = LoadCursor(nullptr, IDC_ARROW);
    cv.lpszClassName = L"PaperViewCanvas";
    RegisterClassExW(&cv);
    window_ = CreateWindowExW(0, wc.lpszClassName, L"PaperView · 教学 PDF 阅读器",
                              WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, CW_USEDEFAULT, CW_USEDEFAULT, 1280, 850,
                              nullptr, nullptr, instance, this);
    if (!window_)
        throw std::runtime_error("Cannot create application window");
    ShowWindow(window_, show);
    UpdateWindow(window_);
    if (paths.empty())
        loadSession();
    else
        for (auto &p : paths)
            openFile(p);
    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        if (!key(message)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }
    return static_cast<int>(message.wParam);
}
void AppWindow::createControls() {
    dpi_ = GetDpiForWindow(window_);
    storage_ = storageDirectory();
    tabs_ = CreateWindowExW(0, WC_TABCONTROLW, L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | TCS_TOOLTIPS, 0, 0,
                            0, 0, window_, nullptr, nullptr, nullptr);
    SendMessageW(tabs_, TCM_SETMINTABWIDTH, 0, px(160));
    auto button = [&](int id, const wchar_t *label, int width) {
        controls_[id] =
            CreateWindowExW(0, L"BUTTON", label, WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW, 0, 0, 0,
                            0, window_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), nullptr, nullptr);
        toolbar_.emplace_back(id, width);
    };
    button(Open, L"打开", 62);
    button(Sidebar, L"目录", 62);
    button(Draw, L"绘制", 64);
    button(Palette, L"▾", 40);
    button(Marker, L"荧光笔", 76);
    button(Eraser, L"橡皮", 62);
    button(Undo, L"撤销", 62);
    button(Redo, L"重做", 62);
    button(ZoomOut, L"−", 40);
    controls_[ZoomLabel] =
        CreateWindowExW(0, L"STATIC", L"100%", WS_CHILD | WS_VISIBLE | SS_CENTER | SS_CENTERIMAGE, 0, 0, 0, 0,
                        window_, nullptr, nullptr, nullptr);
    toolbar_.emplace_back(ZoomLabel, 58);
    button(ZoomIn, L"＋", 40);
    button(Fit, L"适应宽度", 90);
    button(Rotate, L"旋转", 60);
    controls_[PageEdit] =
        CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                        WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_NUMBER | ES_CENTER | ES_AUTOHSCROLL, 0, 0, 0,
                        0, window_, reinterpret_cast<HMENU>(PageEdit), nullptr, nullptr);
    toolbar_.emplace_back(PageEdit, 48);
    controls_[PageTotal] = CreateWindowExW(0, L"STATIC", L"/ 0", WS_CHILD | WS_VISIBLE | SS_CENTERIMAGE, 0, 0,
                                           0, 0, window_, nullptr, nullptr, nullptr);
    toolbar_.emplace_back(PageTotal, 60);
    button(Find, L"搜索", 60);
    button(CloseTab, L"关闭页签", 86);
    button(Fullscreen, L"全屏", 60);
    button(Help, L"帮助", 60);
    controls_[SearchEdit] =
        CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_TABSTOP | ES_AUTOHSCROLL, 0, 0, 0, 0,
                        window_, reinterpret_cast<HMENU>(SearchEdit), nullptr, nullptr);
    for (auto [id, label] : std::vector<std::pair<int, const wchar_t *>>{{SearchGo, L"查找"},
                                                                         {SearchPrev, L"上一个"},
                                                                         {SearchNext, L"下一个"},
                                                                         {OutlineTab, L"目录"},
                                                                         {ThumbnailTab, L"缩略图"}})
        controls_[id] =
            CreateWindowExW(0, L"BUTTON", label, WS_CHILD | WS_TABSTOP | BS_OWNERDRAW, 0, 0, 0, 0, window_,
                            reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), nullptr, nullptr);
    tree_ = CreateWindowExW(0, WC_TREEVIEWW, L"",
                            WS_CHILD | TVS_HASBUTTONS | TVS_HASLINES | TVS_LINESATROOT | TVS_SHOWSELALWAYS, 0,
                            0, 0, 0, window_, nullptr, nullptr, nullptr);
    TreeView_SetItemHeight(tree_, px(36));
    thumbs_ = CreateWindowExW(0, L"LISTBOX", L"",
                              WS_CHILD | WS_VSCROLL | LBS_NOTIFY | LBS_OWNERDRAWFIXED | LBS_NODATA |
                                  LBS_NOINTEGRALHEIGHT,
                              0, 0, 0, 0, window_, reinterpret_cast<HMENU>(99), nullptr, nullptr);
    SendMessageW(thumbs_, LB_SETITEMHEIGHT, 0, px(170));
    canvas_ = CreateWindowExW(0, L"PaperViewCanvas", L"", WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_TABSTOP, 0,
                              0, 0, 0, window_, nullptr, nullptr, this);
    status_ = CreateWindowExW(0, L"STATIC", L"打开 PDF，开始阅读与批注",
                              WS_CHILD | WS_VISIBLE | SS_LEFT | SS_CENTERIMAGE, 0, 0, 0, 0, window_, nullptr,
                              nullptr, nullptr);
    SetWindowTheme(tabs_, L"Explorer", nullptr);
    SetWindowTheme(tree_, L"Explorer", nullptr);
    DragAcceptFiles(window_, TRUE);
    renderer_ = std::make_unique<CanvasRenderer>(canvas_);
    scheduler_ = std::make_unique<RenderScheduler>(window_, executableDirectory() / L"pdfium.dll", storage_);
    saver_ = std::make_unique<SaveService>(window_, storage_);
    updateFont();
    SetTimer(window_, 3, 500, nullptr);
    layout();
    updateUi();
}
void AppWindow::updateFont() {
    HFONT previous = font_;
    font_ = CreateFontW(-px(14), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, 0, 0,
                        CLEARTYPE_QUALITY, 0, L"Microsoft YaHei UI");
    for (auto &[id, w] : controls_)
        SendMessageW(w, WM_SETFONT, reinterpret_cast<WPARAM>(font_), TRUE);
    for (HWND w : {tabs_, tree_, thumbs_, status_})
        SendMessageW(w, WM_SETFONT, reinterpret_cast<WPARAM>(font_), TRUE);
    if (previous)
        DeleteObject(previous);
    TreeView_SetItemHeight(tree_, px(36));
    SendMessageW(thumbs_, LB_SETITEMHEIGHT, 0, px(170));
}
void AppWindow::layout() {
    if (!canvas_)
        return;
    RECT area{};
    GetClientRect(window_, &area);
    int width = area.right, height = area.bottom;
    MoveWindow(tabs_, px(5), px(3), width - px(10), px(34), TRUE);
    int x = px(6), y = px(39), row = px(44);
    for (auto [id, w] : toolbar_) {
        int bw = px(w);
        if (x + bw > width - px(6)) {
            x = px(6);
            y += row + px(3);
        }
        MoveWindow(controls_[id], x, y, bw, row, TRUE);
        x += bw + px(3);
    }
    y += row + px(5);
    for (int id : {SearchEdit, SearchGo, SearchPrev, SearchNext})
        ShowWindow(controls_[id], find_ ? SW_SHOW : SW_HIDE);
    if (find_) {
        int ew = std::max(px(160), width - px(330));
        MoveWindow(controls_[SearchEdit], px(10), y, ew, px(36), TRUE);
        int xx = ew + px(18);
        for (int id : {SearchGo, SearchPrev, SearchNext}) {
            MoveWindow(controls_[id], xx, y, px(92), px(38), TRUE);
            xx += px(98);
        }
        y += px(44);
    }
    int side = sidebar_ ? std::min(px(sidebarWidth_), width / 3) : 0;
    int body = std::max(1, height - y - px(28));
    ShowWindow(controls_[OutlineTab], sidebar_ ? SW_SHOW : SW_HIDE);
    ShowWindow(controls_[ThumbnailTab], sidebar_ ? SW_SHOW : SW_HIDE);
    if (side) {
        MoveWindow(controls_[OutlineTab], px(5), y, side / 2 - px(6), px(42), TRUE);
        MoveWindow(controls_[ThumbnailTab], side / 2, y, side / 2 - px(5), px(42), TRUE);
        MoveWindow(tree_, 0, y + px(45), side - px(2), std::max(1, body - px(45)), TRUE);
        MoveWindow(thumbs_, 0, y + px(45), side - px(2), std::max(1, body - px(45)), TRUE);
    }
    ShowWindow(tree_, sidebar_ && !thumbnails_ ? SW_SHOW : SW_HIDE);
    ShowWindow(thumbs_, sidebar_ && thumbnails_ ? SW_SHOW : SW_HIDE);
    MoveWindow(canvas_, side, y, std::max(1, width - side), body, TRUE);
    MoveWindow(status_, px(10), height - px(26), width - px(20), px(24), TRUE);
    RECT cr{};
    GetClientRect(canvas_, &cr);
    for (auto &s : documents_)
        s->view.resize(cr.right, cr.bottom);
    renderer_->resize();
    refresh();
    InvalidateRect(window_, nullptr, TRUE);
}
void AppWindow::populateSidebar() {
    TreeView_DeleteAllItems(tree_);
    SendMessageW(thumbs_, LB_SETCOUNT, 0, 0);
    auto *s = active();
    if (!s || s->loading || !s->info)
        return;
    std::vector<HTREEITEM> levels(34, nullptr);
    for (auto &o : s->info->outline) {
        TVINSERTSTRUCTW insert{};
        int depth = std::clamp(o.depth, 0, 32);
        insert.hParent = depth && levels[depth - 1] ? levels[depth - 1] : TVI_ROOT;
        insert.hInsertAfter = TVI_LAST;
        insert.item.mask = TVIF_TEXT | TVIF_PARAM;
        insert.item.pszText = const_cast<wchar_t *>(o.title.c_str());
        insert.item.lParam = o.page;
        auto item = TreeView_InsertItem(tree_, &insert);
        levels[depth] = item;
    }
    if (s->info->outline.empty()) {
        TVINSERTSTRUCTW insert{};
        insert.hParent = TVI_ROOT;
        insert.hInsertAfter = TVI_LAST;
        insert.item.mask = TVIF_TEXT | TVIF_PARAM;
        insert.item.pszText = const_cast<wchar_t *>(L"此文档没有内置目录");
        insert.item.lParam = -1;
        TreeView_InsertItem(tree_, &insert);
    }
    SendMessageW(thumbs_, LB_SETCOUNT, s->view.count(), 0);
    SendMessageW(thumbs_, LB_SETCURSEL, s->view.currentPage(), 0);
}
void AppWindow::updateUi() {
    auto *s = active();
    bool ready = s && !s->loading && s->info;
    for (auto [id, w] : toolbar_)
        if (id != Open && id != Sidebar && id != Help && id != Fullscreen && id != ZoomLabel &&
            id != PageTotal)
            EnableWindow(controls_[id], ready || (id == CloseTab && s));
    for (int id : {Draw, Palette, Marker, Eraser})
        EnableWindow(controls_[id], ready && s->info->writable);
    EnableWindow(controls_[Undo], ready && s->annotations.canUndo());
    EnableWindow(controls_[Redo], ready && s->annotations.canRedo());
    if (GetFocus() != controls_[PageEdit])
        SetWindowTextW(controls_[PageEdit], ready ? std::to_wstring(s->view.currentPage() + 1).c_str() : L"");
    SetWindowTextW(controls_[PageTotal], ready ? (L"/ " + std::to_wstring(s->view.count())).c_str() : L"/ 0");
    SetWindowTextW(controls_[ZoomLabel],
                   ready ? (std::to_wstring(static_cast<int>(std::lround(s->view.zoom * 100))) + L"%").c_str()
                         : L"—");
    std::wstring status = s ? s->status : L"打开 PDF，开始阅读与批注 · Ctrl+O 打开 · F11 全屏";
    if (ready) {
        status += L"    ";
        status += drawing_ ? (tool_ == Tool::Eraser   ? L"橡皮：整笔擦除"
                              : tool_ == Tool::Marker ? L"荧光笔：单指绘制 / 双指移动缩放"
                                                      : L"画笔：单指绘制 / 双指移动缩放")
                           : L"阅读：单指拖动 / 双指缩放";
        if (!s->hits.empty())
            status +=
                L"    搜索 " + std::to_wstring(s->hitIndex + 1) + L" / " + std::to_wstring(s->hits.size());
    }
    SetWindowTextW(status_, status.c_str());
    for (int id : {Draw, Marker, Eraser, Sidebar, OutlineTab, ThumbnailTab})
        InvalidateRect(controls_[id], nullptr, TRUE);
    if (ready) {
        SCROLLINFO info{sizeof(info), SIF_RANGE | SIF_PAGE | SIF_POS};
        info.nMin = 0;
        info.nMax = 1000000;
        double h = std::max(s->view.contentHeight(), 1.);
        info.nPage = static_cast<UINT>(std::min(1000001., s->view.height / h * 1000000));
        info.nPos = static_cast<int>(s->view.scrollY / h * 1000000);
        SetScrollInfo(canvas_, SB_VERT, &info, TRUE);
    } else {
        SCROLLINFO info{sizeof(info), SIF_RANGE | SIF_PAGE | SIF_POS};
        info.nMax = 0;
        SetScrollInfo(canvas_, SB_VERT, &info, TRUE);
    }
}
void AppWindow::activate(int index) {
    input_.cancel();
    if (auto *old = active())
        save(*old);
    active_ = index;
    TabCtrl_SetCurSel(tabs_, index);
    if (auto *s = active()) {
        SetWindowTextW(window_,
                       (std::filesystem::path(s->path).filename().wstring() + L" — PaperView").c_str());
        if (s->info && s->info->outline.empty())
            thumbnails_ = true;
    }
    populateSidebar();
    layout();
    updateUi();
    SetFocus(canvas_);
}
void AppWindow::openFile(const std::wstring &path) {
    if (documents_.size() >= 32) {
        MessageBoxW(window_, L"同时最多打开 32 份文档，请先关闭部分标签页。", L"PaperView", MB_OK);
        return;
    }
    std::wstring full;
    try {
        full = std::filesystem::absolute(path).lexically_normal().wstring();
    } catch (...) {
        return;
    }
    for (size_t i = 0; i < documents_.size(); ++i)
        if (_wcsicmp(documents_[i]->path.c_str(), full.c_str()) == 0) {
            activate(static_cast<int>(i));
            return;
        }
    int id = nextId_++;
    documents_.push_back(std::make_unique<DocumentSession>(id, full));
    std::wstring name = std::filesystem::path(full).filename().wstring();
    TCITEMW item{};
    item.mask = TCIF_TEXT;
    item.pszText = name.data();
    TabCtrl_InsertItem(tabs_, static_cast<int>(documents_.size()) - 1, &item);
    activate(static_cast<int>(documents_.size()) - 1);
    scheduler_->open(id, full);
}
void AppWindow::closeTab(int index) {
    if (index < 0 || index >= static_cast<int>(documents_.size()))
        return;
    input_.cancel();
    auto &s = *documents_[index];
    save(s);
    saver_->flush();
    for (auto &r : saver_->take()) {
        if (auto *doc = byId(r.document)) {
            if (r.kind == WorkerResult::Kind::SaveFailed) {
                doc->saveFailed = true;
                doc->dirty = true;
                doc->status = L"保存失败，请按 Ctrl+S 重试";
            } else if (r.token == doc->revision) {
                doc->saveFailed = false;
                doc->status = L"已自动保存到本机";
            }
        }
    }
    if (s.saveFailed && MessageBoxW(window_, L"批注保存失败。仍然关闭此标签页会丢失未保存的修改。是否关闭？",
                                    L"批注未保存", MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) != IDYES)
        return;
    scheduler_->close(s.id);
    cache_.removeDocument(s.id);
    renderer_->clearDocument(s.id);
    for (auto i = failedTiles_.begin(); i != failedTiles_.end();)
        if (i->document == s.id)
            i = failedTiles_.erase(i);
        else
            ++i;
    documents_.erase(documents_.begin() + index);
    TabCtrl_DeleteItem(tabs_, index);
    active_ = documents_.empty() ? -1 : std::min(index, static_cast<int>(documents_.size()) - 1);
    activate(active_);
    saveSession();
}
void AppWindow::openDialog() {
    std::vector<wchar_t> buffer(65536, 0);
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = window_;
    ofn.lpstrFilter = L"PDF 文档 (*.pdf)\0*.pdf\0所有文件\0*.*\0";
    ofn.lpstrFile = buffer.data();
    ofn.nMaxFile = static_cast<DWORD>(buffer.size());
    ofn.Flags = OFN_EXPLORER | OFN_ALLOWMULTISELECT | OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (!GetOpenFileNameW(&ofn))
        return;
    std::wstring first = buffer.data();
    auto *p = buffer.data() + first.size() + 1;
    if (!*p)
        openFile(first);
    else
        while (*p) {
            openFile((std::filesystem::path(first) / p).wstring());
            p += wcslen(p) + 1;
        }
}
void AppWindow::save(DocumentSession &s) {
    if (s.loading || !s.info || !s.info->writable || !s.dirty)
        return;
    ++s.revision;
    saver_->save(s.id, s.info->fingerprint, {s.annotations.pages(), s.view.state()}, s.revision);
    s.status = L"正在保存本地批注…";
    s.dirty = false;
}
void AppWindow::viewChanged() {
    if (auto *s = active()) {
        s->dirty = true;
        SetTimer(window_, 2, 1000, nullptr);
    }
}
void AppWindow::refresh(bool interacting) {
    if (interacting) {
        interactionUntil_ = GetTickCount64() + 140;
        SetTimer(window_, 1, 150, nullptr);
    }
    schedule(interacting);
    InvalidateRect(canvas_, nullptr, FALSE);
    updateUi();
}
void AppWindow::schedule(bool previewOnly) {
    std::vector<RenderRequest> requests;
    auto *s = active();
    if (!s || s->loading || !s->info) {
        scheduler_->request({});
        return;
    }
    std::set<TileKey> seen;
    auto add = [&](TileKey key, int priority) {
        if (seen.insert(key).second && !cache_.get(key) && !failedTiles_.count(key))
            requests.push_back({key, priority});
    };
    int scale = std::max(1, static_cast<int>(std::lround(s->view.zoom * 64)));
    for (int page : s->view.visible(80)) {
        auto r = s->view.pageRect(page);
        add({s->id, page, -1, s->view.rotation, 0, 0}, 0);
        if (!previewOnly) {
            double factor = (scale / 64.) / s->view.zoom;
            int x0 = std::max(0, static_cast<int>(std::floor(-r.x * factor / 512))),
                y0 = std::max(0, static_cast<int>(std::floor(-r.y * factor / 512)));
            int x1 = static_cast<int>(std::ceil(std::min(r.w, s->view.width - r.x) * factor / 512)),
                y1 = static_cast<int>(std::ceil(std::min(r.h, s->view.height - r.y) * factor / 512));
            for (int y = y0; y < y1; ++y)
                for (int x = x0; x < x1; ++x)
                    add({s->id, page, scale, s->view.rotation, x, y}, 10 + std::abs(y - y0));
        }
    }
    if (sidebar_ && thumbnails_) {
        int top = static_cast<int>(SendMessageW(thumbs_, LB_GETTOPINDEX, 0, 0));
        RECT cr{};
        GetClientRect(thumbs_, &cr);
        int count = cr.bottom / std::max(1, px(170)) + 2;
        for (int p = std::max(0, top); p < std::min(s->view.count(), top + count); ++p)
            add({s->id, p, -1, s->view.rotation, 0, 0}, 100);
    }
    scheduler_->request(std::move(requests));
}
void AppWindow::paint() {
    PAINTSTRUCT ps{};
    BeginPaint(canvas_, &ps);
    if (renderer_->begin(ps.rcPaint)) {
        RECT client{};
        GetClientRect(canvas_, &client);
        renderer_->fill({0, 0, static_cast<double>(client.right), static_cast<double>(client.bottom)},
                        0xe8eaed);
        auto *s = active();
        if (!s) {
            renderer_->text(L"PaperView", {60, 65, 600, 40}, 0x1e293b);
            renderer_->text(L"将 PDF 拖到窗口，或点击“打开”\n\n顶部“绘制”按钮开启手写批注\n单指书写 · "
                            L"双指移动和缩放\n\n批注自动保存在这台电脑中",
                            {60, 120, 750, 300});
        } else if (s->loading)
            renderer_->text(L"正在打开文档…", {60, 70, 650, 80});
        else if (s->info) {
            int scale = std::max(1, static_cast<int>(std::lround(s->view.zoom * 64)));
            for (int p : s->view.visible()) {
                auto r = s->view.pageRect(p);
                renderer_->fill({r.x + 3, r.y + 3, r.w, r.h}, 0xcbd0d8);
                renderer_->fill(r, 0xffffff);
                renderer_->pushClip(r);
                TileKey preview{s->id, p, -1, s->view.rotation, 0, 0};
                auto low = cache_.get(preview);
                if (low)
                    renderer_->bitmap(preview, low, r);
                else
                    renderer_->text(failedTiles_.count(preview) ? L"此页渲染失败" : L"正在加载…",
                                    {r.x + 24, r.y + 24, r.w - 48, 40});
                double factor = s->view.zoom / (scale / 64.);
                int x0 = std::max(0, static_cast<int>(std::floor(-r.x / factor / 512))),
                    y0 = std::max(0, static_cast<int>(std::floor(-r.y / factor / 512)));
                int x1 = static_cast<int>(std::ceil(std::min(r.w, s->view.width - r.x) / factor / 512)),
                    y1 = static_cast<int>(std::ceil(std::min(r.h, s->view.height - r.y) / factor / 512));
                for (int y = y0; y < y1; ++y)
                    for (int x = x0; x < x1; ++x) {
                        TileKey key{s->id, p, scale, s->view.rotation, x, y};
                        auto tile = cache_.get(key);
                        if (tile)
                            renderer_->bitmap(key, tile,
                                              {r.x + x * 512 * factor, r.y + y * 512 * factor,
                                               tile->width * factor, tile->height * factor});
                    }
                if (s->hitIndex >= 0 && s->hitIndex < static_cast<int>(s->hits.size())) {
                    auto &hit = s->hits[s->hitIndex];
                    if (hit.page == p)
                        for (auto box : hit.boxes) {
                            auto a = s->view.screenPoint(p, {box.x, box.y}),
                                 b = s->view.screenPoint(p, {box.x + box.w, box.y + box.h});
                            renderer_->fill({std::min(a.x, b.x), std::min(a.y, b.y), std::abs(b.x - a.x),
                                             std::abs(b.y - a.y)},
                                            0xf59e0b, .3f);
                        }
                }
                for (auto &stroke : s->annotations.page(p))
                    if (!eraseIds_.count(stroke->id))
                        renderer_->stroke(s->id, *stroke, s->view);
                if (live_ && live_->page == p)
                    renderer_->stroke(s->id, *live_, s->view, true);
                renderer_->popClip();
            }
        }
        renderer_->end();
    } else
        FillRect(ps.hdc, &ps.rcPaint, reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1));
    EndPaint(canvas_, &ps);
}
void AppWindow::invalidateInk(Point a, Point b, double w) {
    int margin = static_cast<int>(std::ceil(w)) + 5;
    RECT r{static_cast<LONG>(std::floor(std::min(a.x, b.x))) - margin,
           static_cast<LONG>(std::floor(std::min(a.y, b.y))) - margin,
           static_cast<LONG>(std::ceil(std::max(a.x, b.x))) + margin,
           static_cast<LONG>(std::ceil(std::max(a.y, b.y))) + margin};
    InvalidateRect(canvas_, &r, FALSE);
}
void AppWindow::beginStroke(Point p) {
    cancelStroke();
    auto *s = active();
    if (!s || !s->info || !s->info->writable)
        return;
    auto location = s->view.pageAt(p);
    if (!location)
        return;
    lastPointer_ = p;
    if (tool_ == Tool::Eraser) {
        erasePage_ = location->first;
        appendStroke(p);
        return;
    }
    live_ = Stroke{};
    live_->page = location->first;
    live_->kind = tool_ == Tool::Marker ? PenKind::Highlighter : PenKind::Ink;
    live_->color = tool_ == Tool::Marker ? markerColor_ : inkColor_;
    live_->width = tool_ == Tool::Marker ? markerWidth_ : inkWidth_;
    live_->points.push_back(location->second);
    invalidateInk(p, p, live_->width * s->view.zoom);
}
void AppWindow::appendStroke(Point p) {
    auto *s = active();
    if (!s || !s->info)
        return;
    auto loc = s->view.pageAt(p);
    if (!loc)
        return;
    if (erasePage_ >= 0 && loc->first == erasePage_) {
        bool changed = false;
        for (auto &stroke : s->annotations.page(erasePage_))
            if (stroke->hit(loc->second, 12 / s->view.zoom))
                changed = eraseIds_.insert(stroke->id).second || changed;
        if (changed)
            InvalidateRect(canvas_, nullptr, FALSE);
        return;
    }
    if (!live_ || loc->first != live_->page)
        return;
    if (distance(live_->points.back(), loc->second) * s->view.zoom < .55)
        return;
    auto prev = s->view.screenPoint(live_->page, live_->points.back());
    live_->points.push_back(loc->second);
    invalidateInk(prev, p, live_->width * s->view.zoom);
    lastPointer_ = p;
}
void AppWindow::commitStroke() {
    auto *s = active();
    if (!s)
        return;
    bool changed = false;
    if (live_) {
        s->annotations.add(std::move(*live_));
        live_.reset();
        changed = true;
    }
    if (!eraseIds_.empty())
        changed =
            s->annotations.eraseIds(erasePage_, std::vector<uint64_t>(eraseIds_.begin(), eraseIds_.end())) ||
            changed;
    eraseIds_.clear();
    erasePage_ = -1;
    renderer_->clearLive();
    if (changed) {
        s->dirty = true;
        save(*s);
        updateUi();
    }
    InvalidateRect(canvas_, nullptr, FALSE);
}
void AppWindow::cancelStroke() {
    live_.reset();
    eraseIds_.clear();
    erasePage_ = -1;
    if (renderer_)
        renderer_->clearLive();
    if (canvas_)
        InvalidateRect(canvas_, nullptr, FALSE);
}
void AppWindow::processResults() {
    auto results = scheduler_->take();
    auto saved = saver_->take();
    while (!saved.empty()) {
        results.push_back(std::move(saved.front()));
        saved.pop_front();
    }
    for (auto &r : results) {
        auto *s = byId(r.document);
        switch (r.kind) {
        case WorkerResult::Kind::Opened: {
            if (!s) {
                scheduler_->close(r.document);
                break;
            }
            int duplicate = -1;
            for (size_t i = 0; i < documents_.size(); ++i)
                if (documents_[i].get() != s && documents_[i]->info &&
                    documents_[i]->info->fingerprint == r.info->fingerprint) {
                    duplicate = static_cast<int>(i);
                    break;
                }
            if (duplicate >= 0) {
                int existingId = documents_[duplicate]->id;
                for (size_t i = 0; i < documents_.size(); ++i)
                    if (documents_[i].get() == s) {
                        closeTab(static_cast<int>(i));
                        break;
                    }
                for (size_t i = 0; i < documents_.size(); ++i)
                    if (documents_[i]->id == existingId)
                        activate(static_cast<int>(i));
                break;
            }
            s->info = r.info;
            s->loading = false;
            s->view.setPages(r.info->sizes);
            s->view.restore(r.info->saved.view);
            s->annotations.restore(r.info->saved.pages);
            s->status = r.info->writable ? L"本地批注已就绪" : L"批注数据异常：只读";
            if (s == active()) {
                if (r.info->outline.empty())
                    thumbnails_ = true;
                populateSidebar();
                layout();
                refresh();
            }
            if (!r.info->warning.empty())
                MessageBoxW(window_, r.info->warning.c_str(), L"批注读取提示", MB_OK | MB_ICONWARNING);
            saveSession();
            break;
        }
        case WorkerResult::Kind::OpenFailed:
            if (s) {
                if (r.errorCode == 4) {
                    auto password = prompt(window_, L"受密码保护的 PDF", L"请输入文档打开密码", true);
                    if (password) {
                        scheduler_->open(s->id, s->path, utf8(*password));
                        break;
                    }
                } else {
                    std::wstring error = L"无法打开此 PDF。";
                    if (r.errorCode == 2)
                        error += L"请确认文件存在且可以读取。";
                    else if (r.errorCode == 3)
                        error += L"文件可能损坏或不是有效的 PDF。";
                    error += L"\n\n" + r.message;
                    MessageBoxW(window_, error.c_str(), L"打开失败", MB_OK | MB_ICONERROR);
                }
                for (size_t i = 0; i < documents_.size(); ++i)
                    if (documents_[i].get() == s) {
                        closeTab(static_cast<int>(i));
                        break;
                    }
            }
            break;
        case WorkerResult::Kind::Rendered:
            if (s) {
                cache_.put(r.key, r.tile);
                if (s == active()) {
                    auto page = s->view.pageRect(r.key.page);
                    RECT dirty{static_cast<LONG>(page.x), static_cast<LONG>(page.y),
                               static_cast<LONG>(page.x + page.w + 1),
                               static_cast<LONG>(page.y + page.h + 1)};
                    InvalidateRect(canvas_, &dirty, FALSE);
                    if (r.key.scale < 0)
                        InvalidateRect(thumbs_, nullptr, FALSE);
                }
            }
            break;
        case WorkerResult::Kind::RenderFailed:
            if (s) {
                failedTiles_.insert(r.key);
                s->status = L"部分页面渲染失败：" + r.message;
                InvalidateRect(canvas_, nullptr, FALSE);
            }
            break;
        case WorkerResult::Kind::Search:
            if (s && r.token == s->searchToken) {
                s->hits = std::move(r.hits);
                s->hitIndex = -1;
                s->status =
                    s->hits.empty() ? L"未找到匹配文字（扫描图片需要 OCR，本版不包含 OCR）" : L"搜索完成";
                if (s == active())
                    nextHit(1);
            }
            break;
        case WorkerResult::Kind::Text:
            if (!r.message.empty())
                clipboard(window_, r.message);
            else
                MessageBoxW(window_, L"此页没有可提取文字。扫描图片不能直接复制文字。", L"复制本页文字",
                            MB_OK);
            break;
        case WorkerResult::Kind::Saved:
            if (s && r.token == s->revision) {
                s->status = L"已自动保存到本机";
                s->saveFailed = false;
            }
            break;
        case WorkerResult::Kind::SaveFailed:
            if (s) {
                s->status = L"保存失败，请按 Ctrl+S 重试";
                s->dirty = true;
                s->saveFailed = true;
            }
            MessageBoxW(window_, (L"批注尚未保存，请检查磁盘空间或目录权限。\n" + r.message).c_str(),
                        L"保存失败", MB_OK | MB_ICONERROR);
            break;
        case WorkerResult::Kind::Fatal:
            MessageBoxW(window_, r.message.c_str(), L"运行错误", MB_OK | MB_ICONERROR);
            break;
        }
    }
    updateUi();
}
void AppWindow::search() {
    auto *s = active();
    if (!s || !s->info)
        return;
    std::wstring q = windowText(controls_[SearchEdit]);
    s->hits.clear();
    s->hitIndex = -1;
    ++s->searchToken;
    s->status = q.empty() ? L"请输入要查找的文字" : L"正在搜索文字…";
    scheduler_->search(s->id, q, s->searchToken);
    updateUi();
    InvalidateRect(canvas_, nullptr, FALSE);
}
void AppWindow::nextHit(int delta) {
    auto *s = active();
    if (!s || s->hits.empty())
        return;
    int count = static_cast<int>(s->hits.size());
    s->hitIndex = (s->hitIndex + delta + count) % count;
    auto &h = s->hits[s->hitIndex];
    s->view.goTo(h.page);
    if (!h.boxes.empty()) {
        auto b = h.boxes.front();
        auto p = s->view.screenPoint(h.page, {b.x, b.y});
        s->view.scrollY += p.y - s->view.height * .35;
        s->view.clamp();
    }
    refresh();
    viewChanged();
}
void AppWindow::scroll(WPARAM w) {
    auto *s = active();
    if (!s)
        return;
    auto &v = s->view;
    switch (LOWORD(w)) {
    case SB_LINEUP:
        v.scrollY -= px(45);
        break;
    case SB_LINEDOWN:
        v.scrollY += px(45);
        break;
    case SB_PAGEUP:
        v.scrollY -= v.height * .85;
        break;
    case SB_PAGEDOWN:
        v.scrollY += v.height * .85;
        break;
    case SB_TOP:
        v.scrollY = 0;
        break;
    case SB_BOTTOM:
        v.scrollY = v.contentHeight();
        break;
    case SB_THUMBTRACK:
    case SB_THUMBPOSITION: {
        SCROLLINFO i{sizeof(i), SIF_TRACKPOS};
        GetScrollInfo(canvas_, SB_VERT, &i);
        v.scrollY = i.nTrackPos / 1000000. * v.contentHeight();
        break;
    }
    default:
        return;
    }
    v.clamp();
    refresh(true);
    viewChanged();
}
void AppWindow::toggleFullscreen() {
    if (!fullscreen_) {
        GetWindowPlacement(window_, &placement_);
        SetWindowLongPtrW(window_, GWL_STYLE, GetWindowLongPtrW(window_, GWL_STYLE) & ~WS_OVERLAPPEDWINDOW);
        MONITORINFO mi{sizeof(mi)};
        GetMonitorInfoW(MonitorFromWindow(window_, MONITOR_DEFAULTTONEAREST), &mi);
        SetWindowPos(window_, HWND_TOP, mi.rcMonitor.left, mi.rcMonitor.top,
                     mi.rcMonitor.right - mi.rcMonitor.left, mi.rcMonitor.bottom - mi.rcMonitor.top,
                     SWP_FRAMECHANGED);
        fullscreen_ = true;
    } else {
        SetWindowLongPtrW(window_, GWL_STYLE, GetWindowLongPtrW(window_, GWL_STYLE) | WS_OVERLAPPEDWINDOW);
        SetWindowPlacement(window_, &placement_);
        SetWindowPos(window_, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
        fullscreen_ = false;
    }
}
void AppWindow::palette() {
    HMENU menu = CreatePopupMenu();
    uint32_t selected = tool_ == Tool::Marker ? markerColor_ : inkColor_;
    for (int i = 0; i < 7; ++i)
        AppendMenuW(menu, MF_OWNERDRAW | (colors[i] == selected ? MF_CHECKED : 0), 200 + i,
                    reinterpret_cast<LPCWSTR>(static_cast<INT_PTR>(200 + i)));
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    for (int i = 0; i < 4; ++i)
        AppendMenuW(menu, MF_OWNERDRAW, 300 + i, reinterpret_cast<LPCWSTR>(static_cast<INT_PTR>(300 + i)));
    RECT r{};
    GetWindowRect(controls_[Palette], &r);
    int choice = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_LEFTALIGN, r.left, r.bottom, 0, window_, nullptr);
    DestroyMenu(menu);
    if (choice >= 200 && choice < 207) {
        if (tool_ == Tool::Marker)
            markerColor_ = colors[choice - 200];
        else
            inkColor_ = colors[choice - 200];
    }
    if (choice >= 300 && choice < 304) {
        if (tool_ == Tool::Marker)
            markerWidth_ = markerWidths[choice - 300];
        else
            inkWidth_ = penWidths[choice - 300];
    }
    SetFocus(canvas_);
}
void AppWindow::command(int id) {
    auto *s = active();
    switch (id) {
    case Open:
        openDialog();
        return;
    case Sidebar:
        sidebar_ = !sidebar_;
        layout();
        return;
    case Fullscreen:
        toggleFullscreen();
        return;
    case Help:
        MessageBoxW(window_,
                    L"PaperView 0.1.0\n\n打开：Ctrl+O，也可拖入 "
                    L"PDF\n绘制按钮：开启／关闭手写\n荧光笔／橡皮：再次点击返回普通画笔\n单指：阅读时拖动，绘"
                    L"制时书写\n双指：移动与缩放；会取消未提交的单指笔迹\n橡皮：整笔擦除\n搜索：Ctrl+F，F3 "
                    L"下一个，Shift+F3 上一个\n撤销／重做：Ctrl+Z / Ctrl+Y\n缩放：Ctrl+滚轮，Ctrl+0 "
                    L"适应宽度\n复制本页文字：Ctrl+Shift+C\n标签页：Ctrl+Tab 切换，Ctrl+W "
                    L"关闭\n全屏：F11\n\n批注自动保存在本机，不改动原 "
                    L"PDF。\n数据目录：%LOCALAPPDATA%\\PaperView\\annotations\n扫描图片的 "
                    L"OCR、打印和批注导出不在本版范围。",
                    L"使用帮助", MB_OK);
        return;
    case OutlineTab:
        thumbnails_ = false;
        layout();
        return;
    case ThumbnailTab:
        thumbnails_ = true;
        layout();
        return;
    case Find:
        find_ = !find_;
        layout();
        if (find_)
            SetFocus(controls_[SearchEdit]);
        else
            SetFocus(canvas_);
        return;
    case CloseTab:
        closeTab(active_);
        return;
    case SearchGo:
        search();
        return;
    case SearchPrev:
        nextHit(-1);
        return;
    case SearchNext:
        nextHit(1);
        return;
    }
    if (!s || !s->info)
        return;
    input_.cancel();
    switch (id) {
    case Draw:
        drawing_ = !drawing_;
        break;
    case Marker:
        tool_ = tool_ == Tool::Marker ? Tool::Pen : Tool::Marker;
        drawing_ = true;
        break;
    case Eraser:
        tool_ = tool_ == Tool::Eraser ? Tool::Pen : Tool::Eraser;
        drawing_ = true;
        break;
    case Palette:
        palette();
        break;
    case Undo:
        if (s->annotations.undo()) {
            s->dirty = true;
            save(*s);
        }
        break;
    case Redo:
        if (s->annotations.redo()) {
            s->dirty = true;
            save(*s);
        }
        break;
    case ZoomOut:
        s->view.setZoom(s->view.zoom / 1.2, {s->view.width / 2, s->view.height / 2});
        viewChanged();
        break;
    case ZoomIn:
        s->view.setZoom(s->view.zoom * 1.2, {s->view.width / 2, s->view.height / 2});
        viewChanged();
        break;
    case Fit: {
        auto state = s->view.state();
        s->view.fitWidth();
        s->view.goTo(state.page);
        viewChanged();
        break;
    }
    case Rotate:
        s->view.setRotation(s->view.rotation + 1);
        viewChanged();
        break;
    }
    refresh();
    SetFocus(canvas_);
}
void AppWindow::measureItem(MEASUREITEMSTRUCT &i) {
    if (i.CtlType == ODT_MENU) {
        i.itemWidth = px(190);
        i.itemHeight = px(44);
    } else if (i.CtlType == ODT_LISTBOX)
        i.itemHeight = px(170);
}
void AppWindow::drawItem(const DRAWITEMSTRUCT &i) {
    if (i.CtlType == ODT_LISTBOX) {
        auto *s = active();
        HBRUSH bg = CreateSolidBrush(i.itemState & ODS_SELECTED ? RGB(220, 234, 252) : RGB(246, 247, 249));
        FillRect(i.hDC, &i.rcItem, bg);
        DeleteObject(bg);
        if (!s || !s->info || i.itemID >= static_cast<UINT>(s->view.count()))
            return;
        TileKey key{s->id, static_cast<int>(i.itemID), -1, s->view.rotation, 0, 0};
        auto tile = cache_.get(key);
        int cw = i.rcItem.right - i.rcItem.left, ch = i.rcItem.bottom - i.rcItem.top - px(28);
        if (tile) {
            double scale = std::min((cw - px(28)) / static_cast<double>(tile->width),
                                    (ch - px(12)) / static_cast<double>(tile->height));
            int w = static_cast<int>(tile->width * scale), h = static_cast<int>(tile->height * scale);
            BITMAPINFO bi{};
            bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
            bi.bmiHeader.biWidth = tile->width;
            bi.bmiHeader.biHeight = -tile->height;
            bi.bmiHeader.biPlanes = 1;
            bi.bmiHeader.biBitCount = 32;
            SetStretchBltMode(i.hDC, HALFTONE);
            StretchDIBits(i.hDC, i.rcItem.left + (cw - w) / 2, i.rcItem.top + px(6), w, h, 0, 0, tile->width,
                          tile->height, tile->pixels.data(), &bi, DIB_RGB_COLORS, SRCCOPY);
        }
        RECT text = i.rcItem;
        text.top = text.bottom - px(26);
        SetBkMode(i.hDC, TRANSPARENT);
        SetTextColor(i.hDC, RGB(51, 65, 85));
        SelectObject(i.hDC, font_);
        DrawTextW(i.hDC, std::to_wstring(i.itemID + 1).c_str(), -1, &text,
                  DT_CENTER | DT_SINGLELINE | DT_VCENTER);
        return;
    }
    if (i.CtlType == ODT_MENU) {
        HBRUSH bg = CreateSolidBrush(i.itemState & ODS_SELECTED ? RGB(225, 237, 252) : RGB(255, 255, 255));
        FillRect(i.hDC, &i.rcItem, bg);
        DeleteObject(bg);
        RECT r = i.rcItem;
        r.left += px(14);
        std::wstring text;
        if (i.itemID >= 200 && i.itemID < 207) {
            int c = i.itemID - 200;
            auto rgb = colors[c];
            HBRUSH swatch = CreateSolidBrush(RGB((rgb >> 16) & 255, (rgb >> 8) & 255, rgb & 255));
            RECT cr{r.left, r.top + px(12), r.left + px(20), r.bottom - px(12)};
            FillRect(i.hDC, &cr, swatch);
            DeleteObject(swatch);
            r.left += px(35);
            text = colorNames[c];
        } else if (i.itemID >= 300 && i.itemID < 304) {
            int n = i.itemID - 300;
            text = L"粗细 " + std::to_wstring(n + 1) + (n == 0 ? L" · 细" : n == 3 ? L" · 粗" : L"");
        }
        if (i.itemState & ODS_CHECKED)
            text += L"  ✓";
        SelectObject(i.hDC, font_);
        SetBkMode(i.hDC, TRANSPARENT);
        SetTextColor(i.hDC, RGB(30, 41, 59));
        DrawTextW(i.hDC, text.c_str(), -1, &r, DT_SINGLELINE | DT_VCENTER);
        return;
    }
    if (i.CtlType == ODT_BUTTON) {
        bool selected =
            (i.CtlID == Draw && drawing_) || (i.CtlID == Marker && drawing_ && tool_ == Tool::Marker) ||
            (i.CtlID == Eraser && drawing_ && tool_ == Tool::Eraser) || (i.CtlID == Sidebar && sidebar_) ||
            (i.CtlID == OutlineTab && !thumbnails_) || (i.CtlID == ThumbnailTab && thumbnails_);
        COLORREF bg = selected                     ? RGB(219, 234, 254)
                      : i.itemState & ODS_SELECTED ? RGB(225, 229, 235)
                                                   : RGB(248, 249, 251);
        HBRUSH brush = CreateSolidBrush(bg);
        FillRect(i.hDC, &i.rcItem, brush);
        DeleteObject(brush);
        SelectObject(i.hDC, font_);
        SetBkMode(i.hDC, TRANSPARENT);
        SetTextColor(i.hDC, i.itemState & ODS_DISABLED ? RGB(160, 165, 175)
                            : selected                 ? RGB(29, 78, 216)
                                                       : RGB(51, 65, 85));
        RECT r = i.rcItem;
        DrawTextW(i.hDC, windowText(i.hwndItem).c_str(), -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        if (i.itemState & ODS_FOCUS) {
            InflateRect(&r, -3, -3);
            DrawFocusRect(i.hDC, &r);
        }
        return;
    }
}
void AppWindow::loadSession() {
    try {
        std::ifstream f(storage_.parent_path() / L"session.txt", std::ios::binary);
        std::string line;
        int n = 0;
        while (n++ < 32 && std::getline(f, line)) {
            if (!line.empty()) {
                auto path = utf16(line);
                if (std::filesystem::exists(path))
                    openFile(path);
            }
        }
    } catch (...) {
    }
}
void AppWindow::saveSession() {
    try {
        std::filesystem::create_directories(storage_.parent_path());
        auto path = storage_.parent_path() / L"session.txt", temp = storage_.parent_path() / L"session.tmp";
        {
            std::ofstream f(temp, std::ios::binary);
            for (auto &s : documents_)
                f << utf8(s->path) << '\n';
            f.flush();
            if (!f)
                return;
        }
        MoveFileExW(temp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
    } catch (...) {
    }
}
void AppWindow::shutdown() {
    if (closing_)
        return;
    input_.cancel();
    for (auto &s : documents_)
        save(*s);
    saver_->flush();
    processResults();
    for (auto &s : documents_)
        if (s->saveFailed) {
            if (MessageBoxW(window_,
                            L"部分批注尚未成功保存。仍然关闭会丢失这些未保存的修改。\n是否仍然关闭？",
                            L"批注未保存", MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) != IDYES)
                return;
            break;
        }
    closing_ = true;
    saveSession();
    KillTimer(window_, 1);
    KillTimer(window_, 2);
    KillTimer(window_, 3);
    scheduler_.reset();
    saver_.reset();
    DestroyWindow(window_);
}
bool AppWindow::key(MSG &m) {
    if (m.message != WM_KEYDOWN)
        return false;
    bool ctrl = GetKeyState(VK_CONTROL) < 0, shift = GetKeyState(VK_SHIFT) < 0;
    auto *s = active();
    HWND focus = GetFocus();
    if (m.wParam == VK_RETURN && focus == controls_[PageEdit]) {
        if (s) {
            int p = _wtoi(windowText(focus).c_str());
            if (p >= 1 && p <= s->view.count()) {
                input_.cancel();
                s->view.goTo(p - 1);
                refresh();
                viewChanged();
            }
        }
        SetFocus(canvas_);
        return true;
    }
    if (m.wParam == VK_RETURN && focus == controls_[SearchEdit]) {
        search();
        return true;
    }
    if (ctrl && m.wParam == 'O') {
        command(Open);
        return true;
    }
    if (ctrl && m.wParam == 'F') {
        if (!find_)
            command(Find);
        SetFocus(controls_[SearchEdit]);
        SendMessageW(controls_[SearchEdit], EM_SETSEL, 0, -1);
        return true;
    }
    if (ctrl && m.wParam == 'W') {
        command(CloseTab);
        return true;
    }
    if (ctrl && m.wParam == VK_TAB && !documents_.empty()) {
        activate((active_ + (shift ? -1 : 1) + static_cast<int>(documents_.size())) %
                 static_cast<int>(documents_.size()));
        return true;
    }
    if (m.wParam == VK_F11) {
        toggleFullscreen();
        return true;
    }
    if (m.wParam == VK_F3) {
        nextHit(shift ? -1 : 1);
        return true;
    }
    if (m.wParam == VK_ESCAPE) {
        input_.cancel();
        if (fullscreen_)
            toggleFullscreen();
        else if (find_) {
            find_ = false;
            layout();
        } else {
            drawing_ = false;
            updateUi();
        }
        return true;
    }
    if (focus == controls_[SearchEdit] || focus == controls_[PageEdit])
        return false;
    if (ctrl && m.wParam == 'S') {
        if (s) {
            s->dirty = true;
            save(*s);
            updateUi();
        }
        return true;
    }
    if (ctrl && m.wParam == 'Z') {
        command(shift ? Redo : Undo);
        return true;
    }
    if (ctrl && m.wParam == 'Y') {
        command(Redo);
        return true;
    }
    if (ctrl && m.wParam == '0') {
        command(Fit);
        return true;
    }
    if (ctrl && (m.wParam == VK_OEM_PLUS || m.wParam == VK_ADD)) {
        command(ZoomIn);
        return true;
    }
    if (ctrl && (m.wParam == VK_OEM_MINUS || m.wParam == VK_SUBTRACT)) {
        command(ZoomOut);
        return true;
    }
    if (ctrl && shift && m.wParam == 'C') {
        if (s && s->info)
            scheduler_->copyText(s->id, s->view.currentPage());
        return true;
    }
    if (s && s->info && focus == canvas_) {
        switch (m.wParam) {
        case VK_NEXT:
            scroll(SB_PAGEDOWN);
            return true;
        case VK_PRIOR:
            scroll(SB_PAGEUP);
            return true;
        case VK_DOWN:
            scroll(SB_LINEDOWN);
            return true;
        case VK_UP:
            scroll(SB_LINEUP);
            return true;
        case VK_HOME:
            scroll(SB_TOP);
            return true;
        case VK_END:
            scroll(SB_BOTTOM);
            return true;
        default:
            break;
        }
    }
    return false;
}
LRESULT CALLBACK AppWindow::WindowProc(HWND w, UINT m, WPARAM a, LPARAM b) {
    auto *self = reinterpret_cast<AppWindow *>(GetWindowLongPtrW(w, GWLP_USERDATA));
    if (m == WM_NCCREATE) {
        self = static_cast<AppWindow *>(reinterpret_cast<CREATESTRUCTW *>(b)->lpCreateParams);
        self->window_ = w;
        SetWindowLongPtrW(w, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    if (!self)
        return DefWindowProcW(w, m, a, b);
    try {
        return self->message(m, a, b);
    } catch (const std::exception &e) {
        MessageBoxW(w, utf16(e.what()).c_str(), L"PaperView 错误", MB_OK | MB_ICONERROR);
        return m == WM_CREATE ? -1 : 0;
    }
}
LRESULT CALLBACK AppWindow::CanvasProc(HWND w, UINT m, WPARAM a, LPARAM b) {
    auto *self = reinterpret_cast<AppWindow *>(GetWindowLongPtrW(w, GWLP_USERDATA));
    if (m == WM_NCCREATE) {
        self = static_cast<AppWindow *>(reinterpret_cast<CREATESTRUCTW *>(b)->lpCreateParams);
        self->canvas_ = w;
        SetWindowLongPtrW(w, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    if (!self)
        return DefWindowProcW(w, m, a, b);
    return self->canvasMessage(m, a, b);
}
LRESULT AppWindow::message(UINT m, WPARAM a, LPARAM b) {
    switch (m) {
    case WM_CREATE:
        createControls();
        return 0;
    case WM_SIZE:
        if (a != SIZE_MINIMIZED)
            layout();
        return 0;
    case WM_GETMINMAXINFO: {
        auto *i = reinterpret_cast<MINMAXINFO *>(b);
        i->ptMinTrackSize = {px(740), px(520)};
        return 0;
    }
    case WM_DPICHANGED: {
        dpi_ = HIWORD(a);
        updateFont();
        auto *r = reinterpret_cast<RECT *>(b);
        SetWindowPos(window_, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        layout();
        return 0;
    }
    case WM_COMMAND:
        if (LOWORD(a) == 99 && HIWORD(a) == LBN_SELCHANGE) {
            if (auto *s = active()) {
                int p = static_cast<int>(SendMessageW(thumbs_, LB_GETCURSEL, 0, 0));
                s->view.goTo(p);
                refresh();
                viewChanged();
            }
            return 0;
        }
        if (HIWORD(a) == BN_CLICKED) {
            command(LOWORD(a));
            return 0;
        }
        break;
    case WM_NOTIFY: {
        auto *header = reinterpret_cast<NMHDR *>(b);
        if (header->hwndFrom == tabs_ && header->code == TCN_SELCHANGE) {
            activate(TabCtrl_GetCurSel(tabs_));
            return 0;
        }
        if (header->hwndFrom == tree_ && header->code == TVN_SELCHANGEDW) {
            auto *n = reinterpret_cast<NMTREEVIEWW *>(b);
            if (auto *s = active(); s && n->itemNew.lParam >= 0) {
                s->view.goTo(static_cast<int>(n->itemNew.lParam));
                refresh();
                viewChanged();
            }
            return 0;
        }
        break;
    }
    case WM_DRAWITEM:
        drawItem(*reinterpret_cast<DRAWITEMSTRUCT *>(b));
        return TRUE;
    case WM_MEASUREITEM:
        measureItem(*reinterpret_cast<MEASUREITEMSTRUCT *>(b));
        return TRUE;
    case WM_COPYDATA: {
        auto *d = reinterpret_cast<COPYDATASTRUCT *>(b);
        if (d && d->dwData == 1 && d->lpData && d->cbData >= 2 && d->cbData <= 65536 && d->cbData % 2 == 0) {
            auto *p = static_cast<const wchar_t *>(d->lpData);
            if (p[d->cbData / 2 - 1] == 0)
                openFile(p);
        }
        return TRUE;
    }
    case WM_DROPFILES: {
        HDROP drop = reinterpret_cast<HDROP>(a);
        UINT n = DragQueryFileW(drop, 0xffffffff, nullptr, 0);
        for (UINT i = 0; i < n; ++i) {
            UINT size = DragQueryFileW(drop, i, nullptr, 0);
            std::wstring path(size + 1, 0);
            DragQueryFileW(drop, i, path.data(), size + 1);
            path.resize(size);
            openFile(path);
        }
        DragFinish(drop);
        return 0;
    }
    case RenderScheduler::ReadyMessage:
        if (scheduler_ && saver_)
            processResults();
        return 0;
    case WM_TIMER:
        if (a == 1) {
            if (GetTickCount64() >= interactionUntil_) {
                KillTimer(window_, 1);
                schedule(false);
            }
        } else if (a == 2) {
            KillTimer(window_, 2);
            if (auto *s = active())
                save(*s);
            updateUi();
        } else if (a == 3 && sidebar_ && thumbnails_) {
            int top = static_cast<int>(SendMessageW(thumbs_, LB_GETTOPINDEX, 0, 0));
            if (top != lastThumbTop_) {
                lastThumbTop_ = top;
                schedule(GetTickCount64() < interactionUntil_);
            }
        }
        return 0;
    case WM_ACTIVATEAPP:
        if (!a)
            input_.cancel();
        break;
    case WM_ERASEBKGND: {
        RECT r{};
        GetClientRect(window_, &r);
        HBRUSH brush = CreateSolidBrush(RGB(248, 249, 251));
        FillRect(reinterpret_cast<HDC>(a), &r, brush);
        DeleteObject(brush);
        return 1;
    }
    case WM_CLOSE:
        shutdown();
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(window_, m, a, b);
}
LRESULT AppWindow::canvasMessage(UINT m, WPARAM a, LPARAM b) {
    auto *s = active();
    switch (m) {
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT:
        if (renderer_)
            paint();
        else {
            PAINTSTRUCT p{};
            BeginPaint(canvas_, &p);
            EndPaint(canvas_, &p);
        }
        return 0;
    case WM_SIZE:
        if (renderer_)
            renderer_->resize();
        return 0;
    case WM_VSCROLL:
        scroll(a);
        return 0;
    case WM_MOUSEWHEEL:
        if (s && s->info) {
            input_.cancel();
            if (GET_KEYSTATE_WPARAM(a) & MK_CONTROL) {
                POINT p{GET_X_LPARAM(b), GET_Y_LPARAM(b)};
                ScreenToClient(canvas_, &p);
                s->view.setZoom(s->view.zoom * std::pow(1.15, GET_WHEEL_DELTA_WPARAM(a) / 120.),
                                {static_cast<double>(p.x), static_cast<double>(p.y)});
            } else
                s->view.scrollY -= GET_WHEEL_DELTA_WPARAM(a) / 120. * px(120);
            s->view.clamp();
            refresh(true);
            viewChanged();
        }
        return 0;
    case WM_MOUSEHWHEEL:
        if (s) {
            s->view.scrollX += GET_WHEEL_DELTA_WPARAM(a) / 120. * px(100);
            s->view.clamp();
            refresh(true);
            viewChanged();
        }
        return 0;
    case WM_LBUTTONDOWN:
    case WM_MBUTTONDOWN:
        if ((GetMessageExtraInfo() & 0xffffff00) == 0xff515700)
            return 0;
        SetFocus(canvas_);
        SetCapture(canvas_);
        input_.down(0, {static_cast<double>(GET_X_LPARAM(b)), static_cast<double>(GET_Y_LPARAM(b))},
                    m == WM_LBUTTONDOWN && drawing_);
        return 0;
    case WM_MOUSEMOVE:
        if ((GetMessageExtraInfo() & 0xffffff00) == 0xff515700)
            return 0;
        if (GetCapture() == canvas_)
            input_.move(0, {static_cast<double>(GET_X_LPARAM(b)), static_cast<double>(GET_Y_LPARAM(b))});
        return 0;
    case WM_LBUTTONUP:
    case WM_MBUTTONUP:
        if ((GetMessageExtraInfo() & 0xffffff00) == 0xff515700)
            return 0;
        input_.up(0, {static_cast<double>(GET_X_LPARAM(b)), static_cast<double>(GET_Y_LPARAM(b))});
        if (GetCapture() == canvas_)
            ReleaseCapture();
        return 0;
    case WM_CAPTURECHANGED:
        input_.cancel();
        return 0;
    case WM_POINTERDOWN:
    case WM_POINTERUPDATE:
    case WM_POINTERUP: {
        UINT32 id = GET_POINTERID_WPARAM(a);
        POINTER_INFO info{};
        if (!GetPointerInfo(id, &info) || info.pointerType == PT_MOUSE)
            break;
        POINT p = info.ptPixelLocation;
        ScreenToClient(canvas_, &p);
        Point pos{static_cast<double>(p.x), static_cast<double>(p.y)};
        if (info.pointerFlags & POINTER_FLAG_CANCELED) {
            input_.cancel();
            return 0;
        }
        if (m == WM_POINTERDOWN) {
            SetFocus(canvas_);
            input_.down(id, pos, drawing_);
        } else if (m == WM_POINTERUPDATE)
            input_.move(id, pos);
        else
            input_.up(id, pos);
        return 0;
    }
    case WM_POINTERCAPTURECHANGED:
    case WM_CANCELMODE:
        input_.cancel();
        return 0;
    case WM_SETCURSOR:
        if (LOWORD(b) == HTCLIENT) {
            SetCursor(LoadCursor(nullptr, drawing_ ? IDC_CROSS : IDC_ARROW));
            return TRUE;
        }
        break;
    case WM_GETDLGCODE:
        return DLGC_WANTARROWS;
    }
    return DefWindowProcW(canvas_, m, a, b);
}
} // namespace paper
