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
#include "NormalizedUsageRecord.h"
#include "TokenEstimate.h"
#include "error/ErrorInfo.h"
#include "error/ResultCode.h"
#include "llm/LLMRequest.h"
#include "llm/LLMResponse.h"
#include "provider/ModelCatalogEntry.h"
#include "provider/ProviderDescriptor.h"
#include "prompt/IPromptComposer.h"
#include "prompt/IPromptPipeline.h"
#include "prompt/IPromptPolicy.h"
#include "prompt/IPromptRegistry.h"
#include "prompt/ModelBudgetHint.h"
#include "prompt/PromptPipelineConfig.h"
#include "prompt/PromptPipelineResult.h"
#include "prompt/PromptPolicyConfig.h"
#include "prompt/PromptPolicyDecision.h"
#include "prompt/PromptPolicyInput.h"
#include "prompt/PromptComposeRequest.h"
#include "prompt/PromptComposeResult.h"
#include "prompt/PromptComposerConfig.h"
#include "prompt/PromptQuery.h"
#include "prompt/PromptRegistryConfig.h"
#include "prompt/PromptRegistryResult.h"
#include "prompt/PromptRelease.h"
#include "route/ModelSelectionHint.h"
#include "route/ResolvedModelRoute.h"
#include "stream/StreamSessionRef.h"
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

void test_iprompt_registry_surface_freezes_spi_signatures() {
  using dasall::llm::prompt::IPromptRegistry;
  using dasall::llm::prompt::PromptQuery;
  using dasall::llm::prompt::PromptRegistryConfig;
  using dasall::llm::prompt::PromptRegistryResult;
  using dasall::tests::support::assert_true;

  static_assert(std::is_same_v<decltype(&IPromptRegistry::init),
                 bool (IPromptRegistry::*)(const PromptRegistryConfig&)>);
  static_assert(std::is_same_v<decltype(&IPromptRegistry::select),
                 PromptRegistryResult (IPromptRegistry::*)(const PromptQuery&) const>);
  static_assert(std::is_abstract_v<IPromptRegistry>);

  assert_true(std::is_abstract_v<IPromptRegistry>,
              "IPromptRegistry should remain a pure abstract prompt selection SPI");
}

void test_prompt_query_freezes_selection_dimensions() {
  using dasall::llm::prompt::PromptQuery;
  using dasall::tests::support::assert_true;

  static_assert(std::is_same_v<decltype(PromptQuery{}.stage), std::string>);
  static_assert(std::is_same_v<decltype(PromptQuery{}.task_type), std::string>);
  static_assert(std::is_same_v<decltype(PromptQuery{}.language), std::string>);
  static_assert(std::is_same_v<decltype(PromptQuery{}.model_family), std::string>);
  static_assert(std::is_same_v<decltype(PromptQuery{}.scene_id), std::string>);
  static_assert(std::is_same_v<decltype(PromptQuery{}.persona_id), std::string>);
  static_assert(std::is_same_v<decltype(PromptQuery{}.profile_id), std::string>);
  static_assert(std::is_same_v<decltype(PromptQuery{}.available_tools),
                 std::vector<std::string>>);
  static_assert(std::is_same_v<decltype(PromptQuery{}.trusted_sources),
                 std::vector<std::string>>);

  const PromptQuery query{
    .stage = "planner",
    .task_type = "analysis",
    .language = "zh-CN",
    .model_family = "deepseek",
    .scene_id = "ops_diagnosis",
    .persona_id = "default_planner",
    .profile_id = "desktop_full",
    .available_tools = {"search", "summarize"},
    .trusted_sources = {"baseline", "deployment_override"},
  };

  assert_true(query.available_tools.size() == 2U,
              "PromptQuery should keep available_tools as an explicit selection dimension");
  assert_true(query.trusted_sources.size() == 2U,
              "PromptQuery should keep trusted_sources as an explicit fail-closed selection filter");
}

void test_prompt_registry_config_freezes_asset_root_and_trusted_sources() {
  using dasall::llm::prompt::PromptRegistryConfig;
  using dasall::tests::support::assert_true;

  static_assert(std::is_same_v<decltype(PromptRegistryConfig{}.asset_root), std::string>);
  static_assert(std::is_same_v<decltype(PromptRegistryConfig{}.trusted_sources),
                 std::vector<std::string>>);

  const PromptRegistryConfig config{
    .asset_root = "/opt/dasall/prompts",
    .trusted_sources = {"baseline", "trusted_snapshot"},
  };

  assert_true(config.asset_root == "/opt/dasall/prompts",
              "PromptRegistryConfig should freeze the prompt asset root field");
  assert_true(config.trusted_sources.size() == 2U,
              "PromptRegistryConfig should preserve trusted_sources as registry init input");
}

