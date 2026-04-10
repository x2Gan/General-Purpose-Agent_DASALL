#include <exception>
#include <iostream>
#include <memory>
#include <optional>
#include <string_view>
#include <type_traits>
#include <vector>

#include "ILLMAdapter.h"
#include "ILLMManager.h"
#include "LLMAdapterConfig.h"
#include "LLMGenerateRequest.h"
#include "LLMManagerResult.h"
#include "error/ErrorInfo.h"
#include "error/ResultCode.h"
#include "llm/LLMRequest.h"
#include "llm/LLMResponse.h"
#include "support/TestAssertions.h"

#include "../../../llm/src/adapters/AdapterCallResult.h"

namespace {

// Keep a unique llm-specific test anchor registered before public headers land.
void test_llm_unit_surface_anchor_uses_a_collision_free_ctest_name() {
  using dasall::tests::support::assert_true;

  constexpr std::string_view ctest_name = "LLMInterfaceSurfaceTest";
  constexpr std::string_view target_name = "dasall_llm_interface_surface_unit_test";

  assert_true(ctest_name != "InterfaceSurfaceTest",
              "llm unit topology should not reuse the platform InterfaceSurfaceTest name");
  assert_true(ctest_name.find("LLM") == 0U,
              "llm unit topology should keep an llm-specific ctest prefix");
  assert_true(target_name.find("dasall_llm_") == 0U,
              "llm unit topology target should remain namespaced under dasall_llm");
}

void test_illm_adapter_surface_freezes_spi_signatures() {
  using dasall::contracts::LLMRequest;
  using dasall::llm::AdapterCallResult;
  using dasall::llm::HealthStatus;
  using dasall::llm::ILLMAdapter;
  using dasall::llm::IStreamObserver;
  using dasall::llm::LLMAdapterConfig;
  using dasall::llm::StreamSessionRef;
  using dasall::tests::support::assert_true;

  static_assert(std::is_same_v<decltype(&ILLMAdapter::init),
                 bool (ILLMAdapter::*)(const LLMAdapterConfig&)>);
  static_assert(std::is_same_v<decltype(&ILLMAdapter::generate),
                 AdapterCallResult (ILLMAdapter::*)(const LLMRequest&)>);
  static_assert(std::is_same_v<decltype(&ILLMAdapter::stream_generate),
                 StreamSessionRef (ILLMAdapter::*)(const LLMRequest&, IStreamObserver*)>);
  static_assert(std::is_same_v<decltype(&ILLMAdapter::health_check),
                 HealthStatus (ILLMAdapter::*)()>);
  static_assert(std::is_abstract_v<ILLMAdapter>);

  assert_true(std::is_abstract_v<ILLMAdapter>,
              "ILLMAdapter should remain a pure abstract adapter SPI");
}

void test_llm_adapter_config_freezes_expected_fields() {
  using dasall::llm::LLMAdapterConfig;
  using dasall::tests::support::assert_true;

  static_assert(std::is_same_v<decltype(LLMAdapterConfig{}.adapter_id), std::string>);
  static_assert(std::is_same_v<decltype(LLMAdapterConfig{}.adapter_family), std::string>);
  static_assert(std::is_same_v<decltype(LLMAdapterConfig{}.base_url), std::string>);
  static_assert(std::is_same_v<decltype(LLMAdapterConfig{}.auth_ref), std::string>);
  static_assert(std::is_same_v<decltype(LLMAdapterConfig{}.header_refs),
                 std::vector<std::string>>);
  static_assert(std::is_same_v<decltype(LLMAdapterConfig{}.timeout_ms), std::uint32_t>);
  static_assert(std::is_same_v<decltype(LLMAdapterConfig{}.max_retries), std::uint32_t>);
  static_assert(std::is_same_v<decltype(LLMAdapterConfig{}.capability_tags),
                 std::vector<std::string>>);

  const LLMAdapterConfig config{
    .adapter_id = "deepseek-primary",
    .adapter_family = "openai_compatible",
    .base_url = "https://api.deepseek.example/v1",
    .auth_ref = "secret://llm/deepseek-primary",
    .header_refs = {"profile://headers/x-trace-id"},
    .timeout_ms = 45000,
    .max_retries = 2,
    .capability_tags = {"reasoning", "tool_call"},
  };

  assert_true(config.adapter_id == "deepseek-primary",
              "LLMAdapterConfig should keep adapter_id as an explicit stable identity field");
  assert_true(config.header_refs.size() == 1U,
              "LLMAdapterConfig should preserve header_refs for injected provider headers");
  assert_true(config.capability_tags.size() == 2U,
              "LLMAdapterConfig should keep capability_tags as a stable vector field");
}

void test_adapter_call_result_freezes_non_exception_error_boundary() {
  using dasall::contracts::ErrorInfo;
  using dasall::contracts::ErrorSourceRefMinimal;
  using dasall::contracts::LLMResponse;
  using dasall::contracts::ResultCode;
  using dasall::contracts::ResultCodeCategory;
  using dasall::llm::AdapterCallResult;
  using dasall::tests::support::assert_true;

  static_assert(std::is_same_v<decltype(AdapterCallResult{}.response),
                 std::optional<LLMResponse>>);
  static_assert(std::is_same_v<decltype(AdapterCallResult{}.error),
                 std::optional<ErrorInfo>>);
  static_assert(std::is_same_v<decltype(AdapterCallResult{}.result_code),
                 std::optional<ResultCode>>);

  LLMResponse success_response;
  success_response.model_name = std::string("deepseek-chat");

  const AdapterCallResult success_result{
    .response = success_response,
    .error = std::nullopt,
    .result_code = std::nullopt,
  };

  const AdapterCallResult failure_result{
    .response = std::nullopt,
    .error = ErrorInfo{
      .failure_type = ResultCodeCategory::Provider,
      .retryable = true,
      .safe_to_replan = false,
      .details = {
        .code = static_cast<int>(ResultCode::ProviderTimeout),
        .message = std::string("provider timeout"),
        .stage = std::string("adapter.generate"),
      },
      .source_ref = ErrorSourceRefMinimal{
        .ref_type = std::string("adapter"),
        .ref_id = std::string("deepseek-primary"),
      },
    },
    .result_code = ResultCode::ProviderTimeout,
  };

  const AdapterCallResult invalid_result{
    .response = LLMResponse{},
    .error = failure_result.error,
    .result_code = ResultCode::ProviderTimeout,
  };

  assert_true(success_result.has_consistent_values(),
              "AdapterCallResult should accept a success payload without forcing exception flow");
  assert_true(failure_result.has_consistent_values(),
              "AdapterCallResult should accept provider failures through error and result_code fields");
  assert_true(!invalid_result.has_consistent_values(),
              "AdapterCallResult should reject mixed success and failure payloads");
}

void test_illm_manager_surface_freezes_spi_signatures() {
  using dasall::llm::HealthStatus;
  using dasall::llm::ILLMManager;
  using dasall::llm::IStreamObserver;
  using dasall::llm::LLMGenerateRequest;
  using dasall::llm::LLMManagerResult;
  using dasall::llm::LLMSubsystemConfig;
  using dasall::tests::support::assert_true;

  static_assert(std::is_same_v<decltype(&ILLMManager::init),
                 bool (ILLMManager::*)(const LLMSubsystemConfig&)>);
  static_assert(std::is_same_v<decltype(&ILLMManager::generate),
                 LLMManagerResult (ILLMManager::*)(const LLMGenerateRequest&)>);
  static_assert(std::is_same_v<decltype(&ILLMManager::stream_generate),
                 LLMManagerResult (ILLMManager::*)(const LLMGenerateRequest&, IStreamObserver*)>);
  static_assert(std::is_same_v<decltype(&ILLMManager::health_check),
                 HealthStatus (ILLMManager::*)() const>);
  static_assert(std::is_abstract_v<ILLMManager>);

  assert_true(std::is_abstract_v<ILLMManager>,
              "ILLMManager should remain a pure abstract runtime-facing SPI");
}

void test_llm_generate_request_freezes_runtime_handoff_fields() {
  using dasall::contracts::LLMRequest;
  using dasall::llm::LLMGenerateRequest;
  using dasall::llm::ModelSelectionHint;
  using dasall::tests::support::assert_true;

  static_assert(std::is_same_v<decltype(LLMGenerateRequest{}.stage), std::string>);
  static_assert(std::is_same_v<decltype(LLMGenerateRequest{}.task_type), std::string>);
  static_assert(std::is_same_v<decltype(LLMGenerateRequest{}.request), LLMRequest>);
  static_assert(std::is_same_v<decltype(LLMGenerateRequest{}.selection_hint),
                 std::shared_ptr<const ModelSelectionHint>>);

  LLMRequest request;
  request.request_id = std::string("req-llm-006");
  request.llm_call_id = std::string("llm-call-006");
  request.request_mode = dasall::contracts::LLMRequestMode::Unary;
  request.messages = std::vector<std::string>{"system:plan carefully", "user:diagnose timeout"};
  request.prompt_id = std::string("prompt.plan.default");
  request.prompt_version = std::string("2026-04-10.1");
  request.timeout_ms = 4000U;

  const LLMGenerateRequest generate_request{
    .stage = "planner",
    .task_type = "diagnostics",
    .request = request,
    .selection_hint = nullptr,
  };

  assert_true(generate_request.stage == "planner",
              "LLMGenerateRequest should freeze stage as an explicit routing dimension");
  assert_true(generate_request.task_type == "diagnostics",
              "LLMGenerateRequest should preserve task_type for deterministic model selection");
  assert_true(!generate_request.request.model_route.has_value(),
              "LLMGenerateRequest should allow pre-route LLMRequest handoff before ModelRouter resolves a final route");
}

void test_llm_manager_result_freezes_success_failure_and_fallback_semantics() {
  using dasall::contracts::ErrorInfo;
  using dasall::contracts::ErrorSourceRefMinimal;
  using dasall::contracts::LLMResponse;
  using dasall::contracts::LLMResponseKind;
  using dasall::contracts::ResultCode;
  using dasall::contracts::ResultCodeCategory;
  using dasall::llm::LLMFailureCategory;
  using dasall::llm::LLMManagerResult;
  using dasall::tests::support::assert_true;

  static_assert(std::is_same_v<decltype(LLMManagerResult{}.code),
                 std::optional<ResultCode>>);
  static_assert(std::is_same_v<decltype(LLMManagerResult{}.response),
                 std::optional<LLMResponse>>);
  static_assert(std::is_same_v<decltype(LLMManagerResult{}.error),
                 std::optional<ErrorInfo>>);
  static_assert(std::is_same_v<decltype(LLMManagerResult{}.resolved_route), std::string>);
  static_assert(std::is_same_v<decltype(LLMManagerResult{}.attempted_routes),
                 std::vector<std::string>>);
  static_assert(std::is_same_v<decltype(LLMManagerResult{}.failure_category),
                 std::optional<LLMFailureCategory>>);
  static_assert(std::is_same_v<decltype(LLMManagerResult{}.fallback_used), bool>);

  LLMResponse success_response;
  success_response.request_id = std::string("req-llm-006");
  success_response.llm_call_id = std::string("llm-call-006");
  success_response.response_kind = LLMResponseKind::DirectResponse;
  success_response.content_payload = std::string("diagnosis-ready");
  success_response.completed_at = 1710001001000;
  success_response.model_name = std::string("deepseek-reasoner");
  success_response.prompt_id = std::string("prompt.plan.default");
  success_response.prompt_version = std::string("2026-04-10.1");

  const LLMManagerResult success_result{
    .code = std::nullopt,
    .response = success_response,
    .error = std::nullopt,
    .resolved_route = "planner.cloud.reasoner",
    .attempted_routes = {"planner.cloud.reasoner"},
    .failure_category = std::nullopt,
    .fallback_used = false,
  };

  const LLMManagerResult fallback_failure_result{
    .code = ResultCode::ProviderTimeout,
    .response = std::nullopt,
    .error = ErrorInfo{
      .failure_type = ResultCodeCategory::Provider,
      .retryable = true,
      .safe_to_replan = false,
      .details = {
        .code = static_cast<int>(ResultCode::ProviderTimeout),
        .message = std::string("fallback routes exhausted after provider timeout"),
        .stage = std::string("llm.manager.generate"),
      },
      .source_ref = ErrorSourceRefMinimal{
        .ref_type = std::string("route"),
        .ref_id = std::string("planner.cloud.fallback"),
      },
    },
    .resolved_route = "planner.cloud.fallback",
    .attempted_routes = {"planner.cloud.primary", "planner.cloud.fallback"},
    .failure_category = LLMFailureCategory::FallbackExhausted,
    .fallback_used = true,
  };

  const LLMManagerResult invalid_result{
    .code = ResultCode::ProviderTimeout,
    .response = success_response,
    .error = fallback_failure_result.error,
    .resolved_route = "planner.cloud.fallback",
    .attempted_routes = {"planner.cloud.primary", "planner.cloud.fallback"},
    .failure_category = LLMFailureCategory::FallbackExhausted,
    .fallback_used = true,
  };

  assert_true(success_result.has_consistent_values(),
              "LLMManagerResult should accept a success payload without forcing a synthetic success code");
  assert_true(fallback_failure_result.has_consistent_values(),
              "LLMManagerResult should carry fallback attempt trace and final failure category when routes are exhausted");
  assert_true(!invalid_result.has_consistent_values(),
              "LLMManagerResult should reject mixed success and failure payloads");
}

}  // namespace

int main() {
  try {
    test_llm_unit_surface_anchor_uses_a_collision_free_ctest_name();
    test_illm_adapter_surface_freezes_spi_signatures();
    test_llm_adapter_config_freezes_expected_fields();
    test_adapter_call_result_freezes_non_exception_error_boundary();
    test_illm_manager_surface_freezes_spi_signatures();
    test_llm_generate_request_freezes_runtime_handoff_fields();
    test_llm_manager_result_freezes_success_failure_and_fallback_semantics();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}