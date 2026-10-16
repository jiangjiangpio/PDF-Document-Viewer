#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "core.hpp"
#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include <windows.h>
namespace paper {
struct OutlineItem {
    std::wstring title;
    int page = -1, depth = 0;
};
struct DocumentInfo {
    int id = 0;
    std::wstring path;
    std::string fingerprint;
    std::vector<Size> sizes;
    std::vector<OutlineItem> outline;
    Snapshot saved;
    std::wstring warning;
    bool writable = true;
};
struct SearchHit {
    int page = 0;
    std::vector<Rect> boxes;
};
struct PdfError : std::runtime_error {
    unsigned long code;
    PdfError(std::string text, unsigned long c = 0) : runtime_error(text), code(c) {}
};
class PdfBackend {
    struct FileAccess {
        unsigned long length;
        int(__cdecl *read)(void *, unsigned long, unsigned char *, unsigned long);
        void *context;
    };
    struct SizeF {
        float width, height;
    };
    struct Pause {
        int version;
        int(__cdecl *needPause)(Pause *);
        void *user;
    };
    HMODULE library_ = nullptr;
#define PDF_API(ret, name, args) ret(__cdecl *name) args = nullptr;
#include "pdf_api.inc"
#undef PDF_API
    struct Document {
        HANDLE file = INVALID_HANDLE_VALUE;
        FileAccess access{};
        void *pdf = nullptr;
        std::vector<Size> sizes;
    };
    std::map<int, std::unique_ptr<Document>> documents_;
    struct CachedPage {
        int document, index;
        void *page;
    };
    std::list<CachedPage> pageCache_;
    void *cachedPage(int document, int index);
    static int __cdecl readBlock(void *, unsigned long, unsigned char *, unsigned long);
    void collectOutline(Document &, void *, int, std::vector<OutlineItem> &, std::vector<void *> &);

  public:
    explicit PdfBackend(const std::filesystem::path &dll);
    ~PdfBackend();
    DocumentInfo open(int id, const std::wstring &path, const std::string &password,
                      const std::function<bool()> &cancel);
    void close(int id);
    std::shared_ptr<Tile> render(TileKey key, const std::function<bool()> &cancel);
    std::vector<SearchHit> search(int id, const std::wstring &query, const std::function<bool()> &cancel);
    std::wstring text(int id, int page);
};
struct RenderRequest {
    TileKey key;
    int priority = 0;
};
struct WorkerResult {
    enum class Kind { Opened, OpenFailed, Rendered, RenderFailed, Search, Text, Saved, SaveFailed, Fatal };
    Kind kind;
    int document = 0;
    uint64_t token = 0;
    unsigned long errorCode = 0;
    std::wstring message;
    std::shared_ptr<DocumentInfo> info;
    TileKey key;
    std::shared_ptr<Tile> tile;
    std::vector<SearchHit> hits;
    explicit WorkerResult(Kind k, int d = 0) : kind(k), document(d) {}
};
class RenderScheduler {
    HWND window_;
    std::filesystem::path dll_, storage_;
    std::thread thread_;
    std::mutex mutex_;
    std::condition_variable wake_;
    std::deque<std::function<void(PdfBackend &)>> commands_;
    std::vector<RenderRequest> requests_;
    std::deque<WorkerResult> results_;
    std::atomic<bool> stop_{false};
    std::atomic<uint64_t> generation_{0}, searchGeneration_{0};
    void run();
    void emit(WorkerResult result);
    void enqueue(std::function<void(PdfBackend &)> command);

  public:
    static constexpr UINT ReadyMessage = WM_APP + 10;
    RenderScheduler(HWND window, std::filesystem::path dll, std::filesystem::path storage);
    ~RenderScheduler();
    void open(int id, std::wstring path, std::string password = {});
    void close(int id);
    void request(std::vector<RenderRequest> tiles);
    void search(int id, std::wstring query, uint64_t token);
    void copyText(int id, int page);
    std::deque<WorkerResult> take();
};
class SaveService {
    struct Job {
        int id;
        std::string key;
        Snapshot snapshot;
        uint64_t revision;
    };
    AnnotationRepository repository_;
    HWND window_;
    std::thread thread_;
    std::mutex mutex_;
    std::condition_variable wake_;
    std::map<std::string, Job> jobs_;
    bool stop_ = false, working_ = false;
    std::deque<WorkerResult> results_;
    void run();

  public:
    SaveService(HWND w, std::filesystem::path path);
    ~SaveService();
    void flush();
    void save(int id, std::string key, Snapshot snapshot, uint64_t revision);
    std::deque<WorkerResult> take();
};
std::filesystem::path executableDirectory();
std::filesystem::path storageDirectory();
std::wstring utf16(const std::string &);
std::string utf8(const std::wstring &);
} // namespace paper