void test_prompt_registry_result_freezes_selection_metadata() {
  using dasall::contracts::CompositionStage;
  using dasall::contracts::PromptEvalStatus;
  using dasall::contracts::PromptRelease;
  using dasall::contracts::ResultCode;
  using dasall::llm::prompt::PromptRegistryResult;
  using dasall::tests::support::assert_true;

  static_assert(std::is_same_v<decltype(PromptRegistryResult{}.code),
                 std::optional<ResultCode>>);
  static_assert(std::is_same_v<decltype(PromptRegistryResult{}.release),
                 std::optional<PromptRelease>>);
  static_assert(std::is_same_v<decltype(PromptRegistryResult{}.selected_prompt_id), std::string>);
  static_assert(std::is_same_v<decltype(PromptRegistryResult{}.selected_version), std::string>);
  static_assert(std::is_same_v<decltype(PromptRegistryResult{}.selection_reason), std::string>);
  static_assert(std::is_same_v<decltype(PromptRegistryResult{}.trusted_sources_matched),
                 std::vector<std::string>>);

  PromptRelease selected_release;
  selected_release.prompt_id = std::string("prompt.plan.default");
  selected_release.version = std::string("2026-04-10.1");
  selected_release.stage = CompositionStage::Planning;
  selected_release.eval_status = PromptEvalStatus::Stable;
  selected_release.trusted_source = std::string("baseline");

  const PromptRegistryResult success_result{
    .code = std::nullopt,
    .release = selected_release,
    .selected_prompt_id = "prompt.plan.default",
    .selected_version = "2026-04-10.1",
    .selection_reason = "matched stage/task_type and trusted source",
    .trusted_sources_matched = {"baseline"},
  };

  const PromptRegistryResult failure_result{
    .code = ResultCode::PolicyDenied,
    .release = std::nullopt,
    .selected_prompt_id = "",
    .selected_version = "",
    .selection_reason = "",
    .trusted_sources_matched = {},
  };

  const PromptRegistryResult invalid_result{
    .code = ResultCode::PolicyDenied,
    .release = selected_release,
    .selected_prompt_id = "prompt.plan.default",
    .selected_version = "2026-04-10.1",
    .selection_reason = "mixed success and failure",
    .trusted_sources_matched = {"baseline"},
  };

  assert_true(success_result.has_consistent_values(),
              "PromptRegistryResult should accept a selected release with auditable prompt metadata");
  assert_true(failure_result.has_consistent_values(),
              "PromptRegistryResult should allow selection failure without fabricating a PromptRelease");
  assert_true(!invalid_result.has_consistent_values(),
              "PromptRegistryResult should reject mixed success and failure selection payloads");
}

void test_iprompt_composer_surface_freezes_spi_signatures() {
  using dasall::contracts::PromptComposeRequest;
  using dasall::contracts::PromptComposeResult;
  using dasall::contracts::PromptRelease;
  using dasall::llm::prompt::IPromptComposer;
  using dasall::llm::prompt::ModelBudgetHint;
  using dasall::llm::prompt::PromptComposerConfig;
  using dasall::tests::support::assert_true;

  static_assert(std::is_same_v<decltype(&IPromptComposer::init),
                 bool (IPromptComposer::*)(const PromptComposerConfig&)>);
  static_assert(std::is_same_v<decltype(&IPromptComposer::compose),
                 PromptComposeResult (IPromptComposer::*)(
                     const PromptComposeRequest&, const PromptRelease&, const ModelBudgetHint&) const>);
  static_assert(std::is_abstract_v<IPromptComposer>);

  assert_true(std::is_abstract_v<IPromptComposer>,
              "IPromptComposer should remain a pure abstract prompt composition SPI");
}

void test_model_budget_hint_freezes_context_window_inputs() {
  using dasall::llm::prompt::ModelBudgetHint;
  using dasall::tests::support::assert_true;

  static_assert(std::is_same_v<decltype(ModelBudgetHint{}.context_window), std::uint32_t>);
  static_assert(std::is_same_v<decltype(ModelBudgetHint{}.max_output_tokens), std::uint32_t>);
  static_assert(std::is_same_v<decltype(ModelBudgetHint{}.reserved_output_tokens), std::uint32_t>);

  const ModelBudgetHint budget_hint{
    .context_window = 128000,
    .max_output_tokens = 8192,
    .reserved_output_tokens = 2048,
  };

  assert_true(budget_hint.context_window == 128000U,
              "ModelBudgetHint should freeze context_window for pre-call prompt budgeting");
  assert_true(budget_hint.max_output_tokens == 8192U,
              "ModelBudgetHint should keep max_output_tokens visible to PromptComposer");
  assert_true(budget_hint.reserved_output_tokens == 2048U,
              "ModelBudgetHint should preserve reserved_output_tokens for over-budget warnings");
}

