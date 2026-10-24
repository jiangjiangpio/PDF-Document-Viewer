#pragma once
#include <windows.h>
#include <commctrl.h>
#include <algorithm>
#include <cstdint>
#include <cmath>
namespace paper {
class PenPalette {
    HWND window_=nullptr, slider_=nullptr;
    HFONT font_=nullptr;
    UINT dpi_=96;
    uint32_t &color_; float &width_; bool marker_, done_=false;
    static constexpr uint32_t colors_[30]={0x000000,0x555759,0x808385,0xa4a7aa,0xd0d2d4,0xffffff,0xb31368,0xe81818,0xff5900,0xffac00,0xffd000,0xffeb00,0xa4e619,0x26d800,0x00865d,0x00a8c3,0x006bff,0x4200ba,0x6500cb,0x650080,0xf7d6c4,0xbb9168,0x91552e,0x694333,0xf77cfb,0xffc789,0xffff80,0x7cfb9b,0x7cd8fb,0xb5a4fb};
    int p(int v) const{return MulDiv(v,dpi_,96);}
    static COLORREF rgb(uint32_t c){return RGB(c>>16,(c>>8)&255,c&255);}
    void paint(HDC dc) {
        RECT r{};GetClientRect(window_,&r);
        HDC mem=CreateCompatibleDC(dc);HBITMAP bitmap=CreateCompatibleBitmap(dc,r.right,r.bottom);
        auto old=SelectObject(mem,bitmap);FillRect(mem,&r,(HBRUSH)GetStockObject(WHITE_BRUSH));
        SelectObject(mem,font_);SetBkMode(mem,TRANSPARENT);SetTextColor(mem,RGB(40,40,40));
        TextOutW(mem,p(20),p(16),L"颜色",2);
        for(int i=0;i<30;i++) {
            int x=p(38+44*(i%6)),y=p(66+44*(i/6));
            HPEN pen=CreatePen(PS_SOLID,p(1),RGB(150,150,150));HBRUSH fill=CreateSolidBrush(rgb(colors_[i]));
            auto op=SelectObject(mem,pen), ob=SelectObject(mem,fill);
            Ellipse(mem,x-p(15),y-p(15),x+p(15),y+p(15));
            SelectObject(mem,ob);SelectObject(mem,op);DeleteObject(fill);DeleteObject(pen);
            if(color_==colors_[i]) {
                pen=CreatePen(PS_SOLID,p(2),RGB(0,115,255));op=SelectObject(mem,pen);ob=SelectObject(mem,GetStockObject(NULL_BRUSH));
                Ellipse(mem,x-p(19),y-p(19),x+p(19),y+p(19));SelectObject(mem,ob);SelectObject(mem,op);DeleteObject(pen);
            }
        }
        LOGBRUSH lb{BS_SOLID,rgb(color_),0};
        HPEN pen=ExtCreatePen(PS_GEOMETRIC|PS_SOLID|PS_ENDCAP_ROUND|PS_JOIN_ROUND,std::max(1,p((int)std::lround(width_))),&lb,0,nullptr);
        auto op=SelectObject(mem,pen);POINT curve[]={{p(30),p(304)},{p(105),p(240)},{p(150),p(348)},{p(266),p(282)}};
        PolyBezier(mem,curve,4);SelectObject(mem,op);DeleteObject(pen);
        TextOutW(mem,p(20),p(340),L"粗细",2);TextOutW(mem,p(22),p(411),L"细",1);TextOutW(mem,p(266),p(411),L"粗",1);
        BitBlt(dc,0,0,r.right,r.bottom,mem,0,0,SRCCOPY);SelectObject(mem,old);DeleteObject(bitmap);DeleteDC(mem);
    }
    static LRESULT CALLBACK proc(HWND w,UINT m,WPARAM a,LPARAM b) {
        auto self=(PenPalette*)GetWindowLongPtrW(w,GWLP_USERDATA);
        if(m==WM_NCCREATE){self=(PenPalette*)((CREATESTRUCTW*)b)->lpCreateParams;self->window_=w;SetWindowLongPtrW(w,GWLP_USERDATA,(LONG_PTR)self);}
        if(!self)return DefWindowProcW(w,m,a,b);
        switch(m){
        case WM_ERASEBKGND:return 1;
        case WM_PAINT:{PAINTSTRUCT ps{};BeginPaint(w,&ps);self->paint(ps.hdc);EndPaint(w,&ps);return 0;}
        case WM_ACTIVATE:if(LOWORD(a)==WA_INACTIVE)self->done_=true;return 0;
        case WM_CLOSE:self->done_=true;return 0;
        case WM_COMMAND:if(HIWORD(a)==BN_CLICKED&&LOWORD(a)>=200&&LOWORD(a)<230){self->color_=colors_[LOWORD(a)-200];InvalidateRect(w,nullptr,FALSE);}return 0;
        case WM_DRAWITEM: {
            auto item=(DRAWITEMSTRUCT*)b; RECT r{};GetWindowRect(item->hwndItem,&r);MapWindowPoints(nullptr,w,(POINT*)&r,2);
            int saved=SaveDC(item->hDC);SetViewportOrgEx(item->hDC,-r.left,-r.top,nullptr);self->paint(item->hDC);RestoreDC(item->hDC,saved);if ((item->itemState & ODS_FOCUS) && !(item->itemState & ODS_NOFOCUSRECT)) {RECT focus=item->rcItem;InflateRect(&focus,-2,-2);DrawFocusRect(item->hDC,&focus);}return TRUE;
        }
        case WM_CTLCOLORSTATIC:SetBkColor((HDC)a,RGB(255,255,255));return (LRESULT)GetStockObject(WHITE_BRUSH);
        case WM_HSCROLL:self->width_=(float)SendMessageW(self->slider_,TBM_GETPOS,0,0)/10.f;InvalidateRect(w,nullptr,FALSE);return 0;
        }
        return DefWindowProcW(w,m,a,b);
    }
public:
    PenPalette(uint32_t &color,float &width,bool marker):color_(color),width_(width),marker_(marker){}
    void show(HWND owner,HWND anchor){
        INITCOMMONCONTROLSEX cc{sizeof(cc),ICC_BAR_CLASSES};InitCommonControlsEx(&cc);
        dpi_=GetDpiForWindow(owner);
        WNDCLASSW wc{};wc.lpfnWndProc=proc;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"PDFViewPenPalette";wc.hCursor=LoadCursor(nullptr,IDC_ARROW);wc.style=CS_DROPSHADOW;RegisterClassW(&wc);
        RECT r{};GetWindowRect(anchor,&r);MONITORINFO mi{sizeof(mi)};GetMonitorInfoW(MonitorFromRect(&r,MONITOR_DEFAULTTONEAREST),&mi);
        int width=p(296),height=p(444);int x=std::clamp((int)r.left,(int)mi.rcWork.left,std::max((int)mi.rcWork.left,(int)mi.rcWork.right-width));int y=r.bottom+ p(4);if(y+height>mi.rcWork.bottom)y=std::max((int)mi.rcWork.top,(int)r.top-height);
        window_=CreateWindowExW(WS_EX_TOOLWINDOW,L"PDFViewPenPalette",L"画笔颜色与粗细",WS_POPUP|WS_BORDER,x,y,width,height,owner,nullptr,wc.hInstance,this);
        font_=CreateFontW(-p(15),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Microsoft YaHei UI");
        for(int i=0;i<30;i++) {
            wchar_t label[40];swprintf(label,40,L"颜色 #%06X",colors_[i]);
            HWND c=CreateWindowW(L"BUTTON",label,WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_OWNERDRAW,p(16+44*(i%6)),p(44+44*(i/6)),p(44),p(44),window_,(HMENU)(INT_PTR)(200+i),nullptr,nullptr);
            SendMessageW(c,WM_SETFONT,(WPARAM)font_,FALSE);
        }
        slider_=CreateWindowW(TRACKBAR_CLASSW,L"画笔粗细",WS_CHILD|WS_VISIBLE|WS_TABSTOP|TBS_HORZ|TBS_NOTICKS,p(18),p(365),p(262),p(44),window_,nullptr,nullptr,nullptr);
        SendMessageW(slider_,TBM_SETRANGE,TRUE,MAKELPARAM(marker_?40:5,marker_?320:120));SendMessageW(slider_,TBM_SETPOS,TRUE,(LPARAM)std::lround(width_*10));
        ShowWindow(window_,SW_SHOW);SetFocus(slider_);
        MSG msg{};while(!done_){BOOL result=GetMessageW(&msg,nullptr,0,0);if(result<=0){if(!result)PostQuitMessage((int)msg.wParam);break;}if(msg.message==WM_KEYDOWN&&msg.wParam==VK_ESCAPE){done_=true;continue;}if(!IsDialogMessageW(window_,&msg)){TranslateMessage(&msg);DispatchMessageW(&msg);}}
        DestroyWindow(window_);DeleteObject(font_);
    }
};
}
