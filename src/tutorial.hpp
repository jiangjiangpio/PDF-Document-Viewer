#pragma once
#include <filesystem>
#include <utility>
namespace paper {
// Owns the bundled lesson and its editable practice copy, separate from user files.
class TutorialDocument {
    std::filesystem::path annotations_;

  public:
    explicit TutorialDocument(std::filesystem::path annotations) : annotations_(std::move(annotations)) {}
    std::filesystem::path prepare() const;
};
} // namespace paper