void test_prompt_composer_config_freezes_renderer_controls() {
  using dasall::llm::prompt::PromptComposerConfig;
  using dasall::tests::support::assert_true;

  static_assert(std::is_same_v<decltype(PromptComposerConfig{}.template_engine), std::string>);
  static_assert(std::is_same_v<decltype(PromptComposerConfig{}.max_few_shot_count), std::uint32_t>);

  const PromptComposerConfig config{
    .template_engine = "simple_var",
    .max_few_shot_count = 4,
  };

  assert_true(config.template_engine == "simple_var",
              "PromptComposerConfig should freeze template_engine as the renderer selection knob");
  assert_true(config.max_few_shot_count == 4U,
              "PromptComposerConfig should preserve max_few_shot_count as a composition guardrail");
}

void test_iprompt_policy_surface_freezes_spi_signatures() {
  using dasall::contracts::PromptComposeResult;
  using dasall::llm::prompt::IPromptPolicy;
  using dasall::llm::prompt::PromptPolicyConfig;
  using dasall::llm::prompt::PromptPolicyDecision;
  using dasall::llm::prompt::PromptPolicyInput;
  using dasall::tests::support::assert_true;

  static_assert(std::is_same_v<decltype(&IPromptPolicy::init),
                 bool (IPromptPolicy::*)(const PromptPolicyConfig&)>);
  static_assert(std::is_same_v<decltype(&IPromptPolicy::evaluate),
                 PromptPolicyDecision (IPromptPolicy::*)(
                     const PromptComposeResult&, const PromptPolicyInput&) const>);
  static_assert(std::is_abstract_v<IPromptPolicy>);

  assert_true(std::is_abstract_v<IPromptPolicy>,
              "IPromptPolicy should remain a pure abstract prompt governance SPI");
}

void test_prompt_policy_input_freezes_profile_and_governance_dimensions() {
  using dasall::llm::prompt::PromptPolicyInput;
  using dasall::tests::support::assert_true;

  static_assert(std::is_same_v<decltype(PromptPolicyInput{}.profile_id), std::string>);
  static_assert(std::is_same_v<decltype(PromptPolicyInput{}.allowed_prompt_releases),
                 std::vector<std::string>>);
  static_assert(std::is_same_v<decltype(PromptPolicyInput{}.trusted_sources),
                 std::vector<std::string>>);
  static_assert(std::is_same_v<decltype(PromptPolicyInput{}.tool_visibility_rules),
                 std::vector<std::string>>);
  static_assert(std::is_same_v<decltype(PromptPolicyInput{}.render_budget_tokens),
                 std::uint32_t>);
  static_assert(std::is_same_v<decltype(PromptPolicyInput{}.active_scene), std::string>);
  static_assert(std::is_same_v<decltype(PromptPolicyInput{}.active_persona), std::string>);

  const PromptPolicyInput input{
    .profile_id = "desktop_full",
    .allowed_prompt_releases = {"prompt.plan.default@2026-04-10.1"},
    .trusted_sources = {"baseline", "trusted_snapshot"},
    .tool_visibility_rules = {"fs.read=visible", "shell.exec=hidden"},
    .render_budget_tokens = 4096,
    .active_scene = "ops_diagnosis",
    .active_persona = "default_planner",
  };

  assert_true(input.allowed_prompt_releases.size() == 1U,
              "PromptPolicyInput should preserve prompt release allowlist as an explicit governance input");
  assert_true(input.trusted_sources.size() == 2U,
              "PromptPolicyInput should keep trusted_sources as an explicit fail-closed filter");
  assert_true(input.render_budget_tokens == 4096U,
              "PromptPolicyInput should keep render_budget_tokens visible for over-budget evaluation");
}

void test_prompt_policy_config_freezes_fail_closed_defaults() {
  using dasall::llm::prompt::PromptPolicyConfig;
  using dasall::tests::support::assert_true;

  static_assert(std::is_same_v<decltype(PromptPolicyConfig{}.default_allowed_releases),
                 std::vector<std::string>>);
  static_assert(std::is_same_v<decltype(PromptPolicyConfig{}.default_trusted_sources),
                 std::vector<std::string>>);
  static_assert(std::is_same_v<decltype(PromptPolicyConfig{}.deny_on_missing_allowlist), bool>);

  const PromptPolicyConfig config{
    .default_allowed_releases = {"prompt.plan.default@2026-04-10.1"},
    .default_trusted_sources = {"baseline"},
    .deny_on_missing_allowlist = true,
  };

  assert_true(config.default_allowed_releases.size() == 1U,
              "PromptPolicyConfig should keep default_allowed_releases as a stable init-time allowlist");
  assert_true(config.default_trusted_sources.size() == 1U,
              "PromptPolicyConfig should preserve default_trusted_sources as a stable init-time filter");
  assert_true(config.deny_on_missing_allowlist,
              "PromptPolicyConfig should default to deny_on_missing_allowlist for fail-closed governance");
}

