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
    settings.blurLevel = 3;
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
              restored.officeEngine == OfficeEngine::Word && restored.bindings == settings.bindings,
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
