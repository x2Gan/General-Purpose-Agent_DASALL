#include <chrono>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#if defined(__linux__)
#include <unistd.h>
#endif

#include "support/TestAssertions.h"
#include "util/TokenEstimator.h"

namespace {

namespace fs = std::filesystem;

class ScopedEnvironmentVariable final {
 public:
  explicit ScopedEnvironmentVariable(const char* name)
      : name_(name) {
    if (const char* current_value = std::getenv(name_); current_value != nullptr) {
      original_value_ = std::string(current_value);
    }
  }

  ~ScopedEnvironmentVariable() {
    if (original_value_.has_value()) {
      ::setenv(name_, original_value_->c_str(), 1);
      return;
    }

    ::unsetenv(name_);
  }

  void set(const std::string& value) const {
    ::setenv(name_, value.c_str(), 1);
  }

 private:
  const char* name_;
  std::optional<std::string> original_value_;
};

class ScopedDirectoryCleanup final {
 public:
  explicit ScopedDirectoryCleanup(fs::path path)
      : path_(std::move(path)) {}

  ~ScopedDirectoryCleanup() {
    std::error_code error_code;
    if (!path_.empty()) {
      fs::permissions(path_, fs::perms::owner_all, fs::perm_options::replace, error_code);
      error_code.clear();
      fs::remove_all(path_.parent_path(), error_code);
    }
  }

 private:
  fs::path path_;
};

struct TokenExpectation {
  std::string_view text;
  int expected_tokens;
};

[[nodiscard]] fs::path make_temp_inaccessible_root() {
  const auto suffix = std::to_string(
      std::chrono::steady_clock::now().time_since_epoch().count());
  const auto parent = fs::temp_directory_path() /
                      ("dasall-token-estimator-" + std::to_string(::getpid()) + "-" + suffix);
  const auto inaccessible_root = parent / "no-access";
  fs::create_directories(inaccessible_root);
  fs::permissions(inaccessible_root, fs::perms::none, fs::perm_options::replace);
  return inaccessible_root;
}

void test_tiktoken_estimator_matches_openai_cl100k_reference_examples() {
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  const auto estimator = dasall::memory::util::create_token_estimator(
      dasall::memory::TokenEstimatorBackend::Tiktoken);
  assert_true(
      dynamic_cast<const dasall::memory::util::HeuristicTokenEstimator*>(estimator.get()) ==
          nullptr,
      "tiktoken accuracy test requires the vendored cl100k tokenizer instead of the heuristic fallback");

  const std::vector<TokenExpectation> expectations{
      TokenExpectation{.text = "antidisestablishmentarianism", .expected_tokens = 6},
      TokenExpectation{.text = "2 + 2 = 4", .expected_tokens = 7},
      TokenExpectation{.text = "お誕生日おめでとう", .expected_tokens = 9},
  };

  for (const auto& expectation : expectations) {
    assert_equal(
        expectation.expected_tokens,
        estimator->estimate_text_tokens(expectation.text),
        "tiktoken estimator should match the published cl100k_base token count for: " +
            std::string(expectation.text));
  }
}

void test_tiktoken_estimator_skips_inaccessible_override_root() {
  using dasall::tests::support::assert_true;

  const auto inaccessible_root = make_temp_inaccessible_root();
  const ScopedDirectoryCleanup cleanup(inaccessible_root);
  const ScopedEnvironmentVariable asset_dir_override("DASALL_MEMORY_TIKTOKEN_ASSET_DIR");
  asset_dir_override.set(inaccessible_root.string());

  const auto estimator = dasall::memory::util::create_token_estimator(
      dasall::memory::TokenEstimatorBackend::Tiktoken);
  assert_true(
      dynamic_cast<const dasall::memory::util::HeuristicTokenEstimator*>(estimator.get()) ==
          nullptr,
      "tiktoken estimator should ignore inaccessible override roots and keep loading the packaged tokenizer asset");
}

}  // namespace

int main() {
  try {
    test_tiktoken_estimator_matches_openai_cl100k_reference_examples();
    test_tiktoken_estimator_skips_inaccessible_override_root();
  } catch (const std::exception& exception) {
    std::cerr << exception.what() << '\n';
    return 1;
  }

  return 0;
}