void test_prompt_policy_decision_freezes_disposition_and_message_boundary() {
  using dasall::llm::prompt::PromptPolicyDecision;
  using dasall::llm::prompt::PromptPolicyDisposition;
  using dasall::tests::support::assert_true;

  static_assert(std::is_same_v<decltype(PromptPolicyDecision{}.disposition),
                 PromptPolicyDisposition>);
  static_assert(std::is_same_v<decltype(PromptPolicyDecision{}.governed_messages),
                 std::vector<std::string>>);
  static_assert(std::is_same_v<decltype(PromptPolicyDecision{}.redactions),
                 std::vector<std::string>>);
  static_assert(std::is_same_v<decltype(PromptPolicyDecision{}.tool_visibility_patch),
                 std::vector<std::string>>);
  static_assert(std::is_same_v<decltype(PromptPolicyDecision{}.reason), std::string>);

  const PromptPolicyDecision allow_decision{
    .disposition = PromptPolicyDisposition::Allow,
    .governed_messages = {"system:plan carefully", "user:diagnose timeout"},
    .redactions = {"secret_ref masked"},
    .tool_visibility_patch = {"shell.exec=hidden"},
    .reason = "trusted source and allowlist matched",
  };

  const PromptPolicyDecision deny_decision{
    .disposition = PromptPolicyDisposition::Deny,
    .governed_messages = {},
    .redactions = {},
    .tool_visibility_patch = {},
    .reason = "prompt release missing from allowlist",
  };

  const PromptPolicyDecision over_budget_decision{
    .disposition = PromptPolicyDisposition::OverBudget,
    .governed_messages = {},
    .redactions = {},
    .tool_visibility_patch = {},
    .reason = "render budget exceeded configured token limit",
  };

  const PromptPolicyDecision recompose_decision{
    .disposition = PromptPolicyDisposition::RequireRecompose,
    .governed_messages = {},
    .redactions = {"tool summary removed"},
    .tool_visibility_patch = {"fs.read=visible"},
    .reason = "tool visibility drift requires recomposition",
  };

  const PromptPolicyDecision invalid_decision{
    .disposition = PromptPolicyDisposition::Deny,
    .governed_messages = {"assistant:this should not pass"},
    .redactions = {},
    .tool_visibility_patch = {},
    .reason = "deny result must not emit governed_messages",
  };

  assert_true(allow_decision.has_consistent_values(),
              "PromptPolicyDecision should allow governed_messages only when disposition is Allow");
  assert_true(deny_decision.has_consistent_values(),
              "PromptPolicyDecision should express deny without fabricating governed_messages");
  assert_true(over_budget_decision.has_consistent_values(),
              "PromptPolicyDecision should preserve OverBudget as a first-class disposition");
  assert_true(recompose_decision.has_consistent_values(),
              "PromptPolicyDecision should preserve RequireRecompose without implying Allow");
  assert_true(!invalid_decision.has_consistent_values(),
              "PromptPolicyDecision should reject deny payloads that still emit governed_messages");
}

void test_iprompt_pipeline_surface_freezes_facade_signatures() {
  using dasall::contracts::PromptComposeRequest;
  using dasall::llm::prompt::IPromptPipeline;
  using dasall::llm::prompt::PromptPipelineConfig;
  using dasall::llm::prompt::PromptPipelineResult;
  using dasall::llm::prompt::PromptPolicyInput;
  using dasall::llm::prompt::PromptQuery;
  using dasall::tests::support::assert_true;

  static_assert(std::is_same_v<decltype(&IPromptPipeline::init),
                 bool (IPromptPipeline::*)(const PromptPipelineConfig&)>);
  static_assert(std::is_same_v<decltype(&IPromptPipeline::run),
                 PromptPipelineResult (IPromptPipeline::*)(
                     const PromptQuery&, const PromptComposeRequest&, const PromptPolicyInput&) const>);
  static_assert(std::is_abstract_v<IPromptPipeline>);

  assert_true(std::is_abstract_v<IPromptPipeline>,
              "IPromptPipeline should remain a pure abstract facade over select-compose-evaluate");
}

void test_prompt_pipeline_config_freezes_three_stage_init_bundle() {
  using dasall::llm::prompt::PromptComposerConfig;
  using dasall::llm::prompt::PromptPipelineConfig;
  using dasall::llm::prompt::PromptPolicyConfig;
  using dasall::llm::prompt::PromptRegistryConfig;
  using dasall::tests::support::assert_true;

  static_assert(std::is_same_v<decltype(PromptPipelineConfig{}.registry_config), PromptRegistryConfig>);
  static_assert(std::is_same_v<decltype(PromptPipelineConfig{}.composer_config), PromptComposerConfig>);
  static_assert(std::is_same_v<decltype(PromptPipelineConfig{}.policy_config), PromptPolicyConfig>);

  const PromptPipelineConfig config{
    .registry_config = {
      .asset_root = "assets/prompts",
      .trusted_sources = {"baseline", "signed_overlay"},
    },
    .composer_config = {
      .template_engine = "simple_var",
      .max_few_shot_count = 4,
    },
    .policy_config = {
      .default_allowed_releases = {"prompt.plan.default@2026-04-10.1"},
      .default_trusted_sources = {"baseline"},
      .deny_on_missing_allowlist = true,
    },
  };

  assert_true(config.registry_config.trusted_sources.size() == 2U,
              "PromptPipelineConfig should freeze PromptRegistry init config inside the facade bundle");
  assert_true(config.composer_config.max_few_shot_count == 4U,
              "PromptPipelineConfig should preserve PromptComposerConfig without widening the facade scope");
  assert_true(config.policy_config.deny_on_missing_allowlist,
              "PromptPipelineConfig should preserve fail-closed PromptPolicyConfig defaults");
}

