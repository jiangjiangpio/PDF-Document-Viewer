#include "backend.hpp"
#include <bcrypt.h>
#include <iomanip>
#include <set>
#include <shlobj.h>
#include <sstream>
namespace paper {
std::wstring utf16(const std::string &s) {
    if (s.empty())
        return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring r(n, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), r.data(), n);
    return r;
}
std::string utf8(const std::wstring &s) {
    if (s.empty())
        return {};
    int n =
        WideCharToMultiByte(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0, nullptr, nullptr);
    std::string r(n, 0);
    WideCharToMultiByte(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), r.data(), n, nullptr, nullptr);
    return r;
}
std::filesystem::path executableDirectory() {
    std::wstring p(32768, 0);
    auto n = GetModuleFileNameW(nullptr, p.data(), static_cast<DWORD>(p.size()));
    p.resize(n);
    return std::filesystem::path(p).parent_path();
}
std::filesystem::path storageDirectory() {
    PWSTR p = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &p)))
        throw std::runtime_error("Local app data folder unavailable");
    std::filesystem::path path = p;
    CoTaskMemFree(p);
    return path / L"PaperView" / L"annotations";
}
PdfBackend::PdfBackend(const std::filesystem::path &dll) {
    library_ = LoadLibraryExW(dll.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (!library_)
        throw PdfError("Cannot load bundled pdfium.dll");
    try {
#define PDF_API(ret, name, args)                                                                             \
    name = reinterpret_cast<decltype(name)>(GetProcAddress(library_, #name));                                \
    if (!name)                                                                                               \
        throw PdfError("Missing PDFium function: " #name);
#include "pdf_api.inc"
#undef PDF_API
        FPDF_InitLibrary();
    } catch (...) {
        FreeLibrary(library_);
        library_ = nullptr;
        throw;
    }
}
PdfBackend::~PdfBackend() {
    while (!documents_.empty())
        close(documents_.begin()->first);
    if (library_) {
        FPDF_DestroyLibrary();
        FreeLibrary(library_);
    }
}
int __cdecl PdfBackend::readBlock(void *context, unsigned long pos, unsigned char *buffer,
                                  unsigned long size) {
    auto *d = static_cast<Document *>(context);
    LARGE_INTEGER offset{};
    offset.QuadPart = pos;
    DWORD actual = 0;
    return SetFilePointerEx(d->file, offset, nullptr, FILE_BEGIN) &&
           ReadFile(d->file, buffer, size, &actual, nullptr) && actual == size;
}
static std::string fingerprint(HANDLE file, const std::function<bool()> &cancel) {
    BCRYPT_ALG_HANDLE alg = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0)
        throw PdfError("SHA256 provider unavailable");
    if (BCryptCreateHash(alg, &hash, nullptr, 0, nullptr, 0, 0) < 0) {
        BCryptCloseAlgorithmProvider(alg, 0);
        throw PdfError("SHA256 initialization failed");
    }
    try {
        std::vector<unsigned char> buffer(1024 * 1024);
        LARGE_INTEGER start{};
        SetFilePointerEx(file, start, nullptr, FILE_BEGIN);
        DWORD got = 0;
        while (true) {
            if (cancel())
                throw PdfError("Opening cancelled");
            if (!ReadFile(file, buffer.data(), static_cast<DWORD>(buffer.size()), &got, nullptr))
                throw PdfError("Cannot read PDF file");
            if (!got)
                break;
            if (BCryptHashData(hash, buffer.data(), got, 0) < 0)
                throw PdfError("SHA256 update failed");
        }
        unsigned char digest[32];
        if (BCryptFinishHash(hash, digest, 32, 0) < 0)
            throw PdfError("SHA256 finalization failed");
        BCryptDestroyHash(hash);
        BCryptCloseAlgorithmProvider(alg, 0);
        std::ostringstream out;
        for (auto c : digest)
            out << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(c);
        return out.str();
    } catch (...) {
        BCryptDestroyHash(hash);
        BCryptCloseAlgorithmProvider(alg, 0);
        throw;
    }
}
void PdfBackend::collectOutline(Document &d, void *parent, int depth, std::vector<OutlineItem> &items,
                                std::vector<void *> &visited) {
    if (depth > 32 || items.size() >= 10000)
        return;
    for (void *b = FPDFBookmark_GetFirstChild(d.pdf, parent); b; b = FPDFBookmark_GetNextSibling(d.pdf, b)) {
        if (items.size() >= 10000 || std::find(visited.begin(), visited.end(), b) != visited.end())
            break;
        visited.push_back(b);
        auto bytes = std::min<unsigned long>(FPDFBookmark_GetTitle(b, nullptr, 0), 65536);
        std::wstring title(bytes / 2 + 1, 0);
        FPDFBookmark_GetTitle(b, title.data(), bytes);
        title.resize(wcslen(title.c_str()));
        auto dest = FPDFBookmark_GetDest(d.pdf, b);
        if (!dest) {
            auto a = FPDFBookmark_GetAction(b);
            if (a)
                dest = FPDFAction_GetDest(d.pdf, a);
        }
        int page = dest ? FPDFDest_GetDestPageIndex(d.pdf, dest) : -1;
        items.push_back({title, page, depth});
        collectOutline(d, b, depth + 1, items, visited);
    }
}
DocumentInfo PdfBackend::open(int id, const std::wstring &path, const std::string &password,
                              const std::function<bool()> &cancel) {
    auto d = std::make_unique<Document>();
    d->file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                          FILE_ATTRIBUTE_NORMAL, nullptr);
    if (d->file == INVALID_HANDLE_VALUE)
        throw PdfError("Unable to open PDF file", 2);
    try {
        LARGE_INTEGER size{};
        if (!GetFileSizeEx(d->file, &size) || size.QuadPart <= 0 || size.QuadPart > 0xffffffffll)
            throw PdfError("PDF file must be between 1 byte and 4 GB");
        DocumentInfo info;
        info.id = id;
        info.path = path;
        info.fingerprint = fingerprint(d->file, cancel);
        d->access = {static_cast<unsigned long>(size.QuadPart), readBlock, d.get()};
        d->pdf = FPDF_LoadCustomDocument(&d->access, password.empty() ? nullptr : password.c_str());
        if (!d->pdf)
            throw PdfError("PDF could not be opened", FPDF_GetLastError());
        int n = FPDF_GetPageCount(d->pdf);
        if (n < 1 || n > 100000)
            throw PdfError("Unsupported PDF page count");
        info.sizes.reserve(n);
        for (int p = 0; p < n; ++p) {
            if (cancel())
                throw PdfError("Opening cancelled");
            SizeF s{};
            if (!FPDF_GetPageSizeByIndexF(d->pdf, p, &s) || !std::isfinite(s.width) ||
                !std::isfinite(s.height) || s.width <= 0 || s.height <= 0 || s.width > 200000 ||
                s.height > 200000)
                throw PdfError("Invalid PDF page dimensions");
            info.sizes.push_back({s.width, s.height});
        }
        d->sizes = info.sizes;
        std::vector<void *> visited;
        collectOutline(*d, nullptr, 0, info.outline, visited);
        documents_[id] = std::move(d);
        return info;
    } catch (...) {
        if (d) {
            if (d->pdf)
                FPDF_CloseDocument(d->pdf);
            if (d->file != INVALID_HANDLE_VALUE)
                CloseHandle(d->file);
        }
        throw;
    }
}
void PdfBackend::close(int id) {
    for (auto p = pageCache_.begin(); p != pageCache_.end();)
        if (p->document == id) {
            FPDF_ClosePage(p->page);
            p = pageCache_.erase(p);
        } else
            ++p;
    auto i = documents_.find(id);
    if (i == documents_.end())
        return;
    FPDF_CloseDocument(i->second->pdf);
    CloseHandle(i->second->file);
    documents_.erase(i);
}
void *PdfBackend::cachedPage(int document, int index) {
    for (auto p = pageCache_.begin(); p != pageCache_.end(); ++p)
        if (p->document == document && p->index == index) {
            auto handle = p->page;
            pageCache_.splice(pageCache_.end(), pageCache_, p);
            return handle;
        }
    auto handle = FPDF_LoadPage(documents_.at(document)->pdf, index);
    if (!handle)
        return nullptr;
    pageCache_.push_back({document, index, handle});
    while (pageCache_.size() > 3) {
        FPDF_ClosePage(pageCache_.front().page);
        pageCache_.pop_front();
    }
    return handle;
}
std::shared_ptr<Tile> PdfBackend::render(TileKey key, const std::function<bool()> &cancel) {
    auto it = documents_.find(key.document);
    if (it == documents_.end() || key.page < 0 || key.page >= static_cast<int>(it->second->sizes.size()) ||
        cancel())
        return {};
    auto &d = *it->second;
    Size s = PageTransform(d.sizes[key.page], key.rotation).extent();
    double scale = key.scale < 0 ? std::min(1., 720. / std::max(s.w, s.h)) : key.scale / 64.;
    int fullW = std::max(1, static_cast<int>(std::ceil(s.w * scale))),
        fullH = std::max(1, static_cast<int>(std::ceil(s.h * scale)));
    int x = key.scale < 0 ? 0 : key.x * 512, y = key.scale < 0 ? 0 : key.y * 512;
    int w = key.scale < 0 ? fullW : std::min(512, fullW - x),
        h = key.scale < 0 ? fullH : std::min(512, fullH - y);
    if (w <= 0 || h <= 0)
        return {};
    auto tile = std::make_shared<Tile>();
    tile->width = w;
    tile->height = h;
    tile->pixels.assign(static_cast<size_t>(w) * h * 4, 255);
    void *page = cachedPage(key.document, key.page);
    if (!page)
        throw PdfError("Cannot render this PDF page");
    void *bitmap = FPDFBitmap_CreateEx(w, h, 4, tile->pixels.data(), w * 4);
    if (!bitmap) {
        throw PdfError("Cannot allocate page bitmap");
    }
    struct PauseContext {
        const std::function<bool()> *cancel;
        ULONGLONG start;
    };
    PauseContext context{&cancel, GetTickCount64()};
    Pause pause{1,
                [](Pause *p) -> int {
                    auto *c = static_cast<PauseContext *>(p->user);
                    return (*c->cancel)() || GetTickCount64() - c->start >= 8;
                },
                &context};
    int status =
        FPDF_RenderPageBitmap_Start(bitmap, page, -x, -y, fullW, fullH, key.rotation, 0x01 | 0x200, &pause);
    while (status == 1 && !cancel()) {
        context.start = GetTickCount64();
        status = FPDF_RenderPage_Continue(page, &pause);
    }
    FPDF_RenderPage_Close(page);
    FPDFBitmap_Destroy(bitmap);
    if (cancel())
        return {};
    if (status != 2)
        throw PdfError("PDF rendering failed");
    return tile;
}
std::vector<SearchHit> PdfBackend::search(int id, const std::wstring &q,
                                          const std::function<bool()> &cancel) {
    std::vector<SearchHit> hits;
    auto it = documents_.find(id);
    if (it == documents_.end() || q.empty())
        return hits;
    auto &d = *it->second;
    for (int p = 0; p < static_cast<int>(d.sizes.size()) && !cancel() && hits.size() < 2000; ++p) {
        auto page = FPDF_LoadPage(d.pdf, p);
        if (!page)
            continue;
        auto text = FPDFText_LoadPage(page);
        if (!text) {
            FPDF_ClosePage(page);
            continue;
        }
        auto search = FPDFText_FindStart(text, reinterpret_cast<const unsigned short *>(q.c_str()), 0, 0);
        if (search) {
            while (!cancel() && hits.size() < 2000 && FPDFText_FindNext(search)) {
                SearchHit hit;
                hit.page = p;
                int n = FPDFText_CountRects(text, FPDFText_GetSchResultIndex(search),
                                            FPDFText_GetSchCount(search));
                for (int r = 0; r < n && r < 1000; ++r) {
                    double l, t, rr, b;
                    if (FPDFText_GetRect(text, r, &l, &t, &rr, &b)) {
                        int x1, y1, x2, y2;
                        auto sz = d.sizes[p];
                        int dw = static_cast<int>(sz.w * 10), dh = static_cast<int>(sz.h * 10);
                        FPDF_PageToDevice(page, 0, 0, dw, dh, 0, l, t, &x1, &y1);
                        FPDF_PageToDevice(page, 0, 0, dw, dh, 0, rr, b, &x2, &y2);
                        hit.boxes.push_back({std::min(x1, x2) / 10., std::min(y1, y2) / 10.,
                                             std::abs(x2 - x1) / 10., std::abs(y2 - y1) / 10.});
                    }
                }
                hits.push_back(std::move(hit));
            }
            FPDFText_FindClose(search);
        }
        FPDFText_ClosePage(text);
        FPDF_ClosePage(page);
    }
    return hits;
}
std::wstring PdfBackend::text(int id, int index) {
    auto it = documents_.find(id);
    if (it == documents_.end())
        return {};
    auto page = FPDF_LoadPage(it->second->pdf, index);
    if (!page)
        return {};
    auto text = FPDFText_LoadPage(page);
    std::wstring result;
    if (text) {
        int n = FPDFText_CountChars(text);
        if (n > 0 && n < 10000000) {
            result.resize(n + 1);
            int got = FPDFText_GetText(text, 0, n, reinterpret_cast<unsigned short *>(result.data()));
            result.resize(std::max(0, got - 1));
        }
        FPDFText_ClosePage(text);
    }
    FPDF_ClosePage(page);
    return result;
}
RenderScheduler::RenderScheduler(HWND w, std::filesystem::path dll, std::filesystem::path storage)
    : window_(w), dll_(std::move(dll)), storage_(std::move(storage)) {
    thread_ = std::thread(&RenderScheduler::run, this);
}
RenderScheduler::~RenderScheduler() {
    stop_ = true;
    ++generation_;
    ++searchGeneration_;
    wake_.notify_all();
    if (thread_.joinable())
        thread_.join();
}
void RenderScheduler::emit(WorkerResult r) {
    {
        std::lock_guard lock(mutex_);
        results_.push_back(std::move(r));
    }
    PostMessageW(window_, ReadyMessage, 0, 0);
}
void RenderScheduler::enqueue(std::function<void(PdfBackend &)> f) {
    {
        std::lock_guard lock(mutex_);
        commands_.push_back(std::move(f));
    }
    wake_.notify_one();
}
void RenderScheduler::run() {
    try {
        PdfBackend pdf(dll_);
        while (!stop_) {
            std::function<void(PdfBackend &)> command;
            std::optional<RenderRequest> request;
            uint64_t epoch = 0;
            {
                std::unique_lock lock(mutex_);
                wake_.wait(lock, [&] { return stop_ || !commands_.empty() || !requests_.empty(); });
                if (stop_)
                    break;
                if (!commands_.empty()) {
                    command = std::move(commands_.front());
                    commands_.pop_front();
                } else {
                    request = requests_.front();
                    requests_.erase(requests_.begin());
                    epoch = generation_;
                }
            }
            if (command) {
                try {
                    command(pdf);
                } catch (const std::exception &e) {
                    WorkerResult r(WorkerResult::Kind::Fatal);
                    r.message = utf16(e.what());
                    emit(std::move(r));
                }
            } else if (request) {
                try {
                    auto tile = pdf.render(request->key, [&] { return stop_ || epoch != generation_; });
                    if (tile) {
                        WorkerResult r(WorkerResult::Kind::Rendered, request->key.document);
                        r.key = request->key;
                        r.tile = std::move(tile);
                        emit(std::move(r));
                    }
                } catch (const std::exception &e) {
                    WorkerResult r(WorkerResult::Kind::RenderFailed, request->key.document);
                    r.key = request->key;
                    r.message = utf16(e.what());
                    emit(std::move(r));
                }
            }
        }
    } catch (const std::exception &e) {
        WorkerResult r(WorkerResult::Kind::Fatal);
        r.message = utf16(e.what());
        emit(std::move(r));
    }
}
void RenderScheduler::open(int id, std::wstring path, std::string password) {
    enqueue([this, id, path = std::move(path), password = std::move(password)](PdfBackend &pdf) {
        try {
            auto info =
                std::make_shared<DocumentInfo>(pdf.open(id, path, password, [&] { return stop_.load(); }));
            try {
                info->saved = AnnotationRepository(storage_).load(info->fingerprint);
            } catch (const std::exception &e) {
                info->writable = false;
                info->warning = L"已有批注数据无法读取，为保护原数据已暂停保存。\n" + utf16(e.what());
            }
            WorkerResult r(WorkerResult::Kind::Opened, id);
            r.info = std::move(info);
            emit(std::move(r));
        } catch (const PdfError &e) {
            WorkerResult r(WorkerResult::Kind::OpenFailed, id);
            r.errorCode = e.code;
            r.message = utf16(e.what());
            emit(std::move(r));
        }
    });
}
void RenderScheduler::close(int id) {
    enqueue([id](PdfBackend &pdf) { pdf.close(id); });
}
void RenderScheduler::request(std::vector<RenderRequest> r) {
    std::stable_sort(r.begin(), r.end(), [](auto &a, auto &b) { return a.priority < b.priority; });
    {
        std::lock_guard lock(mutex_);
        requests_ = std::move(r);
        ++generation_;
    }
    wake_.notify_one();
}
void RenderScheduler::search(int id, std::wstring q, uint64_t token) {
    auto generation = ++searchGeneration_;
    enqueue([this, id, q = std::move(q), token, generation](PdfBackend &pdf) {
        WorkerResult r(WorkerResult::Kind::Search, id);
        r.token = token;
        r.hits = pdf.search(id, q, [&] { return stop_ || searchGeneration_ != generation; });
        if (!stop_ && searchGeneration_ == generation)
            emit(std::move(r));
    });
}
void RenderScheduler::copyText(int id, int page) {
    enqueue([this, id, page](PdfBackend &pdf) {
        WorkerResult r(WorkerResult::Kind::Text, id);
        r.message = pdf.text(id, page);
        emit(std::move(r));
    });
}
std::deque<WorkerResult> RenderScheduler::take() {
    std::lock_guard lock(mutex_);
    std::deque<WorkerResult> r;
    r.swap(results_);
    return r;
}
SaveService::SaveService(HWND w, std::filesystem::path p) : repository_(std::move(p)), window_(w) {
    thread_ = std::thread(&SaveService::run, this);
}
SaveService::~SaveService() {
    {
        std::lock_guard lock(mutex_);
        stop_ = true;
    }
    wake_.notify_one();
    thread_.join();
}
void SaveService::save(int id, std::string key, Snapshot s, uint64_t revision) {
    {
        std::lock_guard lock(mutex_);
        jobs_.insert_or_assign(key, Job{id, key, std::move(s), revision});
    }
    wake_.notify_one();
}
void SaveService::run() {
    while (true) {
        std::optional<Job> job;
        {
            std::unique_lock lock(mutex_);
            wake_.wait(lock, [&] { return stop_ || !jobs_.empty(); });
            if (jobs_.empty() && stop_)
                break;
            auto i = jobs_.begin();
            job = std::move(i->second);
            jobs_.erase(i);
            working_ = true;
        }
        WorkerResult r(WorkerResult::Kind::Saved, job->id);
        r.token = job->revision;
        try {
            repository_.save(job->key, job->snapshot);
        } catch (const std::exception &e) {
            r.kind = WorkerResult::Kind::SaveFailed;
            r.message = utf16(e.what());
        }
        {
            std::lock_guard lock(mutex_);
            results_.push_back(std::move(r));
            working_ = false;
        }
        wake_.notify_all();
        PostMessageW(window_, RenderScheduler::ReadyMessage, 0, 0);
    }
}
void SaveService::flush() {
    std::unique_lock lock(mutex_);
    wake_.wait(lock, [&] { return jobs_.empty() && !working_; });
}
std::deque<WorkerResult> SaveService::take() {
    std::lock_guard lock(mutex_);
    std::deque<WorkerResult> r;
    r.swap(results_);
    return r;
}
} // namespace paper
