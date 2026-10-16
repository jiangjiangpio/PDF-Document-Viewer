#pragma once
#include "backend.hpp"
#include "renderer.hpp"
#include "toolbar.hpp"
#include <commctrl.h>
#include <set>
namespace paper {
struct DocumentSession {
    int id;
    std::wstring path;
    std::shared_ptr<DocumentInfo> info;
    ViewportController view;
    AnnotationDocument annotations;
    bool loading = true, dirty = false, saveFailed = false;
    uint64_t revision = 0, searchToken = 0;
    std::wstring status = L"正在打开…";
    std::vector<SearchHit> hits;
    int hitIndex = -1;
    DocumentSession(int i, std::wstring p) : id(i), path(std::move(p)) {}
};
class AppWindow {
    enum Id {
        Open = 100,
        Sidebar,
        Draw,
        Palette,
        Marker,
        Eraser,
        Undo,
        Redo,
        ZoomOut,
        ZoomIn,
        Fit,
        Rotate,
        Find,
        CloseTab,
        Fullscreen,
        Help,
        PageEdit,
        PageTotal,
        ZoomLabel,
        SearchEdit,
        SearchGo,
        SearchPrev,
        SearchNext,
        OutlineTab,
        ThumbnailTab,
        Save
    };
    enum class Tool { Pen, Marker, Eraser };
    HWND window_ = nullptr, canvas_ = nullptr, tabs_ = nullptr, tree_ = nullptr, thumbs_ = nullptr,
         status_ = nullptr, tooltips_ = nullptr;
    std::map<int, HWND> controls_;
    std::vector<std::pair<int, int>> toolbar_;
    std::map<int, ToolbarIcon> icons_;
    std::map<int, std::wstring> tooltipText_;
    std::vector<RECT> separators_;
    std::unique_ptr<ToolbarPainter> toolbarPainter_;
    HWND hotButton_ = nullptr;
    int toolbarBottom_ = 0;
    HFONT font_ = nullptr;
    UINT dpi_ = 96;
    int sidebarWidth_ = 240, lastThumbTop_ = -1;
    bool sidebar_ = true, thumbnails_ = false, find_ = false, drawing_ = false, fullscreen_ = false,
         closing_ = false;
    Tool tool_ = Tool::Pen;
    uint32_t inkColor_ = 0x2563eb, markerColor_ = 0xfacc15;
    float inkWidth_ = 2.5f, markerWidth_ = 14;
    WINDOWPLACEMENT placement_{sizeof(WINDOWPLACEMENT)};
    std::vector<std::unique_ptr<DocumentSession>> documents_;
    int active_ = -1, nextId_ = 1;
    TileCache cache_{192ull * 1024 * 1024};
    std::set<TileKey> failedTiles_;
    std::unique_ptr<CanvasRenderer> renderer_;
    std::unique_ptr<RenderScheduler> scheduler_;
    std::unique_ptr<SaveService> saver_;
    InputController input_;
    std::optional<Stroke> live_;
    std::set<uint64_t> eraseIds_;
    int erasePage_ = -1;
    ULONGLONG interactionUntil_ = 0;
    Point lastPointer_{};
    std::filesystem::path storage_;
    int px(double value) const {
        return static_cast<int>(std::lround(value * dpi_ / 96.));
    }
    DocumentSession *active();
    DocumentSession *byId(int id);
    void createControls();
    void updateFont();
    void layout();
    void populateSidebar();
    void updateUi();
    void activate(int index);
    void closeTab(int index);
    void openDialog();
    void command(int id);
    void palette();
    void refresh(bool interacting = false);
    void schedule(bool previewOnly = false);
    void paint();
    void processResults();
    void beginStroke(Point);
    void appendStroke(Point);
    void commitStroke();
    void cancelStroke();
    void invalidateInk(Point a, Point b, double width);
    void save(DocumentSession &);
    void viewChanged();
    void search();
    void nextHit(int delta);
    void scroll(WPARAM);
    void toggleFullscreen();
    void drawItem(const DRAWITEMSTRUCT &);
    void measureItem(MEASUREITEMSTRUCT &);
    void loadSession();
    void saveSession();
    void shutdown();
    static LRESULT CALLBACK WindowProc(HWND, UINT, WPARAM, LPARAM);
    static LRESULT CALLBACK CanvasProc(HWND, UINT, WPARAM, LPARAM);
    static LRESULT CALLBACK ButtonProc(HWND, UINT, WPARAM, LPARAM, UINT_PTR, DWORD_PTR);
    LRESULT message(UINT, WPARAM, LPARAM);
    LRESULT canvasMessage(UINT, WPARAM, LPARAM);

  public:
    AppWindow();
    ~AppWindow();
    int run(HINSTANCE, int, const std::vector<std::wstring> &paths);
    void openFile(const std::wstring &path);
    bool key(MSG &message);
};
int selfTest(const std::filesystem::path &pdf, const std::filesystem::path &output);
} // namespace paper