void test_prompt_pipeline_result_freezes_three_stage_outputs_without_model_inputs() {
  using dasall::contracts::CompositionStage;
  using dasall::contracts::PromptEvalStatus;
  using dasall::contracts::PromptComposeResult;
  using dasall::contracts::PromptRelease;
  using dasall::llm::prompt::PromptPipelineResult;
  using dasall::llm::prompt::PromptPolicyDecision;
  using dasall::llm::prompt::PromptPolicyDisposition;
  using dasall::llm::prompt::PromptRegistryResult;
  using dasall::tests::support::assert_true;

  static_assert(std::is_same_v<decltype(PromptPipelineResult{}.disposition),
                 PromptPolicyDisposition>);
  static_assert(std::is_same_v<decltype(PromptPipelineResult{}.compose_result),
                 std::optional<PromptComposeResult>>);
  static_assert(std::is_same_v<decltype(PromptPipelineResult{}.policy_decision),
                 std::optional<PromptPolicyDecision>>);
  static_assert(std::is_same_v<decltype(PromptPipelineResult{}.registry_result),
                 std::optional<PromptRegistryResult>>);
  static_assert(std::is_same_v<decltype(PromptPipelineResult{}.reason), std::string>);

  const PromptRelease selected_release{
    .prompt_id = "prompt.plan.default",
    .version = "2026-04-10.1",
    .stage = CompositionStage::Planning,
    .eval_status = PromptEvalStatus::Stable,
    .release_scope = "desktop_full",
    .system_instructions = "plan carefully",
    .task_template = "diagnose timeout",
    .output_schema_ref = std::nullopt,
    .few_shot_refs = std::nullopt,
    .policy_notes = std::nullopt,
    .rollback_from = std::nullopt,
    .trusted_source = "baseline",
    .tags = std::nullopt,
  };

  const PromptPipelineResult allow_result{
    .disposition = PromptPolicyDisposition::Allow,
    .compose_result = PromptComposeResult{},
    .policy_decision = PromptPolicyDecision{
      .disposition = PromptPolicyDisposition::Allow,
      .governed_messages = {"system:plan carefully", "user:diagnose timeout"},
      .redactions = {},
      .tool_visibility_patch = {},
      .reason = "policy accepted composed prompt",
    },
    .registry_result = PromptRegistryResult{
      .code = std::nullopt,
      .release = selected_release,
      .selected_prompt_id = "prompt.plan.default",
      .selected_version = "2026-04-10.1",
      .selection_reason = "matched profile and scene",
      .trusted_sources_matched = {"baseline"},
    },
    .reason = "",
  };

  const PromptPipelineResult over_budget_result{
    .disposition = PromptPolicyDisposition::OverBudget,
    .compose_result = PromptComposeResult{},
    .policy_decision = PromptPolicyDecision{
      .disposition = PromptPolicyDisposition::OverBudget,
      .governed_messages = {},
      .redactions = {},
      .tool_visibility_patch = {},
      .reason = "render budget exceeded configured token limit",
    },
    .registry_result = PromptRegistryResult{
      .code = std::nullopt,
      .release = selected_release,
      .selected_prompt_id = "prompt.plan.default",
      .selected_version = "2026-04-10.1",
      .selection_reason = "matched profile and scene",
      .trusted_sources_matched = {"baseline"},
    },
    .reason = "policy requested context recomposition via over-budget signal",
  };

  assert_true(allow_result.compose_result.has_value(),
              "PromptPipelineResult should surface PromptComposeResult so Runtime can build the next LLM request");
  assert_true(allow_result.policy_decision.has_value(),
              "PromptPipelineResult should carry the final PromptPolicyDecision from the facade");
  assert_true(allow_result.registry_result.has_value(),
              "PromptPipelineResult should preserve PromptRegistry selection metadata for auditing");
  assert_true(over_budget_result.disposition == PromptPolicyDisposition::OverBudget,
              "PromptPipelineResult should preserve OverBudget as a first-class pipeline outcome");
}

