#include <exception>
#include <iostream>
#include <string>

#include "support/TestAssertions.h"

#include "../../../llm/src/route/ModelRouter.h"

#include "ModelRouterTestSupport.h"

namespace {

void test_router_prefers_explicit_stage_fallback_before_degrade_chain() {
  using dasall::llm::ModelSelectionHint;
  using dasall::llm::route::ModelRouter;
  using dasall::llm::route::ModelRouterHealthState;
  using dasall::llm::test_support::has_reason;
  using dasall::llm::test_support::join_routes;
  using dasall::llm::test_support::make_config;
  using dasall::llm::test_support::make_default_catalog;
  using dasall::llm::test_support::make_health_snapshot;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  ModelRouter router;
  assert_true(router.init(make_config("planner", "cloud.reasoning", std::string("lan.general"), {"local.small"}, true, true)),
              "ModelRouter should initialize with explicit stage fallback and degrade chain");

  const auto result = router.resolve(ModelSelectionHint{
                                         .stage = "planner",
                                         .task_type = "plan",
                                         .complexity_tier = "high",
                                         .latency_sla_tier = "balanced",
                                         .budget_tier = "balanced",
                                         .requires_tools = false,
                                         .requires_reasoning = false,
                                         .prefers_visible_reasoning = false,
                                         .estimated_input_tokens = 4096U,
                                         .target_output_tokens = 2048U,
                                         .previous_route_failures = 2U,
                                     },
                                     make_default_catalog(),
                                     make_health_snapshot({
                                         ModelRouterHealthState{.provider_id = "deepseek-prod", .model_id = "deepseek-chat", .blocked = true},
                                         ModelRouterHealthState{.provider_id = "deepseek-prod", .model_id = "deepseek-reasoner", .blocked = true},
                                     }));

  assert_true(result.has_route(),
              "ModelRouter should continue onto explicit fallback and degrade-chain candidates when all primary cloud candidates are blocked");
  assert_equal(std::string("lan-ollama/lan-general"),
               result.resolved_route->primary_route,
               "ModelRouter should promote the explicit stage fallback route before any degrade-chain route");
  assert_equal(std::string("local-runtime/local-small"),
               join_routes(result.resolved_route->fallback_routes),
               "ModelRouter should keep degrade-chain routes after the explicit stage fallback in fallback order");
  assert_true(has_reason(result.selection_reason_codes, "selected_from_fallback_chain"),
              "ModelRouter should expose that the final primary route came from the fallback chain");
  assert_true(has_reason(result.selection_reason_codes, "fallback_chain_prepared"),
              "ModelRouter should keep fallback-chain preparation visible when secondary routes remain available");
}

void test_router_does_not_append_degrade_chain_when_failover_is_disabled() {
  using dasall::llm::ModelSelectionHint;
  using dasall::llm::route::ModelRouter;
  using dasall::llm::route::ModelRouterHealthState;
  using dasall::llm::test_support::join_routes;
  using dasall::llm::test_support::make_config;
  using dasall::llm::test_support::make_default_catalog;
  using dasall::llm::test_support::make_health_snapshot;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  ModelRouter router;
  assert_true(router.init(make_config("planner", "cloud.reasoning", std::string("lan.general"), {"local.small"}, false, false)),
              "ModelRouter should initialize even when degrade-policy failover is disabled");

  const auto result = router.resolve(ModelSelectionHint{
                                         .stage = "planner",
                                         .task_type = "plan",
                                         .complexity_tier = "high",
                                         .latency_sla_tier = "balanced",
                                         .budget_tier = "balanced",
                                         .requires_tools = false,
                                         .requires_reasoning = false,
                                         .prefers_visible_reasoning = false,
                                         .estimated_input_tokens = 2048U,
                                         .target_output_tokens = 1024U,
                                         .previous_route_failures = 1U,
                                     },
                                     make_default_catalog(),
                                     make_health_snapshot({
                                         ModelRouterHealthState{.provider_id = "deepseek-prod", .model_id = "deepseek-chat", .blocked = true},
                                         ModelRouterHealthState{.provider_id = "deepseek-prod", .model_id = "deepseek-reasoner", .blocked = true},
                                     }));

  assert_true(result.has_route(),
              "ModelRouter should still resolve the explicit stage fallback when degrade failover is disabled");
  assert_equal(std::string("lan-ollama/lan-general"),
               result.resolved_route->primary_route,
               "ModelRouter should preserve the explicit stage fallback as the only recovery path when degrade failover is disabled");
  assert_equal(std::string(),
               join_routes(result.resolved_route->fallback_routes),
               "ModelRouter should not append degrade-chain routes when allow_model_failover is disabled");
}

}  // namespace

int main() {
  try {
    test_router_prefers_explicit_stage_fallback_before_degrade_chain();
    test_router_does_not_append_degrade_chain_when_failover_is_disabled();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}