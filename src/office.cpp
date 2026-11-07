#include "office.hpp"
#include <cwctype>
namespace paper {
using automation::call;
using automation::get;
using automation::Value;
namespace {
std::wstring extension(const std::filesystem::path &path) {
    auto e = path.extension().wstring();
    for (auto &c : e)
        c = (wchar_t)towlower(c);
    return e;
}
Value item(IDispatch *collection, int index) {
    return call(collection, L"Item", DISPATCH_METHOD | DISPATCH_PROPERTYGET, {Value(index)});
}
} // namespace
bool isOfficeDocument(const std::filesystem::path &path) {
    auto e = extension(path);
    return e == L".doc" || e == L".docx";
}
bool isSupportedDocument(const std::filesystem::path &path) {
    return isOfficeDocument(path) || extension(path) == L".pdf";
}
OfficePageBackend::Document::~Document() {
    if (document.v.vt == VT_DISPATCH)
        try {
            call(document.object(), L"Close", DISPATCH_METHOD, {Value(0)});
        } catch (...) {
        }
}
OfficePageBackend::Application::~Application() {
    if (value.v.vt == VT_DISPATCH)
        try {
            call(value.object(), L"Quit", DISPATCH_METHOD, {Value(0)});
        } catch (...) {
        }
}

OfficePageBackend::~OfficePageBackend() {
    while (!documents_.empty())
        close(documents_.begin()->first);
}
DocumentInfo OfficePageBackend::open(int id, const std::wstring &path, OfficeEngine preferred,
                                     const std::function<bool()> &cancel) {
    if (!isOfficeDocument(path))
        throw PdfError("Not a DOC / DOCX document");
    const auto sourceIdentity = hashFile(path, cancel);
    auto engines = preferred == OfficeEngine::Auto
                       ? std::vector<OfficeEngine>{OfficeEngine::Word, OfficeEngine::Wps}
                       : std::vector<OfficeEngine>{preferred};
    std::string error;
    for (auto engine : engines) {
        if (cancel())
            throw PdfError("Opening cancelled");
        auto doc = std::make_unique<Document>();
        try {
            doc->app = applications_[engine].lock();
            if (!doc->app) {
                doc->app = std::make_shared<Application>(factory_(engine));
                applications_[engine] = doc->app;
            }
            doc->document = automation::openReadOnly(doc->app->value.object(), path);
            auto windows = get(doc->document.object(), L"Windows");
            auto window = item(windows.object(), 1);
            auto view = get(window.object(), L"View");
            call(view.object(), L"Type", DISPATCH_PROPERTYPUT, {Value(3)}); // print layout
            call(doc->document.object(), L"Repaginate", DISPATCH_METHOD);
            auto panes = get(window.object(), L"Panes");
            auto pane = item(panes.object(), 1);
            doc->pages = get(pane.object(), L"Pages");
            int count = (int)get(doc->pages.object(), L"Count").number();
            if (count < 1 || count > 20000)
                throw PdfError("Unsupported native Word page count");
            for (int p = 1; p <= count; ++p) {
                if (cancel())
                    throw PdfError("Opening cancelled");
                auto page = item(doc->pages.object(), p);
                Size size{get(page.object(), L"Width").number(), get(page.object(), L"Height").number()};
                if (!std::isfinite(size.w) || !std::isfinite(size.h) || size.w <= 0 || size.h <= 0 ||
                    size.w > 200000 || size.h > 200000)
                    throw PdfError("Invalid native Word page size");
                doc->sizes.push_back(size);
            }
            DocumentInfo info;
            info.id = id;
            info.path = path;
            info.sizes = doc->sizes;
            info.nativeOffice = true;
            info.officeEngine = engine == OfficeEngine::Word ? L"Microsoft Word" : L"WPS Office";
            info.fingerprint =
                hashText("native-office-v1:" + std::to_string((int)engine) + ":" + sourceIdentity);
            documents_[id] = std::move(doc);
            // Validate a page drawing now. Unsupported WPS versions fail clearly
            // rather than reporting a blank native view as a successful open.
            try {
                drawing(id, 0, cancel);
            } catch (...) {
                close(id);
                throw;
            }
            return info;
        } catch (const std::exception &e) {
            error += e.what();
            error += '\n';
        }
    }
    throw PdfError("Cannot read native Office page drawings. Install desktop Word/WPS with native page "
                   "rendering support, or explicitly select PDF compatibility mode in Settings.\n" +
                   error);
}
void OfficePageBackend::close(int id) {
    for (auto p = pages_.begin(); p != pages_.end();)
        if (p->document == id) {
            pageBytes_ -= p->bytes;
            DeleteEnhMetaFile(p->drawing);
            p = pages_.erase(p);
        } else
            ++p;
    documents_.erase(id);
}
HENHMETAFILE OfficePageBackend::drawing(int id, int index, const std::function<bool()> &cancel) {
    for (auto p = pages_.begin(); p != pages_.end(); ++p)
        if (p->document == id && p->index == index) {
            auto h = p->drawing;
            pages_.splice(pages_.end(), pages_, p);
            return h;
        }
    if (cancel())
        return nullptr;
    auto &doc = *documents_.at(id);
    auto page = item(doc.pages.object(), index + 1);
    auto data = get(page.object(), L"EnhMetaFileBits");
    if (data.v.vt != (VT_ARRAY | VT_UI1) || !data.v.parray || SafeArrayGetDim(data.v.parray) != 1)
        throw PdfError("Office page did not return native metafile bytes");
    LONG first = 0, last = -1;
    SafeArrayGetLBound(data.v.parray, 1, &first);
    SafeArrayGetUBound(data.v.parray, 1, &last);
    auto size = (long long)last - first + 1;
    if (size <= 0 || size > 64ll * 1024 * 1024)
        throw PdfError("Native Office page drawing is too large");
    BYTE *bits = nullptr;
    if (FAILED(SafeArrayAccessData(data.v.parray, (void **)&bits)))
        throw PdfError("Cannot read Office drawing");
    auto h = SetEnhMetaFileBits((UINT)size, bits);
    SafeArrayUnaccessData(data.v.parray);
    if (!h)
        throw PdfError("Invalid native Office metafile");
    while (!pages_.empty() && (pages_.size() >= 3 || pageBytes_ + (size_t)size > 96u * 1024 * 1024)) {
        pageBytes_ -= pages_.front().bytes;
        DeleteEnhMetaFile(pages_.front().drawing);
        pages_.pop_front();
    }
    pages_.push_back({id, index, h, (size_t)size});
    pageBytes_ += (size_t)size;
    return h;
}
std::shared_ptr<Tile> OfficePageBackend::render(TileKey key, const std::function<bool()> &cancel) {
    auto found = documents_.find(key.document);
    if (found == documents_.end() || key.page < 0 || key.page >= (int)found->second->sizes.size() || cancel())
        return {};
    auto original = found->second->sizes[key.page];
    auto extent = PageTransform(original, key.rotation).extent();
    double preview = key.scale <= -2 ? std::clamp(-key.scale, 240, 1440) : 720;
    double scale = key.scale < 0 ? std::min(2., preview / std::max(extent.w, extent.h)) : key.scale / 64.;
    int fullW = std::max(1, (int)std::ceil(extent.w * scale)),
        fullH = std::max(1, (int)std::ceil(extent.h * scale));
    int x = key.scale < 0 ? 0 : key.x * 512, y = key.scale < 0 ? 0 : key.y * 512;
    int w = key.scale < 0 ? fullW : std::min(512, fullW - x),
        h = key.scale < 0 ? fullH : std::min(512, fullH - y);
    if (w <= 0 || h <= 0)
        return {};
    auto metafile = drawing(key.document, key.page, cancel);
    if (!metafile || cancel())
        return {};
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = w;
    info.bmiHeader.biHeight = -h;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    void *pixels = nullptr;
    HDC dc = CreateCompatibleDC(nullptr);
    HBITMAP bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
    if (!dc || !bitmap || !pixels) {
        if (bitmap)
            DeleteObject(bitmap);
        if (dc)
            DeleteDC(dc);
        throw PdfError("Cannot allocate Office render tile");
    }
    HGDIOBJ old = SelectObject(dc, bitmap);
    memset(pixels, 255, (size_t)w * h * 4);
    SetGraphicsMode(dc, GM_ADVANCED);
    SetStretchBltMode(dc, HALFTONE);
    SetBrushOrgEx(dc, 0, 0, nullptr);
    XFORM transform{};
    switch ((key.rotation % 4 + 4) % 4) {
    case 0:
        transform = {1, 0, 0, 1, (FLOAT)-x, (FLOAT)-y};
        break;
    case 1:
        transform = {0, 1, -1, 0, (FLOAT)(fullW - x), (FLOAT)-y};
        break;
    case 2:
        transform = {-1, 0, 0, -1, (FLOAT)(fullW - x), (FLOAT)(fullH - y)};
        break;
    case 3:
        transform = {0, -1, 1, 0, (FLOAT)-x, (FLOAT)(fullH - y)};
        break;
    }
    SetWorldTransform(dc, &transform);
    RECT pageRect{0, 0, std::max(1L, (LONG)std::ceil(original.w * scale)),
                  std::max(1L, (LONG)std::ceil(original.h * scale))};
    BOOL rendered = PlayEnhMetaFile(dc, metafile, &pageRect);
    GdiFlush();
    auto tile = std::make_shared<Tile>();
    tile->width = w;
    tile->height = h;
    tile->pixels.resize((size_t)w * h * 4);
    memcpy(tile->pixels.data(), pixels, tile->pixels.size());
    SelectObject(dc, old);
    DeleteObject(bitmap);
    DeleteDC(dc);
    if (!rendered)
        throw PdfError("Native Office page drawing failed");
    if (cancel())
        return {};
    for (size_t i = 3; i < tile->pixels.size(); i += 4)
        tile->pixels[i] = 255;
    return tile;
}
std::wstring OfficePageBackend::text(int id, int page) {
    auto &doc = *documents_.at(id);
    if (page < 0 || page >= (int)doc.sizes.size())
        return {};
    auto start = call(doc.document.object(), L"GoTo", DISPATCH_METHOD, {Value(1), Value(1), Value(page + 1)});
    auto content = get(doc.document.object(), L"Content");
    int begin = (int)get(start.object(), L"Start").number();
    int end = (int)get(content.object(), L"End").number();
    if (page + 1 < (int)doc.sizes.size()) {
        auto next =
            call(doc.document.object(), L"GoTo", DISPATCH_METHOD, {Value(1), Value(1), Value(page + 2)});
        end = (int)get(next.object(), L"Start").number();
    }
    auto range = call(doc.document.object(), L"Range", DISPATCH_METHOD, {Value(begin), Value(end)});
    auto result = get(range.object(), L"Text").text();
    if (result.size() > 1000000)
        result.resize(1000000);
    return result;
}
std::vector<SearchHit> OfficePageBackend::search(int id, const std::wstring &query,
                                                 const std::function<bool()> &cancel) {
    if (query.empty())
        return {};
    std::wstring needle = query;
    for (auto &c : needle)
        c = (wchar_t)towlower(c);
    std::vector<SearchHit> hits;
    for (int p = 0; p < (int)documents_.at(id)->sizes.size() && hits.size() < 2000; ++p) {
        if (cancel())
            return {};
        auto page = text(id, p);
        for (auto &c : page)
            c = (wchar_t)towlower(c);
        if (page.find(needle) != std::wstring::npos)
            hits.push_back({p, {}});
    }
    return hits;
}
} // namespace paper