void test_provider_descriptor_freezes_provider_asset_identity() {
  using dasall::llm::ProviderDescriptor;
  using dasall::tests::support::assert_true;

  static_assert(std::is_same_v<decltype(ProviderDescriptor{}.provider_id), std::string>);
  static_assert(std::is_same_v<decltype(ProviderDescriptor{}.adapter_family), std::string>);
  static_assert(std::is_same_v<decltype(ProviderDescriptor{}.api_family), std::string>);
  static_assert(std::is_same_v<decltype(ProviderDescriptor{}.base_url), std::string>);
  static_assert(std::is_same_v<decltype(ProviderDescriptor{}.auth_ref), std::string>);
  static_assert(std::is_same_v<decltype(ProviderDescriptor{}.header_refs),
                 std::vector<std::string>>);
  static_assert(std::is_same_v<decltype(ProviderDescriptor{}.capability_tags),
                 std::vector<std::string>>);
  static_assert(std::is_same_v<decltype(ProviderDescriptor{}.source_version), std::string>);

  const ProviderDescriptor descriptor{
    .provider_id = "deepseek-primary",
    .adapter_family = "openai_compatible",
    .api_family = "deepseek_api",
    .base_url = "https://api.deepseek.example/v1",
    .auth_ref = "secret://llm/deepseek-primary",
    .header_refs = {"profile://headers/x-trace-id"},
    .capability_tags = {"reasoning", "prompt_cache", "tool_call"},
    .source_version = "providers/deepseek@2026-04-11.1",
  };

  assert_true(descriptor.header_refs.size() == 1U,
              "ProviderDescriptor should preserve injected header references as provider metadata");
  assert_true(descriptor.capability_tags.size() == 3U,
              "ProviderDescriptor should freeze capability_tags for provider governance and routing");
  assert_true(descriptor.source_version == "providers/deepseek@2026-04-11.1",
              "ProviderDescriptor should keep source_version visible for catalog traceability");
}

void test_model_catalog_entry_freezes_pricing_and_verification_axes() {
  using dasall::llm::ModelCatalogEntry;
  using dasall::tests::support::assert_true;

  static_assert(std::is_same_v<decltype(ModelCatalogEntry{}.provider_id), std::string>);
  static_assert(std::is_same_v<decltype(ModelCatalogEntry{}.model_id), std::string>);
  static_assert(std::is_same_v<decltype(ModelCatalogEntry{}.model_version), std::string>);
  static_assert(std::is_same_v<decltype(ModelCatalogEntry{}.tier_family), std::string>);
  static_assert(std::is_same_v<decltype(ModelCatalogEntry{}.latency_tier), std::string>);
  static_assert(std::is_same_v<decltype(ModelCatalogEntry{}.cost_tier), std::string>);
  static_assert(std::is_same_v<decltype(ModelCatalogEntry{}.reasoning_depth_tier), std::string>);
  static_assert(std::is_same_v<decltype(ModelCatalogEntry{}.context_window), std::uint32_t>);
  static_assert(std::is_same_v<decltype(ModelCatalogEntry{}.default_max_output_tokens), std::uint32_t>);
  static_assert(std::is_same_v<decltype(ModelCatalogEntry{}.max_output_tokens_hard_limit), std::uint32_t>);
  static_assert(std::is_same_v<decltype(ModelCatalogEntry{}.supports_tools), bool>);
  static_assert(std::is_same_v<decltype(ModelCatalogEntry{}.supports_reasoning), bool>);
  static_assert(std::is_same_v<decltype(ModelCatalogEntry{}.supports_visible_reasoning), bool>);
  static_assert(std::is_same_v<decltype(ModelCatalogEntry{}.supports_prompt_cache), bool>);
  static_assert(std::is_same_v<decltype(ModelCatalogEntry{}.input_cache_hit_usd_per_1m), double>);
  static_assert(std::is_same_v<decltype(ModelCatalogEntry{}.input_cache_miss_usd_per_1m), double>);
  static_assert(std::is_same_v<decltype(ModelCatalogEntry{}.output_usd_per_1m), double>);
  static_assert(std::is_same_v<decltype(ModelCatalogEntry{}.metadata_source_uri), std::string>);
  static_assert(std::is_same_v<decltype(ModelCatalogEntry{}.metadata_effective_at), std::string>);
  static_assert(std::is_same_v<decltype(ModelCatalogEntry{}.verification_state), std::string>);

  const ModelCatalogEntry entry{
    .provider_id = "deepseek-primary",
    .model_id = "deepseek-reasoner",
    .model_version = "2026-04-11",
    .tier_family = "reasoning",
    .latency_tier = "high",
    .cost_tier = "premium",
    .reasoning_depth_tier = "deep",
    .context_window = 128000,
    .default_max_output_tokens = 8192,
    .max_output_tokens_hard_limit = 65536,
    .supports_tools = true,
    .supports_reasoning = true,
    .supports_visible_reasoning = true,
    .supports_prompt_cache = true,
    .input_cache_hit_usd_per_1m = 0.14,
    .input_cache_miss_usd_per_1m = 0.55,
    .output_usd_per_1m = 2.19,
    .metadata_source_uri = "assets/providers/deepseek/models.yaml",
    .metadata_effective_at = "2026-04-11T00:00:00Z",
    .verification_state = "verified",
  };

  assert_true(entry.context_window == 128000U,
              "ModelCatalogEntry should freeze context_window as a static provider catalog fact");
  assert_true(entry.supports_visible_reasoning,
              "ModelCatalogEntry should preserve reasoning visibility as a first-class capability flag");
  assert_true(entry.output_usd_per_1m > entry.input_cache_hit_usd_per_1m,
              "ModelCatalogEntry should expose pricing tiers for usage aggregation and routing");
}

