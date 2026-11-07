#pragma once
#include "settings.hpp"
#include <windows.h>
namespace paper {
std::wstring shortcutLabel(KeyChord key);
bool showSettingsDialog(HWND owner, AppSettings &settings);
} // namespace paper
