#include "toolbar.hpp"

namespace paper {
ToolbarPainter::ToolbarPainter() {
    D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &factory_);
    if (factory_) {
        auto props = D2D1::RenderTargetProperties(
            D2D1_RENDER_TARGET_TYPE_DEFAULT,
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE), 96, 96);
        factory_->CreateDCRenderTarget(&props, &target_);
        auto style = D2D1::StrokeStyleProperties(D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND,
                                                 D2D1_CAP_STYLE_ROUND, D2D1_LINE_JOIN_ROUND);
        factory_->CreateStrokeStyle(style, nullptr, 0, &style_);
    }
    if (target_)
        target_->CreateSolidColorBrush(D2D1::ColorF(0), &brush_);
}
ToolbarPainter::~ToolbarPainter() {
    release(brush_);
    release(target_);
    release(style_);
    release(factory_);
}
void ToolbarPainter::line(float x1, float y1, float x2, float y2) {
    target_->DrawLine({x1, y1}, {x2, y2}, brush_, 1.55f, style_);
}
void ToolbarPainter::path(std::initializer_list<D2D1_POINT_2F> points, bool closed) {
    if (points.size() < 2)
        return;
    ID2D1PathGeometry *geometry = nullptr;
    ID2D1GeometrySink *sink = nullptr;
    if (SUCCEEDED(factory_->CreatePathGeometry(&geometry)) && SUCCEEDED(geometry->Open(&sink))) {
        auto p = points.begin();
        sink->BeginFigure(*p++, D2D1_FIGURE_BEGIN_HOLLOW);
        sink->AddLines(p, static_cast<UINT32>(points.size() - 1));
        sink->EndFigure(closed ? D2D1_FIGURE_END_CLOSED : D2D1_FIGURE_END_OPEN);
        sink->Close();
        target_->DrawGeometry(geometry, brush_, 1.55f, style_);
    }
    release(sink);
    release(geometry);
}
void ToolbarPainter::icon(ToolbarIcon i, uint32_t accent) {
    switch (i) {
    case ToolbarIcon::Open:
        path({{3, 19}, {3, 6}, {9, 6}, {11, 9}, {21, 9}});
        path({{3, 19}, {6, 11}, {22, 11}, {19, 19}}, true);
        break;
    case ToolbarIcon::Sidebar:
        target_->DrawRoundedRectangle(D2D1::RoundedRect({2.5f, 3.5f, 21.5f, 20.5f}, 1.5f, 1.5f), brush_,
                                      1.5f);
        line(8, 4, 8, 20);
        line(11, 8, 18, 8);
        line(11, 12, 18, 12);
        line(11, 16, 16, 16);
        break;
    case ToolbarIcon::Pen:
        path({{4, 19}, {5.5f, 13.5f}, {16, 3}, {21, 8}, {10.5f, 18.5f}}, true);
        line(13.5f, 5.5f, 18.5f, 10.5f);
        line(5.5f, 13.5f, 10.5f, 18.5f);
        brush_->SetColor(D2D1::ColorF(accent));
        line(3, 22, 21, 22);
        break;
    case ToolbarIcon::Chevron:
        path({{8, 10}, {12, 14}, {16, 10}});
        break;
    case ToolbarIcon::Marker:
        path({{7, 4}, {17, 4}, {17, 10}, {14, 15}, {10, 15}, {7, 10}}, true);
        path({{10, 15}, {10, 19}, {14, 17}, {14, 15}});
        line(7, 9, 17, 9);
        brush_->SetColor(D2D1::ColorF(accent));
        line(3, 22, 21, 22);
        break;
    case ToolbarIcon::Eraser:
        path({{3, 14}, {13, 3}, {22, 11}, {12, 21}, {9, 21}}, true);
        line(8, 9, 17, 16);
        line(12, 21, 22, 21);
        break;
    case ToolbarIcon::Undo:
    case ToolbarIcon::Redo: {
        bool redo = i == ToolbarIcon::Redo;
        auto flip = [&](float x) { return redo ? 24 - x : x; };
        path({{flip(8), 5}, {flip(3), 10}, {flip(8), 15}});
        line(flip(3), 10, flip(14), 10);
        ID2D1PathGeometry *p = nullptr;
        ID2D1GeometrySink *sink = nullptr;
        if (SUCCEEDED(factory_->CreatePathGeometry(&p)) && SUCCEEDED(p->Open(&sink))) {
            sink->BeginFigure({flip(14), 10}, D2D1_FIGURE_BEGIN_HOLLOW);
            sink->AddBezier({{flip(23), 10}, {flip(23), 20}, {flip(14), 20}});
            sink->EndFigure(D2D1_FIGURE_END_OPEN);
            sink->Close();
            target_->DrawGeometry(p, brush_, 1.55f, style_);
        }
        release(sink);
        release(p);
        break;
    }
    case ToolbarIcon::Minus:
        line(5, 12, 19, 12);
        break;
    case ToolbarIcon::Plus:
        line(5, 12, 19, 12);
        line(12, 5, 12, 19);
        break;
    case ToolbarIcon::Fit:
        target_->DrawRoundedRectangle(D2D1::RoundedRect({2, 5, 22, 19}, 1.5f, 1.5f), brush_, 1.5f);
        line(6, 12, 18, 12);
        path({{8, 10}, {6, 12}, {8, 14}});
        path({{16, 10}, {18, 12}, {16, 14}});
        break;
    case ToolbarIcon::Rotate: {
        ID2D1PathGeometry *p = nullptr;
        ID2D1GeometrySink *sink = nullptr;
        if (SUCCEEDED(factory_->CreatePathGeometry(&p)) && SUCCEEDED(p->Open(&sink))) {
            sink->BeginFigure({5, 16}, D2D1_FIGURE_BEGIN_HOLLOW);
            sink->AddBezier({{0, 7}, {10, 0}, {17, 5}});
            sink->AddBezier({{24, 10}, {19, 20}, {12, 20}});
            sink->EndFigure(D2D1_FIGURE_END_OPEN);
            sink->Close();
            target_->DrawGeometry(p, brush_, 1.55f, style_);
        }
        release(sink);
        release(p);
        path({{16, 17}, {12, 20}, {16, 23}});
        break;
    }
    case ToolbarIcon::Search:
        target_->DrawEllipse(D2D1::Ellipse({10, 10}, 6.5f, 6.5f), brush_, 1.55f);
        line(15, 15, 21, 21);
        break;
    case ToolbarIcon::Close:
        line(6, 6, 18, 18);
        line(18, 6, 6, 18);
        break;
    case ToolbarIcon::Fullscreen:
        path({{9, 3}, {3, 3}, {3, 9}});
        path({{15, 21}, {21, 21}, {21, 15}});
        line(3, 3, 10, 10);
        line(14, 14, 21, 21);
        break;
    case ToolbarIcon::Help:
        target_->DrawEllipse(D2D1::Ellipse({12, 12}, 9, 9), brush_, 1.5f);
        path({{9, 9}, {9, 8}, {12, 7}, {15, 9}, {15, 10}, {12, 12}, {12, 14}});
        target_->FillEllipse(D2D1::Ellipse({12, 17}, .85f, .85f), brush_);
        break;
    case ToolbarIcon::Save:
        path({{4, 3}, {17, 3}, {21, 7}, {21, 21}, {3, 21}, {3, 3}}, true);
        path({{7, 3}, {7, 9}, {16, 9}, {16, 3}});
        path({{7, 21}, {7, 14}, {17, 14}, {17, 21}});
        break;
    case ToolbarIcon::Outline:
        for (int y : {6, 12, 18}) {
            line(8, y, 21, y);
            line(3, y, 4, y);
        }
        break;
    case ToolbarIcon::Thumbnails:
        for (int y : {3, 13})
            for (int x : {4, 14})
                target_->DrawRoundedRectangle(
                    D2D1::RoundedRect({float(x), float(y), float(x + 6), float(y + 8)}, 1, 1), brush_, 1.4f);
        break;
    case ToolbarIcon::Previous:
        path({{15, 5}, {8, 12}, {15, 19}});
        break;
    case ToolbarIcon::Next:
        path({{9, 5}, {16, 12}, {9, 19}});
        break;
    }
}
void ToolbarPainter::draw(HDC dc, RECT r, ToolbarIcon i, bool selected, bool hovered, bool pressed,
                          bool disabled, bool focused, uint32_t accent, UINT dpi, bool label) {
    if (!target_ || !brush_ || FAILED(target_->BindDC(dc, &r)))
        return;
    float width = float(r.right - r.left), height = float(r.bottom - r.top);
    float scale = dpi / 96.f;
    target_->BeginDraw();
    target_->SetTransform(D2D1::Matrix3x2F::Identity());
    target_->Clear(D2D1::ColorF(0xf8f8f8));
    auto box = D2D1::RoundedRect({2 * scale, 4 * scale, width - 2 * scale, height - 4 * scale}, 4 * scale,
                                 4 * scale);
    if (selected || hovered || pressed) {
        brush_->SetColor(D2D1::ColorF(pressed ? 0xd6d8da : selected ? 0xe1e9f3 : 0xe9e9e9));
        target_->FillRoundedRectangle(box, brush_);
    }
    if (focused) {
        brush_->SetColor(D2D1::ColorF(0x2563eb));
        target_->DrawRoundedRectangle(box, brush_, scale);
    }
    brush_->SetColor(D2D1::ColorF(disabled ? 0xb9bdc2 : selected ? 0x205898 : 0x50545a));
    float size = 21 * scale;
    float x = label ? 10 * scale : (width - size) / 2;
    float y = (height - size) / 2;
    target_->SetTransform(D2D1::Matrix3x2F::Scale(size / 24, size / 24) *
                          D2D1::Matrix3x2F::Translation(x, y));
    icon(i, disabled ? 0xc4c6ca : accent);
    target_->SetTransform(D2D1::Matrix3x2F::Identity());
    if (target_->EndDraw() == static_cast<HRESULT>(D2DERR_RECREATE_TARGET)) {
        release(brush_);
        release(target_);
        auto props = D2D1::RenderTargetProperties(
            D2D1_RENDER_TARGET_TYPE_DEFAULT,
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE), 96, 96);
        factory_->CreateDCRenderTarget(&props, &target_);
        if (target_)
            target_->CreateSolidColorBrush(D2D1::ColorF(0), &brush_);
    }
}
} // namespace paper