void test_resolved_model_route_freezes_primary_and_fallback_chain() {
  using dasall::llm::ResolvedModelRoute;
  using dasall::tests::support::assert_true;

  static_assert(std::is_same_v<decltype(ResolvedModelRoute{}.stage), std::string>);
  static_assert(std::is_same_v<decltype(ResolvedModelRoute{}.primary_route), std::string>);
  static_assert(std::is_same_v<decltype(ResolvedModelRoute{}.fallback_routes),
                 std::vector<std::string>>);
  static_assert(std::is_same_v<decltype(ResolvedModelRoute{}.streaming_enabled), bool>);

  const ResolvedModelRoute route{
    .stage = "planning",
    .primary_route = "deepseek-reasoner",
    .fallback_routes = {"deepseek-chat", "deepseek-economy"},
    .streaming_enabled = false,
  };

  assert_true(route.primary_route == "deepseek-reasoner",
              "ResolvedModelRoute should freeze the resolved primary route string");
  assert_true(route.fallback_routes.size() == 2U,
              "ResolvedModelRoute should preserve fallback_routes as an ordered degradation chain");
  assert_true(!route.streaming_enabled,
              "ResolvedModelRoute should keep streaming_enabled explicit instead of inferring it from route names");
}

void test_model_selection_hint_freezes_routing_governance_inputs() {
  using dasall::llm::ModelSelectionHint;
  using dasall::tests::support::assert_true;

  static_assert(std::is_same_v<decltype(ModelSelectionHint{}.stage), std::string>);
  static_assert(std::is_same_v<decltype(ModelSelectionHint{}.task_type), std::string>);
  static_assert(std::is_same_v<decltype(ModelSelectionHint{}.complexity_tier), std::string>);
  static_assert(std::is_same_v<decltype(ModelSelectionHint{}.latency_sla_tier), std::string>);
  static_assert(std::is_same_v<decltype(ModelSelectionHint{}.budget_tier), std::string>);
  static_assert(std::is_same_v<decltype(ModelSelectionHint{}.requires_tools), bool>);
  static_assert(std::is_same_v<decltype(ModelSelectionHint{}.requires_reasoning), bool>);
  static_assert(std::is_same_v<decltype(ModelSelectionHint{}.prefers_visible_reasoning), bool>);
  static_assert(std::is_same_v<decltype(ModelSelectionHint{}.estimated_input_tokens), std::uint32_t>);
  static_assert(std::is_same_v<decltype(ModelSelectionHint{}.target_output_tokens), std::uint32_t>);
  static_assert(std::is_same_v<decltype(ModelSelectionHint{}.previous_route_failures), std::uint32_t>);

  const ModelSelectionHint hint{
    .stage = "planning",
    .task_type = "diagnosis",
    .complexity_tier = "high",
    .latency_sla_tier = "interactive",
    .budget_tier = "balanced",
    .requires_tools = true,
    .requires_reasoning = true,
    .prefers_visible_reasoning = false,
    .estimated_input_tokens = 6144,
    .target_output_tokens = 2048,
    .previous_route_failures = 1,
  };

  assert_true(hint.requires_tools,
              "ModelSelectionHint should preserve tool requirements as an explicit routing input");
  assert_true(hint.requires_reasoning,
              "ModelSelectionHint should preserve reasoning requirements as an explicit routing input");
  assert_true(hint.previous_route_failures == 1U,
              "ModelSelectionHint should keep previous_route_failures visible for deterministic fallback scoring");
}

void test_stream_session_ref_freezes_module_local_lifecycle_anchor() {
  using dasall::llm::StreamSessionRef;
  using dasall::tests::support::assert_true;

  static_assert(std::is_same_v<decltype(StreamSessionRef{}.session_id), std::string>);

  const StreamSessionRef session_ref{
    .session_id = "stream-2026-04-11-0001",
  };

  assert_true(session_ref.session_id == "stream-2026-04-11-0001",
              "StreamSessionRef should remain a module-local lifecycle anchor instead of a shared streaming contract");
}

