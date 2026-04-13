#include <exception>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "support/TestAssertions.h"

#include "../../../llm/src/route/AdapterRegistry.h"
#include "../../../llm/src/route/ModelRouter.h"

#include "../../mocks/include/MockLLMAdapter.h"

#include "ModelRouterTestSupport.h"

namespace {

dasall::llm::route::AdapterRegistration make_registration(
    std::string provider_id,
    std::string model_id,
    std::string adapter_id,
    std::string deployment_type,
    std::vector<std::string> capability_tags,
    bool supports_streaming,
    std::shared_ptr<dasall::tests::mocks::MockLLMAdapter> adapter) {
  return dasall::llm::route::AdapterRegistration{
      .provider_id = std::move(provider_id),
      .model_id = std::move(model_id),
      .adapter_id = std::move(adapter_id),
      .deployment_type = std::move(deployment_type),
      .capability_tags = std::move(capability_tags),
      .supports_streaming = supports_streaming,
      .adapter = std::move(adapter),
  };
}

void test_registry_health_snapshot_drives_router_fallback() {
  using dasall::llm::ModelSelectionHint;
  using dasall::llm::route::AdapterRegistry;
  using dasall::llm::route::AdapterRegistryConfig;
  using dasall::llm::route::ModelRouter;
  using dasall::llm::test_support::make_config;
  using dasall::llm::test_support::make_default_catalog;
  using dasall::tests::mocks::MockLLMAdapter;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  AdapterRegistry registry;
  assert_true(registry.init(AdapterRegistryConfig{.blocked_failure_threshold = 2U}),
              "AdapterRegistry should accept a small blocked threshold for fallback-oriented tests");

  auto deepseek_chat = std::make_shared<MockLLMAdapter>();
  auto deepseek_reasoner = std::make_shared<MockLLMAdapter>();
  auto lan_general = std::make_shared<MockLLMAdapter>();

  assert_true(registry.register_adapter(make_registration("deepseek-prod",
                                                          "deepseek-chat",
                                                          "deepseek-cloud",
                                                          "cloud",
                                                          {"cloud", "external"},
                                                          true,
                                                          deepseek_chat)),
              "AdapterRegistry should register the cloud chat route for fallback tests");
  assert_true(registry.register_adapter(make_registration("deepseek-prod",
                                                          "deepseek-reasoner",
                                                          "deepseek-cloud",
                                                          "cloud",
                                                          {"cloud", "external", "reasoning"},
                                                          true,
                                                          deepseek_reasoner)),
              "AdapterRegistry should register the cloud reasoning route for fallback tests");
  assert_true(registry.register_adapter(make_registration("lan-ollama",
                                                          "lan-general",
                                                          "ollama-lan",
                                                          "lan",
                                                          {"lan", "internal"},
                                                          false,
                                                          lan_general)),
              "AdapterRegistry should register the LAN fallback route for fallback tests");

  assert_true(registry.record_call_failure("deepseek-prod/deepseek-chat"),
              "AdapterRegistry should accept the first cloud chat failure record");
  assert_true(registry.record_call_failure("deepseek-prod/deepseek-chat"),
              "AdapterRegistry should block the cloud chat route after the second failure record");
  assert_true(registry.record_call_failure("deepseek-prod/deepseek-reasoner"),
              "AdapterRegistry should accept the first cloud reasoning failure record");
  assert_true(registry.record_call_failure("deepseek-prod/deepseek-reasoner"),
              "AdapterRegistry should block the cloud reasoning route after the second failure record");

  ModelRouter router;
  assert_true(router.init(make_config("planner", "cloud.reasoning", std::string("lan.general"), {"local.small"}, true, true)),
              "ModelRouter should initialize with a cloud reasoning primary route and LAN fallback");

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
                                     registry.health_snapshot());

  assert_true(result.has_route(),
              "LLMManager fallback fixture should still resolve a route when registry health blocks every cloud candidate");
  assert_equal(std::string("lan-ollama/lan-general"),
               result.resolved_route->primary_route,
               "LLMManager fallback fixture should consume AdapterRegistry health snapshot and choose the LAN fallback route");

  const auto resolved_route = registry.resolve_route(result.resolved_route->primary_route);
  assert_true(resolved_route.has_value(),
              "AdapterRegistry should resolve the selected fallback route back to an adapter handle");
  assert_equal(std::string("ollama-lan"),
               resolved_route->adapter_id,
               "AdapterRegistry should preserve adapter metadata for the fallback route selected by ModelRouter");
  assert_true(resolved_route->adapter == lan_general,
              "AdapterRegistry should return the same fallback adapter instance that was originally registered");
}

}  // namespace

int main() {
  try {
    test_registry_health_snapshot_drives_router_fallback();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}
