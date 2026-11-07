#pragma once
#include "office_automation.hpp"
#include "settings.hpp"
namespace paper {
bool isOfficeDocument(const std::filesystem::path &path);
bool isSupportedDocument(const std::filesystem::path &path);
// Word owns native DOC/DOCX layout. Its page drawing commands are rendered
// directly into viewport tiles, without creating an intermediate PDF.
class OfficePageBackend {
    using Value = automation::Value;
    struct Application {
        Value value;
        explicit Application(Value v) : value(std::move(v)) {}
        ~Application();
    };
    struct Document {
        std::shared_ptr<Application> app;
        Value document, pages;
        std::vector<Size> sizes;
        ~Document();
    };
    struct Page {
        int document, index;
        HENHMETAFILE drawing;
        size_t bytes;
    };
    std::map<int, std::unique_ptr<Document>> documents_;
    std::map<OfficeEngine, std::weak_ptr<Application>> applications_;
    std::list<Page> pages_;
    size_t pageBytes_ = 0;
    std::function<Value(OfficeEngine)> factory_;
    HENHMETAFILE drawing(int id, int page, const std::function<bool()> &cancel);

  public:
    explicit OfficePageBackend(std::function<Value(OfficeEngine)> factory = automation::createApplication)
        : factory_(std::move(factory)) {}
    ~OfficePageBackend();
    bool contains(int id) const {
        return documents_.count(id) != 0;
    }
    DocumentInfo open(int id, const std::wstring &path, OfficeEngine, const std::function<bool()> &cancel);
    void close(int id);
    std::shared_ptr<Tile> render(TileKey, const std::function<bool()> &cancel);
    std::wstring text(int id, int page);
    std::vector<SearchHit> search(int id, const std::wstring &, const std::function<bool()> &cancel);
};
} // namespace paper
