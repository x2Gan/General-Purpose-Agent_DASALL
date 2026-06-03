#include <chrono>
#include <exception>
#include <filesystem>
#include <iostream>
#include <string>
#include <system_error>

#include "MemoryInstalledProofRunner.h"
#include "support/TestAssertions.h"

namespace {

using dasall::tests::support::assert_equal;
using dasall::tests::support::assert_true;

class TempDir {
 public:
  explicit TempDir(const std::string& stem)
      : path_(std::filesystem::temp_directory_path() /
              (stem + "-" +
               std::to_string(std::chrono::duration_cast<std::chrono::microseconds>(
                                  std::chrono::system_clock::now().time_since_epoch())
                                  .count()))) {
    std::filesystem::create_directories(path_);
  }

  ~TempDir() {
    std::error_code error;
    std::filesystem::remove_all(path_, error);
  }

  [[nodiscard]] const std::filesystem::path& path() const {
    return path_;
  }

 private:
  std::filesystem::path path_;
};

void test_memory_installed_proof_runner_collects_prepare_context_and_writeback_evidence() {
  const TempDir state_root("dasall-memory-installed-proof-state");

  const auto result = dasall::apps::daemon::collect_memory_installed_proof(
      dasall::apps::daemon::MemoryInstalledProofOptions{
          .requested_profile_id = "edge_minimal",
          .deployment_config_path = std::nullopt,
          .state_root_override = state_root.path(),
      });

  assert_true(result.ok(),
              "memory installed proof runner should succeed on a temp state root: " +
                  result.error);
  assert_equal(std::string("mem-fix-006-local-proof"),
               result.expected_marker,
               "memory installed proof runner should preserve the expected marker");
  assert_equal(std::string("wal"),
               result.journal_mode,
               "memory installed proof runner should operate on a WAL-backed sqlite database");
  assert_true(result.core_table_count >= 5,
              "memory installed proof runner should observe the five core sqlite tables");
  assert_true(result.vector_table_count >= 1,
              "memory installed proof runner should observe the vector sidecar table");
  assert_true(result.session_summary_count_after_first >= 1,
              "memory installed proof runner should persist a first summary row");
  assert_true(result.session_turn_count_after_second >= 2,
              "memory installed proof runner should persist at least two turns");
  assert_true(result.session_summary_count_after_second >= 2,
              "memory installed proof runner should persist at least two summaries");
  assert_true(result.prepare_context_marker_visible,
              "memory installed proof runner should surface the expected marker via prepare_context");
  assert_true(result.latest_summary_references_second_turn,
              "memory installed proof runner should keep the second turn referenced by the latest summary");
  assert_true(result.latest_summary_text_prefix.find(result.expected_marker) !=
                  std::string::npos,
              "memory installed proof runner should keep the expected marker in the latest summary prefix");
}

}  // namespace

int main() {
  try {
    test_memory_installed_proof_runner_collects_prepare_context_and_writeback_evidence();
  } catch (const std::exception& exception) {
    std::cerr << exception.what() << '\n';
    return 1;
  }

  return 0;
}