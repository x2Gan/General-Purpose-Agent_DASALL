#include <exception>
#include <iostream>
#include <string>

#include "support/TestAssertions.h"

#include "../../../llm/src/route/ModelRouter.h"

#include "ModelRouterTestSupport.h"

namespace {

void test_router_rejects_candidates_that_cannot_fit_budget() {
  using dasall::llm::ModelSelectionHint;
  using dasall::llm::route::ModelRouter;
  using dasall::llm::test_support::has_reason;
  using dasall::llm::test_support::make_config;
  using dasall::llm::test_support::make_default_catalog;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  auto catalog = make_default_catalog();
  for (auto& model : catalog.models) {
    if (model.summary.model_id == "deepseek-chat") {
      model.summary.context_window = 1024U;
      model.summary.max_output_tokens_hard_limit = 128U;
      model.summary.default_max_output_tokens = 128U;
    }

    if (model.summary.model_id == "deepseek-reasoner") {
      model.summary.context_window = 256U;
      model.summary.max_output_tokens_hard_limit = 128U;
      model.summary.default_max_output_tokens = 128U;
    }
  }

  ModelRouter router;
  assert_true(router.init(make_config("responder",
                                      "cloud.general",
                                      std::nullopt,
                                      {"local.small"},
                                      false,
                                      false)),
              "ModelRouter should initialize with a valid responder route config");

  const ModelSelectionHint hint{
      .stage = "responder",
      .task_type = "summary",
      .complexity_tier = "standard",
      .latency_sla_tier = "interactive",
      .budget_tier = "balanced",
      .requires_tools = false,
      .requires_reasoning = false,
      .prefers_visible_reasoning = false,
      .estimated_input_tokens = 300U,
      .target_output_tokens = 300U,
      .previous_route_failures = 0U,
  };

  const auto result = router.resolve(hint, catalog);

  assert_true(!result.has_route(),
              "ModelRouter should fail closed when every allowed cloud candidate breaches the input or output budget");
  assert_true(has_reason(result.selection_reason_codes, "context_window_insufficient"),
              "ModelRouter should expose context window rejection as a reason code");
  assert_true(has_reason(result.selection_reason_codes, "output_limit_exceeded"),
              "ModelRouter should expose output limit rejection as a reason code");
  assert_true(has_reason(result.selection_reason_codes, "no_candidate_after_hard_filter"),
              "ModelRouter should expose fail-closed routing when no hard-filtered candidate survives");
}

void test_router_filters_unverified_reasoning_tools_and_degrades_to_chat() {
  using dasall::llm::ModelSelectionHint;
  using dasall::llm::route::ModelRouter;
  using dasall::llm::test_support::has_reason;
  using dasall::llm::test_support::make_config;
  using dasall::llm::test_support::make_default_catalog;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  ModelRouter router;
  assert_true(router.init(make_config("planner", "cloud.reasoning")),
              "ModelRouter should initialize with a valid planner route config");

  const auto result = router.resolve(ModelSelectionHint{
                                         .stage = "planner",
                                         .task_type = "plan",
                                         .complexity_tier = "standard",
                                         .latency_sla_tier = "interactive",
                                         .budget_tier = "hard_cap",
                                         .requires_tools = true,
                                         .requires_reasoning = false,
                                         .prefers_visible_reasoning = false,
                                         .estimated_input_tokens = 4096U,
                                         .target_output_tokens = 2048U,
                                         .previous_route_failures = 0U,
                                     },
                                     make_default_catalog());

  assert_true(result.has_route(),
              "ModelRouter should still resolve a concrete route when the preferred reasoning tier is filtered by tool verification");
  assert_equal(std::string("deepseek-prod/deepseek-chat"),
               result.resolved_route->primary_route,
               "ModelRouter should degrade to the verified chat model instead of routing to an unverified reasoning-tools candidate");
  assert_true(has_reason(result.selection_reason_codes, "tier_degraded"),
              "ModelRouter should expose the reasoning-to-chat downgrade in selection reason codes");
  assert_true(has_reason(result.selection_reason_codes, "requires_tools"),
              "ModelRouter should keep the explicit tool requirement visible in selection reason codes");
  assert_true(has_reason(result.selection_reason_codes, "budget_low_cost"),
              "ModelRouter should keep hard-cap budget bias visible in selection reason codes");
  assert_true(has_reason(result.selection_reason_codes, "interactive_latency_bias"),
              "ModelRouter should keep interactive latency bias visible in selection reason codes");
}

}  // namespace

int main() {
  try {
    test_router_rejects_candidates_that_cannot_fit_budget();
    test_router_filters_unverified_reasoning_tools_and_degrades_to_chat();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}