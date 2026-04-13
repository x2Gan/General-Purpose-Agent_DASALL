#include <algorithm>
#include <exception>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "support/TestAssertions.h"

#include "../../../llm/src/adapters/OpenAICompatibleAdapter.h"
#include "../../../llm/src/route/AdapterRegistry.h"
#include "../../mocks/include/MockLLMAdapter.h"

namespace {

bool has_capability_tag(const std::vector<std::string>& tags, std::string_view tag) {
  return std::find(tags.begin(), tags.end(), tag) != tags.end();
}

class HealthTransport final : public dasall::llm::ILLMTransport {
 public:
  [[nodiscard]] dasall::llm::LLMTransportResponse send(
      const dasall::llm::LLMTransportRequest& request) override {
    ++call_count;
    last_request = request;
    return next_response;
  }

  dasall::llm::LLMTransportResponse next_response;
  std::optional<dasall::llm::LLMTransportRequest> last_request;
  int call_count = 0;
};

dasall::llm::LLMAdapterConfig make_openai_adapter_config() {
  return dasall::llm::LLMAdapterConfig{
      .adapter_id = "deepseek-prod",
      .adapter_family = "openai_compatible",
      .provider_instance_id = "deepseek-prod",
      .base_url = "https://api.deepseek.example/v1",
      .base_url_alias = "deepseek/default",
      .auth_ref = "secret://llm/providers/deepseek-prod",
      .header_refs = {"header://llm/providers/deepseek-org"},
      .activation_flag = true,
      .snapshot_version = "2026.04.13",
      .timeout_ms = 4000U,
      .max_retries = 1U,
      .capability_tags = {"cloud", "external"},
  };
}

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

void test_registry_probes_health_and_preserves_metadata() {
  using dasall::llm::HealthStatus;
  using dasall::llm::route::AdapterRegistry;
  using dasall::tests::mocks::MockLLMAdapter;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  AdapterRegistry registry;
  assert_true(registry.init(), "AdapterRegistry should initialize with the default failure threshold");

  auto healthy_adapter = std::make_shared<MockLLMAdapter>();
  healthy_adapter->set_health_status(HealthStatus{.ready = true, .degraded = false, .message = "healthy"});
  auto degraded_adapter = std::make_shared<MockLLMAdapter>();
  degraded_adapter->set_health_status(HealthStatus{.ready = true, .degraded = true, .message = "warming"});
  auto blocked_adapter = std::make_shared<MockLLMAdapter>();
  blocked_adapter->set_health_status(HealthStatus{.ready = false, .degraded = true, .message = "endpoint unavailable"});

  assert_true(registry.register_adapter(make_registration("deepseek-prod",
                                                          "deepseek-chat",
                                                          "deepseek-cloud",
                                                          "cloud",
                                                          {"cloud", "external"},
                                                          true,
                                                          healthy_adapter)),
              "AdapterRegistry should register the healthy cloud route");
  assert_true(registry.register_adapter(make_registration("lan-ollama",
                                                          "lan-general",
                                                          "ollama-lan",
                                                          "lan",
                                                          {"lan", "internal"},
                                                          false,
                                                          degraded_adapter)),
              "AdapterRegistry should register the degraded LAN route");
  assert_true(registry.register_adapter(make_registration("local-runtime",
                                                          "local-small",
                                                          "local-runtime",
                                                          "local",
                                                          {"local", "internal"},
                                                          false,
                                                          blocked_adapter)),
              "AdapterRegistry should register the unavailable local route");

  assert_true(registry.probe_health("deepseek-prod/deepseek-chat"),
              "AdapterRegistry should probe the registered healthy route");
  assert_true(registry.probe_health("lan-ollama/lan-general"),
              "AdapterRegistry should probe the registered degraded route");
  assert_true(registry.probe_health("local-runtime/local-small"),
              "AdapterRegistry should probe the registered unavailable route");

  const auto snapshot = registry.snapshot();
  assert_true(snapshot != nullptr && snapshot->has_consistent_values(),
              "AdapterRegistry should publish a consistent immutable snapshot after probing health");

  const auto healthy_route = snapshot->resolve_route("deepseek-prod/deepseek-chat");
  assert_true(healthy_route.has_value(), "AdapterRegistry should expose the healthy route in the published snapshot");
  assert_true(healthy_route->last_health.is_healthy(),
              "AdapterRegistry should keep healthy routes marked as healthy after a successful probe");
  assert_true(!healthy_route->blocked,
              "AdapterRegistry should not hard-block a route whose health probe reports ready and non-degraded");
  assert_equal(0,
               static_cast<int>(healthy_route->consecutive_failures),
               "AdapterRegistry should reset the healthy route failure counter after a successful probe");
  assert_true(healthy_route->supports_streaming,
              "AdapterRegistry should preserve route-level streaming support metadata in the snapshot");
  assert_true(has_capability_tag(healthy_route->capability_tags, "cloud"),
              "AdapterRegistry should preserve capability tags in the route snapshot");

  const auto degraded_route = snapshot->resolve_route("lan-ollama/lan-general");
  assert_true(degraded_route.has_value(), "AdapterRegistry should expose the degraded route in the published snapshot");
  assert_true(degraded_route->last_health.ready && degraded_route->last_health.degraded,
              "AdapterRegistry should preserve a degraded-yet-ready health probe result");
  assert_true(!degraded_route->blocked,
              "AdapterRegistry should keep degraded-but-ready routes visible so ModelRouter can penalize rather than hard-filter them");
  assert_equal(1,
               static_cast<int>(degraded_route->consecutive_failures),
               "AdapterRegistry should increment failure counters when the probe reports degraded health");

  const auto blocked_route = snapshot->resolve_route("local-runtime/local-small");
  assert_true(blocked_route.has_value(), "AdapterRegistry should expose the blocked route in the published snapshot");
  assert_true(!blocked_route->last_health.ready,
              "AdapterRegistry should preserve an unavailable route as not ready after probing health");
  assert_true(blocked_route->blocked,
              "AdapterRegistry should hard-block routes whose health probe reports ready=false");
  assert_equal(1,
               static_cast<int>(blocked_route->consecutive_failures),
               "AdapterRegistry should increment failure counters when the probe reports the route unavailable");

  const auto health_snapshot = registry.health_snapshot();
  assert_true(!health_snapshot.route_is_blocked("deepseek-prod", "deepseek-chat"),
              "AdapterRegistry should project healthy routes into a non-blocked ModelRouter health snapshot");
  assert_true(!health_snapshot.route_is_blocked("lan-ollama", "lan-general"),
              "AdapterRegistry should keep degraded-but-ready routes available in the ModelRouter health snapshot");
  assert_true(health_snapshot.route_is_blocked("local-runtime", "local-small"),
              "AdapterRegistry should project unavailable routes into a blocked ModelRouter health snapshot");
  assert_equal(1,
               static_cast<int>(health_snapshot.consecutive_failures_for("lan-ollama", "lan-general")),
               "AdapterRegistry should propagate degraded probe counters into the ModelRouter health snapshot");
  assert_equal(1, healthy_adapter->health_check_call_count(),
               "AdapterRegistry should invoke the healthy adapter probe exactly once");
  assert_equal(1, degraded_adapter->health_check_call_count(),
               "AdapterRegistry should invoke the degraded adapter probe exactly once");
  assert_equal(1, blocked_adapter->health_check_call_count(),
               "AdapterRegistry should invoke the unavailable adapter probe exactly once");
}

void test_registry_fails_closed_for_missing_routes_and_unregisters_cleanly() {
  using dasall::llm::route::AdapterRegistry;
  using dasall::tests::mocks::MockLLMAdapter;
  using dasall::tests::support::assert_true;

  AdapterRegistry registry;
  assert_true(registry.init(), "AdapterRegistry should initialize before negative path checks");

  auto adapter = std::make_shared<MockLLMAdapter>();
  assert_true(registry.register_adapter(make_registration("deepseek-prod",
                                                          "deepseek-chat",
                                                          "deepseek-cloud",
                                                          "cloud",
                                                          {"cloud"},
                                                          true,
                                                          adapter)),
              "AdapterRegistry should register a route before exercising missing-route behavior");
  assert_true(!registry.probe_health("missing/provider"),
              "AdapterRegistry should fail closed when probing a route that was never registered");
  assert_true(!registry.last_error_message().empty(),
              "AdapterRegistry should preserve an error message when probing a missing route fails");
  assert_true(registry.unregister_adapter("deepseek-prod/deepseek-chat"),
              "AdapterRegistry should allow unregistering a previously registered route");
  assert_true(!registry.resolve_route("deepseek-prod/deepseek-chat").has_value(),
              "AdapterRegistry should stop resolving a route after it has been unregistered");
}

void test_openai_compatible_adapter_health_check_reports_concrete_probe_states() {
  using dasall::llm::LLMTransportMethod;
  using dasall::llm::OpenAICompatibleAdapter;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  auto healthy_transport = std::make_shared<HealthTransport>();
  healthy_transport->next_response = {
      .status_code = 200U,
      .body = R"({"data":[]})",
      .error_message = {},
  };
  OpenAICompatibleAdapter healthy_adapter(healthy_transport);
  assert_true(healthy_adapter.init(make_openai_adapter_config()),
              "OpenAICompatibleAdapter should initialize before healthy probe coverage");

  const auto healthy = healthy_adapter.health_check();
  assert_true(healthy.ready && !healthy.degraded,
              "OpenAICompatibleAdapter should report ready=true and degraded=false for a 2xx /models probe");
  assert_equal(1, healthy_transport->call_count,
               "OpenAICompatibleAdapter should issue exactly one transport probe per health_check()");
  assert_true(healthy_transport->last_request.has_value() &&
                  healthy_transport->last_request->method == LLMTransportMethod::Get,
              "OpenAICompatibleAdapter health_check() should probe via GET");
  assert_true(healthy_transport->last_request->url == "https://api.deepseek.example/v1/models",
              "OpenAICompatibleAdapter health_check() should append /models to the projected base_url");

  auto degraded_transport = std::make_shared<HealthTransport>();
  degraded_transport->next_response = {
      .status_code = 429U,
      .body = {},
      .error_message = {},
  };
  OpenAICompatibleAdapter degraded_adapter(degraded_transport);
  assert_true(degraded_adapter.init(make_openai_adapter_config()),
              "OpenAICompatibleAdapter should initialize before degraded probe coverage");

  const auto degraded = degraded_adapter.health_check();
  assert_true(degraded.ready && degraded.degraded,
              "OpenAICompatibleAdapter should treat 429 as degraded-but-ready so AdapterRegistry can penalize instead of hard-blocking");

  auto unavailable_transport = std::make_shared<HealthTransport>();
  unavailable_transport->next_response = {
      .status_code = 0U,
      .body = {},
      .error_message = "connection refused",
  };
  OpenAICompatibleAdapter unavailable_adapter(unavailable_transport);
  assert_true(unavailable_adapter.init(make_openai_adapter_config()),
              "OpenAICompatibleAdapter should initialize before unavailable probe coverage");

  const auto unavailable = unavailable_adapter.health_check();
  assert_true(!unavailable.ready && unavailable.degraded,
              "OpenAICompatibleAdapter should report unavailable transport probes as not ready and degraded");
}

}  // namespace

int main() {
  try {
    test_registry_probes_health_and_preserves_metadata();
    test_registry_fails_closed_for_missing_routes_and_unregisters_cleanly();
    test_openai_compatible_adapter_health_check_reports_concrete_probe_states();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}
