#include "renderer.hpp"
namespace paper {
static D2D1_RECT_F rect(Rect r) {
    return D2D1::RectF(static_cast<float>(r.x), static_cast<float>(r.y), static_cast<float>(r.x + r.w),
                       static_cast<float>(r.y + r.h));
}
static D2D1_COLOR_F color(uint32_t c, float a = 1) {
    return D2D1::ColorF(c, a);
}
CanvasRenderer::CanvasRenderer(HWND w) : window_(w) {
    if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &factory_)))
        throw std::runtime_error("Direct2D initialization failed");
    if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                                   reinterpret_cast<IUnknown **>(&write_))))
        throw std::runtime_error("DirectWrite initialization failed");
    write_->CreateTextFormat(L"Microsoft YaHei UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL,
                             DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 18, L"zh-CN", &format_);
    D2D1_STROKE_STYLE_PROPERTIES style =
        D2D1::StrokeStyleProperties(D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND,
                                    D2D1_LINE_JOIN_ROUND, 10, D2D1_DASH_STYLE_SOLID, 0);
    factory_->CreateStrokeStyle(style, nullptr, 0, &strokeStyle_);
}
CanvasRenderer::~CanvasRenderer() {
    discardTarget();
    for (auto &[k, p] : paths_)
        release(p);
    clearLive();
    release(strokeStyle_);
    release(format_);
    release(write_);
    release(factory_);
}
void CanvasRenderer::discardTarget() {
    for (auto &[k, e] : bitmaps_)
        release(e.bitmap);
    bitmaps_.clear();
    gpuBytes_ = 0;
    release(brush_);
    release(target_);
}
bool CanvasRenderer::ensure() {
    if (target_)
        return true;
    RECT r{};
    GetClientRect(window_, &r);
    auto props = D2D1::RenderTargetProperties();
    props.dpiX = 96;
    props.dpiY = 96;
    auto hp =
        D2D1::HwndRenderTargetProperties(window_, D2D1::SizeU(std::max(1L, r.right), std::max(1L, r.bottom)),
                                         D2D1_PRESENT_OPTIONS_RETAIN_CONTENTS);
    HRESULT result = factory_->CreateHwndRenderTarget(props, hp, &target_);
    if (FAILED(result)) {
        props.type = D2D1_RENDER_TARGET_TYPE_SOFTWARE;
        result = factory_->CreateHwndRenderTarget(props, hp, &target_);
    }
    if (FAILED(result))
        return false;
    if (FAILED(target_->CreateSolidColorBrush(color(0), &brush_))) {
        discardTarget();
        return false;
    }
    return true;
}
bool CanvasRenderer::begin(RECT dirty) {
    if (!ensure())
        return false;
    target_->BeginDraw();
    target_->SetTransform(D2D1::Matrix3x2F::Identity());
    pushClip({static_cast<double>(dirty.left), static_cast<double>(dirty.top),
              static_cast<double>(dirty.right - dirty.left), static_cast<double>(dirty.bottom - dirty.top)});
    return true;
}
void CanvasRenderer::end() {
    popClip();
    if (target_->EndDraw() == static_cast<HRESULT>(D2DERR_RECREATE_TARGET)) {
        discardTarget();
        InvalidateRect(window_, nullptr, FALSE);
    }
}
void CanvasRenderer::resize() {
    if (target_) {
        RECT r{};
        GetClientRect(window_, &r);
        if (FAILED(target_->Resize(D2D1::SizeU(std::max(1L, r.right), std::max(1L, r.bottom)))))
            discardTarget();
    }
    InvalidateRect(window_, nullptr, FALSE);
}
void CanvasRenderer::fill(Rect r, uint32_t c, float alpha) {
    brush_->SetColor(color(c, alpha));
    target_->FillRectangle(rect(r), brush_);
}
void CanvasRenderer::border(Rect r, uint32_t c, float width) {
    brush_->SetColor(color(c));
    target_->DrawRectangle(rect(r), brush_, width);
}
void CanvasRenderer::text(const std::wstring &s, Rect r, uint32_t c) {
    if (!format_)
        return;
    brush_->SetColor(color(c));
    target_->DrawText(s.c_str(), static_cast<UINT32>(s.size()), format_, rect(r), brush_);
}
void CanvasRenderer::bitmap(const TileKey &k, const std::shared_ptr<Tile> &tile, Rect r) {
    auto i = bitmaps_.find(k);
    if (i == bitmaps_.end()) {
        size_t bytes = tile->pixels.size();
        while (gpuBytes_ + bytes > 96ull * 1024 * 1024 && !bitmaps_.empty()) {
            auto old = std::min_element(bitmaps_.begin(), bitmaps_.end(),
                                        [](auto &a, auto &b) { return a.second.age < b.second.age; });
            gpuBytes_ -= old->second.bytes;
            release(old->second.bitmap);
            bitmaps_.erase(old);
        }
        ID2D1Bitmap *b = nullptr;
        auto properties = D2D1::BitmapProperties(
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), 96, 96);
        if (FAILED(target_->CreateBitmap(D2D1::SizeU(tile->width, tile->height), tile->pixels.data(),
                                         tile->width * 4, properties, &b)))
            return;
        i = bitmaps_.emplace(k, BitmapEntry{b, ++clock_, bytes}).first;
        gpuBytes_ += bytes;
    }
    i->second.age = ++clock_;
    target_->DrawBitmap(i->second.bitmap, rect(r), 1, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
}
ID2D1PathGeometry *CanvasRenderer::geometry(const std::vector<Point> &p, size_t first, size_t end) {
    if (end <= first)
        return nullptr;
    ID2D1PathGeometry *path = nullptr;
    if (FAILED(factory_->CreatePathGeometry(&path)))
        return nullptr;
    ID2D1GeometrySink *sink = nullptr;
    if (FAILED(path->Open(&sink))) {
        release(path);
        return nullptr;
    }
    sink->BeginFigure(D2D1::Point2F(static_cast<float>(p[first].x), static_cast<float>(p[first].y)),
                      D2D1_FIGURE_BEGIN_HOLLOW);
    for (size_t i = first + 1; i < end; ++i)
        sink->AddLine(D2D1::Point2F(static_cast<float>(p[i].x), static_cast<float>(p[i].y)));
    sink->EndFigure(D2D1_FIGURE_END_OPEN);
    HRESULT hr = sink->Close();
    release(sink);
    if (FAILED(hr))
        release(path);
    return path;
}
void CanvasRenderer::clearLive() {
    for (auto *p : livePaths_)
        release(p);
    livePaths_.clear();
    liveBuilt_ = 0;
}
void CanvasRenderer::clearDocument(int id) {
    for (auto i = bitmaps_.begin(); i != bitmaps_.end();)
        if (i->first.document == id) {
            gpuBytes_ -= i->second.bytes;
            release(i->second.bitmap);
            i = bitmaps_.erase(i);
        } else
            ++i;
    for (auto i = paths_.begin(); i != paths_.end();)
        if (i->first.first == id) {
            release(i->second);
            i = paths_.erase(i);
        } else
            ++i;
}
void CanvasRenderer::stroke(int document, const Stroke &s, const ViewportController &v, bool live) {
    if (s.points.empty() || s.page >= v.count())
        return;
    auto size = v.sizes()[s.page];
    auto page = v.pageRect(s.page);
    auto rotate = D2D1::Matrix3x2F::Identity();
    switch (v.rotation) {
    case 1:
        rotate = {0, 1, -1, 0, static_cast<float>(size.h), 0};
        break;
    case 2:
        rotate = {-1, 0, 0, -1, static_cast<float>(size.w), static_cast<float>(size.h)};
        break;
    case 3:
        rotate = {0, -1, 1, 0, 0, static_cast<float>(size.w)};
        break;
    }
    target_->SetTransform(
        rotate * D2D1::Matrix3x2F::Scale(static_cast<float>(v.zoom), static_cast<float>(v.zoom)) *
        D2D1::Matrix3x2F::Translation(static_cast<float>(page.x), static_cast<float>(page.y)));
    brush_->SetColor(color(s.color, s.kind == PenKind::Highlighter ? .25f : 1.f));
    if (s.points.size() == 1) {
        target_->FillEllipse(
            D2D1::Ellipse(D2D1::Point2F(static_cast<float>(s.points[0].x), static_cast<float>(s.points[0].y)),
                          s.width / 2, s.width / 2),
            brush_);
    } else if (live && s.kind == PenKind::Ink) {
        while (s.points.size() > liveBuilt_ + 65) {
            auto *p = geometry(s.points, liveBuilt_, liveBuilt_ + 65);
            if (p)
                livePaths_.push_back(p);
            liveBuilt_ += 64;
        }
        for (auto *p : livePaths_)
            target_->DrawGeometry(p, brush_, s.width, strokeStyle_);
        auto *p = geometry(s.points, liveBuilt_, s.points.size());
        if (p) {
            target_->DrawGeometry(p, brush_, s.width, strokeStyle_);
            release(p);
        }
    } else {
        ID2D1PathGeometry *p = nullptr;
        if (live)
            p = geometry(s.points, 0, s.points.size());
        else {
            auto key = std::make_pair(document, s.id);
            auto i = paths_.find(key);
            if (i == paths_.end()) {
                if (paths_.size() > 12000) {
                    for (auto &[k, g] : paths_)
                        release(g);
                    paths_.clear();
                }
                p = geometry(s.points, 0, s.points.size());
                if (p)
                    paths_[key] = p;
            } else
                p = i->second;
        }
        if (p)
            target_->DrawGeometry(p, brush_, s.width, strokeStyle_);
        if (live)
            release(p);
    }
    target_->SetTransform(D2D1::Matrix3x2F::Identity());
}
void CanvasRenderer::pushClip(Rect r) {
    target_->PushAxisAlignedClip(rect(r), D2D1_ANTIALIAS_MODE_ALIASED);
}
void CanvasRenderer::popClip() {
    target_->PopAxisAlignedClip();
}
} // namespace paper
