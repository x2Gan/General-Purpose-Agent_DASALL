#include <exception>
#include <iostream>
#include <string>

#include "support/TestAssertions.h"

#include "../../../llm/src/route/ModelRouter.h"

#include "ModelRouterTestSupport.h"

namespace {

void test_router_selects_reasoner_for_explicit_reasoning_workload() {
  using dasall::llm::ModelSelectionHint;
  using dasall::llm::route::ModelRouter;
  using dasall::llm::test_support::has_reason;
  using dasall::llm::test_support::make_config;
  using dasall::llm::test_support::make_default_catalog;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  ModelRouter router;
  assert_true(router.init(make_config("planner", "cloud.reasoning")),
              "ModelRouter should initialize with a reasoning-first planner route");

  const auto result = router.resolve(ModelSelectionHint{
                                         .stage = "planner",
                                         .task_type = "root_cause_analysis",
                                         .complexity_tier = "high",
                                         .latency_sla_tier = "balanced",
                                         .budget_tier = "balanced",
                                         .requires_tools = false,
                                         .requires_reasoning = true,
                                         .prefers_visible_reasoning = true,
                                         .estimated_input_tokens = 8192U,
                                         .target_output_tokens = 4096U,
                                         .previous_route_failures = 0U,
                                     },
                                     make_default_catalog());

  assert_true(result.has_route(),
              "ModelRouter should resolve a concrete route for reasoning-first planning workloads");
  assert_equal(std::string("deepseek-prod/deepseek-reasoner"),
               result.resolved_route->primary_route,
               "ModelRouter should choose the reasoning model when stage, complexity and explicit reasoning requirement all point to deep reasoning");
  assert_true(has_reason(result.selection_reason_codes, "requires_reasoning"),
              "ModelRouter should preserve explicit reasoning demand in the selected reason codes");
  assert_true(has_reason(result.selection_reason_codes, "visible_reasoning_preferred"),
              "ModelRouter should preserve visible reasoning preference in the selected reason codes");
}

void test_router_selects_chat_for_interactive_hard_cap_workload() {
  using dasall::llm::ModelSelectionHint;
  using dasall::llm::route::ModelRouter;
  using dasall::llm::test_support::has_reason;
  using dasall::llm::test_support::make_config;
  using dasall::llm::test_support::make_default_catalog;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  ModelRouter router;
  assert_true(router.init(make_config("planner", "cloud.reasoning")),
              "ModelRouter should initialize with a reasoning-first planner route");

  auto catalog = make_default_catalog();
  for (auto& model : catalog.models) {
    if (model.summary.model_id == "deepseek-reasoner") {
      model.verification_states["tools"] = "verified";
      model.summary.verification_state = "verified";
    }
  }

  const auto result = router.resolve(ModelSelectionHint{
                                         .stage = "planner",
                                         .task_type = "plan",
                                         .complexity_tier = "standard",
                                         .latency_sla_tier = "interactive",
                                         .budget_tier = "hard_cap",
                                         .requires_tools = true,
                                         .requires_reasoning = false,
                                         .prefers_visible_reasoning = false,
                                         .estimated_input_tokens = 2048U,
                                         .target_output_tokens = 1024U,
                                         .previous_route_failures = 0U,
                                     },
                                     catalog);

  assert_true(result.has_route(),
              "ModelRouter should resolve a concrete route for interactive planner workloads");
  assert_equal(std::string("deepseek-prod/deepseek-chat"),
               result.resolved_route->primary_route,
               "ModelRouter should prefer the lower-latency lower-cost chat model when reasoning is optional but tools, interactive SLA and hard-cap budget are present");
  assert_true(has_reason(result.selection_reason_codes, "tier_degraded"),
              "ModelRouter should expose the reasoning-to-chat downgrade for interactive hard-cap workloads");
  assert_true(has_reason(result.selection_reason_codes, "interactive_latency_bias"),
              "ModelRouter should expose the interactive SLA bias that favored the chat model");
  assert_true(has_reason(result.selection_reason_codes, "budget_low_cost"),
              "ModelRouter should expose the hard-cap budget bias that favored the chat model");
}

}  // namespace

int main() {
  try {
    test_router_selects_reasoner_for_explicit_reasoning_workload();
    test_router_selects_chat_for_interactive_hard_cap_workload();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}