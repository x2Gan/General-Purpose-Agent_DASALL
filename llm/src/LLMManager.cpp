#include "LLMManager.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "llm/LLMBoundaryGuards.h"
#include "prompt/PromptComposeRequestGuards.h"

#include "UsageAggregator.h"
#include "execution/ResponseNormalizer.h"
#include "observability/LLMMetricsBridge.h"
#include "observability/LLMTraceBridge.h"
#include "prompt/PromptPipeline.h"
#include "provider/ProviderCatalogRepository.h"
#include "route/ModelRouter.h"

namespace {

using AdapterCallResult = dasall::llm::AdapterCallResult;
using LLMCallExecutionFailureReason = dasall::llm::LLMCallExecutionFailureReason;
using LLMCallExecutionResult = dasall::llm::LLMCallExecutionResult;
using LLMFailureCategory = dasall::llm::LLMFailureCategory;
using LLMGenerateRequest = dasall::llm::LLMGenerateRequest;
using LLMManagerResult = dasall::llm::LLMManagerResult;
using LLMTimeoutConfig = dasall::llm::LLMTimeoutConfig;
using ModelSelectionHint = dasall::llm::ModelSelectionHint;
using NormalizedUsageRecord = dasall::llm::NormalizedUsageRecord;
using PromptComposeRequest = dasall::contracts::PromptComposeRequest;
using PromptPipelineConfig = dasall::llm::prompt::PromptPipelineConfig;
using PromptPipelineResult = dasall::llm::prompt::PromptPipelineResult;
using PromptPolicyDisposition = dasall::llm::prompt::PromptPolicyDisposition;
using PromptQuery = dasall::llm::prompt::PromptQuery;
using LLMCallSummary = dasall::llm::observability::LLMCallSummary;
using LLMMetricsBridge = dasall::llm::observability::LLMMetricsBridge;
using LLMTraceBridge = dasall::llm::observability::LLMTraceBridge;
using LLMTraceSpanKind = dasall::llm::observability::LLMTraceSpanKind;
using LLMTraceSpanSignal = dasall::llm::observability::LLMTraceSpanSignal;
using ProviderCatalogSnapshot = dasall::llm::provider::ProviderCatalogSnapshot;
using ProviderModelMetadata = dasall::llm::provider::ProviderModelMetadata;
using ResponseNormalizationResult = dasall::llm::execution::ResponseNormalizationResult;
using ResponseNormalizerContext = dasall::llm::execution::ResponseNormalizerContext;
using ResultCode = dasall::contracts::ResultCode;

constexpr std::string_view kExecutionStage = "llm.manager.execute_unary";
constexpr std::string_view kManagerStage = "llm.manager.generate";
constexpr std::string_view kManagerStreamStage = "llm.manager.stream_generate";

std::int64_t current_time_ms() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}

std::uint32_t elapsed_ms_since(const std::chrono::steady_clock::time_point& started_at) {
  const auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                              std::chrono::steady_clock::now() - started_at)
                              .count();
  return static_cast<std::uint32_t>(std::clamp<std::int64_t>(
      elapsed_ms, 0, std::numeric_limits<std::uint32_t>::max()));
}

std::string to_lower_copy(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
    return static_cast<char>(std::tolower(character));
  });
  return value;
}

void append_unique_string(std::vector<std::string>& values, std::string value) {
  if (value.empty()) {
    return;
  }

  if (std::find(values.begin(), values.end(), value) == values.end()) {
    values.push_back(std::move(value));
  }
}

std::vector<std::string> normalized_selection_reasons(
    const std::vector<std::string>& reason_codes) {
  std::vector<std::string> normalized;
  normalized.reserve(reason_codes.size() + 1U);

  for (const auto& reason_code : reason_codes) {
    append_unique_string(normalized, reason_code);
  }

  if (normalized.empty()) {
    normalized.push_back("selected_route");
  }

  return normalized;
}

std::string requested_reasoning_mode(const ModelSelectionHint& hint) {
  return (hint.requires_reasoning || hint.prefers_visible_reasoning) ? "thinking" : "chat";
}

std::string effective_reasoning_mode(const ProviderModelMetadata& model_metadata,
                                    const std::string& requested_mode) {
  return model_metadata.reasoning_mode.empty() ? requested_mode : model_metadata.reasoning_mode;
}

bool reasoning_mode_escalated(const ModelSelectionHint& hint,
                              const ProviderModelMetadata& model_metadata) {
  const std::string requested_mode = requested_reasoning_mode(hint);
  const std::string effective_mode = effective_reasoning_mode(model_metadata, requested_mode);
  return requested_mode == "chat" && effective_mode == "thinking";
}

std::string trace_detail_ref(LLMTraceSpanKind kind) {
  switch (kind) {
    case LLMTraceSpanKind::PromptSelect:
      return "llm://trace/prompt-select";
    case LLMTraceSpanKind::PromptCompose:
      return "llm://trace/prompt-compose";
    case LLMTraceSpanKind::PromptPolicy:
      return "llm://trace/prompt-policy";
    case LLMTraceSpanKind::RouteResolve:
      return "llm://trace/route-resolve";
    case LLMTraceSpanKind::AdapterInvoke:
      return "llm://trace/adapter-invoke";
    case LLMTraceSpanKind::ResponseNormalize:
      return "llm://trace/response-normalize";
  }

  return "llm://trace/unknown";
}

LLMTraceSpanSignal make_trace_signal(
    LLMTraceSpanKind kind,
    const dasall::contracts::LLMResponse& response,
    const ModelSelectionHint& selection_hint,
    const ProviderModelMetadata& model_metadata,
    std::string_view stage,
    std::string_view resolved_route,
    const std::vector<std::string>& selection_reason_codes,
    std::uint32_t latency_ms,
    const std::optional<NormalizedUsageRecord>& usage_record,
    bool fallback_used,
    std::string outcome) {
  return LLMTraceSpanSignal{
      .kind = kind,
      .request_id = response.request_id.value_or(std::string{}),
      .llm_call_id = response.llm_call_id.value_or(std::string{}),
      .stage = std::string(stage),
      .resolved_route = std::string(resolved_route),
      .model_name = response.model_name.value_or(model_metadata.display_name),
      .prompt_id = response.prompt_id.value_or(std::string{}),
      .prompt_version = response.prompt_version.value_or(std::string{}),
      .fallback_used = fallback_used,
      .latency_ms = latency_ms,
      .failure_category = {},
      .error_type = {},
      .selection_reason_codes = selection_reason_codes,
      .estimated_input_tokens = selection_hint.estimated_input_tokens,
      .prompt_cache_hit_tokens = usage_record.has_value() ? usage_record->prompt_cache_hit_tokens : 0U,
      .prompt_cache_miss_tokens = usage_record.has_value() ? usage_record->prompt_cache_miss_tokens : 0U,
      .actual_cost_estimate_usd = usage_record.has_value() ? usage_record->estimated_cost_usd : 0.0,
      .reasoning_mode_requested = requested_reasoning_mode(selection_hint),
      .reasoning_mode_effective = effective_reasoning_mode(model_metadata, requested_reasoning_mode(selection_hint)),
      .completed_at_ms = response.completed_at.value_or(current_time_ms()),
      .parent_context = std::nullopt,
      .detail_ref = trace_detail_ref(kind),
      .outcome = std::move(outcome),
  };
}

