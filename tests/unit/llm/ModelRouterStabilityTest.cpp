#include <exception>
#include <iostream>
#include <string>

#include "support/TestAssertions.h"

#include "../../../llm/src/route/ModelRouter.h"

#include "ModelRouterTestSupport.h"

namespace {

void test_router_keeps_deterministic_order_for_equal_score_candidates() {
  using dasall::llm::ModelSelectionHint;
  using dasall::llm::provider::ProviderCatalogSnapshot;
  using dasall::llm::route::ModelRouter;
  using dasall::llm::test_support::join_routes;
  using dasall::llm::test_support::make_config;
  using dasall::llm::test_support::make_model;
  using dasall::llm::test_support::make_provider;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  ProviderCatalogSnapshot snapshot;
  snapshot.default_source_version = "2026.04.13";
  snapshot.providers = {
      make_provider("cloud-a", {"cloud"}),
      make_provider("cloud-b", {"cloud"}),
  };
  snapshot.models = {
      make_model("cloud-a", "general-a", "default", "low", "low", "standard", 65536U, 4096U, 8192U, true, false, false, {{"tools", "verified"}}),
      make_model("cloud-b", "general-b", "default", "low", "low", "standard", 65536U, 4096U, 8192U, true, false, false, {{"tools", "verified"}}),
  };

  ModelRouter router;
  assert_true(router.init(make_config("responder", "cloud.general", std::nullopt, {"local.small"}, false, false)),
              "ModelRouter should initialize with a single cloud route for deterministic tie-break checks");

  const ModelSelectionHint hint{
      .stage = "responder",
      .task_type = "rewrite",
      .complexity_tier = "standard",
      .latency_sla_tier = "interactive",
      .budget_tier = "balanced",
      .requires_tools = false,
      .requires_reasoning = false,
      .prefers_visible_reasoning = false,
      .estimated_input_tokens = 512U,
      .target_output_tokens = 256U,
      .previous_route_failures = 0U,
  };

  const auto first = router.resolve(hint, snapshot);
  assert_true(first.has_route(),
              "ModelRouter should resolve a route even when candidates are tied on score");
  assert_equal(std::string("cloud-a/general-a"),
               first.resolved_route->primary_route,
               "ModelRouter should break equal-score ties deterministically by route identifier");
  assert_equal(std::string("cloud-b/general-b"),
               join_routes(first.resolved_route->fallback_routes),
               "ModelRouter should keep the remaining tied candidate in deterministic fallback order");

  for (int iteration = 0; iteration < 10; ++iteration) {
    const auto repeated = router.resolve(hint, snapshot);
    assert_true(repeated.has_route(),
                "ModelRouter should remain resolvable across repeated identical calls");
    assert_equal(first.resolved_route->primary_route,
                 repeated.resolved_route->primary_route,
                 "ModelRouter should keep the same primary route for identical repeated input");
    assert_equal(join_routes(first.resolved_route->fallback_routes),
                 join_routes(repeated.resolved_route->fallback_routes),
                 "ModelRouter should keep the same fallback order for identical repeated input");
    assert_equal(join_routes(first.selection_reason_codes),
                 join_routes(repeated.selection_reason_codes),
                 "ModelRouter should keep the same reason-code ordering for identical repeated input");
  }
}

}  // namespace

int main() {
  try {
    test_router_keeps_deterministic_order_for_equal_score_candidates();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}