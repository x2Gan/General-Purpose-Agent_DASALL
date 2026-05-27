#include <chrono>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "logging/LogQueryService.h"
#include "support/TestAssertions.h"

namespace {

namespace fs = std::filesystem;

class ScopedTempDir {
 public:
  explicit ScopedTempDir(const std::string& stem)
      : path_(fs::temp_directory_path() /
              (stem + "-" +
               std::to_string(std::chrono::steady_clock::now()
                                  .time_since_epoch()
                                  .count()))) {
    fs::create_directories(path_);
  }

  ~ScopedTempDir() {
    std::error_code error;
    fs::remove_all(path_, error);
  }

  [[nodiscard]] const fs::path& path() const {
    return path_;
  }

 private:
  fs::path path_;
};

void write_text(const fs::path& path, std::string_view text) {
  fs::create_directories(path.parent_path());
  std::ofstream stream(path, std::ios::binary | std::ios::trunc);
  stream << text;
}

void test_log_retention_policy_prunes_expired_and_overflow_artifacts_without_touching_runtime_log() {
  using dasall::infra::logging::LogQueryArtifactIndexEntry;
  using dasall::infra::logging::LogQuerySelectorKind;
  using dasall::infra::logging::LogRetentionPolicy;
  using dasall::infra::logging::LogRetentionPolicyOptions;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  ScopedTempDir temp_dir("dasall-log-retention-policy");
  const auto artifact_root = temp_dir.path() / "query-artifacts";
  const auto runtime_log_path = temp_dir.path() / "runtime" / "runtime.log";
  fs::create_directories(artifact_root);

  write_text(artifact_root / "expired.json", "expired");
  write_text(artifact_root / "overflow.json", "overflow");
  write_text(artifact_root / "keep.json", "keep");
  write_text(runtime_log_path, "runtime baseline");

  LogRetentionPolicy policy(LogRetentionPolicyOptions{.retention_days = 1, .max_artifact_count = 1});
  const auto retained_entries = policy.apply(
      std::vector<LogQueryArtifactIndexEntry>{
          LogQueryArtifactIndexEntry{
              .artifact_ref = "diag://infra/logging/query/expired",
              .artifact_file_name = "expired.json",
              .query_id = "expired",
              .selector_kind = LogQuerySelectorKind::TraceId,
              .selector_value = "trace-expired",
              .checksum = "checksum-expired",
              .match_count = 1,
              .truncated = false,
              .created_at = 1712100000000,
          },
          LogQueryArtifactIndexEntry{
              .artifact_ref = "diag://infra/logging/query/overflow",
              .artifact_file_name = "overflow.json",
              .query_id = "overflow",
              .selector_kind = LogQuerySelectorKind::TraceId,
              .selector_value = "trace-overflow",
              .checksum = "checksum-overflow",
              .match_count = 1,
              .truncated = false,
              .created_at = 1712399999000,
          },
          LogQueryArtifactIndexEntry{
              .artifact_ref = "diag://infra/logging/query/keep",
              .artifact_file_name = "keep.json",
              .query_id = "keep",
              .selector_kind = LogQuerySelectorKind::TraceId,
              .selector_value = "trace-keep",
              .checksum = "checksum-keep",
              .match_count = 1,
              .truncated = false,
              .created_at = 1712400000000,
          },
      },
      artifact_root,
      1712400000000);

  assert_equal(1,
               static_cast<int>(retained_entries.size()),
               "LogRetentionPolicyTest should keep only the newest artifact when retention window and max count are enforced together");
  assert_true(retained_entries.front().query_id == "keep",
              "LogRetentionPolicyTest should retain the newest query artifact entry");
  assert_true(!fs::exists(artifact_root / "expired.json") &&
                  !fs::exists(artifact_root / "overflow.json") &&
                  fs::exists(artifact_root / "keep.json"),
              "LogRetentionPolicyTest should delete expired or overflow query artifacts while preserving the retained file");
  assert_true(fs::exists(runtime_log_path),
              "LogRetentionPolicyTest should never touch the primary runtime log during query artifact cleanup");
}

}  // namespace

int main() {
  try {
    test_log_retention_policy_prunes_expired_and_overflow_artifacts_without_touching_runtime_log();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}