std::optional<LLMCallSummary> make_call_summary(
    const dasall::contracts::LLMResponse& response,
    const ModelSelectionHint& selection_hint,
    const ProviderModelMetadata& model_metadata,
    std::string_view stage,
    std::string_view resolved_route,
    std::string_view provider_id,
  std::string_view profile_id,
    const std::vector<std::string>& attempted_routes,
    const std::vector<std::string>& selection_reason_codes,
    const std::optional<NormalizedUsageRecord>& usage_record,
    std::uint32_t total_latency_ms,
    bool fallback_used) {
  LLMCallSummary summary{
      .request_id = response.request_id.value_or(std::string{}),
      .llm_call_id = response.llm_call_id.value_or(std::string{}),
      .stage = std::string(stage),
      .resolved_route = std::string(resolved_route),
      .model_name = response.model_name.value_or(model_metadata.display_name),
      .prompt_id = response.prompt_id.value_or(std::string{}),
      .prompt_version = response.prompt_version.value_or(std::string{}),
      .fallback_used = fallback_used,
      .completed_at_ms = response.completed_at.value_or(current_time_ms()),
      .latency_ms = total_latency_ms,
      .failure_category = {},
      .error_type = {},
      .selection_reason_codes = selection_reason_codes,
      .estimated_input_tokens = selection_hint.estimated_input_tokens,
      .prompt_cache_hit_tokens = usage_record.has_value() ? usage_record->prompt_cache_hit_tokens : 0U,
      .prompt_cache_miss_tokens = usage_record.has_value() ? usage_record->prompt_cache_miss_tokens : 0U,
      .actual_cost_estimate_usd = usage_record.has_value() ? usage_record->estimated_cost_usd : 0.0,
      .reasoning_mode_requested = requested_reasoning_mode(selection_hint),
      .reasoning_mode_effective = effective_reasoning_mode(model_metadata, requested_reasoning_mode(selection_hint)),
      .provider_id = std::string(provider_id),
      .profile_id = std::string(profile_id),
      .outcome = fallback_used ? "degraded" : "success",
      .from_route = fallback_used && !attempted_routes.empty() ? attempted_routes.front() : std::string{},
      .to_route = fallback_used ? std::string(resolved_route) : std::string{},
      .prompt_policy_denied = false,
      .prompt_compose_over_budget = false,
      .adapter_timeout = false,
      .health_degraded = false,
      .reasoning_escalated = reasoning_mode_escalated(selection_hint, model_metadata),
  };

  if (!summary.has_consistent_values()) {
    return std::nullopt;
  }

  return summary;
}

void record_success_observability(
    LLMMetricsBridge* metrics_bridge,
    LLMTraceBridge* trace_bridge,
    const dasall::contracts::LLMResponse& response,
    const ModelSelectionHint& selection_hint,
    const ProviderModelMetadata& model_metadata,
    std::string_view stage,
    std::string_view resolved_route,
    std::string_view provider_id,
    std::string_view profile_id,
    const std::vector<std::string>& attempted_routes,
    const std::vector<std::string>& selection_reason_codes,
    const std::optional<NormalizedUsageRecord>& usage_record,
    std::uint32_t total_latency_ms,
    std::uint32_t route_latency_ms,
    std::uint32_t adapter_latency_ms,
    std::uint32_t normalize_latency_ms,
    bool fallback_used) {
  const auto summary = make_call_summary(response, selection_hint, model_metadata,
                                         stage, resolved_route, provider_id,
                                         profile_id,
                                         attempted_routes, selection_reason_codes,
                                         usage_record, total_latency_ms,
                                         fallback_used);
  if (summary.has_value() && metrics_bridge != nullptr) {
    static_cast<void>(metrics_bridge->record_call(*summary));
  }

  if (trace_bridge == nullptr) {
    return;
  }

  const std::string outcome = fallback_used ? "degraded" : "success";
  static_cast<void>(trace_bridge->record_span(make_trace_signal(
      LLMTraceSpanKind::RouteResolve,
      response,
      selection_hint,
      model_metadata,
      stage,
      resolved_route,
      selection_reason_codes,
      route_latency_ms,
      usage_record,
      fallback_used,
      outcome)));
  static_cast<void>(trace_bridge->record_span(make_trace_signal(
      LLMTraceSpanKind::AdapterInvoke,
      response,
      selection_hint,
      model_metadata,
      stage,
      resolved_route,
      selection_reason_codes,
      adapter_latency_ms,
      usage_record,
      fallback_used,
      outcome)));
  static_cast<void>(trace_bridge->record_span(make_trace_signal(
      LLMTraceSpanKind::ResponseNormalize,
      response,
      selection_hint,
      model_metadata,
      stage,
      resolved_route,
      selection_reason_codes,
      normalize_latency_ms,
      usage_record,
      fallback_used,
      outcome)));
}

void append_response_tag(dasall::contracts::LLMResponse& response, std::string tag) {
  if (tag.empty()) {
    return;
  }

  if (!response.tags.has_value()) {
    response.tags = std::vector<std::string>{};
  }

  append_unique_string(*response.tags, std::move(tag));
}

std::optional<dasall::contracts::CompositionStage> to_composition_stage(
    std::string_view stage) {
  const std::string normalized = to_lower_copy(std::string(stage));
  if (normalized == "planner" || normalized == "planning") {
    return dasall::contracts::CompositionStage::Planning;
  }

  if (normalized == "execution" || normalized == "execute") {
    return dasall::contracts::CompositionStage::Execution;
  }

  if (normalized == "reflection" || normalized == "reflect") {
    return dasall::contracts::CompositionStage::Reflection;
  }

  if (normalized == "response" || normalized == "respond") {
    return dasall::contracts::CompositionStage::Response;
  }

  return std::nullopt;
}

