#pragma once
#include <windows.h>

namespace paper {
struct AppSettings;
int showClearInkDialog(HWND owner, const AppSettings &settings);
}
