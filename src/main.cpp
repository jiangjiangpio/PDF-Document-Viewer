#include "app.hpp"
#include <fstream>
#include <shellapi.h>
namespace paper {
static void writeBitmap(const std::filesystem::path &path, const Tile &t) {
    BITMAPFILEHEADER file{};
    BITMAPINFOHEADER info{};
    file.bfType = 0x4d42;
    file.bfOffBits = sizeof(file) + sizeof(info);
    file.bfSize = file.bfOffBits + static_cast<DWORD>(t.pixels.size());
    info.biSize = sizeof(info);
    info.biWidth = t.width;
    info.biHeight = -t.height;
    info.biPlanes = 1;
    info.biBitCount = 32;
    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<char *>(&file), sizeof(file));
    out.write(reinterpret_cast<char *>(&info), sizeof(info));
    out.write(reinterpret_cast<const char *>(t.pixels.data()), t.pixels.size());
}
int selfTest(const std::filesystem::path &path, const std::filesystem::path &output) {
    std::filesystem::create_directories(output);
    std::ofstream report(output / L"report.txt");
    try {
        PdfBackend backend(executableDirectory() / L"pdfium.dll");
        auto start = GetTickCount64();
        auto doc = backend.open(1, path.wstring(), "", [] { return false; });
        if (doc.sizes.empty())
            throw std::runtime_error("No pages loaded");
        report << "pages=" << doc.sizes.size() << "\nfingerprint=" << doc.fingerprint
               << "\nopen_ms=" << GetTickCount64() - start << "\n";
        for (int p = 0; p < std::min(3, static_cast<int>(doc.sizes.size())); ++p) {
            auto tile = backend.render({1, p, -1, 0, 0, 0}, [] { return false; });
            if (!tile || tile->pixels.empty())
                throw std::runtime_error("Page render empty");
            size_t nonwhite = 0;
            for (size_t i = 0; i < tile->pixels.size(); i += 4)
                if (tile->pixels[i] < 240 || tile->pixels[i + 1] < 240 || tile->pixels[i + 2] < 240)
                    ++nonwhite;
            if (nonwhite < 100)
                throw std::runtime_error("Expected visible PDF contents");
            writeBitmap(output / (L"page-" + std::to_wstring(p + 1) + L".bmp"), *tile);
            report << "page_" << p + 1 << "_nonwhite=" << nonwhite << "\n";
        }
        auto rotated = backend.render({1, 0, -1, 1, 0, 0}, [] { return false; });
        if (!rotated)
            throw std::runtime_error("Rotation render failed");
        writeBitmap(output / L"rotated.bmp", *rotated);
        auto detail = backend.render({1, 0, 128, 0, 0, 0}, [] { return false; });
        if (!detail || detail->width != 512)
            throw std::runtime_error("Tile render failed");
        writeBitmap(output / L"tile.bmp", *detail);
        auto hits = backend.search(1, L"Equation", [] { return false; });
        if (hits.empty())
            throw std::runtime_error("Text search failed");
        report << "search_hits=" << hits.size() << "\n";
        auto text = backend.text(1, 0);
        if (text.find(L"Equation") == std::wstring::npos)
            throw std::runtime_error("Text extraction failed");
        bool cancelled = false;
        auto cancelledTile = backend.render({1, 0, 256, 0, 0, 0}, [&] {
            cancelled = true;
            return true;
        });
        if (!cancelled || cancelledTile)
            throw std::runtime_error("Render cancellation failed");
        AnnotationDocument ink;
        Stroke stroke;
        stroke.page = 0;
        stroke.points = {{10, 20}, {40, 50}, {80, 70}};
        ink.add(stroke);
        AnnotationRepository repo(output / L"annotations");
        Snapshot snap{ink.pages(), {0, 1, .2, 1.5, false}};
        repo.save(doc.fingerprint, snap);
        auto restored = repo.load(doc.fingerprint);
        if (restored.pages.at(0).size() != 1 || restored.pages.at(0)[0]->points.size() != 3 ||
            restored.view.rotation != 1)
            throw std::runtime_error("Annotation persistence failed");
        backend.close(1);
        auto copy = output / L"renamed.pdf";
        std::filesystem::copy_file(path, copy, std::filesystem::copy_options::overwrite_existing);
        auto renamed = backend.open(2, copy.wstring(), "", [] { return false; });
        if (renamed.fingerprint != doc.fingerprint)
            throw std::runtime_error("Identity changed on rename");
        backend.close(2);
        report << "rotation=PASS\ntile_render=PASS\ntext_extraction=PASS\ncancellation=PASS\nannotation_"
                  "roundtrip=PASS\nrenamed_document_identity=PASS\nRESULT=PASS\n";
        return 0;
    } catch (const std::exception &e) {
        report << "RESULT=FAIL\nerror=" << e.what() << "\n";
        return 1;
    }
}
} // namespace paper
int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, LPWSTR, int show) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    int argc = 0;
    LPWSTR *argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    std::vector<std::wstring> args;
    for (int i = 1; i < argc; ++i)
        args.emplace_back(argv[i]);
    LocalFree(argv);
    if (args.size() >= 3 && args[0] == L"--self-test") {
        int result = paper::selfTest(args[1], args[2]);
        CoUninitialize();
        return result;
    }
    HANDLE mutex = CreateMutexW(nullptr, FALSE, L"Local\\PaperView.Application.v1");
    if (mutex && GetLastError() == ERROR_ALREADY_EXISTS) {
        HWND first = FindWindowW(L"PaperViewMain", nullptr);
        if (first) {
            for (auto &path : args) {
                COPYDATASTRUCT data{1, static_cast<DWORD>((path.size() + 1) * sizeof(wchar_t)), path.data()};
                DWORD_PTR result = 0;
                SendMessageTimeoutW(first, WM_COPYDATA, 0, reinterpret_cast<LPARAM>(&data), SMTO_ABORTIFHUNG,
                                    5000, &result);
            }
            ShowWindow(first, SW_RESTORE);
            SetForegroundWindow(first);
        }
        CloseHandle(mutex);
        CoUninitialize();
        return 0;
    }
    int result = 0;
    try {
        paper::AppWindow app;
        result = app.run(instance, show, args);
    } catch (const std::exception &e) {
        MessageBoxW(nullptr, paper::utf16(e.what()).c_str(), L"PaperView 启动失败", MB_OK | MB_ICONERROR);
        result = 1;
    }
    if (mutex)
        CloseHandle(mutex);
    CoUninitialize();
    return result;
}
