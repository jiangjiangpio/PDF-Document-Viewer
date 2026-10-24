#pragma once
#include "renderer.hpp"
#include <algorithm>
#include <cmath>
#include <commctrl.h>
#include <cstdint>
namespace paper {
class PenPalette {
    HWND window_ = nullptr, slider_ = nullptr;
    HFONT font_ = nullptr;
    UINT dpi_ = 96;
    uint32_t &color_;
    float &width_;
    bool marker_, done_ = false;
    static constexpr uint32_t colors_[30] = {
        0x000000, 0x555759, 0x808385, 0xa4a7aa, 0xd0d2d4, 0xffffff, 0xb31368, 0xe81818, 0xff5900, 0xffac00,
        0xffd000, 0xffeb00, 0xa4e619, 0x26d800, 0x00865d, 0x00a8c3, 0x006bff, 0x4200ba, 0x6500cb, 0x650080,
        0xf7d6c4, 0xbb9168, 0x91552e, 0x694333, 0xf77cfb, 0xffc789, 0xffff80, 0x7cfb9b, 0x7cd8fb, 0xb5a4fb};
    int p(int v) const {
        return MulDiv(v, dpi_, 96);
    }
    ID2D1Factory *factory_ = nullptr;
    ID2D1DCRenderTarget *target_ = nullptr;
    ID2D1SolidColorBrush *brush_ = nullptr;
    ID2D1StrokeStyle *stroke_ = nullptr;
    bool begin(HDC dc, RECT area, float resolution = 1) {
        if (!target_) {
            auto props = D2D1::RenderTargetProperties(
                D2D1_RENDER_TARGET_TYPE_DEFAULT,
                D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE), 96, 96);
            if (FAILED(factory_->CreateDCRenderTarget(&props, &target_)))
                return false;
            if (FAILED(target_->CreateSolidColorBrush(D2D1::ColorF(0), &brush_))) {
                release(target_);
                return false;
            }
        }
        if (FAILED(target_->BindDC(dc, &area)))
            return false;
        target_->BeginDraw();
        target_->SetTransform(D2D1::Matrix3x2F::Scale(dpi_ / 96.f * resolution, dpi_ / 96.f * resolution));
        target_->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
        target_->Clear(D2D1::ColorF(0xffffff));
        return true;
    }
    void end() {
        if (target_->EndDraw() == static_cast<HRESULT>(D2DERR_RECREATE_TARGET)) {
            release(brush_);
            release(target_);
            InvalidateRect(window_, nullptr, FALSE);
        }
    }
    void previewChanged() {
        RECT preview{0, p(266), p(294), p(332)};
        InvalidateRect(window_, &preview, FALSE);
    }
    void paint(HDC dc) {
        RECT r{};
        GetClientRect(window_, &r);
        if (begin(dc, r)) {
            ID2D1PathGeometry *curve = nullptr;
            ID2D1GeometrySink *sink = nullptr;
            if (SUCCEEDED(factory_->CreatePathGeometry(&curve)) && SUCCEEDED(curve->Open(&sink))) {
                sink->BeginFigure({30, 304}, D2D1_FIGURE_BEGIN_HOLLOW);
                sink->AddBezier({{105, 240}, {150, 348}, {266, 282}});
                sink->EndFigure(D2D1_FIGURE_END_OPEN);
                sink->Close();
                brush_->SetColor(D2D1::ColorF(color_));
                target_->DrawGeometry(curve, brush_, width_, stroke_);
            }
            release(sink);
            release(curve);
            end();
        }
        auto previous = SelectObject(dc, font_);
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, RGB(40, 40, 40));
        TextOutW(dc, p(20), p(16), L"颜色", 2);
        TextOutW(dc, p(20), p(340), L"粗细", 2);
        TextOutW(dc, p(22), p(411), L"细", 1);
        TextOutW(dc, p(266), p(411), L"粗", 1);
        SelectObject(dc, previous);
    }
    void paintColor(const DRAWITEMSTRUCT &item) {
        int index = int(item.CtlID) - 200;
        if (index < 0 || index >= 30)
            return;
        // Re-rasterize vector geometry at 4x for stable coverage on all DC backends.
        HDC dc = CreateCompatibleDC(item.hDC);
        int width = item.rcItem.right - item.rcItem.left, height = item.rcItem.bottom - item.rcItem.top;
        BITMAPINFO info{};
        info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = width * 4;
        info.bmiHeader.biHeight = -height * 4;
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        void *pixels = nullptr;
        HBITMAP bitmap = CreateDIBSection(item.hDC, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
        if (!bitmap || !pixels) {
            DeleteDC(dc);
            return;
        }
        auto previous = SelectObject(dc, bitmap);
        RECT area{0, 0, width * 4, height * 4};
        if (begin(dc, area, 4)) {
            auto circle = D2D1::Ellipse({22, 22}, 15, 15);
            brush_->SetColor(D2D1::ColorF(colors_[index]));
            target_->FillEllipse(circle, brush_);
            brush_->SetColor(D2D1::ColorF(0x999999));
            target_->DrawEllipse(circle, brush_, .8f);
            if (color_ == colors_[index]) {
                brush_->SetColor(D2D1::ColorF(0x0073ff));
                target_->DrawEllipse(D2D1::Ellipse({22, 22}, 19, 19), brush_, 2);
            }
            end();
        }
        GdiFlush();
        std::vector<uint32_t> resolved(width * height);
        auto source = static_cast<const unsigned char *>(pixels);
        for (int y = 0; y < height; y++)
            for (int x = 0; x < width; x++) {
                unsigned sums[3]{};
                for (int yy = 0; yy < 4; yy++)
                    for (int xx = 0; xx < 4; xx++)
                        for (int c = 0; c < 3; c++)
                            sums[c] += source[((y * 4 + yy) * width * 4 + x * 4 + xx) * 4 + c];
                resolved[y * width + x] =
                    ((sums[2] + 8) / 16 << 16) | ((sums[1] + 8) / 16 << 8) | ((sums[0] + 8) / 16);
            }
        info.bmiHeader.biWidth = width;
        info.bmiHeader.biHeight = -height;
        SetDIBitsToDevice(item.hDC, item.rcItem.left, item.rcItem.top, width, height, 0, 0, 0, height,
                          resolved.data(), &info, DIB_RGB_COLORS);
        SelectObject(dc, previous);
        DeleteObject(bitmap);
        DeleteDC(dc);
        if ((item.itemState & ODS_FOCUS) && !(item.itemState & ODS_NOFOCUSRECT)) {
            RECT focus = item.rcItem;
            InflateRect(&focus, -p(2), -p(2));
            DrawFocusRect(item.hDC, &focus);
        }
    }
    static LRESULT CALLBACK proc(HWND w, UINT m, WPARAM a, LPARAM b) {
        auto self = (PenPalette *)GetWindowLongPtrW(w, GWLP_USERDATA);
        if (m == WM_NCCREATE) {
            self = (PenPalette *)((CREATESTRUCTW *)b)->lpCreateParams;
            self->window_ = w;
            SetWindowLongPtrW(w, GWLP_USERDATA, (LONG_PTR)self);
        }
        if (!self)
            return DefWindowProcW(w, m, a, b);
        switch (m) {
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT: {
            PAINTSTRUCT ps{};
            BeginPaint(w, &ps);
            self->paint(ps.hdc);
            EndPaint(w, &ps);
            return 0;
        }
        case WM_ACTIVATE:
            if (LOWORD(a) == WA_INACTIVE)
                self->done_ = true;
            return 0;
        case WM_CLOSE:
            self->done_ = true;
            return 0;
        case WM_COMMAND:
            if (HIWORD(a) == BN_CLICKED && LOWORD(a) >= 200 && LOWORD(a) < 230) {
                auto previous = self->color_;
                self->color_ = colors_[LOWORD(a) - 200];
                for (int i = 0; i < 30; i++)
                    if (colors_[i] == previous || colors_[i] == self->color_)
                        InvalidateRect(GetDlgItem(w, 200 + i), nullptr, FALSE);
                self->previewChanged();
            }
            return 0;
        case WM_DRAWITEM:
            self->paintColor(*(DRAWITEMSTRUCT *)b);
            return TRUE;
        case WM_CTLCOLORSTATIC:
            SetBkColor((HDC)a, RGB(255, 255, 255));
            return (LRESULT)GetStockObject(WHITE_BRUSH);
        case WM_HSCROLL:
            if ((HWND)b == self->slider_) {
                float width = (float)SendMessageW(self->slider_, TBM_GETPOS, 0, 0) / 10.f;
                if (width != self->width_) {
                    self->width_ = width;
                    self->previewChanged();
                }
            }
            return 0;
        }
        return DefWindowProcW(w, m, a, b);
    }

  public:
    PenPalette(uint32_t &color, float &width, bool marker) : color_(color), width_(width), marker_(marker) {}
    ~PenPalette() {
        release(brush_);
        release(target_);
        release(stroke_);
        release(factory_);
    }
    void show(HWND owner, HWND anchor) {
        if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &factory_)))
            throw std::runtime_error("Palette rendering initialization failed");
        auto style = D2D1::StrokeStyleProperties(D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND,
                                                 D2D1_CAP_STYLE_ROUND, D2D1_LINE_JOIN_ROUND);
        factory_->CreateStrokeStyle(style, nullptr, 0, &stroke_);
        INITCOMMONCONTROLSEX cc{sizeof(cc), ICC_BAR_CLASSES};
        InitCommonControlsEx(&cc);
        dpi_ = GetDpiForWindow(owner);
        WNDCLASSW wc{};
        wc.lpfnWndProc = proc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = L"PDFViewPenPalette";
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.style = CS_DROPSHADOW;
        RegisterClassW(&wc);
        RECT r{};
        GetWindowRect(anchor, &r);
        MONITORINFO mi{sizeof(mi)};
        GetMonitorInfoW(MonitorFromRect(&r, MONITOR_DEFAULTTONEAREST), &mi);
        int width = p(296), height = p(444);
        int x = std::clamp((int)r.left, (int)mi.rcWork.left,
                           std::max((int)mi.rcWork.left, (int)mi.rcWork.right - width));
        int y = r.bottom + p(4);
        if (y + height > mi.rcWork.bottom)
            y = std::max((int)mi.rcWork.top, (int)r.top - height);
        window_ = CreateWindowExW(WS_EX_TOOLWINDOW, L"PDFViewPenPalette", L"画笔颜色与粗细",
                                  WS_POPUP | WS_BORDER | WS_CLIPCHILDREN, x, y, width, height, owner, nullptr,
                                  wc.hInstance, this);
        font_ = CreateFontW(-p(15), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, 0, 0,
                            CLEARTYPE_QUALITY, 0, L"Microsoft YaHei UI");
        for (int i = 0; i < 30; i++) {
            wchar_t label[40];
            swprintf(label, 40, L"颜色 #%06X", colors_[i]);
            HWND c = CreateWindowW(L"BUTTON", label, WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
                                   p(16 + 44 * (i % 6)), p(44 + 44 * (i / 6)), p(44), p(44), window_,
                                   (HMENU)(INT_PTR)(200 + i), nullptr, nullptr);
            SendMessageW(c, WM_SETFONT, (WPARAM)font_, FALSE);
        }
        slider_ = CreateWindowW(TRACKBAR_CLASSW, L"画笔粗细",
                                WS_CHILD | WS_VISIBLE | WS_TABSTOP | TBS_HORZ | TBS_NOTICKS, p(18), p(365),
                                p(262), p(44), window_, nullptr, nullptr, nullptr);
        SendMessageW(slider_, TBM_SETRANGE, TRUE, MAKELPARAM(marker_ ? 40 : 5, marker_ ? 320 : 120));
        SendMessageW(slider_, TBM_SETPOS, TRUE, (LPARAM)std::lround(width_ * 10));
        ShowWindow(window_, SW_SHOW);
        SetFocus(slider_);
        MSG msg{};
        while (!done_) {
            BOOL result = GetMessageW(&msg, nullptr, 0, 0);
            if (result <= 0) {
                if (!result)
                    PostQuitMessage((int)msg.wParam);
                break;
            }
            if (msg.message == WM_KEYDOWN && msg.wParam == VK_ESCAPE) {
                done_ = true;
                continue;
            }
            if (!IsDialogMessageW(window_, &msg)) {
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }
        }
        DestroyWindow(window_);
        DeleteObject(font_);
    }
};
} // namespace paper
