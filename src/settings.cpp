#include "settings.hpp"
#include <fstream>
#include <sstream>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
namespace paper {
AppSettings::AppSettings() {
    bindings = {KeyChord{'O', 1}, {'F', 1},  {'S', 1},  {'W', 1},  {'Z', 1}, {'Y', 1},
                {'0', 1},         {0xbb, 1}, {0xbd, 1}, {0x7a, 0}, {'D', 1}, {'B', 1},
                {0xbc, 1},        {0x72, 0}, {0x72, 2}, {9, 1},    {9, 3},   {'C', 3}};
}
void AppSettings::validate() const {
    if (blurLevel < 0 || blurLevel > 3 || !std::isfinite(scrollSensitivity) || scrollSensitivity < .25 ||
        scrollSensitivity > 3 || static_cast<int>(officeEngine) < 0 || static_cast<int>(officeEngine) > 2)
        throw std::runtime_error("Invalid settings value");
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
        L"适应宽度", L"放大",           L"缩小",           L"全屏",         L"绘制开关",     L"侧边栏开关",
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
    bool version = false;
    while (std::getline(in, line)) {
        std::istringstream row(line);
        std::string key;
        row >> key;
        if (key.empty() || key[0] == '#')
            continue;
        if (key == "PDFVIEW_SETTINGS") {
            int v = 0;
            row >> v;
            if (v != 1)
                throw std::runtime_error("Unsupported settings version");
            version = true;
        } else if (key == "sidebar" || key == "restore" || key == "office_pdf") {
            int v = -1;
            row >> v;
            if (v < 0 || v > 1)
                throw std::runtime_error("Invalid boolean");
            (key == "sidebar"   ? s.openSidebar
             : key == "restore" ? s.restoreSession
                                : s.officePdfMode) = v != 0;
        } else if (key == "blur")
            row >> s.blurLevel;
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
    if (!version)
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
        out << "PDFVIEW_SETTINGS 1\nsidebar " << s.openSidebar << "\nrestore " << s.restoreSession
            << "\noffice_pdf " << s.officePdfMode << "\nblur " << s.blurLevel << "\nscroll "
            << s.scrollSensitivity << "\noffice " << static_cast<int>(s.officeEngine) << "\n";
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
