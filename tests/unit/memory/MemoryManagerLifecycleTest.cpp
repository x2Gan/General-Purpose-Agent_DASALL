#include <exception>
#include <iostream>

#include "IMemoryManager.h"
#include "support/TestAssertions.h"

namespace {

void test_memory_manager_rejects_prepare_context_before_init() {
  using dasall::tests::support::assert_true;

  dasall::memory::MemoryConfig config;
  config.storage.backend = "memory";

  auto manager = dasall::memory::create_memory_manager(config);

  dasall::memory::MemoryContextRequest request;
  request.request_id = "req-001";
  request.session_id = "session-001";
  request.goal_summary = "Bootstrap memory lifecycle";

  const auto result = manager->prepare_context(request);

  assert_true(result.result_code ==
                  dasall::contracts::ResultCode::RuntimeRetryExhausted,
              "prepare_context should reject calls before init");
  assert_true(result.degraded,
              "prepare_context should report degraded execution before init");
  assert_true(result.warnings.size() == 1U &&
                  result.warnings.front() == "memory_manager_not_running",
              "prepare_context should surface the not-running warning before init");
}

void test_memory_manager_init_prepare_and_shutdown_follow_the_lifecycle_skeleton() {
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  dasall::memory::MemoryConfig config;
  config.storage.backend = "memory";

  auto manager = dasall::memory::create_memory_manager(config);

  const auto first_init_code = manager->init(config);
  const auto second_init_code = manager->init(config);

  assert_equal(0, static_cast<int>(first_init_code),
               "memory backend bootstrap should use the temporary zero-valued init success sentinel");
  assert_equal(0, static_cast<int>(second_init_code),
               "memory manager init should be idempotent while already running");

  dasall::memory::MemoryContextRequest request;
  request.request_id = "req-002";
  request.session_id = "session-001";
  request.goal_summary = "Assemble a bootstrap packet";
  request.latest_observation_digest_summary = "No prior observation";
  request.visible_tools = {"shell", "search"};
  request.external_evidence = {"profile:desktop_full"};
  request.token_budget_hint = 1024;

  const auto result = manager->prepare_context(request);

  assert_true(!result.result_code.has_value(),
              "bootstrap context assembly should succeed after init");
  assert_true(result.context_packet.request_id ==
                  std::optional<std::string>{"req-002"},
              "bootstrap context assembly should preserve the request id");
  assert_true(result.context_packet.current_goal_summary ==
                  std::optional<std::string>{"Assemble a bootstrap packet"},
              "bootstrap context assembly should preserve the goal summary");
  assert_true(result.context_packet.active_tools.has_value() &&
                  result.context_packet.active_tools->size() == 2U,
              "bootstrap context assembly should project visible tools");
  assert_true(result.context_packet.retrieval_evidence.has_value() &&
                  result.context_packet.retrieval_evidence->size() == 1U,
              "bootstrap context assembly should project external evidence into retrieval_evidence");
  assert_true(result.context_packet.token_budget_report ==
                  std::optional<std::string>{"token_budget_hint=1024"},
              "bootstrap context assembly should preserve the token budget hint");
  assert_true(result.context_packet.recent_history.has_value() &&
                  result.context_packet.recent_history->empty(),
              "bootstrap context assembly should emit an empty recent_history list");

  manager->shutdown();
  manager->shutdown();

  const auto post_shutdown_result = manager->prepare_context(request);
  assert_true(post_shutdown_result.result_code ==
                  dasall::contracts::ResultCode::RuntimeRetryExhausted,
              "prepare_context should fail again after shutdown");
}

}  // namespace

int main() {
  try {
    test_memory_manager_rejects_prepare_context_before_init();
    test_memory_manager_init_prepare_and_shutdown_follow_the_lifecycle_skeleton();
  } catch (const std::exception& exception) {
    std::cerr << exception.what() << '\n';
    return 1;
  }

  return 0;
}
