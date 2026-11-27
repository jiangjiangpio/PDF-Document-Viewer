#include "settings.hpp"
#include <chrono>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
namespace paper {
int64_t beijingDay(int64_t utcSeconds) {
    const int64_t shifted = utcSeconds + 8 * 60 * 60;
    return shifted / 86400 - (shifted < 0 && shifted % 86400 != 0 ? 1 : 0);
}
int64_t beijingDay() {
    return beijingDay(std::chrono::duration_cast<std::chrono::seconds>(
                          std::chrono::system_clock::now().time_since_epoch()).count());
}
AppSettings::AppSettings() {
    bindings = {KeyChord{'O', 1}, {'F', 1},  {'S', 1},  {'W', 1},  {'Z', 1}, {'Y', 1},
                {'0', 1},         {0xbb, 1}, {0xbd, 1}, {0x7a, 0}, {'D', 1}, {'B', 1},
                {0xbc, 1},        {0x72, 0}, {0x72, 2}, {9, 1},    {9, 3},   {'C', 3}};
}
void AppSettings::validate() const {
    if (blurLevel < 0 || blurLevel > 3 || frostTransparency < 0 || frostTransparency > 100 ||
        (frostTint != FrostTint::White && frostTint != FrostTint::Gray) ||
        !std::isfinite(scrollSensitivity) || scrollSensitivity < .25 ||
        scrollSensitivity > 3 || (penColor & 0xff000000u) != 0 || !std::isfinite(penWidth) ||
        penWidth <= 0 || penWidth > 4096 || !std::isfinite(penScreenWidthMm) ||
        penScreenWidthMm < minScreenPenWidthMm || penScreenWidthMm > maxScreenPenWidthMm ||
        !std::isfinite(dailyPenWidthMm) || dailyPenWidthMm < minScreenPenWidthMm ||
        dailyPenWidthMm > maxScreenPenWidthMm || penWidthOverrideDay < -1 ||
        static_cast<int>(officeEngine) < 0 || static_cast<int>(officeEngine) > 2)
        throw std::runtime_error("Invalid settings value");
    if (screenCalibrations.size() > 128)
        throw std::runtime_error("Too many screen calibrations");
    for (const auto &[display, density] : screenCalibrations)
        if (display.empty() || display.size() > 2048 || display.find_first_of("\r\n") != std::string::npos ||
            !std::isfinite(density) || density < .5 || density > 20)
            throw std::runtime_error("Invalid screen calibration");
    for (size_t i = 0; i < bindings.size(); ++i) {
        auto c = bindings[i];
        if (c.key > 254 || c.modifiers > 7 || (!c.key && c.modifiers))
            throw std::runtime_error("Invalid shortcut");
        if (!c.key)
            continue;
        if (c.key == 0x1b || c.key == 0x10 || c.key == 0x11 || c.key == 0x12 || c.key == 0x5b ||
            c.key == 0x5c || (c.key == 9 && !c.modifiers) || c.key == 13 ||
            (c.key == 0x73 && (c.modifiers & 4)) || (c.key == 9 && (c.modifiers & 4)) ||
            (c.key == 0x2e && (c.modifiers & 5) == 5) || (c.key == 0x1b && (c.modifiers & 1)))
            throw std::runtime_error("Reserved shortcut");
        if (!c.modifiers && !(c.key >= 0x70 && c.key <= 0x87))
            throw std::runtime_error("Use Ctrl, Shift or Alt with non-function keys");
        for (size_t j = 0; j < i; ++j)
            if (c == bindings[j])
                throw std::runtime_error("Duplicate shortcut");
    }
}
std::optional<Action> AppSettings::action(KeyChord chord) const {
    if (!chord.key)
        return {};
    for (size_t i = 0; i < bindings.size(); ++i)
        if (bindings[i] == chord)
            return static_cast<Action>(i);
    return {};
}
std::wstring AppSettings::actionName(Action a) {
    static constexpr const wchar_t *names[] = {
        L"打开文档", L"查找文字",       L"保存批注",       L"关闭标签页",   L"撤销",         L"重做",
        L"适应宽度", L"放大",           L"缩小",           L"全屏",         L"画笔开关",     L"侧边栏开关",
        L"设置",     L"下一个搜索结果", L"上一个搜索结果", L"下一个标签页", L"上一个标签页", L"复制本页文字"};
    return names[static_cast<size_t>(a)];
}
AppSettings SettingsRepository::load() const {
    AppSettings s;
    std::ifstream in(path_);
    if (!in) {
        if (std::filesystem::exists(path_))
            throw std::runtime_error("Cannot read settings");
        return s;
    }
    std::string line;
    int settingsVersion = 0;
    while (std::getline(in, line)) {
        std::istringstream row(line);
        std::string key;
        row >> key;
        if (key.empty() || key[0] == '#')
            continue;
        if (key == "PDFVIEW_SETTINGS") {
            int v = 0;
            row >> v;
            if (v != 1 && v != 2)
                throw std::runtime_error("Unsupported settings version");
            settingsVersion = v;
        } else if (key == "sidebar" || key == "restore" || key == "office_pdf") {
            int v = -1;
            row >> v;
            if (v < 0 || v > 1)
                throw std::runtime_error("Invalid boolean");
            (key == "sidebar"   ? s.openSidebar
             : key == "restore" ? s.restoreSession
                                : s.officePdfMode) = v != 0;
        } else if (key == "auto_pen_width") {
            int v = -1;
            row >> v;
            if (v < 0 || v > 1)
                throw std::runtime_error("Invalid boolean");
            s.autoPenWidth = v != 0;
        } else if (key == "pen_color") {
            row >> std::hex >> s.penColor >> std::dec;
        } else if (key == "pen_width")
            row >> s.penWidth;
        else if (key == "pen_screen_width_mm")
            row >> s.penScreenWidthMm;
        else if (key == "daily_pen_width_mm")
            row >> s.dailyPenWidthMm;
        else if (key == "screen_calibration") {
            std::string display;
            double density = 0;
            row >> std::quoted(display) >> density;
            s.screenCalibrations[display] = density;
        }
        else if (key == "pen_width_override_day")
            row >> s.penWidthOverrideDay;
        else if (key == "blur")
            row >> s.blurLevel;
        else if (key == "frost_transparency")
            row >> s.frostTransparency;
        else if (key == "frost_tint") {
            int value = -1;
            row >> value;
            s.frostTint = static_cast<FrostTint>(value);
        }
        else if (key == "scroll")
            row >> s.scrollSensitivity;
        else if (key == "office") {
            int v = -1;
            row >> v;
            s.officeEngine = static_cast<OfficeEngine>(v);
        } else if (key == "key") {
            size_t i = s.bindings.size();
            unsigned k = 0, m = 0;
            row >> i >> k >> m;
            if (i >= s.bindings.size())
                throw std::runtime_error("Invalid shortcut index");
            s.bindings[i] = {k, m};
        } else
            continue;
        if (row.fail())
            throw std::runtime_error("Invalid settings file");
    }
    if (!settingsVersion)
        throw std::runtime_error("Invalid settings header");
    s.validate();
    return s;
}
void SettingsRepository::save(const AppSettings &s) const {
    s.validate();
    std::filesystem::create_directories(path_.parent_path());
    auto tmp = path_;
    tmp += L".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        out << std::setprecision(std::numeric_limits<double>::max_digits10);
        out << "PDFVIEW_SETTINGS 2\nsidebar " << s.openSidebar << "\nrestore " << s.restoreSession
            << "\noffice_pdf " << s.officePdfMode << "\nauto_pen_width " << s.autoPenWidth
            << "\npen_color " << std::hex << s.penColor << std::dec << "\npen_width " << s.penWidth
            << "\npen_screen_width_mm " << s.penScreenWidthMm << "\ndaily_pen_width_mm " << s.dailyPenWidthMm
            << "\npen_width_override_day "
            << s.penWidthOverrideDay << "\nblur " << s.blurLevel
            << "\nfrost_transparency " << s.frostTransparency << "\nfrost_tint " << static_cast<int>(s.frostTint)
            << "\nscroll " << s.scrollSensitivity
            << "\noffice " << static_cast<int>(s.officeEngine) << "\n";
        for (const auto &[display, density] : s.screenCalibrations)
            out << "screen_calibration " << std::quoted(display) << ' ' << density << '\n';
        for (size_t i = 0; i < s.bindings.size(); ++i)
            out << "key " << i << ' ' << s.bindings[i].key << ' ' << s.bindings[i].modifiers << '\n';
        out.flush();
        if (!out)
            throw std::runtime_error("Cannot write settings");
    }
#ifdef _WIN32
    if (!MoveFileExW(tmp.c_str(), path_.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        throw std::runtime_error("Cannot replace settings");
#else
    std::filesystem::rename(tmp, path_);
#endif
}
} // namespace paper
