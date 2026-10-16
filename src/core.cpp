#include "core.hpp"
#include <fstream>
#include <limits>
#include <tuple>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
namespace paper {
Point PageTransform::toView(Point p) const {
    switch (rotation_) {
    case 1:
        return {size_.h - p.y, p.x};
    case 2:
        return {size_.w - p.x, size_.h - p.y};
    case 3:
        return {p.y, size_.w - p.x};
    default:
        return p;
    }
}
Point PageTransform::toPage(Point p) const {
    switch (rotation_) {
    case 1:
        return {p.y, size_.h - p.x};
    case 2:
        return {size_.w - p.x, size_.h - p.y};
    case 3:
        return {size_.w - p.y, p.x};
    default:
        return p;
    }
}
static double segmentDistance(Point p, Point a, Point b) {
    auto d = b - a;
    double len = d.x * d.x + d.y * d.y;
    double t = len > 0 ? std::clamp(((p.x - a.x) * d.x + (p.y - a.y) * d.y) / len, 0., 1.) : 0;
    return distance(p, {a.x + t * d.x, a.y + t * d.y});
}
bool Stroke::hit(Point p, double radius) const {
    if (points.empty())
        return false;
    radius += width / 2.;
    if (distance(p, points.front()) <= radius)
        return true;
    for (size_t i = 1; i < points.size(); ++i)
        if (segmentDistance(p, points[i - 1], points[i]) <= radius)
            return true;
    return false;
}
const std::vector<StrokePtr> &AnnotationDocument::page(int index) const {
    static const std::vector<StrokePtr> empty;
    auto i = pages_.find(index);
    return i == pages_.end() ? empty : i->second;
}
void AnnotationDocument::restore(PageStrokes pages) {
    pages_ = std::move(pages);
    undo_.clear();
    redo_.clear();
    nextId_ = 1;
    for (auto &[p, strokes] : pages_)
        for (auto &s : strokes)
            nextId_ = std::max(nextId_, s->id + 1);
}
void AnnotationDocument::apply(const Edit &e, bool forward) {
    if (e.added) {
        auto &v = pages_[e.added->page];
        if (forward)
            v.push_back(e.added);
        else
            v.erase(std::remove(v.begin(), v.end(), e.added), v.end());
    }
    if (forward) {
        for (auto &[idx, s] : e.removed) {
            auto &v = pages_[s->page];
            v.erase(std::remove(v.begin(), v.end(), s), v.end());
        }
    } else
        for (auto &[idx, s] : e.removed) {
            auto &v = pages_[s->page];
            v.insert(v.begin() + std::min(idx, v.size()), s);
        }
}
void AnnotationDocument::record(Edit e) {
    apply(e, true);
    undo_.push_back(std::move(e));
    if (undo_.size() > 128)
        undo_.erase(undo_.begin());
    redo_.clear();
}
void AnnotationDocument::add(Stroke s) {
    if (s.points.empty())
        return;
    s.id = nextId_++;
    record({{}, std::make_shared<Stroke>(std::move(s))});
}
bool AnnotationDocument::erase(int page, Point p, double radius) {
    Edit e;
    const auto &v = this->page(page);
    for (size_t i = 0; i < v.size(); ++i)
        if (v[i]->hit(p, radius))
            e.removed.emplace_back(i, v[i]);
    if (e.removed.empty())
        return false;
    record(std::move(e));
    return true;
}
bool AnnotationDocument::eraseIds(int page, const std::vector<uint64_t> &ids) {
    Edit e;
    const auto &v = this->page(page);
    for (size_t i = 0; i < v.size(); ++i)
        if (std::find(ids.begin(), ids.end(), v[i]->id) != ids.end())
            e.removed.emplace_back(i, v[i]);
    if (e.removed.empty())
        return false;
    record(std::move(e));
    return true;
}
bool AnnotationDocument::undo() {
    if (undo_.empty())
        return false;
    auto e = undo_.back();
    undo_.pop_back();
    apply(e, false);
    redo_.push_back(std::move(e));
    return true;
}
bool AnnotationDocument::redo() {
    if (redo_.empty())
        return false;
    auto e = redo_.back();
    redo_.pop_back();
    apply(e, true);
    undo_.push_back(std::move(e));
    return true;
}
namespace {
template <class T> void write(std::ostream &o, T value) {
    o.write(reinterpret_cast<const char *>(&value), sizeof(value));
}
template <class T> T read(std::istream &i) {
    T value{};
    if (!i.read(reinterpret_cast<char *>(&value), sizeof(value)))
        throw std::runtime_error("Truncated annotation data");
    return value;
}
void validateKey(const std::string &k) {
    if (k.size() != 64 || k.find_first_not_of("0123456789abcdef") != std::string::npos)
        throw std::runtime_error("Invalid document identity");
}
} // namespace
Snapshot AnnotationRepository::load(const std::string &key) const {
    validateKey(key);
    auto path = root_ / (key + ".ink");
    if (!std::filesystem::exists(path))
        return {};
    if (std::filesystem::file_size(path) > 512ull * 1024 * 1024)
        throw std::runtime_error("Annotation file exceeds safety limit");
    std::ifstream in(path, std::ios::binary);
    char magic[8]{};
    in.read(magic, 8);
    if (std::string(magic, 8) != "PAPER001")
        throw std::runtime_error("Unknown annotation format");
    Snapshot data;
    data.view.page = read<int32_t>(in);
    data.view.rotation = read<int32_t>(in);
    data.view.zoom = read<double>(in);
    data.view.offset = read<double>(in);
    data.view.fit = read<uint32_t>(in) != 0;
    if (data.view.page < 0 || data.view.rotation < 0 || data.view.rotation > 3 ||
        !std::isfinite(data.view.zoom) || data.view.zoom < .1 || data.view.zoom > 8 ||
        !std::isfinite(data.view.offset) || std::abs(data.view.offset) > 2)
        throw std::runtime_error("Invalid saved view");
    auto count = read<uint32_t>(in);
    if (count > 1000000)
        throw std::runtime_error("Too many strokes");
    uint64_t pointCount = 0;
    for (uint32_t n = 0; n < count; ++n) {
        Stroke s;
        s.id = read<uint64_t>(in);
        s.page = read<int32_t>(in);
        s.kind = static_cast<PenKind>(read<uint32_t>(in));
        s.color = read<uint32_t>(in);
        s.width = read<float>(in);
        auto len = read<uint32_t>(in);
        pointCount += len;
        if (s.page < 0 || s.page > 1000000 || static_cast<uint32_t>(s.kind) > 1 || !std::isfinite(s.width) ||
            s.width <= 0 || s.width > 200 || len == 0 || pointCount > 20000000)
            throw std::runtime_error("Invalid stroke data");
        s.points.reserve(len);
        for (uint32_t j = 0; j < len; ++j) {
            Point p{read<double>(in), read<double>(in)};
            if (!std::isfinite(p.x) || !std::isfinite(p.y) || std::abs(p.x) > 1e7 || std::abs(p.y) > 1e7)
                throw std::runtime_error("Invalid stroke coordinates");
            s.points.push_back(p);
        }
        data.pages[s.page].push_back(std::make_shared<Stroke>(std::move(s)));
    }
    if (in.peek() != std::char_traits<char>::eof())
        throw std::runtime_error("Unexpected annotation data");
    return data;
}
void AnnotationRepository::save(const std::string &key, const Snapshot &data) const {
    validateKey(key);
    std::filesystem::create_directories(root_);
    auto path = root_ / (key + ".ink"), temp = root_ / (key + ".tmp");
    {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        out.write("PAPER001", 8);
        write<int32_t>(out, data.view.page);
        write<int32_t>(out, data.view.rotation);
        write(out, data.view.zoom);
        write(out, data.view.offset);
        write<uint32_t>(out, data.view.fit ? 1 : 0);
        uint32_t count = 0;
        for (auto &[page, v] : data.pages)
            count += static_cast<uint32_t>(v.size());
        write(out, count);
        for (auto &[page, v] : data.pages)
            for (auto &s : v) {
                write(out, s->id);
                write<int32_t>(out, s->page);
                write<uint32_t>(out, static_cast<uint32_t>(s->kind));
                write(out, s->color);
                write(out, s->width);
                write<uint32_t>(out, static_cast<uint32_t>(s->points.size()));
                for (auto p : s->points) {
                    write(out, p.x);
                    write(out, p.y);
                }
            }
        out.flush();
        if (!out)
            throw std::runtime_error("Could not write annotations");
    }
#ifdef _WIN32
    if (!MoveFileExW(temp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        throw std::runtime_error("Could not replace annotation file");
#else
    std::filesystem::rename(temp, path);
#endif
}
void ViewportController::layout() {
    tops_.clear();
    contentHeight_ = 16;
    contentWidth_ = width;
    for (auto s : sizes_) {
        auto ext = PageTransform(s, rotation).extent();
        tops_.push_back(contentHeight_);
        contentHeight_ += ext.h * zoom + 16;
        contentWidth_ = std::max(contentWidth_, ext.w * zoom + 32);
    }
    clamp();
}
void ViewportController::setPages(std::vector<Size> s) {
    sizes_ = std::move(s);
    fitWidth();
}
void ViewportController::resize(double w, double h) {
    width = std::max(w, 1.);
    height = std::max(h, 1.);
    auto previous = state();
    if (fit)
        fitWidth();
    else
        layout();
    if (count()) {
        auto r = pageRect(previous.page);
        scrollY += r.y + previous.offset * r.h;
        clamp();
    }
}
void ViewportController::fitWidth() {
    fit = true;
    double maxWidth = 1;
    for (auto s : sizes_)
        maxWidth = std::max(maxWidth, PageTransform(s, rotation).extent().w);
    zoom = std::clamp((width - 40) / maxWidth, .1, 8.);
    layout();
}
void ViewportController::setRotation(int r) {
    auto old = state();
    rotation = (r % 4 + 4) % 4;
    if (fit)
        fitWidth();
    else
        layout();
    goTo(old.page);
}
void ViewportController::setZoom(double z, Point anchor) {
    auto loc = pageAt(anchor);
    double old = zoom;
    double oldY = scrollY;
    fit = false;
    zoom = std::clamp(z, .1, 8.);
    layout();
    if (loc) {
        Point now = screenPoint(loc->first, loc->second);
        scrollX += now.x - anchor.x;
        scrollY += now.y - anchor.y;
    } else
        scrollY = (oldY + anchor.y) * zoom / old - anchor.y;
    clamp();
}
void ViewportController::pan(Point d) {
    scrollX -= d.x;
    scrollY -= d.y;
    clamp();
}
void ViewportController::clamp() {
    scrollX = std::clamp(scrollX, 0., std::max(0., contentWidth_ - width));
    scrollY = std::clamp(scrollY, 0., std::max(0., contentHeight_ - height));
}
Rect ViewportController::pageRect(int p) const {
    if (p < 0 || p >= count())
        return {};
    auto s = PageTransform(sizes_[p], rotation).extent();
    return {std::max(16., (contentWidth_ - s.w * zoom) / 2) - scrollX, tops_[p] - scrollY, s.w * zoom,
            s.h * zoom};
}
std::optional<std::pair<int, Point>> ViewportController::pageAt(Point s) const {
    auto candidates = visible();
    for (int p : candidates) {
        auto r = pageRect(p);
        if (r.contains(s))
            return std::make_pair(
                p, PageTransform(sizes_[p], rotation).toPage({(s.x - r.x) / zoom, (s.y - r.y) / zoom}));
    }
    return {};
}
Point ViewportController::screenPoint(int page, Point p) const {
    auto q = PageTransform(sizes_[page], rotation).toView(p);
    auto r = pageRect(page);
    return {r.x + q.x * zoom, r.y + q.y * zoom};
}
std::vector<int> ViewportController::visible(double margin) const {
    std::vector<int> result;
    if (tops_.empty())
        return result;
    auto it = std::upper_bound(tops_.begin(), tops_.end(), scrollY - margin);
    int start = std::max(0, static_cast<int>(it - tops_.begin()) - 1);
    for (int p = start; p < count() && tops_[p] < scrollY + height + margin; ++p)
        if (tops_[p + 0] + PageTransform(sizes_[p], rotation).extent().h * zoom > scrollY - margin)
            result.push_back(p);
    return result;
}
int ViewportController::currentPage() const {
    if (tops_.empty())
        return 0;
    auto it = std::upper_bound(tops_.begin(), tops_.end(), scrollY + height * .25);
    return std::clamp(static_cast<int>(it - tops_.begin()) - 1, 0, count() - 1);
}
void ViewportController::goTo(int p) {
    if (!count())
        return;
    p = std::clamp(p, 0, count() - 1);
    scrollY = tops_[p] - 16;
    clamp();
}
ViewState ViewportController::state() const {
    int p = currentPage();
    auto r = pageRect(p);
    return {p, rotation, r.h > 0 ? -r.y / r.h : 0, zoom, fit};
}
void ViewportController::restore(ViewState s) {
    if (!count())
        return;
    rotation = s.rotation;
    fit = s.fit;
    zoom = std::clamp(s.zoom, .1, 8.);
    if (fit)
        fitWidth();
    else
        layout();
    int p = std::clamp(s.page, 0, count() - 1);
    auto r = pageRect(p);
    scrollY = tops_[p] + s.offset * r.h;
    clamp();
}
void InputController::down(uint32_t id, Point p, bool drawing) {
    if (pointers_.count(id))
        return;
    pointers_[id] = p;
    if (pointers_.size() == 1) {
        suppress_ = false;
        ink_ = drawing;
        if (ink_)
            callbacks_.begin(p);
    } else {
        if (ink_)
            callbacks_.cancel();
        ink_ = false;
        suppress_ = true;
    }
}
void InputController::move(uint32_t id, Point p) {
    auto i = pointers_.find(id);
    if (i == pointers_.end())
        return;
    if (pointers_.size() == 1) {
        auto delta = p - i->second;
        i->second = p;
        if (ink_ && !suppress_)
            callbacks_.append(p);
        else
            callbacks_.pan(delta);
        return;
    }
    auto a = pointers_.begin(), b = std::next(a);
    Point oldCenter{(a->second.x + b->second.x) / 2, (a->second.y + b->second.y) / 2};
    double oldDistance = distance(a->second, b->second);
    i->second = p;
    Point center{(a->second.x + b->second.x) / 2, (a->second.y + b->second.y) / 2};
    double len = distance(a->second, b->second);
    if (oldDistance > 8 && len > 8)
        callbacks_.pinch(oldCenter, len / oldDistance, center - oldCenter);
}
void InputController::up(uint32_t id, Point p) {
    if (!pointers_.count(id))
        return;
    if (pointers_.size() == 1 && ink_) {
        callbacks_.append(p);
        callbacks_.commit();
    }
    pointers_.erase(id);
    ink_ = false;
    if (pointers_.empty())
        suppress_ = false;
}
void InputController::cancel() {
    if (ink_)
        callbacks_.cancel();
    ink_ = false;
    suppress_ = false;
    pointers_.clear();
}
bool TileKey::operator<(const TileKey &r) const {
    return std::tie(document, page, scale, rotation, x, y) <
           std::tie(r.document, r.page, r.scale, r.rotation, r.x, r.y);
}
bool TileKey::operator==(const TileKey &r) const {
    return !(*this < r) && !(r < *this);
}
std::shared_ptr<Tile> TileCache::get(const TileKey &k) {
    auto i = entries_.find(k);
    if (i == entries_.end())
        return {};
    age_.splice(age_.end(), age_, i->second.age);
    return i->second.tile;
}
void TileCache::put(const TileKey &k, std::shared_ptr<Tile> t) {
    auto i = entries_.find(k);
    if (i != entries_.end()) {
        bytes_ -= i->second.tile->pixels.size();
        age_.erase(i->second.age);
        entries_.erase(i);
    }
    if (t->pixels.size() > budget_)
        return;
    bytes_ += t->pixels.size();
    age_.push_back(k);
    entries_.emplace(k, Item{t, std::prev(age_.end())});
    while (bytes_ > budget_ && !age_.empty()) {
        auto old = age_.front();
        auto e = entries_.find(old);
        bytes_ -= e->second.tile->pixels.size();
        entries_.erase(e);
        age_.pop_front();
    }
}
void TileCache::removeDocument(int id) {
    for (auto i = entries_.begin(); i != entries_.end();)
        if (i->first.document == id) {
            bytes_ -= i->second.tile->pixels.size();
            age_.erase(i->second.age);
            i = entries_.erase(i);
        } else
            ++i;
}
void TileCache::clear() {
    entries_.clear();
    age_.clear();
    bytes_ = 0;
}
} // namespace paper
