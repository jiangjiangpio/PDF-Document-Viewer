#pragma once
#include "core.hpp"
#include <array>
#include <cstdint>
namespace paper {
enum class OfficeEngine : int { Auto, Word, Wps };
enum class FrostTint : int { White, Gray };
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
    static constexpr double minScreenPenWidthMm = 0.1;
    static constexpr double maxScreenPenWidthMm = 10.0;

    bool openSidebar = true, restoreSession = true, officePdfMode = false;
    bool autoPenWidth = true;
    int blurLevel = 1;
    int frostTransparency = 60;
    FrostTint frostTint = FrostTint::Gray;
    double scrollSensitivity = 1;
    uint32_t penColor = 0x2563eb;
    double penWidth = 2.5;
    double penScreenWidthMm = 2;
    double dailyPenWidthMm = 2;
    std::map<std::string, double> screenCalibrations;
    int64_t penWidthOverrideDay = -1;
    OfficeEngine officeEngine = OfficeEngine::Auto;
    std::array<KeyChord, static_cast<size_t>(Action::Count)> bindings{};
    AppSettings();
    void validate() const;
    std::optional<Action> action(KeyChord chord) const;
    static std::wstring actionName(Action action);
};
int64_t beijingDay(int64_t utcSeconds);
int64_t beijingDay();
class SettingsRepository {
    std::filesystem::path path_;

  public:
    explicit SettingsRepository(std::filesystem::path path) : path_(std::move(path)) {}
    AppSettings load() const;
    void save(const AppSettings &settings) const;
};
} // namespace paper