void test_token_estimate_freezes_budget_projection_fields() {
  using dasall::llm::TokenEstimate;
  using dasall::tests::support::assert_true;

  static_assert(std::is_same_v<decltype(TokenEstimate{}.estimated_input_tokens), std::uint32_t>);
  static_assert(std::is_same_v<decltype(TokenEstimate{}.reserved_output_tokens), std::uint32_t>);
  static_assert(std::is_same_v<decltype(TokenEstimate{}.context_window), std::uint32_t>);
  static_assert(std::is_same_v<decltype(TokenEstimate{}.over_budget), bool>);
  static_assert(std::is_same_v<decltype(TokenEstimate{}.safety_margin), double>);

  const TokenEstimate estimate{
    .estimated_input_tokens = 5800,
    .reserved_output_tokens = 2048,
    .context_window = 4096,
    .over_budget = true,
    .safety_margin = 0.08,
  };

  assert_true(estimate.over_budget,
              "TokenEstimate should preserve over_budget as an explicit governance outcome");
  assert_true(estimate.safety_margin == 0.08,
              "TokenEstimate should freeze safety_margin so estimation policy stays observable");
  assert_true(estimate.estimated_input_tokens + estimate.reserved_output_tokens > estimate.context_window,
              "TokenEstimate should expose the arithmetic inputs behind over-budget decisions");
}

void test_normalized_usage_record_freezes_usage_aggregation_output() {
  using dasall::llm::NormalizedUsageRecord;
  using dasall::tests::support::assert_true;

  static_assert(std::is_same_v<decltype(NormalizedUsageRecord{}.prompt_tokens), std::uint32_t>);
  static_assert(std::is_same_v<decltype(NormalizedUsageRecord{}.completion_tokens), std::uint32_t>);
  static_assert(std::is_same_v<decltype(NormalizedUsageRecord{}.total_tokens), std::uint32_t>);
  static_assert(std::is_same_v<decltype(NormalizedUsageRecord{}.prompt_cache_hit_tokens), std::uint32_t>);
  static_assert(std::is_same_v<decltype(NormalizedUsageRecord{}.prompt_cache_miss_tokens), std::uint32_t>);
  static_assert(std::is_same_v<decltype(NormalizedUsageRecord{}.estimated_cost_usd), double>);
  static_assert(std::is_same_v<decltype(NormalizedUsageRecord{}.provider_id), std::string>);
  static_assert(std::is_same_v<decltype(NormalizedUsageRecord{}.model_id), std::string>);
  static_assert(std::is_same_v<decltype(NormalizedUsageRecord{}.pricing_ref), std::string>);

  const NormalizedUsageRecord usage{
    .prompt_tokens = 1200,
    .completion_tokens = 600,
    .total_tokens = 1800,
    .prompt_cache_hit_tokens = 400,
    .prompt_cache_miss_tokens = 800,
    .estimated_cost_usd = 0.0048,
    .provider_id = "deepseek-primary",
    .model_id = "deepseek-chat",
    .pricing_ref = "providers/deepseek/models.yaml#deepseek-chat",
  };

  assert_true(usage.total_tokens == usage.prompt_tokens + usage.completion_tokens,
              "NormalizedUsageRecord should preserve total_tokens as the normalized aggregate of prompt and completion usage");
  assert_true(usage.prompt_cache_hit_tokens == 400U,
              "NormalizedUsageRecord should expose prompt_cache_hit_tokens for provider-neutral accounting");
  assert_true(usage.estimated_cost_usd > 0.0,
              "NormalizedUsageRecord should keep estimated_cost_usd visible for observability and audit sinks");
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
    test_iprompt_registry_surface_freezes_spi_signatures();
    test_prompt_query_freezes_selection_dimensions();
    test_prompt_registry_config_freezes_asset_root_and_trusted_sources();
    test_prompt_registry_result_freezes_selection_metadata();
    test_iprompt_composer_surface_freezes_spi_signatures();
    test_model_budget_hint_freezes_context_window_inputs();
    test_prompt_composer_config_freezes_renderer_controls();
    test_iprompt_policy_surface_freezes_spi_signatures();
    test_prompt_policy_input_freezes_profile_and_governance_dimensions();
    test_prompt_policy_config_freezes_fail_closed_defaults();
    test_prompt_policy_decision_freezes_disposition_and_message_boundary();
    test_iprompt_pipeline_surface_freezes_facade_signatures();
    test_prompt_pipeline_config_freezes_three_stage_init_bundle();
    test_prompt_pipeline_result_freezes_three_stage_outputs_without_model_inputs();
    test_provider_descriptor_freezes_provider_asset_identity();
    test_model_catalog_entry_freezes_pricing_and_verification_axes();
    test_resolved_model_route_freezes_primary_and_fallback_chain();
    test_model_selection_hint_freezes_routing_governance_inputs();
    test_stream_session_ref_freezes_module_local_lifecycle_anchor();
    test_token_estimate_freezes_budget_projection_fields();
    test_normalized_usage_record_freezes_usage_aggregation_output();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}