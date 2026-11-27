#include "settings.hpp"
#include <fstream>
#include <iostream>
using namespace paper;
int main() {
    int passed = 0;
    auto check = [&](bool ok, const char *name) {
        if (!ok)
            throw std::runtime_error(name);
        ++passed;
        std::cout << "PASS " << name << '\n';
    };
    AppSettings defaults;
    defaults.validate();
    check(defaults.action({'O', 1}) == Action::Open, "default shortcuts route correctly");
    auto settings = defaults;
    settings.openSidebar = false;
    settings.restoreSession = false;
    settings.officePdfMode = true;
    settings.autoPenWidth = false;
    settings.penColor = 0xdc2626;
    settings.penWidth = 6.5;
    settings.penScreenWidthMm = 1.3;
    settings.dailyPenWidthMm = 0.85;
    settings.screenCalibrations["test monitor:1920x1080"] = 4.253721943;
    settings.penWidthOverrideDay = beijingDay();
    settings.blurLevel = 3;
    settings.frostTransparency = 37;
    settings.frostTint = FrostTint::White;
    settings.scrollSensitivity = 2.25;
    settings.officeEngine = OfficeEngine::Word;
    settings.bindings[(int)Action::Open] = {'L', 3};
    settings.validate();
    check(!settings.action({'O', 1}) && settings.action({'L', 3}) == Action::Open,
          "rebind removes old shortcut");
    auto dir = std::filesystem::temp_directory_path() / "pdf-view-settings-regression";
    std::filesystem::remove_all(dir);
    SettingsRepository repo(dir / "settings.txt");
    repo.save(settings);
    auto restored = repo.load();
    check(restored.officePdfMode && !restored.openSidebar && !restored.restoreSession &&
              restored.blurLevel == 3 && restored.scrollSensitivity == 2.25 &&
              restored.frostTransparency == 37 && restored.frostTint == FrostTint::White &&
              restored.officeEngine == OfficeEngine::Word && !restored.autoPenWidth &&
              restored.penColor == settings.penColor && restored.penWidth == settings.penWidth &&
              restored.penScreenWidthMm == settings.penScreenWidthMm &&
              restored.dailyPenWidthMm == settings.dailyPenWidthMm &&
              restored.screenCalibrations == settings.screenCalibrations &&
              restored.penWidthOverrideDay == settings.penWidthOverrideDay &&
              restored.bindings == settings.bindings,
          "all settings persist");
    auto duplicate = settings;
    duplicate.bindings[0] = duplicate.bindings[1];
    bool rejected = false;
    try {
        repo.save(duplicate);
    } catch (...) {
        rejected = true;
    }
    check(rejected && repo.load().bindings == settings.bindings,
          "duplicate shortcuts cannot overwrite saved settings");
    auto reserved = settings;
    reserved.bindings[0] = {0x73, 4};
    rejected = false;
    try {
        reserved.validate();
    } catch (...) {
        rejected = true;
    }
    check(rejected, "system close shortcut remains reserved");
    auto invalid = settings;
    invalid.scrollSensitivity = 0;
    rejected = false;
    try {
        invalid.validate();
    } catch (...) {
        rejected = true;
    }
    check(rejected, "invalid sensitivity rejected");
    {
        std::ofstream out(dir / "settings.txt");
        out << "PDFVIEW_SETTINGS 2\npen_screen_width_mm 1.3\n";
    }
    auto legacy = repo.load();
    check(legacy.dailyPenWidthMm == 2 && legacy.penScreenWidthMm == 1.3,
          "older settings retain manual width and use 2 mm as daily width");
    {
        std::ofstream out(dir / "settings.txt");
        out << "PDFVIEW_SETTINGS 1\nblur garbage\n";
    }
    rejected = false;
    try {
        repo.load();
    } catch (...) {
        rejected = true;
    }
    check(rejected, "malformed settings reported rather than silently saved");
    std::filesystem::remove_all(dir);
    check(repo.load().bindings == defaults.bindings, "missing file restores defaults");
    std::cout << passed << " passed\n";
}
