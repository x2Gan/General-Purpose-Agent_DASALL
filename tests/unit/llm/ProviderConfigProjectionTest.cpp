#include <algorithm>
#include <exception>
#include <iostream>
#include <memory>
#include <string>

#include "LLMSubsystemConfig.h"
#include "support/TestAssertions.h"

#include "../../../llm/src/route/AdapterRegistry.h"
#include "../../mocks/include/MockLLMAdapter.h"

#include "ModelRouterTestSupport.h"

namespace {

using dasall::llm::LLMSubsystemConfig;
using dasall::llm::ProviderDescriptor;
using dasall::llm::ProviderRuntimeProjectionView;
using dasall::llm::project_provider_to_adapter_config;
using dasall::llm::route::AdapterRegistry;
using dasall::llm::route::AdapterRegistryConfig;
using dasall::tests::mocks::MockLLMAdapter;
using dasall::tests::support::assert_equal;
using dasall::tests::support::assert_true;

ProviderDescriptor make_provider_descriptor() {
  return ProviderDescriptor{
      .provider_id = "deepseek-prod",
      .adapter_family = "openai_compatible",
      .api_family = "chat-completions",
      .base_url = "https://api.deepseek.com/v1",
      .auth_ref = "secret://llm/providers/deepseek-prod",
      .header_refs = {"header://llm/providers/deepseek-org"},
      .capability_tags = {"cloud", "external"},
      .source_version = "2026.04.13",
  };
}

ProviderRuntimeProjectionView make_runtime_view(bool activation_flag = true) {
  return ProviderRuntimeProjectionView{
      .provider_instance_id = "deepseek-prod",
      .base_url_alias = "deepseek/canary",
      .snapshot_version = "2026.04.13-canary",
      .runtime_tags = {"cloud", "deepseek", "canary"},
      .activation_flag = activation_flag,
  };
}

LLMSubsystemConfig make_config() {
  return dasall::llm::test_support::make_config(
      "planner", "cloud.reasoning", std::nullopt, {"local.small"}, false, false);
}

bool has_tag(const std::vector<std::string>& values, const std::string& expected_tag) {
  return std::find(values.begin(), values.end(), expected_tag) != values.end();
}

void test_provider_projection_projects_runtime_overlay_into_adapter_config() {
  const auto projected = project_provider_to_adapter_config(
      make_config(), make_provider_descriptor(), make_runtime_view());

  assert_true(projected.has_value(),
              "provider projection should produce an adapter config when config and references are consistent");
  assert_equal(std::string("deepseek-prod"), projected->adapter_id,
               "provider projection should use provider_instance_id as the stable adapter identity");
  assert_equal(std::string("deepseek-prod"), projected->provider_instance_id,
               "provider projection should preserve provider_instance_id for adapter init");
  assert_equal(std::string("deepseek/canary"), projected->base_url_alias,
               "provider projection should preserve mutable base_url alias");
  assert_equal(std::string("2026.04.13-canary"), projected->snapshot_version,
               "provider projection should preserve snapshot version for downstream adapter diagnostics");
  assert_true(projected->activation_flag,
              "provider projection should preserve activation_flag when the provider instance is enabled");
  assert_equal(4000, static_cast<int>(projected->timeout_ms),
               "provider projection should reuse llm timeout policy as adapter init timeout");
  assert_equal(2, static_cast<int>(projected->max_retries),
               "provider projection should reuse llm retry budget as adapter init retry count");
  assert_true(projected->capability_tags.size() == 4U && has_tag(projected->capability_tags, "cloud") &&
                  has_tag(projected->capability_tags, "external") &&
                  has_tag(projected->capability_tags, "deepseek") &&
                  has_tag(projected->capability_tags, "canary"),
              "provider projection should merge descriptor capability tags with runtime tags without duplicates");
}

void test_provider_projection_rejects_invalid_secret_references() {
  auto descriptor = make_provider_descriptor();
  descriptor.auth_ref = "sk-live-plain-text";

  const auto invalid_auth_projection = project_provider_to_adapter_config(
      make_config(), descriptor, make_runtime_view());
  assert_true(!invalid_auth_projection.has_value(),
              "provider projection should reject plain-text auth_ref instead of projecting an unsafe adapter config");

  descriptor = make_provider_descriptor();
  descriptor.header_refs = {"x-org-id: plain-text"};

  const auto invalid_header_projection = project_provider_to_adapter_config(
      make_config(), descriptor, make_runtime_view());
  assert_true(!invalid_header_projection.has_value(),
              "provider projection should reject non-reference header refs instead of leaking raw header material into adapter init");
}

void test_adapter_registry_initializes_adapter_with_projected_provider_config() {
  AdapterRegistry registry;
  assert_true(registry.init(AdapterRegistryConfig{.blocked_failure_threshold = 3U}),
              "adapter registry should initialize before provider-route registration");

  auto adapter = std::make_shared<MockLLMAdapter>();
  assert_true(registry.initialize_and_register_provider_route(
                  make_provider_descriptor(), make_runtime_view(), "deepseek-chat", true,
                  make_config(), adapter),
              "adapter registry should initialize adapter and register route when provider projection succeeds");

  assert_true(adapter->last_init_config().has_value(),
              "adapter registry should call adapter init with the projected provider config");
  assert_equal(std::string("deepseek/canary"), adapter->last_init_config()->base_url_alias,
               "adapter init should receive mutable base_url alias from provider overlay");
  assert_equal(std::string("2026.04.13-canary"), adapter->last_init_config()->snapshot_version,
               "adapter init should receive provider snapshot version for traceability");

  const auto route = registry.resolve_route("deepseek-prod/deepseek-chat");
  assert_true(route.has_value(),
              "adapter registry should publish the provider/model route after successful adapter init");
  assert_equal(std::string("deepseek-prod"), route->adapter_id,
               "registered route should keep adapter_id aligned with provider instance identity");
  assert_equal(std::string("cloud"), route->deployment_type,
               "registered route should infer deployment_type from projected capability tags");
  assert_true(route->supports_streaming,
              "registered route should preserve model-level streaming support");
}

void test_adapter_registry_rejects_disabled_provider_instance_before_init() {
  AdapterRegistry registry;
  assert_true(registry.init(AdapterRegistryConfig{.blocked_failure_threshold = 3U}),
              "adapter registry should initialize before disabled-provider coverage");

  auto adapter = std::make_shared<MockLLMAdapter>();
  assert_true(!registry.initialize_and_register_provider_route(
                  make_provider_descriptor(), make_runtime_view(false), "deepseek-chat", true,
                  make_config(), adapter),
              "adapter registry should refuse disabled provider instances before adapter init is attempted");
  assert_equal(0, adapter->init_call_count(),
               "disabled provider instances should fail closed before adapter init is invoked");
  assert_true(!registry.resolve_route("deepseek-prod/deepseek-chat").has_value(),
              "disabled provider instances should not be published into the registry snapshot");
  assert_true(registry.last_error_message().find("disabled provider instance") != std::string::npos,
              "registry should surface a precise failure reason when a provider instance is disabled");
}

}  // namespace

int main() {
  try {
    test_provider_projection_projects_runtime_overlay_into_adapter_config();
    test_provider_projection_rejects_invalid_secret_references();
    test_adapter_registry_initializes_adapter_with_projected_provider_config();
    test_adapter_registry_rejects_disabled_provider_instance_before_init();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}