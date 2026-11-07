#include <windows.h>
#include <commctrl.h>
#include <gdiplus.h>
#include <fstream>
#include <string>
#include <cstdio>
#include <cmath>
HWND app,cv;
int page=1;
void key(WORD k,bool down=true){INPUT i{};i.type=INPUT_KEYBOARD;i.ki.wVk=k;i.ki.dwFlags=down?0:KEYEVENTF_KEYUP;SendInput(1,&i,sizeof(i));}
void click(HWND w){RECT r{};GetWindowRect(w,&r);SetCursorPos((r.left+r.right)/2,(r.top+r.bottom)/2);INPUT i{};i.type=INPUT_MOUSE;i.mi.dwFlags=MOUSEEVENTF_LEFTDOWN;SendInput(1,&i,sizeof(i));Sleep(45);i.mi.dwFlags=MOUSEEVENTF_LEFTUP;SendInput(1,&i,sizeof(i));Sleep(120);}
void pos(double x,double y){RECT r{};GetClientRect(cv,&r);POINT o{0,0};ClientToScreen(cv,&o);double z=(r.right-40.)/900.;double top=16;if(page==3)top=std::max(16.,r.bottom-560*z-16);SetCursorPos(lround(o.x+20+x*z),lround(o.y+top+(560-y)*z));}
int main(int argc,char**argv){
 app=FindWindowW(L"PaperViewMain",nullptr);if(!app)return 2;
 cv=FindWindowExW(app,nullptr,L"PaperViewCanvas",nullptr);
 SetForegroundWindow(app);std::ifstream f(argv[1]);std::string cmd;
 while(f>>cmd){
  if(cmd=="close"){PostMessageW(app,WM_CLOSE,0,0);Sleep(500);}
  else if(cmd=="resize"){int w,h;f>>w>>h;w=std::min(w,GetSystemMetrics(SM_CXSCREEN)-40);h=std::min(h,GetSystemMetrics(SM_CYSCREEN)-48);ShowWindow(app,SW_RESTORE);SetWindowPos(app,HWND_TOP,24,20,w,h,SWP_SHOWWINDOW);Sleep(700);}
  else if(cmd=="info"){RECT a{},r{};GetWindowRect(app,&a);GetClientRect(cv,&r);POINT o{};ClientToScreen(cv,&o);printf("app %ld %ld %ld %ld; canvas %ld %ld %ld %ld; zoom %.6f\n",a.left,a.top,a.right,a.bottom,o.x,o.y,r.right,r.bottom,(r.right-40.)/900.);}
  else if(cmd=="button"){int id;f>>id;PostMessageW(app,WM_COMMAND,id,0);Sleep(250);}
  else if(cmd=="page"){f>>page;HWND e=GetDlgItem(app,116);click(e);SetWindowTextW(e,std::to_wstring(page).c_str());key(VK_RETURN);key(VK_RETURN,false);Sleep(800);}
  else if(cmd=="palette"){
   int color,width;f>>color>>width;PostMessageW(app,WM_COMMAND,103,0);HWND p=nullptr;
   for(int i=0;i<60&&!p;i++){Sleep(30);p=FindWindowW(L"PDFViewPenPalette",nullptr);}if(!p)return 3;
   SendMessageW(p,WM_COMMAND,MAKEWPARAM(200+color,BN_CLICKED),(LPARAM)GetDlgItem(p,200+color));
   HWND s=FindWindowExW(p,nullptr,TRACKBAR_CLASSW,nullptr);SendMessageW(s,TBM_SETPOS,TRUE,width);SendMessageW(p,WM_HSCROLL,TB_THUMBTRACK,(LPARAM)s);Sleep(120);
   PostMessageW(p,WM_CLOSE,0,0);PostMessageW(p,WM_NULL,0,0);Sleep(250);
  }
  else if(cmd=="stroke"){
   int n;double x,y;f>>n>>x>>y;pos(x,y);Sleep(25);INPUT i{};i.type=INPUT_MOUSE;i.mi.dwFlags=MOUSEEVENTF_LEFTDOWN;SendInput(1,&i,sizeof(i));Sleep(15);
   for(int j=1;j<n;j++){f>>x>>y;pos(x,y);Sleep(12);}Sleep(15);i.mi.dwFlags=MOUSEEVENTF_LEFTUP;SendInput(1,&i,sizeof(i));Sleep(80);
  }
  else if(cmd=="wait"){int ms;f>>ms;Sleep(ms);}
  else if(cmd=="capture"){
   std::string file;f>>file;SetCursorPos(0,0);RedrawWindow(app,nullptr,nullptr,RDW_INVALIDATE|RDW_ALLCHILDREN|RDW_UPDATENOW);Sleep(1200);
   RECT r{};GetWindowRect(app,&r);int w=r.right-r.left,h=r.bottom-r.top;
   HDC screen=GetDC(nullptr),dc=CreateCompatibleDC(screen);HBITMAP bitmap=CreateCompatibleBitmap(screen,w,h);HGDIOBJ old=SelectObject(dc,bitmap);
   if(!BitBlt(dc,0,0,w,h,screen,r.left,r.top,SRCCOPY|CAPTUREBLT))return 5;
   Gdiplus::GdiplusStartupInput input;ULONG_PTR token;Gdiplus::GdiplusStartup(&token,&input,nullptr);
   CLSID png={0x557cf406,0x1a04,0x11d3,{0x9a,0x73,0x00,0x00,0xf8,0x1e,0xf3,0x2e}};
   {Gdiplus::Bitmap image(bitmap,nullptr);auto status=image.Save(std::wstring(file.begin(),file.end()).c_str(),&png);if(status!=Gdiplus::Ok)return 6;}
   Gdiplus::GdiplusShutdown(token);SelectObject(dc,old);DeleteObject(bitmap);DeleteDC(dc);ReleaseDC(nullptr,screen);printf("Screenshot saved: %s (%dx%d)\n",file.c_str(),w,h);
  }
  else if(cmd=="park"){SetCursorPos(0,0);Sleep(300);}
  else return 4;
 }
}
