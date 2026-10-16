#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <list>
#include <map>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace paper {
struct Point {
    double x = 0, y = 0;
};
struct Size {
    double w = 612, h = 792;
};
inline Point operator+(Point a, Point b) {
    return {a.x + b.x, a.y + b.y};
}
inline Point operator-(Point a, Point b) {
    return {a.x - b.x, a.y - b.y};
}
inline double distance(Point a, Point b) {
    return std::hypot(a.x - b.x, a.y - b.y);
}
struct Rect {
    double x = 0, y = 0, w = 0, h = 0;
    bool contains(Point p) const {
        return p.x >= x && p.y >= y && p.x <= x + w && p.y <= y + h;
    }
    bool intersects(Rect r) const {
        return x < r.x + r.w && x + w > r.x && y < r.y + r.h && y + h > r.y;
    }
};
class PageTransform {
    Size size_;
    int rotation_;

  public:
    PageTransform(Size s, int r) : size_(s), rotation_((r % 4 + 4) % 4) {}
    Size extent() const {
        return rotation_ % 2 ? Size{size_.h, size_.w} : size_;
    }
    Point toView(Point p) const;
    Point toPage(Point p) const;
};
enum class PenKind : uint32_t { Ink = 0, Highlighter = 1 };
struct Stroke {
    uint64_t id = 0;
    int page = 0;
    PenKind kind = PenKind::Ink;
    uint32_t color = 0x2563eb;
    float width = 2.5f;
    std::vector<Point> points;
    bool hit(Point point, double radius) const;
};
using StrokePtr = std::shared_ptr<const Stroke>;
using PageStrokes = std::map<int, std::vector<StrokePtr>>;
struct ViewState {
    int page = 0, rotation = 0;
    double offset = 0, zoom = 1;
    bool fit = true;
};
struct Snapshot {
    PageStrokes pages;
    ViewState view;
};
class AnnotationDocument {
    struct Edit {
        std::vector<std::pair<size_t, StrokePtr>> removed;
        StrokePtr added;
    };
    PageStrokes pages_;
    std::vector<Edit> undo_, redo_;
    uint64_t nextId_ = 1;
    void apply(const Edit &e, bool forward);
    void record(Edit e);

  public:
    const PageStrokes &pages() const {
        return pages_;
    }
    const std::vector<StrokePtr> &page(int index) const;
    void restore(PageStrokes pages);
    void add(Stroke stroke);
    bool erase(int page, Point p, double radius);
    bool eraseIds(int page, const std::vector<uint64_t> &ids);
    bool undo();
    bool redo();
    bool canUndo() const {
        return !undo_.empty();
    }
    bool canRedo() const {
        return !redo_.empty();
    }
};
class AnnotationRepository {
    std::filesystem::path root_;

  public:
    explicit AnnotationRepository(std::filesystem::path root) : root_(std::move(root)) {}
    Snapshot load(const std::string &fingerprint) const;
    void save(const std::string &fingerprint, const Snapshot &data) const;
    const std::filesystem::path &root() const {
        return root_;
    }
};
class ViewportController {
    std::vector<Size> sizes_;
    std::vector<double> tops_;
    double contentHeight_ = 0, contentWidth_ = 0;
    void layout();

  public:
    double width = 800, height = 600, zoom = 1, scrollX = 0, scrollY = 0;
    int rotation = 0;
    bool fit = true;
    void setPages(std::vector<Size> sizes);
    void resize(double w, double h);
    void setRotation(int turns);
    void fitWidth();
    void setZoom(double value, Point anchor);
    void pan(Point delta);
    void clamp();
    Rect pageRect(int page) const;
    std::optional<std::pair<int, Point>> pageAt(Point screen) const;
    Point screenPoint(int page, Point point) const;
    std::vector<int> visible(double margin = 0) const;
    int currentPage() const;
    void goTo(int page);
    ViewState state() const;
    void restore(ViewState state);
    int count() const {
        return static_cast<int>(sizes_.size());
    }
    double contentHeight() const {
        return contentHeight_;
    }
    const std::vector<Size> &sizes() const {
        return sizes_;
    }
};
struct InputCallbacks {
    std::function<void(Point)> begin, append;
    std::function<void()> commit, cancel;
    std::function<void(Point)> pan;
    std::function<void(Point, double, Point)> pinch;
};
class InputController {
    std::map<uint32_t, Point> pointers_;
    bool ink_ = false, suppress_ = false;
    InputCallbacks callbacks_;

  public:
    explicit InputController(InputCallbacks callbacks) : callbacks_(std::move(callbacks)) {}
    void down(uint32_t id, Point p, bool drawing);
    void move(uint32_t id, Point p);
    void up(uint32_t id, Point p);
    void cancel();
    bool active() const {
        return !pointers_.empty();
    }
};
struct TileKey {
    int document = 0, page = 0, scale = 0, rotation = 0, x = 0, y = 0;
    bool operator<(const TileKey &r) const;
    bool operator==(const TileKey &r) const;
};
struct Tile {
    int width = 0, height = 0;
    std::vector<uint8_t> pixels;
};
class TileCache {
    struct Item {
        std::shared_ptr<Tile> tile;
        std::list<TileKey>::iterator age;
    };
    size_t budget_, bytes_ = 0;
    std::map<TileKey, Item> entries_;
    std::list<TileKey> age_;

  public:
    explicit TileCache(size_t budget) : budget_(budget) {}
    std::shared_ptr<Tile> get(const TileKey &key);
    void put(const TileKey &key, std::shared_ptr<Tile> tile);
    void removeDocument(int id);
    void clear();
    size_t bytes() const {
        return bytes_;
    }
    size_t count() const {
        return entries_.size();
    }
};
} // namespace paper
