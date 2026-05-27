#include <chrono>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

#include "logging/FileLogSink.h"
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

[[nodiscard]] dasall::infra::logging::LogEvent make_event(std::string message) {
  return dasall::infra::logging::LogEvent{
      .level = dasall::infra::logging::LogLevel::Info,
      .module = std::string("logging"),
      .message = std::move(message),
      .attrs = {{"request_id", "req-file-sink-003"}},
      .ts = 1711968607000,
  };
}

[[nodiscard]] std::string read_text(const fs::path& path) {
  std::ifstream stream(path, std::ios::binary);
  return std::string(std::istreambuf_iterator<char>(stream),
                     std::istreambuf_iterator<char>());
}

void test_file_log_sink_writes_to_installed_authoritative_path_under_state_root_override() {
  using dasall::infra::logging::FileLogPathPolicy;
  using dasall::infra::logging::FileLogSink;
  using dasall::infra::logging::FileLogSinkOptions;
  using dasall::tests::support::assert_true;

  ScopedTempDir state_root("dasall-file-log-sink-state-root");

  FileLogSink sink(FileLogSinkOptions{
      .file_path = {},
      .state_root_override = state_root.path(),
      .rotate_max_size_bytes = 2048U,
      .rotate_max_files = 2U,
      .path_policy = FileLogPathPolicy::InstalledAuthoritative,
  });

  const auto result = sink.write(make_event(
      R"({"schema_version":"dasall.logging.event.v1","message":"Authorization: Bearer <redacted>"})"));
  assert_true(result.ok,
              "FileLogSink should persist a structured redacted event under the installed authoritative path policy");
  assert_true(
      sink.resolved_file_path() == state_root.path() / "logging" / "runtime.log",
      "FileLogSink should resolve the installed authoritative runtime log path under the provided state_root override");
  assert_true(fs::exists(sink.resolved_file_path()),
              "FileLogSink should create the installed authoritative runtime log file when the parent directory is missing");

  const auto text = read_text(sink.resolved_file_path());
  assert_true(text.find("dasall.logging.event.v1") != std::string::npos,
              "FileLogSink should preserve the structured schema version in the persisted line");
  assert_true(text.find("<redacted>") != std::string::npos,
              "FileLogSink should preserve the redacted payload in the persisted line");
}

void test_file_log_sink_rotates_when_the_size_budget_is_exceeded() {
  using dasall::infra::logging::FileLogSink;
  using dasall::infra::logging::FileLogSinkOptions;
  using dasall::tests::support::assert_true;

  ScopedTempDir temp_dir("dasall-file-log-sink-rotation");
  const auto log_path = temp_dir.path() / "rotation" / "runtime.log";
  fs::create_directories(log_path.parent_path());

  FileLogSink sink(FileLogSinkOptions{
      .file_path = log_path,
      .state_root_override = {},
      .rotate_max_size_bytes = 96U,
      .rotate_max_files = 2U,
      .path_policy = dasall::infra::logging::FileLogPathPolicy::BuildTreeDefault,
  });

  const auto first_result = sink.write(make_event(std::string(80U, 'A')));
  const auto second_result = sink.write(make_event(std::string(80U, 'B')));
  assert_true(first_result.ok && second_result.ok,
              "FileLogSink should accept writes before and after a rotation boundary");
  assert_true(sink.rotation_total() == 1U,
              "FileLogSink should report one rotation after the size budget is exceeded once");

  const auto rotated_path = fs::path(log_path.string() + ".1");
  assert_true(fs::exists(log_path),
              "FileLogSink should keep the active runtime log file after rotation");
  assert_true(fs::exists(rotated_path),
              "FileLogSink should preserve the rotated runtime log file when the rotation budget is exceeded");

  const auto active_text = read_text(log_path);
  const auto rotated_text = read_text(rotated_path);
  assert_true(active_text.find(std::string(80U, 'B')) != std::string::npos,
              "FileLogSink should keep the newest payload in the active file after rotation");
  assert_true(rotated_text.find(std::string(80U, 'A')) != std::string::npos,
              "FileLogSink should move the older payload into the first rotation file");
}

}  // namespace

int main() {
  try {
    test_file_log_sink_writes_to_installed_authoritative_path_under_state_root_override();
    test_file_log_sink_rotates_when_the_size_budget_is_exceeded();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}