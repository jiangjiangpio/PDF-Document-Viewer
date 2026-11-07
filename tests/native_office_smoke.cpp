// Native renderer contract test. A fake Office automation server supplies real
// page metafiles; no Word license or PDF conversion is needed for this test.
#include "office.hpp"
#include <fstream>
#include <iostream>
using namespace paper;
using automation::Value;
namespace {
using Args = std::vector<VARIANT>;
struct Dispatch final : IDispatch {
    ULONG refs = 1;
    std::vector<std::wstring> names;
    std::vector<std::function<Value(const Args &, WORD)>> methods;
    void method(std::wstring name, std::function<Value(const Args &, WORD)> f) {
        names.push_back(std::move(name));
        methods.push_back(std::move(f));
    }
    void property(std::wstring name, Value value) {
        method(std::move(name), [value](const Args &, WORD) { return value; });
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void **out) override {
        *out = nullptr;
        if (id == IID_IUnknown || id == IID_IDispatch) {
            *out = (IDispatch *)this;
            AddRef();
            return S_OK;
        }
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override {
        return ++refs;
    }
    ULONG STDMETHODCALLTYPE Release() override {
        auto n = --refs;
        if (!n)
            delete this;
        return n;
    }
    HRESULT STDMETHODCALLTYPE GetTypeInfoCount(UINT *out) override {
        *out = 0;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetTypeInfo(UINT, LCID, ITypeInfo **) override {
        return E_NOTIMPL;
    }
    HRESULT STDMETHODCALLTYPE GetIDsOfNames(REFIID, LPOLESTR *ids, UINT n, LCID, DISPID *out) override {
        for (UINT i = 0; i < n; i++) {
            auto found = std::find(names.begin(), names.end(), ids[i]);
            if (found == names.end())
                return DISP_E_UNKNOWNNAME;
            out[i] = (DISPID)(found - names.begin());
        }
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Invoke(DISPID id, REFIID, LCID, WORD flags, DISPPARAMS *params, VARIANT *out,
                                     EXCEPINFO *, UINT *) override {
        if (id < 0 || id >= (DISPID)methods.size())
            return DISP_E_MEMBERNOTFOUND;
        Args args;
        for (UINT i = params->cArgs; i > 0; --i)
            args.push_back(params->rgvarg[i - 1]);
        try {
            auto result = methods[id](args, flags);
            if (out)
                return VariantCopy(out, &result.v);
            return S_OK;
        } catch (...) {
            return DISP_E_EXCEPTION;
        }
    }
};
Value wrap(Dispatch *p) {
    Value v;
    v.v.vt = VT_DISPATCH;
    v.v.pdispVal = p;
    return v;
}
Value collection(std::vector<Value> values) {
    auto p = new Dispatch;
    p->property(L"Count", Value((int)values.size()));
    p->method(L"Item", [values](const Args &a, WORD) { return values.at(a.at(0).lVal - 1); });
    return wrap(p);
}
Value range(int start, int end, std::wstring text = L"") {
    auto p = new Dispatch;
    p->property(L"Start", Value(start));
    p->property(L"End", Value(end));
    p->property(L"Text", Value(text));
    return wrap(p);
}
Value drawingPage(int width, int height) {
    RECT frame{0, 0, (LONG)(width * 2540 / 72), (LONG)(height * 2540 / 72)};
    HDC dc = CreateEnhMetaFileW(nullptr, nullptr, &frame, L"PDF view native fixture\0Word page\0\0");
    SetMapMode(dc, MM_ANISOTROPIC);
    SetWindowExtEx(dc, width, height, nullptr);
    SetViewportExtEx(dc, width * 96 / 72, height * 96 / 72, nullptr);
    SetBkMode(dc, TRANSPARENT);
    TextOutW(dc, 40, 60, L"Equation native page", 20);
    MoveToEx(dc, 40, 120, nullptr);
    LineTo(dc, width - 40, 120);
    Rectangle(dc, 100, 180, 250, 300);
    auto metafile = CloseEnhMetaFile(dc);
    UINT size = GetEnhMetaFileBits(metafile, 0, nullptr);
    std::vector<BYTE> bits(size);
    GetEnhMetaFileBits(metafile, size, bits.data());
    DeleteEnhMetaFile(metafile);
    Value emf;
    emf.v.vt = VT_ARRAY | VT_UI1;
    emf.v.parray = SafeArrayCreateVector(VT_UI1, 0, size);
    void *data = nullptr;
    SafeArrayAccessData(emf.v.parray, &data);
    memcpy(data, bits.data(), size);
    SafeArrayUnaccessData(emf.v.parray);
    auto page = new Dispatch;
    page->property(L"Width", Value(width));
    page->property(L"Height", Value(height));
    page->property(L"EnhMetaFileBits", emf);
    return wrap(page);
}
} // namespace
int main() {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    int checks = 0;
    auto check = [&](bool ok, const char *name) {
        if (!ok)
            throw std::runtime_error(name);
        ++checks;
        std::cout << "PASS " << name << '\n';
    };
    auto root = std::filesystem::temp_directory_path() / L"PDFViewNativePageTest";
    std::filesystem::create_directories(root);
    auto source = root / L"native.docx";
    {
        std::ofstream out(source);
        out << "native DOCX fixture opened read-only; not a PDF";
    }
    int opened = 0, closed = 0, quit = 0;
    bool macrosOff = false, readOnly = false;
    auto pages = collection({drawingPage(612, 792), drawingPage(792, 612)});
    auto pane = new Dispatch;
    pane->property(L"Pages", pages);
    auto view = new Dispatch;
    view->property(L"Type", Value(3));
    auto window = new Dispatch;
    window->property(L"View", wrap(view));
    window->property(L"Panes", collection({wrap(pane)}));
    auto doc = new Dispatch;
    doc->property(L"Windows", collection({wrap(window)}));
    doc->property(L"Content", range(0, 2000));
    doc->method(L"Repaginate", [](const Args &, WORD) { return Value(); });
    doc->method(L"Close", [&](const Args &a, WORD) {
        if (a.at(0).lVal != 0)
            throw std::runtime_error("save requested");
        ++closed;
        return Value();
    });
    doc->method(L"GoTo", [](const Args &a, WORD) {
        int start = (a.at(2).lVal - 1) * 1000;
        return range(start, start);
    });
    doc->method(L"Range", [](const Args &a, WORD) {
        return range(a.at(0).lVal, a.at(1).lVal,
                     a.at(0).lVal == 0 ? L"Equation alpha β x²" : L"Equation chemistry H₂SO₄");
    });
    auto documents = new Dispatch;
    Value document = wrap(doc);
    documents->method(L"Open", [&](const Args &a, WORD) {
        ++opened;
        readOnly = macrosOff && a.at(2).vt == VT_BOOL && a.at(2).boolVal == VARIANT_TRUE &&
                   a.at(3).boolVal == VARIANT_FALSE;
        return document;
    });
    auto app = new Dispatch;
    app->property(L"Documents", wrap(documents));
    app->property(L"Visible", Value::boolean(false));
    app->property(L"DisplayAlerts", Value(0));
    app->method(L"AutomationSecurity", [&](const Args &a, WORD) {
        macrosOff = a.at(0).lVal == 3;
        return Value();
    });
    app->method(L"Quit", [&](const Args &, WORD) {
        ++quit;
        return Value();
    });
    Value application = wrap(app);
    try {
        auto busyDocs = new Dispatch;
        busyDocs->property(L"Count", Value(1));
        auto busyApp = new Dispatch;
        busyApp->property(L"Documents", wrap(busyDocs));
        auto busy = wrap(busyApp);
        bool rejected = false;
        try {
            automation::verifyIsolatedApplication(busy.object(), OfficeEngine::Wps);
        } catch (...) {
            rejected = true;
        }
        check(rejected, "shared WPS sessions are rejected without hiding or quitting existing documents");
        OfficePageBackend backend([&](OfficeEngine) { return application; });
        auto cancel = [] { return false; };
        auto info = backend.open(1, source.wstring(), OfficeEngine::Word, cancel);
        check(readOnly && opened == 1 && info.nativeOffice,
              "original DOCX opened directly read-only with macros disabled");
        check(info.sizes.size() == 2 && info.sizes[0].w == 612 && info.sizes[1].w == 792,
              "Word native pagination and page sizes preserved");
        auto tile = backend.render({1, 0, -720, 0, 0, 0}, cancel);
        size_t dark = 0;
        for (size_t i = 0; i < tile->pixels.size(); i += 4)
            if (tile->pixels[i] < 240)
                ++dark;
        check(tile && tile->width > 0 && dark > 100,
              "native page metafile renders visible content without PDF conversion");
        check(isOfficeDocument(L"TEST.DOC") && isOfficeDocument(L"TEST.DOCX"),
              "DOC and DOCX extensions are accepted case-insensitively");
        auto rotated = backend.render({1, 0, -720, 1, 0, 0}, cancel);
        check(rotated->width == tile->height && rotated->height == tile->width,
              "native rotation uses the same page coordinate system");
        auto detail = backend.render({1, 0, 128, 0, 0, 0}, cancel);
        check(detail && detail->width == 512 && detail->height == 512,
              "native high-resolution tiles render on demand");
        check(!backend.render({1, 0, 128, 0, 1, 1}, [] { return true; }),
              "obsolete native render can be cancelled");
        check(backend.search(1, L"Equation", cancel).size() == 2 &&
                  backend.text(1, 0).find(L"β") != std::wstring::npos,
              "native text copy and page search preserve Unicode");
        AnnotationDocument annotations;
        Stroke stroke;
        stroke.page = 1;
        stroke.points = {{20, 30}, {100, 120}};
        annotations.add(stroke);
        AnnotationRepository repository(root / L"annotations");
        repository.save(info.fingerprint, {annotations.pages(), {}});
        backend.close(1);
        auto renamed = root / L"renamed.docx";
        std::filesystem::copy_file(source, renamed, std::filesystem::copy_options::overwrite_existing);
        auto reopened = backend.open(2, renamed.wstring(), OfficeEngine::Word, cancel);
        check(reopened.fingerprint == info.fingerprint &&
                  repository.load(reopened.fingerprint).pages.at(1).size() == 1,
              "native pen annotations persist across rename and reopen");
        check(!std::filesystem::exists(root / L"native.pdf"), "native mode creates no intermediate PDF");
        auto legacy = root / L"legacy.doc";
        std::filesystem::copy_file(source, legacy, std::filesystem::copy_options::overwrite_existing);
        auto oldDoc = backend.open(3, legacy.wstring(), OfficeEngine::Word, cancel);
        check(oldDoc.nativeOffice && oldDoc.sizes.size() == 2,
              "legacy DOC uses the same native page and annotation pipeline");
        backend.close(2);
        check(quit == 1, "native documents share one Office instance until the last tab closes");
        backend.close(3);
        check(closed == 3 && quit == 2, "Office documents close without saving source changes");
        std::cout << checks << " native renderer checks passed\n";
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        std::filesystem::remove_all(root);
        CoUninitialize();
        return 1;
    }
    std::filesystem::remove_all(root);
    CoUninitialize();
    return 0;
}
