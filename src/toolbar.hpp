#pragma once
#include "renderer.hpp"

namespace paper {
enum class ToolbarIcon {
    Open,
    Sidebar,
    Pen,
    Chevron,
    Marker,
    Eraser,
    Undo,
    Redo,
    Minus,
    Plus,
    Fit,
    Rotate,
    Search,
    Close,
    Fullscreen,
    Help,
    Save,
    Outline,
    Thumbnails,
    Previous,
    Next
};

// The toolbar owns vector drawing separately from document rendering and input.
// Icons use geometric paths, so their appearance does not depend on an icon font.
class ToolbarPainter {
    ID2D1Factory *factory_ = nullptr;
    ID2D1DCRenderTarget *target_ = nullptr;
    ID2D1SolidColorBrush *brush_ = nullptr;
    ID2D1StrokeStyle *style_ = nullptr;
    void line(float x1, float y1, float x2, float y2);
    void path(std::initializer_list<D2D1_POINT_2F> points, bool closed = false);
    void icon(ToolbarIcon icon, uint32_t accent);

  public:
    ToolbarPainter();
    ~ToolbarPainter();
    void draw(HDC dc, RECT area, ToolbarIcon icon, bool selected, bool hovered, bool pressed, bool disabled,
              bool focused, uint32_t accent, UINT dpi, bool label = false);
};
} // namespace paper
