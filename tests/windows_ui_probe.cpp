// Test-only native resize driver; avoids X11-only resizing under Wine.
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <windows.h>
int main(int argc, char **argv) {
    HWND w = FindWindowW(L"PaperViewMain", nullptr);
    if (!w)
        return 1;
    if (argc == 3) {
        SetWindowPos(w, nullptr, 0, 0, atoi(argv[1]), atoi(argv[2]), SWP_NOMOVE | SWP_NOZORDER);
        Sleep(700);
    }
    RECT client{};
    GetClientRect(w, &client);
    printf("client=%ldx%ld dpi=%u\n", client.right, client.bottom, GetDpiForWindow(w));
    std::vector<RECT> bounds;
    for (int id = 100; id <= 125; id++) {
        HWND c = GetDlgItem(w, id);
        if (!c || !IsWindowVisible(c))
            continue;
        RECT r{};
        GetWindowRect(c, &r);
        MapWindowPoints(nullptr, w, reinterpret_cast<POINT *>(&r), 2);
        printf("id=%d x=%ld y=%ld w=%ld h=%ld\n", id, r.left, r.top, r.right - r.left, r.bottom - r.top);
        if (r.left < 0 || r.right > client.right || r.top < 0 || r.bottom > client.bottom)
            return 2;
        for (auto previous : bounds) {
            RECT overlap{};
            if (IntersectRect(&overlap, &previous, &r))
                return 3;
        }
        bounds.push_back(r);
    }
    return 0;
}
