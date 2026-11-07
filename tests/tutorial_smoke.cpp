#include "backend.hpp"
#include "tutorial.hpp"
#include <fstream>
#include <iostream>
using namespace paper;
int main() {
    OleInitialize(nullptr);
    auto root = std::filesystem::temp_directory_path() /
                (L"PDFView-tutorial-test-" + std::to_wstring(GetCurrentProcessId()));
    bool passed = false;
    try {
        auto annotations = root / L"annotations";
        TutorialDocument tutorial(annotations);
        auto path = tutorial.prepare();
        auto key = hashFile(path, [] { return false; });
        AnnotationRepository repository(annotations);
        auto original = repository.load(key);
        size_t count = 0;
        bool ink = false, marker = false;
        for (const auto &[page, strokes] : original.pages) {
            if (page < 3 || page > 5)
                throw std::runtime_error("Examples must belong to practice pages");
            count += strokes.size();
            for (const auto &stroke : strokes) {
                ink |= stroke->kind == PenKind::Ink;
                marker |= stroke->kind == PenKind::Highlighter;
            }
        }
        if (count != 54 || !ink || !marker)
            throw std::runtime_error("Missing editable ink/highlighter examples");
        PdfBackend backend(executableDirectory() / L"pdfium.dll");
        auto doc = backend.open(1, path.wstring(), "", [] { return false; });
        if (doc.sizes.size() != 6 || doc.outline.size() != 6 || doc.fingerprint != key)
            throw std::runtime_error("Tutorial navigation or PDF identity mismatch");
        if (backend.text(1, 1).find(L"荧光笔") == std::wstring::npos ||
            backend.text(1, 5).find(L"H₂SO₄") == std::wstring::npos)
            throw std::runtime_error("Tutorial text/formulas did not survive embedding");
        for (int page = 0; page < 6; ++page) {
            auto tile = backend.render({1, page, -1, 0, 0, 0}, [] { return false; });
            if (!tile || tile->pixels.empty())
                throw std::runtime_error("Tutorial page rendering failed");
        }
        backend.close(1);
        // Erasing a whole practice page must survive reopening; never silently reseed.
        original.pages.erase(3);
        repository.save(key, original);
        tutorial.prepare();
        auto restored = repository.load(key);
        if (restored.pages.count(3) || !restored.pages.count(4) || !restored.pages.count(5))
            throw std::runtime_error("Reopening replaced the learner's edits");
        // A normal document in the same annotation repository remains untouched.
        const auto otherKey = hashText("a user's unrelated document");
        repository.save(otherKey, {});
        auto unrelated = annotations / (otherKey + ".ink");
        auto modified = std::filesystem::last_write_time(unrelated);
        tutorial.prepare();
        if (std::filesystem::last_write_time(unrelated) != modified)
            throw std::runtime_error("Tutorial modified unrelated annotations");
        passed = true;
        std::cout << "PASS six embedded PDF pages, outlines, Unicode equations, editable example ink, "
                     "persistent learner edits and annotation isolation\n";
    } catch (const std::exception &error) {
        std::cerr << "FAIL " << error.what() << '\n';
    }
    std::filesystem::remove_all(root);
    OleUninitialize();
    return passed ? 0 : 1;
}