PromptPipelineConfig make_prompt_pipeline_config(const dasall::llm::LLMSubsystemConfig& config) {
  return PromptPipelineConfig{
      .registry_config = dasall::llm::prompt::PromptRegistryConfig{
      .asset_sources = config.prompt_asset_sources,
          .trusted_sources = config.trusted_sources,
      },
      .composer_config = dasall::llm::prompt::PromptComposerConfig{
          .template_engine = "simple_var",
          .max_few_shot_count = 5U,
      },
      .policy_config = dasall::llm::prompt::PromptPolicyConfig{
          .default_allowed_releases = config.allowed_prompt_releases,
          .default_trusted_sources = config.trusted_sources,
          .deny_on_missing_allowlist = true,
      },
  };
}

PromptQuery make_prompt_query(const LLMGenerateRequest& request,
                              const dasall::llm::LLMSubsystemConfig& config) {
  PromptQuery query;
  query.stage = request.stage;
  query.task_type = request.task_type;
  query.language = "zh-cn";
  query.model_family = request.request.model_route.value_or(std::string{});
  query.prompt_release_id = request.request.prompt_id.value_or(std::string{});
  query.scene_id = config.prompt_selector_overlay.active_scene;
  query.persona_id = config.prompt_selector_overlay.active_persona;
  query.profile_id = config.profile_id;
  query.available_tools = {};
  query.trusted_sources = config.trusted_sources;
  return query;
}

PromptComposeRequest make_prompt_compose_request(
    const LLMGenerateRequest& request,
    const dasall::contracts::LLMRequest& base_request,
    const std::optional<dasall::llm::LLMStageRouteConfig>& stage_route) {
  PromptComposeRequest compose_request;
  compose_request.request_id = base_request.request_id;
  compose_request.stage = to_composition_stage(request.stage);
  compose_request.context_packet_id = base_request.request_id;
  compose_request.created_at = base_request.created_at;
  if (!request.task_type.empty()) {
    compose_request.task_type = request.task_type;
  }
  compose_request.prompt_release_id = std::nullopt;
  compose_request.visible_tools = std::nullopt;
  if (base_request.model_route.has_value() && !base_request.model_route->empty()) {
    compose_request.model_route = base_request.model_route;
  } else if (stage_route.has_value()) {
    compose_request.model_route = stage_route->route;
  }
  compose_request.output_schema_ref = base_request.output_schema_ref;
  compose_request.response_format = base_request.response_format;
  compose_request.tags = base_request.tags;
  return compose_request;
}

ModelSelectionHint make_selection_hint(const LLMGenerateRequest& request) {
  if (request.selection_hint != nullptr) {
    return *request.selection_hint;
  }

  ModelSelectionHint hint;
  hint.stage = request.stage;
  hint.task_type = request.task_type;
  hint.complexity_tier = "balanced";
  hint.latency_sla_tier = "balanced";
  hint.budget_tier = "balanced";
  hint.requires_tools = false;
  hint.requires_reasoning = false;
  hint.prefers_visible_reasoning = false;
  hint.estimated_input_tokens = 0U;
  hint.target_output_tokens = request.request.max_output_tokens.value_or(0U);
  hint.previous_route_failures = 0U;
  return hint;
}

std::vector<std::string> build_route_candidates(const dasall::llm::ResolvedModelRoute& route) {
  std::vector<std::string> candidates;
  append_unique_string(candidates, route.primary_route);
  for (const auto& fallback_route : route.fallback_routes) {
    append_unique_string(candidates, fallback_route);
  }
  return candidates;
}

dasall::contracts::ErrorInfo make_manager_error(ResultCode result_code,
                                                std::string message,
                                                bool retryable,
                                                std::string stage_name,
                                                std::string ref_type,
                                                std::string ref_id) {
  dasall::contracts::ErrorInfo error;
  error.failure_type = dasall::contracts::classify_result_code(result_code);
  error.retryable = retryable;
  error.safe_to_replan = false;
  error.details.code = static_cast<int>(result_code);
  error.details.message = std::move(message);
  error.details.stage = std::move(stage_name);
  error.source_ref.ref_type = std::move(ref_type);
  error.source_ref.ref_id = std::move(ref_id);
  return error;
}

LLMManagerResult make_manager_failure(ResultCode result_code,
                                      std::string message,
                                      bool retryable,
                                      LLMFailureCategory category,
                                      std::string stage_name,
                                      std::string ref_type,
                                      std::string ref_id,
                                      std::vector<std::string> attempted_routes = {},
                                      std::string resolved_route = {}) {
  if (resolved_route.empty() && !attempted_routes.empty()) {
    resolved_route = attempted_routes.back();
  }

  return LLMManagerResult{
      .code = result_code,
      .response = std::nullopt,
      .error = make_manager_error(result_code, std::move(message), retryable,
                                  std::move(stage_name), std::move(ref_type),
                                  std::move(ref_id)),
      .resolved_route = std::move(resolved_route),
      .attempted_routes = std::move(attempted_routes),
      .failure_category = category,
      .fallback_used = false,
  };
}

LLMManagerResult make_manager_failure_from_error(dasall::contracts::ErrorInfo error,
                                                 ResultCode result_code,
                                                 LLMFailureCategory category,
                                                 std::vector<std::string> attempted_routes = {},
                                                 std::string resolved_route = {}) {
  if (!error.failure_type.has_value()) {
    error.failure_type = dasall::contracts::classify_result_code(result_code);
  }
  if (!error.retryable.has_value()) {
    error.retryable = false;
  }
  if (!error.safe_to_replan.has_value()) {
    error.safe_to_replan = false;
  }
  if (!error.details.code.has_value()) {
    error.details.code = static_cast<int>(result_code);
  }

  if (resolved_route.empty() && !attempted_routes.empty()) {
    resolved_route = attempted_routes.back();
  }

  return LLMManagerResult{
      .code = result_code,
      .response = std::nullopt,
      .error = std::move(error),
      .resolved_route = std::move(resolved_route),
      .attempted_routes = std::move(attempted_routes),
      .failure_category = category,
      .fallback_used = false,
  };
}

LLMManagerResult elevate_to_fallback_exhausted(LLMManagerResult failure) {
  if (!failure.code.has_value() || !failure.error.has_value() ||
      failure.attempted_routes.size() < 2U) {
    return failure;
  }

  failure.failure_category = LLMFailureCategory::FallbackExhausted;
  failure.resolved_route = failure.attempted_routes.back();
  return failure;
}

