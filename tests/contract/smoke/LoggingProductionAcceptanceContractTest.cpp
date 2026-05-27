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
              "logging production acceptance contract should open " + path.string());
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

void test_ssot_matrix_freezes_backend_levels_and_artifacts() {
  const auto text = read_text_file(
      repository_root() / "docs/ssot/LoggingProductionAcceptanceMatrix.md");

  assert_contains_all(
      text,
      {
          "LoggingProductionAcceptanceMatrix",
          "L1 Design / SSOT",
          "L4 local installed authoritative evidence",
          "L5 packaging / release handoff",
          "spdlog-backed file / rotating sink",
          "/var/lib/dasall/logging/runtime.log",
          "logging-installed-proof.json",
          "logging-runtime-proof.json",
          "INF-LOG-GATE-001",
          "INF-LOG-GATE-006",
          "不把 qemu / kvm 作为 logging owner 当前验收前置",
      },
      "LoggingProductionAcceptanceMatrix");
}

void test_logging_design_points_to_frozen_matrix_and_backend_policy() {
  const auto text = read_text_file(
      repository_root() / "docs/architecture/DASALL_infra_logging模块详细设计.md");

  assert_contains_all(
      text,
      {
          "LoggingProductionAcceptanceMatrix",
          "spdlog-backed file / rotating sink",
          "state_root/logging/runtime.log",
          "OpenTelemetry Logs",
          "TraceId / SpanId / Resource",
          "local installed authoritative evidence",
      },
      "logging detailed design");
}

void test_logging_component_todo_keeps_skeleton_non_extrapolation() {
  const auto text = read_text_file(
      repository_root() /
      "docs/todos/infrastructure/DASALL_infrastructure_logging组件专项TODO.md");

  assert_contains_all(
      text,
      {
          "LoggingProductionAcceptanceMatrix.md",
          "只代表接口/骨架/focused evidence 已落盘",
          "不等于 production-ready",
          "INF-LOG-FIX-001~011",
      },
      "logging component TODO");
}

void test_system_gap_todo_marks_inf_log_fix_001_done_and_blocker_closed() {
  const auto text = read_text_file(
      repository_root() / "docs/todos/DASALL_子系统查漏补缺专项记录.md");

  assert_contains_all(
      text,
      {
          "| INF-LOG-FIX-001 | Done | 冻结 logging production acceptance matrix |",
          "| BLK-INF-LOG-001 | Closed |",
          "本轮不使用 qemu / kvm",
      },
      "subsystem gap ledger");
}

}  // namespace

int main() {
  try {
    test_ssot_matrix_freezes_backend_levels_and_artifacts();
    test_logging_design_points_to_frozen_matrix_and_backend_policy();
    test_logging_component_todo_keeps_skeleton_non_extrapolation();
    test_system_gap_todo_marks_inf_log_fix_001_done_and_blocker_closed();
  } catch (const std::exception& ex) {
    std::cerr << "[LoggingProductionAcceptanceContractTest] FAILED: "
              << ex.what() << '\n';
    return 1;
  }

  return 0;
}