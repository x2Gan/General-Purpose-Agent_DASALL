#include <exception>
#include <iostream>
#include <string>
#include <vector>

#include "execution/CompensationLedger.h"
#include "support/TestAssertions.h"

namespace {

using dasall::tests::support::assert_equal;
using dasall::tests::support::assert_true;

[[nodiscard]] dasall::contracts::ToolResult make_result(
    std::string tool_call_id,
    std::string tool_name,
    std::vector<std::string> side_effects) {
  return dasall::contracts::ToolResult{
      .request_id = std::string("req-compensation-unit"),
      .tool_call_id = std::move(tool_call_id),
      .tool_name = std::move(tool_name),
      .success = true,
      .payload = std::string("{\"status\":\"ok\"}"),
      .error = std::nullopt,
      .side_effects = std::move(side_effects),
      .completed_at = 1,
      .duration_ms = 1,
      .goal_id = std::string("goal-compensation-unit"),
      .worker_task_id = std::string("worker-compensation-unit"),
      .tags = std::vector<std::string>{"workflow"},
  };
}

void test_compensation_ledger_builds_lifo_hints() {
  dasall::tools::execution::CompensationLedger ledger;
  ledger.register_result(
      "prepare",
      "builtin",
      make_result("call-prepare", "agent.prepare", {"terminal.cwd_restore"}),
      true);
  ledger.register_result(
      "apply",
      "builtin",
      make_result("call-apply", "agent.apply", {"fs.rollback"}),
      true);

  const auto hints = ledger.build_hints();
  assert_equal(2, static_cast<int>(hints.size()),
               "compensation ledger should emit one hint per reversible side-effect record");
  assert_true(hints[0].compensation_action == std::string("agent.apply") &&
                  hints[0].target_ref == std::string("call-apply"),
              "compensation ledger should emit later reversible effects first to preserve LIFO rollback order");
  assert_true(hints[1].compensation_action == std::string("agent.prepare") &&
                  hints[1].target_ref == std::string("call-prepare"),
              "compensation ledger should retain earlier reversible effects after newer ones");
}

void test_compensation_ledger_keeps_irreversible_effects_out_of_hints() {
  dasall::tools::execution::CompensationLedger ledger;
  ledger.record_irreversible_effect(
      "apply",
      "builtin",
      make_result("call-apply", "agent.apply", {"remote.commit"}));

  const auto record = ledger.lookup("apply");
  assert_true(record.has_value() && !record->reversible,
              "compensation ledger should retain irreversible side effects for audit even when no rollback hint can be produced");
  assert_true(ledger.build_hints().empty(),
              "compensation ledger should not emit compensation hints for irreversible side effects");
}

}  // namespace

int main() {
  try {
    test_compensation_ledger_builds_lifo_hints();
    test_compensation_ledger_keeps_irreversible_effects_out_of_hints();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}