void append_usage_tags(dasall::contracts::LLMResponse& response,
                       const NormalizedUsageRecord& usage_record) {
  append_response_tag(response, "usage:provider_id=" + usage_record.provider_id);
  append_response_tag(response, "usage:model_id=" + usage_record.model_id);
  append_response_tag(response,
                      "usage:prompt_tokens=" + std::to_string(usage_record.prompt_tokens));
  append_response_tag(response,
                      "usage:completion_tokens=" +
                          std::to_string(usage_record.completion_tokens));
  append_response_tag(response,
                      "usage:total_tokens=" + std::to_string(usage_record.total_tokens));
  append_response_tag(response,
                      "usage:prompt_cache_hit_tokens=" +
                          std::to_string(usage_record.prompt_cache_hit_tokens));
  append_response_tag(response,
                      "usage:prompt_cache_miss_tokens=" +
                          std::to_string(usage_record.prompt_cache_miss_tokens));
  if (!usage_record.pricing_ref.empty()) {
    append_response_tag(response, "usage:pricing_ref=" + usage_record.pricing_ref);
  }

  std::ostringstream cost_stream;
  cost_stream.precision(6);
  cost_stream << std::fixed << usage_record.estimated_cost_usd;
  append_response_tag(response, "usage:estimated_cost_usd=" + cost_stream.str());
}

ResponseNormalizerContext make_normalizer_context(
    const dasall::contracts::LLMRequest& request,
    const dasall::llm::route::AdapterRouteState& route_state,
    const ProviderModelMetadata* model_metadata) {
  return ResponseNormalizerContext{
      .route_key = route_state.route_key(),
      .provider_id = route_state.provider_id,
      .model_id = route_state.model_id,
      .model_name = model_metadata != nullptr ? model_metadata->display_name
                                              : route_state.model_id,
      .prompt_id = request.prompt_id.value_or(std::string{}),
      .prompt_version = request.prompt_version.value_or(std::string{}),
      .request_id = request.request_id,
      .llm_call_id = request.llm_call_id,
      .completed_at_ms = current_time_ms(),
  };
}

std::optional<LLMManagerResult> validate_generate_request(
    const LLMGenerateRequest& request,
    const std::optional<dasall::llm::LLMStageRouteConfig>& stage_route) {
  if (request.stage.empty()) {
    return make_manager_failure(ResultCode::ValidationFieldMissing,
                                "llm generate request missing stage",
                                false,
                                LLMFailureCategory::Routing,
                                std::string(kManagerStage),
                                "stage",
                                "missing");
  }

  if (!request.request.request_id.has_value() || request.request.request_id->empty()) {
    return make_manager_failure(ResultCode::ValidationFieldMissing,
                                "llm generate request missing request_id",
                                false,
                                LLMFailureCategory::PromptAsset,
                                std::string(kManagerStage),
                                "request",
                                "request_id");
  }

  if (!request.request.llm_call_id.has_value() || request.request.llm_call_id->empty()) {
    return make_manager_failure(ResultCode::ValidationFieldMissing,
                                "llm generate request missing llm_call_id",
                                false,
                                LLMFailureCategory::PromptAsset,
                                std::string(kManagerStage),
                                "request",
                                "llm_call_id");
  }

  if (!request.request.messages.has_value() || request.request.messages->empty()) {
    return make_manager_failure(ResultCode::ValidationFieldMissing,
                                "llm generate request missing messages",
                                false,
                                LLMFailureCategory::PromptAsset,
                                std::string(kManagerStage),
                                "request",
                                "messages");
  }

  if (!stage_route.has_value()) {
    return make_manager_failure(ResultCode::RuntimeRetryExhausted,
                                "no stage route configured for llm stage: " + request.stage,
                                false,
                                LLMFailureCategory::Routing,
                                std::string(kManagerStage),
                                "stage",
                                request.stage);
  }

  return std::nullopt;
}

LLMManagerResult make_pipeline_failure(const PromptPipelineResult& pipeline_result,
                                       std::string stage) {
  const bool prompt_asset_failure = !pipeline_result.registry_result.has_value() ||
                                    !pipeline_result.registry_result->release.has_value();
  const auto category = prompt_asset_failure
                            ? LLMFailureCategory::PromptAsset
                            : LLMFailureCategory::PromptGovernance;
  const auto code = prompt_asset_failure ? ResultCode::ValidationFieldMissing
                                         : ResultCode::PolicyDenied;
  const auto message = pipeline_result.reason.empty()
                           ? std::string("prompt pipeline failed")
                           : pipeline_result.reason;
  return make_manager_failure(code,
                              message,
                              false,
                              category,
                              std::string(kManagerStage),
                              "stage",
                              std::move(stage));
}

std::uint32_t clamp_timeout_ms(std::uint64_t timeout_ms) {
  return static_cast<std::uint32_t>(std::min<std::uint64_t>(
      timeout_ms, std::numeric_limits<std::uint32_t>::max()));
}

std::uint32_t effective_timeout_ms(const LLMTimeoutConfig& timeout_policy,
                                   const dasall::contracts::LLMRequest& request) {
  const auto configured_timeout_ms = clamp_timeout_ms(
      static_cast<std::uint64_t>(std::max<std::int64_t>(timeout_policy.timeout_ms, 1)));

  if (!request.timeout_ms.has_value()) {
    return configured_timeout_ms;
  }

  return std::min(configured_timeout_ms, *request.timeout_ms);
}

dasall::contracts::LLMRequest make_attempt_request(
    const dasall::contracts::LLMRequest& request,
    std::string_view route_key,
    std::uint32_t timeout_ms) {
  auto attempt_request = request;
  attempt_request.model_route = std::string(route_key);
  attempt_request.request_mode = dasall::contracts::LLMRequestMode::Unary;
  attempt_request.timeout_ms = timeout_ms;
  return attempt_request;
}

dasall::contracts::ErrorInfo make_error(ResultCode result_code,
                                        std::string message,
                                        bool retryable,
                                        std::string route_key) {
  dasall::contracts::ErrorInfo error;
  error.failure_type = dasall::contracts::classify_result_code(result_code);
  error.retryable = retryable;
  error.safe_to_replan = false;
  error.details.code = static_cast<int>(result_code);
  error.details.message = std::move(message);
  error.details.stage = std::string(kExecutionStage);
  error.source_ref.ref_type = "route";
  error.source_ref.ref_id = std::move(route_key);
  return error;
}

