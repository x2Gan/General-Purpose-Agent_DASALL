#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace dasall::tui::view {

class TuiInputHistory {
 public:
  void record(std::string entry) {
    if (contains_only_whitespace(entry)) {
      return;
    }

    entries_.push_back(std::move(entry));
  }

  [[nodiscard]] bool empty() const noexcept { return entries_.empty(); }

  [[nodiscard]] std::size_t size() const noexcept { return entries_.size(); }

  [[nodiscard]] const std::string& at(std::size_t index) const { return entries_.at(index); }

  [[nodiscard]] std::optional<std::size_t> older(
      std::optional<std::size_t> current) const noexcept {
    if (entries_.empty()) {
      return std::nullopt;
    }

    if (!current.has_value()) {
      return entries_.size() - 1U;
    }

    if (*current == 0U) {
      return current;
    }

    return *current - 1U;
  }

  [[nodiscard]] std::optional<std::size_t> newer(
      std::optional<std::size_t> current) const noexcept {
    if (!current.has_value() || *current + 1U >= entries_.size()) {
      return std::nullopt;
    }

    return *current + 1U;
  }

  [[nodiscard]] std::optional<std::size_t> latest_match(
      std::string_view query,
      std::optional<std::size_t> before = std::nullopt) const noexcept {
    const std::size_t upper_bound = before.value_or(entries_.size());
    for (std::size_t index = upper_bound; index > 0U; --index) {
      const auto& candidate = entries_[index - 1U];
      if (query.empty() || candidate.find(query) != std::string::npos) {
        return index - 1U;
      }
    }

    return std::nullopt;
  }

 private:
  [[nodiscard]] static bool contains_only_whitespace(std::string_view text) noexcept {
    for (const char character : text) {
      if (character != ' ' && character != '\t' && character != '\n' &&
          character != '\r') {
        return false;
      }
    }

    return true;
  }

  std::vector<std::string> entries_;
};

}  // namespace dasall::tui::view