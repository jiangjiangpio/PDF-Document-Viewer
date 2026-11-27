// Native integration test: exercises real app message handlers with isolated scratch data.
#include "app.hpp"
#include "display.hpp"
#include "resources.h"
#include <dwmapi.h>
#include <fstream>
#include <thread>
#include <windowsx.h>
namespace {
HWND ownWindow(const wchar_t *name) {
    struct Search { const wchar_t *name; HWND found = nullptr; } search{name};
    EnumWindows([](HWND window, LPARAM parameter) -> BOOL {
        auto &s = *reinterpret_cast<Search *>(parameter);
        DWORD pid = 0;
        GetWindowThreadProcessId(window, &pid);
        wchar_t name[100]{};
        GetClassNameW(window, name, 100);
        if (pid == GetCurrentProcessId() && wcscmp(name, s.name) == 0) {
            s.found = window;
            return FALSE;
        }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&search));
    return search.found;
}
template<class Predicate> void waitFor(Predicate predicate, const char *message) {
    for (int i = 0; i < 250; ++i) {
        if (predicate()) return;
        Sleep(20);
    }
    throw std::runtime_error(message);
}
void click(HWND window, int id) {
    SendMessageW(window, WM_COMMAND, MAKEWPARAM(id, BN_CLICKED), reinterpret_cast<LPARAM>(GetDlgItem(window, id)));
}
std::wstring statusText(HWND window) {
    std::wstring result;
    EnumChildWindows(window, [](HWND child, LPARAM parameter) -> BOOL {
        wchar_t name[32]{}, text[2048]{};
        GetClassNameW(child, name, 32);
        if (wcscmp(name, L"Static") == 0 || wcscmp(name, L"STATIC") == 0) {
            GetWindowTextW(child, text, 2048);
            std::wstring value = text;
            if (value.find(L"阅读：") != std::wstring::npos || value.find(L"画笔：") != std::wstring::npos ||
                value.find(L"橡皮：") != std::wstring::npos)
                *reinterpret_cast<std::wstring *>(parameter) = value;
        }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&result));
    return result;
}
void draw(HWND canvas, int y) {
    RECT area{};
    GetClientRect(canvas, &area);
    int x = area.right / 2;
    SendMessageW(canvas, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(x, y));
    for (int offset = 10; offset <= 70; offset += 10)
        SendMessageW(canvas, WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(x + offset, y));
    SendMessageW(canvas, WM_LBUTTONUP, 0, MAKELPARAM(x + 70, y));
}
void captureClearDialog(HWND backdrop) {
    RECT frame{};
    GetWindowRect(backdrop, &frame);
    const int width = frame.right - frame.left, height = frame.bottom - frame.top;
    BITMAPINFO info{};
    info.bmiHeader = {sizeof(BITMAPINFOHEADER), width, -height, 1, 32, BI_RGB};
    void *pixels = nullptr;
    auto dc = CreateCompatibleDC(nullptr);
    auto bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
    if (dc && bitmap && pixels) {
        auto old = SelectObject(dc, bitmap);
        HDC screen = GetDC(nullptr);
        const bool captured = screen && BitBlt(dc, 0, 0, width, height, screen, frame.left, frame.top, SRCCOPY);
        if (screen) ReleaseDC(nullptr, screen);
        GdiFlush();
        BITMAPFILEHEADER header{};
        header.bfType = 0x4d42;
        header.bfOffBits = sizeof(header) + sizeof(info.bmiHeader);
        DWORD bytes = width * height * 4;
        header.bfSize = header.bfOffBits + bytes;
        if (captured) {
            std::ofstream out(paper::executableDirectory() / L"clear-ink-coverage.bmp", std::ios::binary);
            out.write(reinterpret_cast<const char *>(&header), sizeof(header));
            out.write(reinterpret_cast<const char *>(&info.bmiHeader), sizeof(info.bmiHeader));
            out.write(static_cast<const char *>(pixels), bytes);
        }
        SelectObject(dc, old);
    }
    if (bitmap) DeleteObject(bitmap);
    if (dc) DeleteDC(dc);
}
std::vector<paper::StrokePtr> strokes(const paper::Snapshot &snapshot) {
    std::vector<paper::StrokePtr> found;
    for (auto &[page, entries] : snapshot.pages)
        found.insert(found.end(), entries.begin(), entries.end());
    return found;
}
} // namespace
int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, LPWSTR, int show) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    OleInitialize(nullptr);
    auto root = std::filesystem::temp_directory_path() / (L"PDF-view-pen-ui-" + std::to_wstring(GetCurrentProcessId()));
    std::filesystem::create_directories(root);
    auto pdf = root / L"fixture.pdf";
    auto resource = FindResourceW(instance, MAKEINTRESOURCEW(IDR_TUTORIAL_PDF), RT_RCDATA);
    auto loaded = LoadResource(instance, resource);
    {
        std::ofstream out(pdf, std::ios::binary);
        out.write(static_cast<const char *>(LockResource(loaded)), SizeofResource(instance, resource));
    }
    auto key = paper::hashFile(pdf, [] { return false; });
    paper::AnnotationRepository repository(root / L"annotations");
    paper::AppSettings defaults;
    defaults.restoreSession = false;
    paper::SettingsRepository(root / L"settings.txt").save(defaults);
    int result = 1;
    const bool preview = GetEnvironmentVariableW(L"PAPERVIEW_CLEAR_DIALOG_PREVIEW", nullptr, 0) != 0;
    std::ofstream report(paper::executableDirectory() / L"pen-ui-report.txt");
    report << std::unitbuf;
    auto run = [&](bool restart) {
        bool passed = false;
        std::thread driver([&] {
            HWND window = nullptr;
            try {
                waitFor([&] { window = ownWindow(L"PaperViewMain"); return window && IsWindowEnabled(GetDlgItem(window, 102)) && statusText(window).find(L"阅读：") != std::wstring::npos; }, "document did not become writable");
                HWND canvas = FindWindowExW(window, nullptr, L"PaperViewCanvas", nullptr);
                if (GetDlgItem(window, 104)) throw std::runtime_error("new highlighter tool still present");
                click(window, 102);
                if (statusText(window).find(L"画笔：") == std::wstring::npos)
                    throw std::runtime_error("pen mode did not activate");
                draw(canvas, 160);
                Sleep(1200);
                if (strokes(repository.load(key)).size() != (restart ? 3u : 0u))
                    throw std::runtime_error("idle wrote unsaved ink");
                click(window,125);
                size_t expected = restart ? 4 : 1;
                waitFor([&] { return strokes(repository.load(key)).size() == expected; }, "pen stroke was not persisted");
                auto first = repository.load(key);
                auto density = paper::screenMetrics(canvas, paper::AppSettings{}).pixelsPerMm;
                auto all = strokes(first);
                auto actualMm = all.back()->width * first.view.zoom / density;
                if (std::abs(actualMm - (restart ? 1.3 : 2.)) > .001)
                    throw std::runtime_error("new screen width is incorrect");
                if (restart) {
                    if (all.back()->color != 0xe81818) throw std::runtime_error("pen color did not survive restart");
                    report << "PASS app restart restores global color and manual millimeter width\n";
                } else {
                    auto oldWidth = all.front()->width;
                    click(window, 109);
                    draw(canvas, 220);
                    click(window,125);
                    waitFor([&] { return strokes(repository.load(key)).size() == 2; }, "zoomed pen stroke was not persisted");
                    auto second = repository.load(key);
                    all = strokes(second);
                    if (std::abs(all.back()->width * second.view.zoom / density - 2.) > .001 ||
                        all.front()->width != oldWidth)
                        throw std::runtime_error("zoom compensation changed an old stroke or new width");
                    report << "PASS 2 mm screen width at two actual app zoom levels; old stroke unchanged\n";
                    PostMessageW(window, WM_COMMAND, 103, 0);
                    HWND palette = nullptr;
                    waitFor([&] { palette = ownWindow(L"PDFViewPenPalette"); return palette != nullptr; }, "palette not shown");
                    HWND slider = FindWindowExW(palette, nullptr, TRACKBAR_CLASSW, nullptr);
                    SendMessageW(slider, TBM_SETPOS, TRUE, 13);
                    SendMessageW(palette, WM_HSCROLL, TB_THUMBTRACK, reinterpret_cast<LPARAM>(slider));
                    click(palette, 207);
                    SendMessageW(palette, WM_CLOSE, 0, 0);
                    PostMessageW(palette, WM_NULL, 0, 0);
                    waitFor([&] { auto settings = paper::SettingsRepository(root / L"settings.txt").load(); return settings.penColor == 0xe81818 && std::abs(settings.penScreenWidthMm - 1.3) < .001; }, "palette preferences did not persist");
                    draw(canvas, 280);
                    click(window,125);
                    waitFor([&] { return strokes(repository.load(key)).size() == 3; }, "manual width stroke was not saved");
                    auto manual = repository.load(key);
                    all = strokes(manual);
                    if (std::abs(all.back()->width * manual.view.zoom / density - 1.3) > .001)
                        throw std::runtime_error("manual millimeter width was not used");
                    click(window, 105);
                    draw(canvas, 280);
                    click(window,125);
                    waitFor([&] { return strokes(repository.load(key)).size() == 2; }, "eraser did not replace pen");
                    click(window, 106);
                    click(window,125);
                    waitFor([&] { return strokes(repository.load(key)).size() == 3; }, "undo did not restore erased stroke");
                    click(window, 105);
                    draw(canvas, 340);
                    Sleep(100);
                    if (strokes(repository.load(key)).size() != 3) throw std::runtime_error("second eraser click did not return to reading");
                    click(window, 102);
                    click(window, 102);
                    draw(canvas, 360);
                    Sleep(100);
                    if (strokes(repository.load(key)).size() != 3) throw std::runtime_error("second pen click did not return to reading");
                    report << "PASS tool switching, erase/undo, repeated clicks return to reading, and palette persistence\n";
                    auto clearDialog = [&](int choice) {
                        if (preview && choice == IDCANCEL) {
                            MONITORINFO monitor{sizeof(monitor)};
                            GetMonitorInfoW(MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST), &monitor);
                            auto work = monitor.rcWork;
                            SetWindowPos(window, HWND_TOP, work.left + 20, work.top + 20,
                                         std::min(1280L, work.right - work.left - 40),
                                         std::min(850L, work.bottom - work.top - 40),
                                         SWP_NOACTIVATE | SWP_SHOWWINDOW);
                        }
                        PostMessageW(window, WM_COMMAND, 126, 0);
                        HWND dialog = nullptr;
                        waitFor([&] { dialog = ownWindow(L"#32770"); return dialog && IsWindowVisible(dialog); },
                                "clear dialog did not open");
                        if (GetWindow(dialog, GW_OWNER) != window || IsWindowEnabled(window) ||
                            !GetDlgItem(dialog, IDC_CLEAR_CURRENT_VIEW) || !GetDlgItem(dialog, IDC_CLEAR_CURRENT_PAGE) ||
                            !GetDlgItem(dialog, IDC_CLEAR_ENTIRE_DOCUMENT))
                            throw std::runtime_error("clear dialog is missing its modal owner or scope buttons");
                        RECT prior{};
                        for (int button : {IDC_CLEAR_CURRENT_VIEW, IDC_CLEAR_CURRENT_PAGE,
                                           IDC_CLEAR_ENTIRE_DOCUMENT, IDCANCEL}) {
                            RECT bounds{};
                            if (!GetWindowRect(GetDlgItem(dialog, button), &bounds) ||
                                (prior.right && (bounds.top != prior.top || bounds.left < prior.right)))
                                throw std::runtime_error("clear dialog actions are not on one bottom row");
                            prior = bounds;
                        }
                        if (preview && choice == IDCANCEL) {
                            auto backdrop = ownWindow(L"PaperViewFrostedBackdrop");
                            if (!backdrop || GetWindow(backdrop, GW_OWNER) != window ||
                                GetAncestor(backdrop, GA_ROOT) != backdrop)
                                throw std::runtime_error("frosted backdrop must cover the owner as a separate window");
                            auto above = [](HWND first, HWND second) {
                                for (auto item = GetWindow(first, GW_HWNDNEXT); item; item = GetWindow(item, GW_HWNDNEXT))
                                    if (item == second) return true;
                                return false;
                            };
                            if (!above(dialog, backdrop) || !above(backdrop, window))
                                throw std::runtime_error("incorrect dialog, backdrop and owner stacking order");
                            // Keep other applications out of the compositor screenshot of this test fixture.
                            for (HWND item : {backdrop, dialog})
                                SetWindowPos(item, HWND_TOPMOST, 0, 0, 0, 0,
                                             SWP_NOACTIVATE | SWP_NOMOVE | SWP_NOSIZE | SWP_NOOWNERZORDER);
                            DwmFlush();
                            auto center = [](HWND item) {
                                RECT r{};
                                GetWindowRect(item, &r);
                                return POINT{(r.left + r.right) / 2, (r.top + r.bottom) / 2};
                            };
                            auto thumbs = FindWindowExW(window, nullptr, L"ListBox", nullptr);
                            RECT content{}, frame{};
                            GetWindowRect(canvas, &content);
                            GetWindowRect(backdrop, &frame);
                            for (POINT p : {center(GetDlgItem(window, 102)), center(GetDlgItem(window, 115)),
                                            center(thumbs), POINT{content.right - 24, content.bottom - 24},
                                            POINT{frame.left + 68, frame.top + 16}}) {
                                auto hit = WindowFromPoint(p);
                                if (hit != backdrop) {
                                    wchar_t name[128]{};
                                    GetClassNameW(hit, name, 128);
                                    throw std::runtime_error("backdrop does not cover point " + std::to_string(p.x) +
                                                             "," + std::to_string(p.y) + ": " + paper::utf8(name));
                                }
                            }
                            if (GetAncestor(WindowFromPoint(center(dialog)), GA_ROOT) != dialog)
                                throw std::runtime_error("clear dialog is below the frosted backdrop");
                            // Wait for the system's dialog opening animation to finish.
                            Sleep(300);
                            DwmFlush();
                            captureClearDialog(backdrop);
                            report << "PASS compositor coverage of toolbar, thumbnails and canvas with dialog above\n";
                        }
                        PostMessageW(dialog, WM_COMMAND, choice, 0);
                        waitFor([&] { return !IsWindow(dialog) && IsWindowEnabled(window); },
                                "clear dialog did not return control to the document");
                        waitFor([&] { return !ownWindow(L"PaperViewFrostedBackdrop"); },
                                "frosted backdrop remained after closing the dialog");
                        if (preview && choice == IDCANCEL)
                            ShowWindow(window, SW_HIDE);
                    };
                    clearDialog(IDCANCEL);
                    click(window, 125);
                    if (strokes(repository.load(key)).size() != 3)
                        throw std::runtime_error("cancelling the clear dialog changed ink");
                    clearDialog(IDC_CLEAR_ENTIRE_DOCUMENT);
                    click(window, 125);
                    if (!strokes(repository.load(key)).empty())
                        throw std::runtime_error("clear dialog did not apply its selected scope");
                    click(window, 106);
                    click(window, 125);
                    if (strokes(repository.load(key)).size() != 3)
                        throw std::runtime_error("clearing from the dialog was not undoable");
                    report << "PASS clear dialog scopes, cancel and undo\n";
                }
                // A stroke entirely above the first paper edge must survive an explicit save.
                if (!restart) {
                    click(window,102);
                    SendMessageW(canvas,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(2,2));
                    SendMessageW(canvas,WM_LBUTTONUP,0,MAKELPARAM(6,2));
                    PostMessageW(window,WM_COMMAND,113,0);
                    HWND prompt=nullptr;
                    waitFor([&]{prompt=ownWindow(L"#32770");return prompt!=nullptr;}, "close did not prompt");
                    PostMessageW(prompt,WM_COMMAND,IDCANCEL,0);
                    waitFor([&]{return !IsWindow(prompt);},"could not cancel close");
                    click(window,125);
                    auto saved=strokes(repository.load(key));
                    if (saved.size()!=4 || (saved.back()->points.front().x>=0 && saved.back()->points.front().y>=0))
                        throw std::runtime_error("ink from grey canvas was lost");
                    click(window,106); click(window,125);
                    draw(canvas,180);
                    PostMessageW(window,WM_COMMAND,113,0);
                    waitFor([&]{prompt=ownWindow(L"#32770");return prompt!=nullptr;}, "discard did not prompt");
                    PostMessageW(prompt,WM_COMMAND,IDNO,0);
                    waitFor([&]{return !IsWindow(prompt);},"could not discard");
                    if (strokes(repository.load(key)).size()!=3) throw std::runtime_error("discard overwrote last saved annotations");
                    report << "PASS no idle autosave, grey-canvas ink, cancel close and discard preserve saved data\n";
                }
                passed = true;
            } catch (const std::exception &e) {
                report << "FAIL " << e.what() << '\n';
                if (auto dialog = ownWindow(L"#32770"))
                    SendMessageW(dialog, WM_CLOSE, 0, 0);
            }
            if (window) PostMessageW(window, WM_CLOSE, 0, 0);
        });
        {
            paper::AppWindow app(root / L"annotations");
            app.run(instance, SW_HIDE, {pdf.wstring()});
        }
        driver.join();
        return passed;
    };
    try {
        result = run(false) && run(true) ? 0 : 1;
    } catch (const std::exception &e) {
        report << "FAIL " << e.what() << '\n';
    }
    report << (result ? "RESULT=FAIL\n" : "RESULT=PASS\n");
    OleUninitialize();
    return result;
}