std::string adapter_failure_message(const AdapterCallResult& adapter_result,
                                    std::string fallback_message) {
  if (adapter_result.error.has_value() &&
      !adapter_result.error->details.message.empty()) {
    return adapter_result.error->details.message;
  }

  return fallback_message;
}

bool adapter_failure_is_retryable(const AdapterCallResult& adapter_result) {
  return adapter_result.error.has_value() &&
         adapter_result.error->retryable.value_or(false);
}

bool try_acquire_active_call_slot(std::atomic<std::uint32_t>& active_call_count,
                                  std::uint32_t limit) {
  auto current = active_call_count.load(std::memory_order_acquire);
  while (current < limit) {
    if (active_call_count.compare_exchange_weak(
            current, current + 1U,
            std::memory_order_acq_rel,
            std::memory_order_acquire)) {
      return true;
    }
  }

  return false;
}

class ActiveCallPermit {
 public:
  explicit ActiveCallPermit(std::atomic<std::uint32_t>& active_call_count)
      : active_call_count_(&active_call_count) {}

  ActiveCallPermit(const ActiveCallPermit&) = delete;
  ActiveCallPermit& operator=(const ActiveCallPermit&) = delete;

  ActiveCallPermit(ActiveCallPermit&& other) noexcept
      : active_call_count_(std::exchange(other.active_call_count_, nullptr)) {}

  ActiveCallPermit& operator=(ActiveCallPermit&& other) noexcept {
    if (this == &other) {
      return *this;
    }

    release();
    active_call_count_ = std::exchange(other.active_call_count_, nullptr);
    return *this;
  }

  ~ActiveCallPermit() {
    release();
  }

 private:
  void release() {
    if (active_call_count_ == nullptr) {
      return;
    }

    active_call_count_->fetch_sub(1U, std::memory_order_acq_rel);
    active_call_count_ = nullptr;
  }

  std::atomic<std::uint32_t>* active_call_count_ = nullptr;
};

LLMCallExecutionResult make_success_result(std::string route_key,
                                           std::uint32_t attempts_started,
                                           AdapterCallResult adapter_result) {
  return LLMCallExecutionResult{
      .route_key = std::move(route_key),
      .attempts_started = attempts_started,
      .adapter_result = std::move(adapter_result),
      .error = std::nullopt,
      .result_code = std::nullopt,
      .failure_reason = std::nullopt,
  };
}

LLMCallExecutionResult make_failure_result(
    std::string route_key,
    std::uint32_t attempts_started,
    std::optional<AdapterCallResult> adapter_result,
    ResultCode result_code,
    std::string message,
    bool retryable,
    LLMCallExecutionFailureReason failure_reason) {
  return LLMCallExecutionResult{
      .route_key = std::move(route_key),
      .attempts_started = attempts_started,
      .adapter_result = std::move(adapter_result),
      .error = make_error(result_code, std::move(message), retryable,
                          route_key),
      .result_code = result_code,
      .failure_reason = failure_reason,
  };
}

AdapterCallResult make_timeout_result(std::string route_key, std::string message) {
  AdapterCallResult result;
  result.error = make_error(ResultCode::ProviderTimeout, std::move(message), true,
                            std::move(route_key));
  result.result_code = ResultCode::ProviderTimeout;
  return result;
}

}  // namespace

namespace dasall::llm {

bool LLMCallExecutionResult::succeeded() const {
  return adapter_result.has_value() && adapter_result->response.has_value();
}

bool LLMCallExecutionResult::has_consistent_values() const {
  if (adapter_result.has_value() && !adapter_result->has_consistent_values()) {
    return false;
  }

  if (succeeded()) {
    return attempts_started > 0U && !error.has_value() && !result_code.has_value() &&
           !failure_reason.has_value() && !route_key.empty();
  }

  if (!failure_reason.has_value() || !error.has_value() || !result_code.has_value()) {
    return false;
  }

  if (adapter_result.has_value() && adapter_result->response.has_value()) {
    return false;
  }

  if (error->failure_type.has_value() &&
      contracts::classify_result_code(*result_code) != *error->failure_type) {
    return false;
  }

  return true;
}

bool LLMCallExecutor::init(const LLMSubsystemConfig& config) {
  if (!config.has_consistent_values()) {
    return false;
  }

  config_ = config;
  active_call_count_.store(0U, std::memory_order_release);
  initialized_ = true;
  return true;
}

LLMCallExecutionResult LLMCallExecutor::execute_unary(
    std::string_view route_key,
    const contracts::LLMRequest& request,
    route::AdapterRegistry& registry) {
  const std::string route_key_string(route_key);
  if (!initialized_) {
    return make_failure_result(route_key_string, 0U, std::nullopt,
                               ResultCode::RuntimeRetryExhausted,
                               "llm call executor is not initialized",
                               false,
                               LLMCallExecutionFailureReason::NotInitialized);
  }

  std::uint32_t attempts_started = 0U;
  for (std::uint32_t attempt = 0U;
       attempt <= config_.timeout_policy.retry_budget;
       ++attempt) {
    const auto route_state = registry.resolve_route(route_key);
    if (!route_state.has_value() || route_state->adapter == nullptr) {
      return make_failure_result(route_key_string, attempts_started, std::nullopt,
                                 ResultCode::RuntimeRetryExhausted,
                                 "adapter route is not registered",
                                 false,
                                 LLMCallExecutionFailureReason::RouteUnavailable);
    }

    if (route_state->blocked) {
      return make_failure_result(route_key_string, attempts_started, std::nullopt,
                                 ResultCode::RuntimeRetryExhausted,
                                 "adapter route is blocked by circuit threshold",
                                 false,
                                 LLMCallExecutionFailureReason::RouteBlocked);
    }

    if (!try_acquire_active_call_slot(active_call_count_, config_.worker_threads)) {
      return make_failure_result(route_key_string, attempts_started, std::nullopt,
                                 ResultCode::RuntimeRetryExhausted,
                                 "active llm calls limit reached",
                                 false,
                                 LLMCallExecutionFailureReason::ConcurrencyRejected);
    }

    ActiveCallPermit active_call_permit(active_call_count_);
    const auto attempt_request = make_attempt_request(
        request, route_key,
        effective_timeout_ms(config_.timeout_policy, request));

    // The current adapter SPI is synchronous, so 040 enforces timeout by
    // propagating a deadline hint and rejecting responses that return after the
    // configured budget rather than spawning detached worker threads.
    const auto started_at = std::chrono::steady_clock::now();
    auto adapter_result = route_state->adapter->generate(attempt_request);
    const auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                std::chrono::steady_clock::now() - started_at)
                                .count();
    ++attempts_started;

    if (!adapter_result.has_consistent_values()) {
      registry.record_call_failure(route_key,
                                   "adapter returned inconsistent unary execution result");
      if (const auto updated_route = registry.resolve_route(route_key);
          updated_route.has_value() && updated_route->blocked) {
        return make_failure_result(route_key_string, attempts_started,
                                   std::move(adapter_result),
                                   ResultCode::RuntimeRetryExhausted,
                                   "adapter route blocked after inconsistent execution failures",
                                   false,
                                   LLMCallExecutionFailureReason::RouteBlocked);
      }

      return make_failure_result(route_key_string, attempts_started,
                                 std::move(adapter_result),
                                 ResultCode::RuntimeRetryExhausted,
                                 "adapter returned inconsistent unary execution result",
                                 false,
                                 LLMCallExecutionFailureReason::AdapterFailure);
    }

    if (elapsed_ms > static_cast<std::int64_t>(*attempt_request.timeout_ms)) {
      auto timeout_result = make_timeout_result(
          route_key_string,
          "adapter invocation exceeded timeout_policy after " +
              std::to_string(elapsed_ms) + "ms");
      registry.record_call_failure(route_key,
                                   timeout_result.error->details.message);
      if (const auto updated_route = registry.resolve_route(route_key);
          updated_route.has_value() && updated_route->blocked) {
        return make_failure_result(route_key_string, attempts_started,
                                   std::move(timeout_result),
                                   ResultCode::RuntimeRetryExhausted,
                                   "adapter route blocked after consecutive timeout failures",
                                   false,
                                   LLMCallExecutionFailureReason::RouteBlocked);
      }

      if (attempt < config_.timeout_policy.retry_budget) {
        continue;
      }

      return make_failure_result(route_key_string, attempts_started,
                                 std::move(timeout_result),
                                 ResultCode::ProviderTimeout,
                                 "adapter invocation exceeded timeout_policy",
                                 true,
                                 LLMCallExecutionFailureReason::Timeout);
    }

    if (adapter_result.response.has_value()) {
      registry.record_call_success(route_key, "llm unary execution succeeded");
      return make_success_result(route_key_string, attempts_started,
                                 std::move(adapter_result));
    }

    const auto failure_message = adapter_failure_message(
        adapter_result, "adapter unary execution failed");
    registry.record_call_failure(route_key, failure_message);
    if (const auto updated_route = registry.resolve_route(route_key);
        updated_route.has_value() && updated_route->blocked) {
      return make_failure_result(route_key_string, attempts_started,
                                 std::move(adapter_result),
                                 ResultCode::RuntimeRetryExhausted,
                                 "adapter route blocked after consecutive call failures",
                                 false,
                                 LLMCallExecutionFailureReason::RouteBlocked);
    }

    if (adapter_failure_is_retryable(adapter_result) &&
        attempt < config_.timeout_policy.retry_budget) {
      continue;
    }

    return make_failure_result(
        route_key_string,
        attempts_started,
        std::move(adapter_result),
        adapter_result.result_code.value_or(ResultCode::RuntimeRetryExhausted),
        failure_message,
        adapter_failure_is_retryable(adapter_result),
        LLMCallExecutionFailureReason::AdapterFailure);
  }

