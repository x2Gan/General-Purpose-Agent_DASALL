#include <exception>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <iostream>
#include <iterator>
#include <string>
#include <string_view>

#include "support/TestAssertions.h"

namespace {

using dasall::tests::support::assert_true;

[[nodiscard]] std::filesystem::path repository_root() {
  return std::filesystem::path(__FILE__).parent_path().parent_path().parent_path().parent_path();
}

[[nodiscard]] std::string read_text_file(const std::filesystem::path& path) {
  std::ifstream stream(path);
  assert_true(stream.is_open(),
              "key subsystem logging field matrix contract should open " +
                  path.string());
  return std::string(std::istreambuf_iterator<char>(stream),
                     std::istreambuf_iterator<char>());
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

void test_ssot_matrix_freezes_key_subsystem_field_rules() {
  const auto text = read_text_file(
      repository_root() / "docs/ssot/KeySubsystemLoggingFieldMatrix.md");

  assert_contains_all(
      text,
      {
          "KeySubsystemLoggingFieldMatrix",
          "cognition",
          "memory",
          "knowledge",
          "runtime",
          "services",
          "trace_id -> request_id -> session_id",
          "payload_excerpt",
          "query/body",
          "payload_json",
          "logging-installed-proof.json.subsystems",
          "logging-runtime-proof.json.subsystems",
          "request ledger",
          "local installed authoritative evidence",
      },
      "KeySubsystemLoggingFieldMatrix");
}

void test_logging_design_references_key_subsystem_matrix() {
  const auto text = read_text_file(
      repository_root() / "docs/architecture/DASALL_infra_logging模块详细设计.md");

  assert_contains_all(
      text,
      {
          "KeySubsystemLoggingFieldMatrix",
          "INF-LOG-SYS-FIX-001",
          "payload_excerpt",
          "request ledger",
          "audit=true",
      },
      "logging detailed design");
}

void test_gap_ledger_closes_blocker_and_marks_sys_fix_done() {
  const auto text = read_text_file(
      repository_root() / "docs/todos/DASALL_子系统查漏补缺专项记录.md");

  assert_contains_all(
      text,
      {
          "| INF-LOG-SYS-FIX-001 | Done | 冻结关键子系统 logging 字段矩阵 |",
          "| BLK-INF-LOG-009 | Closed |",
          "KeySubsystemLoggingFieldMatrixContractTest",
          "docs/ssot/KeySubsystemLoggingFieldMatrix.md",
      },
      "subsystem gap ledger");
}

void test_worklog_records_blocker_closeout() {
  const auto text = read_text_file(
      repository_root() / "docs/worklog/DASALL_开发执行记录.md");

  assert_contains_all(
      text,
      {
          "INF-LOG-SYS-FIX-001",
          "BLK-INF-LOG-009",
          "KeySubsystemLoggingFieldMatrix",
          "本轮不使用 qemu / kvm",
      },
      "development worklog");
}

}  // namespace

int main() {
  try {
    test_ssot_matrix_freezes_key_subsystem_field_rules();
    test_logging_design_references_key_subsystem_matrix();
    test_gap_ledger_closes_blocker_and_marks_sys_fix_done();
    test_worklog_records_blocker_closeout();
  } catch (const std::exception& ex) {
    std::cerr << "[KeySubsystemLoggingFieldMatrixContractTest] FAILED: "
              << ex.what() << '\n';
    return 1;
  }

  return 0;
}