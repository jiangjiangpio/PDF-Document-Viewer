#include "tutorial.hpp"
#include "backend.hpp"
#include "resources.h"
#include <fstream>
namespace paper {
namespace {
std::string resource(int id) {
    HMODULE module = GetModuleHandleW(nullptr);
    HRSRC item = FindResourceW(module, MAKEINTRESOURCEW(id), RT_RCDATA);
    HGLOBAL data = item ? LoadResource(module, item) : nullptr;
    DWORD size = item ? SizeofResource(module, item) : 0;
    const char *bytes = data ? static_cast<const char *>(LockResource(data)) : nullptr;
    if (!bytes || !size)
        throw std::runtime_error("Bundled tutorial resource is unavailable");
    return {bytes, size};
}
void materialize(const std::filesystem::path &path, const std::string &bytes) {
    if (std::filesystem::exists(path) && hashFile(path, [] { return false; }) == hashText(bytes))
        return;
    std::filesystem::create_directories(path.parent_path());
    auto pending = path;
    pending += L".tmp";
    {
        std::ofstream out(pending, std::ios::binary | std::ios::trunc);
        out.write(bytes.data(), bytes.size());
        out.close();
        if (!out)
            throw std::runtime_error("Cannot prepare the local tutorial document");
    }
    if (!MoveFileExW(pending.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        throw std::runtime_error("Cannot install the local tutorial document");
}
} // namespace
std::filesystem::path TutorialDocument::prepare() const {
    const auto pdf = resource(IDR_TUTORIAL_PDF);
    const auto key = hashText(pdf);
    const auto folder = annotations_.parent_path() / L"tutorial" / utf16(key.substr(0, 16));
    const auto path = folder / L"PDF view 功能教程.pdf";
    materialize(path, pdf);
    // Seed once. Reopening must preserve edits, including erased example strokes.
    if (!std::filesystem::exists(annotations_ / (key + ".ink"))) {
        auto examples = folder / L"examples";
        materialize(examples / (key + ".ink"), resource(IDR_TUTORIAL_INK));
        auto snapshot = AnnotationRepository(examples).load(key);
        AnnotationRepository(annotations_).save(key, snapshot);
    }
    return path;
}
} // namespace paper