  return make_failure_result(route_key_string, attempts_started, std::nullopt,
                             ResultCode::RuntimeRetryExhausted,
                             "llm unary execution exhausted retry budget",
                             false,
                             LLMCallExecutionFailureReason::AdapterFailure);
}

std::uint32_t LLMCallExecutor::active_call_count() const {
  return active_call_count_.load(std::memory_order_acquire);
}

bool LLMCallExecutor::is_initialized() const {
  return initialized_;
}

LLMManager::LLMManager()
    : LLMManager(std::make_shared<prompt::PromptPipeline>(),
                 std::make_shared<route::ModelRouter>(),
                 std::make_shared<route::AdapterRegistry>(),
                 std::make_shared<LLMCallExecutor>(),
                 std::make_shared<execution::ResponseNormalizer>(),
                 std::make_shared<UsageAggregator>()) {}

LLMManager::LLMManager(
    std::shared_ptr<prompt::IPromptPipeline> prompt_pipeline,
    std::shared_ptr<route::ModelRouter> model_router,
    std::shared_ptr<route::AdapterRegistry> adapter_registry,
    std::shared_ptr<LLMCallExecutor> call_executor,
    std::shared_ptr<execution::ResponseNormalizer> response_normalizer,
    std::shared_ptr<UsageAggregator> usage_aggregator,
    std::shared_ptr<const provider::ProviderCatalogSnapshot> provider_catalog_snapshot,
    std::shared_ptr<observability::LLMMetricsBridge> metrics_bridge,
    std::shared_ptr<observability::LLMTraceBridge> trace_bridge)
    : provider_catalog_snapshot_(std::move(provider_catalog_snapshot)),
      prompt_pipeline_(std::move(prompt_pipeline)),
      model_router_(std::move(model_router)),
      adapter_registry_(std::move(adapter_registry)),
      call_executor_(std::move(call_executor)),
      response_normalizer_(std::move(response_normalizer)),
      usage_aggregator_(std::move(usage_aggregator)),
      metrics_bridge_(std::move(metrics_bridge)),
      trace_bridge_(std::move(trace_bridge)) {}

bool LLMManager::init(const LLMSubsystemConfig& config) {
  initialized_ = false;

  if (!config.has_consistent_values() || prompt_pipeline_ == nullptr ||
      model_router_ == nullptr || adapter_registry_ == nullptr ||
      call_executor_ == nullptr || response_normalizer_ == nullptr ||
      usage_aggregator_ == nullptr) {
    return false;
  }

  if (!prompt_pipeline_->init(make_prompt_pipeline_config(config))) {
    return false;
  }

  if (!model_router_->init(config) || !call_executor_->init(config)) {
    return false;
  }

  if (adapter_registry_->snapshot() == nullptr &&
      !adapter_registry_->init(route::AdapterRegistryConfig{
          .blocked_failure_threshold = config.timeout_policy.circuit_breaker_threshold,
      })) {
    return false;
  }

  if (provider_catalog_snapshot_ == nullptr) {
    if (provider_catalog_repository_ == nullptr) {
      provider_catalog_repository_ = std::make_shared<provider::ProviderCatalogRepository>();
    }

    if (!provider_catalog_repository_->init(config.provider_catalog_sources) ||
        !provider_catalog_repository_->reload()) {
      return false;
    }

    provider_catalog_snapshot_ = provider_catalog_repository_->snapshot();
  }

  if (provider_catalog_snapshot_ == nullptr ||
      !provider_catalog_snapshot_->has_consistent_values()) {
    return false;
  }

  config_ = config;
  initialized_ = true;
  return true;
}

