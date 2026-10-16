#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "core.hpp"
#include <d2d1.h>
#include <dwrite.h>
#include <windows.h>
namespace paper {
template <class T> void release(T *&p) {
    if (p) {
        p->Release();
        p = nullptr;
    }
}
class CanvasRenderer {
    HWND window_;
    ID2D1Factory *factory_ = nullptr;
    IDWriteFactory *write_ = nullptr;
    IDWriteTextFormat *format_ = nullptr;
    ID2D1HwndRenderTarget *target_ = nullptr;
    ID2D1SolidColorBrush *brush_ = nullptr;
    ID2D1StrokeStyle *strokeStyle_ = nullptr;
    struct BitmapEntry {
        ID2D1Bitmap *bitmap;
        uint64_t age;
        size_t bytes;
    };
    std::map<TileKey, BitmapEntry> bitmaps_;
    std::map<std::pair<int, uint64_t>, ID2D1PathGeometry *> paths_;
    size_t gpuBytes_ = 0;
    uint64_t clock_ = 0;
    std::vector<ID2D1PathGeometry *> livePaths_;
    size_t liveBuilt_ = 0;
    ID2D1PathGeometry *geometry(const std::vector<Point> &points, size_t begin, size_t end);
    bool ensure();
    void discardTarget();

  public:
    explicit CanvasRenderer(HWND window);
    ~CanvasRenderer();
    bool begin(RECT dirty);
    void end();
    void resize();
    void clearLive();
    void clearDocument(int id);
    void fill(Rect r, uint32_t rgb, float alpha = 1);
    void border(Rect r, uint32_t rgb, float width = 1);
    void text(const std::wstring &text, Rect r, uint32_t rgb = 0x64748b);
    void bitmap(const TileKey &key, const std::shared_ptr<Tile> &tile, Rect r);
    void stroke(int document, const Stroke &s, const ViewportController &view, bool live = false);
    void pushClip(Rect r);
    void popClip();
};
} // namespace paper
