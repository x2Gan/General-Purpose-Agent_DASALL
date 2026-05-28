#include "PromptComposer.h"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using CompositionStage = dasall::contracts::CompositionStage;
using PromptComposeRequest = dasall::contracts::PromptComposeRequest;
using PromptComposeResult = dasall::contracts::PromptComposeResult;
using PromptRelease = dasall::contracts::PromptRelease;
using TokenEstimate = dasall::llm::TokenEstimate;
using ModelBudgetHint = dasall::llm::prompt::ModelBudgetHint;
using TemplateRenderResult = dasall::llm::prompt::TemplateRenderResult;
using TemplateVariables = dasall::llm::prompt::TemplateVariables;

std::string optional_string(const std::optional<std::string>& value) {
  return value.value_or(std::string());
}

std::string join_values(const std::vector<std::string>& values, std::string_view separator) {
  std::string joined;

  for (const auto& value : values) {
    if (value.empty()) {
      continue;
    }

    if (!joined.empty()) {
      joined.append(separator);
    }

    joined.append(value);
  }

  return joined;
}

void append_unique_warning(std::vector<std::string>& warnings, std::string warning) {
  if (warning.empty()) {
    return;
  }

  if (std::find(warnings.begin(), warnings.end(), warning) == warnings.end()) {
    warnings.push_back(std::move(warning));
  }
}

void append_render_warnings(std::vector<std::string>& warnings,
                            const TemplateRenderResult& render_result) {
  for (const auto& warning : render_result.warnings) {
    append_unique_warning(warnings, warning);
  }
}

std::string stage_to_string(const std::optional<CompositionStage>& stage) {
  if (!stage.has_value()) {
    return std::string();
  }

  switch (*stage) {
    case CompositionStage::Planning:
      return "planning";
    case CompositionStage::Execution:
      return "execution";
    case CompositionStage::Reflection:
      return "reflection";
    case CompositionStage::Response:
      return "response";
    case CompositionStage::Unspecified:
      return std::string();
  }

  return std::string();
}

std::string release_identifier(const PromptRelease& release) {
  if (!release.prompt_id.has_value() || !release.version.has_value()) {
    return std::string();
  }

  return *release.prompt_id + "@" + *release.version;
}

std::optional<std::string> tag_value(const std::optional<std::vector<std::string>>& tags,
                                     std::string_view key) {
  if (!tags.has_value()) {
    return std::nullopt;
  }

  const std::string prefix = std::string(key) + "=";
  for (const auto& tag : *tags) {
    if (tag.starts_with(prefix)) {
      return tag.substr(prefix.size());
    }
  }

  return std::nullopt;
}

TemplateVariables make_template_variables(const PromptComposeRequest& request,
                                          const PromptRelease& release) {
  TemplateVariables variables;

  variables["request_id"] = optional_string(request.request_id);
  variables["stage"] = stage_to_string(request.stage);
  variables["task_type"] = optional_string(request.task_type);
  variables["context_packet_id"] = optional_string(request.context_packet_id);
  variables["prompt_release_id"] = request.prompt_release_id.has_value() &&
                                             !request.prompt_release_id->empty()
                                         ? *request.prompt_release_id
                                         : release_identifier(release);
  variables["visible_tools"] =
      request.visible_tools.has_value() ? join_values(*request.visible_tools, ", ") : std::string();
  variables["available_tools"] = variables["visible_tools"];
  variables["model_route"] = optional_string(request.model_route);
  variables["output_schema_ref"] = request.output_schema_ref.has_value() &&
                                             !request.output_schema_ref->empty()
                                         ? *request.output_schema_ref
                                         : optional_string(release.output_schema_ref);
  variables["response_format"] = optional_string(request.response_format);
  variables["tags"] = request.tags.has_value() ? join_values(*request.tags, ", ") : std::string();
  variables["prompt_id"] = optional_string(release.prompt_id);
  variables["prompt_version"] = optional_string(release.version);
  variables["release_scope"] = optional_string(release.release_scope);
  variables["trusted_source"] = optional_string(release.trusted_source);
  variables["session_summary"] =
      tag_value(request.tags, "session_summary").value_or(std::string());
  if (auto user_goal = tag_value(request.tags, "user_goal"); user_goal.has_value()) {
    variables["user_goal"] = std::move(*user_goal);
  }
  if (auto constraints = tag_value(request.tags, "constraints"); constraints.has_value()) {
    variables["constraints"] = std::move(*constraints);
  }

  return variables;
}

std::uint32_t effective_context_window(const ModelBudgetHint& budget_hint) {
  return budget_hint.context_window > 0U ? budget_hint.context_window
                                         : std::numeric_limits<std::uint32_t>::max();
}