LLMManagerResult LLMManager::generate(const LLMGenerateRequest& request) {
  const auto request_started_at = std::chrono::steady_clock::now();
  if (!initialized_) {
    return make_manager_failure(ResultCode::RuntimeRetryExhausted,
                                "llm manager is not initialized",
                                false,
                                LLMFailureCategory::AdapterTransport,
                                std::string(kManagerStage),
                                "manager",
                                "uninitialized");
  }

  const auto stage_route = config_.stage_route_for(request.stage);
  if (const auto invalid_request = validate_generate_request(request, stage_route);
      invalid_request.has_value()) {
    return *invalid_request;
  }

  auto base_request = request.request;
  if (!base_request.created_at.has_value()) {
    base_request.created_at = current_time_ms();
  }

  if (!base_request.request_mode.has_value()) {
    base_request.request_mode = dasall::contracts::LLMRequestMode::Unary;
  }

  if ((!base_request.model_route.has_value() || base_request.model_route->empty()) &&
      stage_route.has_value()) {
    base_request.model_route = stage_route->route;
  }

  const auto compose_request = make_prompt_compose_request(request, base_request, stage_route);
  const auto compose_validation =
      contracts::validate_prompt_compose_request_field_rules(compose_request);
  if (!compose_validation.ok) {
    return make_manager_failure(ResultCode::ValidationFieldMissing,
                                std::string("invalid prompt compose request: ") +
                                    std::string(compose_validation.reason),
                                false,
                                LLMFailureCategory::PromptAsset,
                                std::string(kManagerStage),
                                "stage",
                                request.stage);
  }

  auto policy_input = config_.make_prompt_policy_input(
      base_request.runtime_budget.has_value()
          ? base_request.runtime_budget->max_tokens.value_or(0U)
          : 0U);
    const auto pipeline_started_at = std::chrono::steady_clock::now();
  const auto pipeline_result =
      prompt_pipeline_->run(make_prompt_query(request, config_), compose_request, policy_input);
    static_cast<void>(elapsed_ms_since(pipeline_started_at));
  if (pipeline_result.disposition != PromptPolicyDisposition::Allow ||
      !pipeline_result.compose_result.has_value() ||
      !pipeline_result.policy_decision.has_value() ||
      pipeline_result.policy_decision->governed_messages.empty()) {
    return make_pipeline_failure(pipeline_result, request.stage);
  }

  auto call_request = base_request;
  call_request.messages = pipeline_result.policy_decision->governed_messages;
  call_request.prompt_id = pipeline_result.compose_result->selected_prompt_id;
  call_request.prompt_version = pipeline_result.compose_result->selected_version;

  const auto call_request_validation = contracts::validate_llm_request_field_rules(call_request);
  if (!call_request_validation.ok) {
    return make_manager_failure(ResultCode::ValidationFieldMissing,
                                std::string("invalid llm call request: ") +
                                    std::string(call_request_validation.reason),
                                false,
                                LLMFailureCategory::PromptGovernance,
                                std::string(kManagerStage),
                                "stage",
                                request.stage);
  }

  const auto catalog_snapshot = provider_catalog_snapshot_;
  if (catalog_snapshot == nullptr || !catalog_snapshot->has_consistent_values()) {
    return make_manager_failure(ResultCode::ValidationFieldMissing,
                                "provider catalog snapshot is unavailable",
                                false,
                                LLMFailureCategory::Routing,
                                std::string(kManagerStage),
                                "catalog",
                                "provider_catalog");
  }

  const auto router_started_at = std::chrono::steady_clock::now();
  const auto router_result = model_router_->resolve(
      make_selection_hint(request), *catalog_snapshot, adapter_registry_->health_snapshot());
  const auto route_latency_ms = elapsed_ms_since(router_started_at);
  if (!router_result.has_route()) {
    return make_manager_failure(ResultCode::RuntimeRetryExhausted,
                                "model router did not resolve a route",
                                false,
                                LLMFailureCategory::Routing,
                                std::string(kManagerStage),
                                "stage",
                                request.stage);
  }

  const auto selection_hint = make_selection_hint(request);
  const auto selection_reason_codes =
      normalized_selection_reasons(router_result.selection_reason_codes);
  const auto route_candidates = build_route_candidates(*router_result.resolved_route);
  if (route_candidates.empty()) {
    return make_manager_failure(ResultCode::RuntimeRetryExhausted,
                                "resolved route chain is empty",
                                false,
                                LLMFailureCategory::Routing,
                                std::string(kManagerStage),
                                "stage",
                                request.stage);
  }

  std::vector<std::string> attempted_routes;
  std::optional<LLMManagerResult> last_failure;
  for (const auto& route_key : route_candidates) {
    const auto adapter_started_at = std::chrono::steady_clock::now();
    const auto execution_result =
        call_executor_->execute_unary(route_key, call_request, *adapter_registry_);
    const auto adapter_latency_ms = elapsed_ms_since(adapter_started_at);
    attempted_routes.push_back(route_key);

    if (!execution_result.has_consistent_values()) {
      last_failure = make_manager_failure(ResultCode::RuntimeRetryExhausted,
                                          "llm call executor returned inconsistent result",
                                          false,
                                          LLMFailureCategory::AdapterTransport,
                                          std::string(kManagerStage),
                                          "route",
                                          route_key,
                                          attempted_routes,
                                          route_key);
      continue;
    }

    if (!execution_result.succeeded()) {
      last_failure = make_manager_failure_from_error(
          execution_result.error.value_or(make_manager_error(
              execution_result.result_code.value_or(ResultCode::RuntimeRetryExhausted),
              "llm call executor failed without error payload",
              false,
              std::string(kExecutionStage),
              "route",
              route_key)),
          execution_result.result_code.value_or(ResultCode::RuntimeRetryExhausted),
          LLMFailureCategory::AdapterTransport,
          attempted_routes,
          route_key);
      continue;
    }

    const auto route_state = adapter_registry_->resolve_route(route_key);
    if (!route_state.has_value()) {
      last_failure = make_manager_failure(ResultCode::RuntimeRetryExhausted,
                                          "adapter registry lost resolved route state",
                                          false,
                                          LLMFailureCategory::Routing,
                                          std::string(kManagerStage),
                                          "route",
                                          route_key,
                                          attempted_routes,
                                          route_key);
      continue;
    }

    const ProviderModelMetadata* model_metadata =
        catalog_snapshot->find_model(route_state->provider_id, route_state->model_id);
    if (model_metadata == nullptr) {
      last_failure = make_manager_failure(ResultCode::ValidationFieldMissing,
                                          "provider catalog metadata missing for resolved route",
                                          false,
                                          LLMFailureCategory::Routing,
                                          std::string(kManagerStage),
                                          "route",
                                          route_key,
                                          attempted_routes,
                                          route_key);
      continue;
    }

    const auto normalization_started_at = std::chrono::steady_clock::now();
    const auto normalization_result = response_normalizer_->normalize(
        *execution_result.adapter_result,
        make_normalizer_context(call_request, *route_state, model_metadata));
    const auto normalize_latency_ms = elapsed_ms_since(normalization_started_at);
    if (!normalization_result.has_consistent_values() ||
        !normalization_result.succeeded()) {
      last_failure = make_manager_failure_from_error(
          normalization_result.error.value_or(make_manager_error(
              normalization_result.result_code.value_or(ResultCode::ValidationFieldMissing),
              "response normalizer failed without error payload",
              false,
              std::string(kManagerStage),
              "route",
              route_key)),
          normalization_result.result_code.value_or(ResultCode::ValidationFieldMissing),
          LLMFailureCategory::ProviderProtocol,
          attempted_routes,
          route_key);
      continue;
    }

    auto response = *normalization_result.response;
    append_response_tag(response, "route=" + route_key);
    for (const auto& reason_code : router_result.selection_reason_codes) {
      append_response_tag(response, "selection_reason=" + reason_code);
    }
    if (!normalization_result.provider_trace_id.empty()) {
      append_response_tag(response,
                          "provider_trace_id=" + normalization_result.provider_trace_id);
    }
    for (const auto& audit_event : normalization_result.audit_events) {
      append_response_tag(response, "audit=" + audit_event);
    }
    if (normalization_result.reasoning_content_stripped) {
      append_response_tag(response, "reasoning_content_stripped=true");
    }

    std::optional<NormalizedUsageRecord> usage_record;
    if (normalization_result.usage_fragment.has_value()) {
      usage_record = usage_aggregator_->aggregate(*normalization_result.usage_fragment,
                                                  *model_metadata);
      append_usage_tags(response, *usage_record);
    }

    LLMManagerResult result{
        .code = std::nullopt,
        .response = std::move(response),
        .error = std::nullopt,
        .resolved_route = route_key,
        .attempted_routes = attempted_routes,
        .failure_category = std::nullopt,
        .fallback_used = attempted_routes.size() > 1U,
    };
    if (!result.has_consistent_values()) {
      return make_manager_failure(ResultCode::RuntimeRetryExhausted,
                                  "llm manager produced inconsistent success result",
                                  false,
                                  LLMFailureCategory::AdapterTransport,
                                  std::string(kManagerStage),
                                  "route",
                                  route_key,
                                  attempted_routes,
                                  route_key);
    }

    record_success_observability(metrics_bridge_.get(),
                                 trace_bridge_.get(),
                                 *result.response,
                                 selection_hint,
                                 *model_metadata,
                                 request.stage,
                                 route_key,
                                 route_state->provider_id,
                                 config_.profile_id,
                                 attempted_routes,
                                 selection_reason_codes,
                                 usage_record,
                                 elapsed_ms_since(request_started_at),
                                 route_latency_ms,
                                 adapter_latency_ms,
                                 normalize_latency_ms,
                                 result.fallback_used);

    return result;
  }

  if (last_failure.has_value()) {
    auto failure = *last_failure;
    if (attempted_routes.size() >= 2U) {
      failure = elevate_to_fallback_exhausted(std::move(failure));
    }

    if (!failure.has_consistent_values()) {
      return make_manager_failure(ResultCode::RuntimeRetryExhausted,
                                  "llm manager produced inconsistent failure result",
                                  false,
                                  attempted_routes.size() >= 2U
                                      ? LLMFailureCategory::FallbackExhausted
                                      : LLMFailureCategory::AdapterTransport,
                                  std::string(kManagerStage),
                                  "stage",
                                  request.stage,
                                  attempted_routes,
                                  attempted_routes.empty() ? std::string{} : attempted_routes.back());
    }

    return failure;
  }

  return make_manager_failure(ResultCode::RuntimeRetryExhausted,
                              "llm manager exhausted route chain without result",
                              false,
                              attempted_routes.size() >= 2U
                                  ? LLMFailureCategory::FallbackExhausted
                                  : LLMFailureCategory::Routing,
                              std::string(kManagerStage),
                              "stage",
                              request.stage,
                              attempted_routes,
                              attempted_routes.empty() ? std::string{} : attempted_routes.back());
}

