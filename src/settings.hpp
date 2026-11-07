#pragma once
#include "core.hpp"
#include <array>
namespace paper {
enum class OfficeEngine : int { Auto, Word, Wps };
enum class Action : int {
    Open,
    Find,
    Save,
    CloseTab,
    Undo,
    Redo,
    Fit,
    ZoomIn,
    ZoomOut,
    Fullscreen,
    Draw,
    Sidebar,
    Settings,
    NextHit,
    PreviousHit,
    NextTab,
    PreviousTab,
    CopyText,
    Count
};
struct KeyChord {
    unsigned key = 0, modifiers = 0; // Ctrl=1, Shift=2, Alt=4; Windows virtual keys.
    bool operator==(const KeyChord &) const = default;
};
struct AppSettings {
    bool openSidebar = true, restoreSession = true, officePdfMode = false;
    int blurLevel = 1;
    double scrollSensitivity = 1;
    OfficeEngine officeEngine = OfficeEngine::Auto;
    std::array<KeyChord, static_cast<size_t>(Action::Count)> bindings{};
    AppSettings();
    void validate() const;
    std::optional<Action> action(KeyChord chord) const;
    static std::wstring actionName(Action action);
};
class SettingsRepository {
    std::filesystem::path path_;

  public:
    explicit SettingsRepository(std::filesystem::path path) : path_(std::move(path)) {}
    AppSettings load() const;
    void save(const AppSettings &settings) const;
};
} // namespace paper
