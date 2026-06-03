#include <exception>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <iostream>
#include <iterator>
#include <string>
#include <string>
#include <string_view>

#include "support/TestAssertions.h"

namespace {

using dasall::tests::support::assert_true;

namespace fs = std::filesystem;

class ScopedTempDirectory {
 public:
  explicit ScopedTempDirectory(std::string stem)
      : path_(fs::temp_directory_path() /
              (std::move(stem) + "-" +
               std::to_string(fs::file_time_type::clock::now().time_since_epoch().count()))) {
    fs::create_directories(path_);
  }

  ~ScopedTempDirectory() {
    std::error_code error;
    fs::remove_all(path_, error);
  }

  [[nodiscard]] const fs::path& path() const {
    return path_;
  }

 private:
  fs::path path_;
};

[[nodiscard]] fs::path repository_root() {
  return fs::path(__FILE__).parent_path().parent_path().parent_path().parent_path();
}

[[nodiscard]] std::string read_text_file(const fs::path& path) {
  std::ifstream stream(path);
  assert_true(stream.is_open(),
              "memory installed smoke should open " + path.string());
  return std::string(std::istreambuf_iterator<char>(stream),
                     std::istreambuf_iterator<char>());
}

void write_text_file(const fs::path& path, std::string_view content) {
  fs::create_directories(path.parent_path());
  std::ofstream stream(path, std::ios::binary | std::ios::trunc);
  assert_true(stream.is_open(), "memory installed smoke should write " + path.string());
  stream << content;
}

void assert_contains_all(std::string_view text,
                         std::initializer_list<std::string_view> needles,
                         std::string_view message_prefix) {
  for (const auto needle : needles) {
    assert_true(text.find(needle) != std::string_view::npos,
                std::string(message_prefix) + " should contain '" +
                    std::string(needle) + "'");
  }
}

void test_memory_installed_gate_wrapper_materializes_latest_json_schema() {
  const ScopedTempDirectory temp_root("dasall-memory-installed-smoke");
  const fs::path artifact_dir = temp_root.path() / "artifacts";
  const fs::path evidence_root = temp_root.path() / "evidence";
  const fs::path latest_json_path = evidence_root / "latest.json";
  const fs::path summary_symlink_path = evidence_root / "latest";

  write_text_file(artifact_dir / "run-first.json",
                  R"({"disposition":"completed","task_completed":true})");
  write_text_file(artifact_dir / "run-second.json",
                  R"({"answer":"mem-installed-marker","task_completed":true})");
  write_text_file(
      artifact_dir / "memory-proof.json",
      R"({
  "session_id":"session:memory-installed-smoke",
  "expected_marker":"mem-installed-marker",
  "first_turn_id":"turn:1",
  "second_turn_id":"turn:2",
  "journal_mode":"wal",
  "core_table_count":5,
  "vector_table_count":1,
  "session_turn_count_after_second":2,
  "session_summary_count_after_second":2,
  "latest_summary_source_turn_ids_json":"[\"turn:2\"]",
  "latest_summary_text_prefix":"mem-installed-marker summary"
})");
  write_text_file(
      artifact_dir / "memory-maintenance-proof.json",
      R"({
  "ok":true,
  "effective_profile_id":"edge_minimal",
  "turns_before":121,
  "turns_after":120,
  "retention_turns":120,
  "quarantine_rows_after":0,
  "maintenance_report":{
    "checkpoint_executed":true,
    "checkpoint_wal_pages_remaining":0
  }
})");

  const std::string command =
      "bash \"" +
      (repository_root() / "scripts" / "packaging" /
       "validate_memory_installed_or_qemu.sh")
          .string() +
      "\" --reuse-artifacts --artifact-dir \"" + artifact_dir.string() +
      "\" --evidence-root \"" + evidence_root.string() + "\" >/dev/null";
  const int command_rc = std::system(command.c_str());
  assert_true(command_rc == 0,
              "memory installed wrapper should summarize reused artifacts successfully");
  assert_true(fs::exists(latest_json_path),
              "memory installed wrapper should materialize latest.json");
  assert_true(fs::exists(summary_symlink_path),
              "memory installed wrapper should materialize latest symlink");

  const auto latest_json = read_text_file(latest_json_path);
  assert_contains_all(
      latest_json,
      {
          "\"schema_version\": 1",
          "\"authoritative_owner\": \"scripts/packaging/validate_memory_installed_or_qemu.sh\"",
          "\"mode\": \"local-installed\"",
          "\"artifact_collection_mode\": \"reuse-artifacts\"",
          "\"init\": true",
          "\"open_store\": true",
          "\"prepare_context\": true",
          "\"write_back\": true",
          "\"maintenance\": true",
          "\"effective_profile_id\": \"edge_minimal\"",
          "\"session_id\": \"session:memory-installed-smoke\"",
          "\"journal_mode\": \"wal\"",
          "\"checkpoint_executed\": true",
          "\"status\": \"not-requested\"",
      },
      "memory installed latest.json schema");
}

void test_memory_installed_gate_script_keeps_authoritative_local_evidence_flow() {
  const auto script_text = read_text_file(
      repository_root() / "scripts" / "packaging" /
      "validate_memory_installed_or_qemu.sh");

  assert_contains_all(
      script_text,
      {
          "pkg_smoke_install.sh",
          "--explicit-start-check",
          "--reuse-artifacts",
          "memory-proof.json",
          "memory-maintenance-proof.json",
          "latest.json",
          ".cache/dasall/memory/installed-evidence",
          "validate_gate_int_10_installed_package_qemu.sh",
      },
      "memory installed gate script");
}

void test_memory_integration_cmake_registers_memory_installed_smoke_entrypoints() {
  const auto cmake_text = read_text_file(
      repository_root() / "tests" / "integration" / "memory" / "CMakeLists.txt");

  assert_contains_all(
      cmake_text,
      {
          "dasall_memory_installed_smoke_integration_test",
          "MemoryInstalledSmokeTest",
          "MemoryInstalledSmokeTest.cpp",
          "add_custom_target(memory_installed_smoke",
      },
      "memory integration cmake");
}

void test_packaging_readme_keeps_memory_owner_gate_wording() {
  const auto readme_text = read_text_file(
      repository_root() / "scripts" / "packaging" / "README.md");

  assert_contains_all(
      readme_text,
      {
          "validate_memory_installed_or_qemu.sh",
          "memory-proof.json",
          "memory-maintenance-proof.json",
          "~/.cache/dasall/memory/installed-evidence/latest.json",
          "qemu / machine isolation 继续用于 release-runner",
      },
      "packaging readme memory row");
}

}  // namespace

int main() {
  try {
    test_memory_installed_gate_wrapper_materializes_latest_json_schema();
    test_memory_installed_gate_script_keeps_authoritative_local_evidence_flow();
    test_memory_integration_cmake_registers_memory_installed_smoke_entrypoints();
    test_packaging_readme_keeps_memory_owner_gate_wording();
  } catch (const std::exception& ex) {
    std::cerr << "[MemoryInstalledSmokeTest] FAILED: " << ex.what() << '\n';
    return 1;
  }

  return 0;
}