LLMManagerResult LLMManager::stream_generate(const LLMGenerateRequest& request,
                                             IStreamObserver* observer) {
  static_cast<void>(request);
  static_cast<void>(observer);
  return make_manager_failure(ResultCode::RuntimeRetryExhausted,
                              "llm manager stream_generate is not implemented in unary phase",
                              false,
                              LLMFailureCategory::AdapterTransport,
                              std::string(kManagerStreamStage),
                              "manager",
                              "stream_unavailable");
}

HealthStatus LLMManager::health_check() const {
  if (!initialized_) {
    return HealthStatus{
        .ready = false,
        .degraded = false,
        .message = "llm manager is not initialized",
    };
  }

  const auto registry_snapshot = adapter_registry_ != nullptr ? adapter_registry_->snapshot() : nullptr;
  const bool ready = prompt_pipeline_ != nullptr && model_router_ != nullptr &&
                     adapter_registry_ != nullptr && call_executor_ != nullptr &&
                     response_normalizer_ != nullptr && usage_aggregator_ != nullptr &&
                     call_executor_->is_initialized() && provider_catalog_snapshot_ != nullptr &&
                     registry_snapshot != nullptr;
  const bool degraded = ready &&
                        (registry_snapshot->routes.empty() ||
                         provider_catalog_snapshot_->models.empty());
  return HealthStatus{
      .ready = ready,
      .degraded = degraded,
      .message = ready ? (degraded ? "llm manager ready with partial dependencies"
                                   : "llm manager ready")
                       : "llm manager dependency chain is incomplete",
  };
}

bool LLMManager::is_initialized() const {
  return initialized_;
}

}  // namespace dasall::llm