std::uint32_t effective_reserved_output_tokens(const ModelBudgetHint& budget_hint) {
  if (budget_hint.reserved_output_tokens > 0U) {
    return budget_hint.reserved_output_tokens;
  }

  return budget_hint.max_output_tokens;
}

std::vector<std::string> default_resolve_few_shots(const PromptRelease& release,
                                                   std::uint32_t max_few_shot_count,
                                                   std::vector<std::string>& warnings) {
  std::vector<std::string> resolved;

  if (!release.few_shot_refs.has_value()) {
    return resolved;
  }

  for (const auto& reference : *release.few_shot_refs) {
    if (resolved.size() >= max_few_shot_count) {
      append_unique_warning(warnings, "few_shot_count_capped");
      break;
    }

    if (reference.rfind("inline:", 0U) == 0U && reference.size() > 7U) {
      resolved.push_back(reference.substr(7U));
      continue;
    }

    if (!reference.empty()) {
      append_unique_warning(warnings, "unresolved_few_shot_ref:" + reference);
    }
  }

  return resolved;
}

std::vector<std::string> build_messages(std::string system_message,
                                        const std::vector<std::string>& few_shots,
                                        std::string user_message) {
  std::vector<std::string> messages;
  messages.reserve(2U + few_shots.size());
  messages.push_back("system: " + std::move(system_message));
  messages.insert(messages.end(), few_shots.begin(), few_shots.end());
  messages.push_back("user: " + std::move(user_message));
  return messages;
}

TokenEstimate estimate_messages(const dasall::llm::TokenEstimator& estimator,
                                const std::vector<std::string>& messages,
                                const ModelBudgetHint& budget_hint) {
  return estimator.estimate(messages, effective_context_window(budget_hint),
                            effective_reserved_output_tokens(budget_hint));
}

}  // namespace

namespace dasall::llm::prompt {

PromptComposer::PromptComposer(std::shared_ptr<ITemplateRenderer> renderer,
                               std::shared_ptr<dasall::llm::TokenEstimator> token_estimator,
                               FewShotResolver few_shot_resolver)
    : renderer_(std::move(renderer)),
      token_estimator_(std::move(token_estimator)),
      few_shot_resolver_(std::move(few_shot_resolver)) {}

bool PromptComposer::init(const PromptComposerConfig& config) {
  config_ = config;
  initialized_ = false;

  if (renderer_ == nullptr || token_estimator_ == nullptr || config_.template_engine.empty()) {
    return false;
  }

  initialized_ = renderer_->init(TemplateRendererConfig{
      .template_engine = config_.template_engine,
      .max_variable_length = 100U * 1024U,
  });
  return initialized_;
}

std::vector<std::string> PromptComposer::resolve_few_shots(
    const dasall::contracts::PromptRelease& release,
    std::vector<std::string>& warnings) const {
  if (few_shot_resolver_) {
    return few_shot_resolver_(release, config_.max_few_shot_count, warnings);
  }

  return default_resolve_few_shots(release, config_.max_few_shot_count, warnings);
}

PromptComposeResult PromptComposer::compose(const PromptComposeRequest& request,
                                            const PromptRelease& release,
                                            const ModelBudgetHint& budget_hint) const {
  PromptComposeResult result;
  result.selected_prompt_id = optional_string(release.prompt_id);
  result.selected_version = optional_string(release.version);

  std::vector<std::string> warnings;

  if (!initialized_) {
    append_unique_warning(warnings, "composer_not_initialized");
    result.messages = std::vector<std::string>{};
    result.estimated_tokens = 0;
    result.composition_warnings = warnings;
    return result;
  }

  if (!release.system_instructions.has_value() || !release.task_template.has_value()) {
    append_unique_warning(warnings, "missing_prompt_template");
    result.messages = std::vector<std::string>{};
    result.estimated_tokens = 0;
    result.composition_warnings = warnings;
    return result;
  }

  const TemplateVariables variables = make_template_variables(request, release);
  const TemplateRenderResult system_render =
      renderer_->render(*release.system_instructions, variables);
  const TemplateRenderResult task_render = renderer_->render(*release.task_template, variables);
  append_render_warnings(warnings, system_render);
  append_render_warnings(warnings, task_render);

  std::vector<std::string> few_shots = resolve_few_shots(release, warnings);
  std::vector<std::string> messages = build_messages(
      system_render.rendered_text, few_shots, task_render.rendered_text);
  const TokenEstimate estimate = estimate_messages(*token_estimator_, messages, budget_hint);

  if (estimate.over_budget) {
    append_unique_warning(warnings, "over_budget");
  }

  result.messages = messages;
  result.estimated_tokens = static_cast<std::int64_t>(estimate.estimated_input_tokens);
  if (!warnings.empty()) {
    result.composition_warnings = std::move(warnings);
  }

  return result;
}

}  // namespace dasall::llm::prompt