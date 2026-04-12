#include "PromptPolicy.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {

using PromptComposeResult = dasall::contracts::PromptComposeResult;
using PromptPolicyConfig = dasall::llm::prompt::PromptPolicyConfig;
using PromptPolicyDecision = dasall::llm::prompt::PromptPolicyDecision;
using PromptPolicyDisposition = dasall::llm::prompt::PromptPolicyDisposition;
using PromptPolicyInput = dasall::llm::prompt::PromptPolicyInput;
using TokenEstimate = dasall::llm::TokenEstimate;

struct ToolVisibilityResult {
  PromptPolicyDisposition disposition = PromptPolicyDisposition::Allow;
  std::vector<std::string> patch;
  std::string reason = "allow";
};

std::string to_lower_copy(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
    return static_cast<char>(std::tolower(character));
  });
  return value;
}

void append_unique(std::vector<std::string>& values, std::string value) {
  if (value.empty()) {
    return;
  }

  if (std::find(values.begin(), values.end(), value) == values.end()) {
    values.push_back(std::move(value));
  }
}

bool contains_value(const std::vector<std::string>& values, std::string_view target) {
  return std::find(values.begin(), values.end(), target) != values.end();
}

std::vector<std::string> effective_allowlist(const PromptPolicyConfig& config,
                                             const PromptPolicyInput& input) {
  if (!input.allowed_prompt_releases.empty()) {
    return input.allowed_prompt_releases;
  }

  return config.default_allowed_releases;
}

std::vector<std::string> effective_trusted_sources(const PromptPolicyConfig& config,
                                                   const PromptPolicyInput& input) {
  if (!input.trusted_sources.empty()) {
    return input.trusted_sources;
  }

  return config.default_trusted_sources;
}

std::string release_identity(const PromptComposeResult& compose_result) {
  if (!compose_result.selected_prompt_id.has_value() || !compose_result.selected_version.has_value() ||
      compose_result.selected_prompt_id->empty() || compose_result.selected_version->empty()) {
    return std::string();
  }

  return *compose_result.selected_prompt_id + "@" + *compose_result.selected_version;
}

bool allowlist_matches(const std::vector<std::string>& allowlist,
                       const PromptComposeResult& compose_result,
                       const PromptPolicyInput& input) {
  const std::string exact_release = release_identity(compose_result);
  const std::string selected_prompt_id =
      compose_result.selected_prompt_id.value_or(std::string());
  const std::string selected_version =
      compose_result.selected_version.value_or(std::string());

  for (const auto& allowed_release : allowlist) {
    if (allowed_release.empty()) {
      continue;
    }

    if ((!exact_release.empty() && allowed_release == exact_release) ||
        (!selected_prompt_id.empty() && allowed_release == selected_prompt_id) ||
        (!selected_version.empty() && allowed_release == selected_version) ||
        (!input.selected_release_scope.empty() && allowed_release == input.selected_release_scope)) {
      return true;
    }
  }

  return false;
}

std::string tool_domain(std::string_view tool_id) {
  const std::size_t separator = tool_id.find('.');
  if (separator == std::string_view::npos || separator == 0U) {
    return std::string(tool_id);
  }

  return std::string(tool_id.substr(0U, separator));
}

ToolVisibilityResult evaluate_tool_visibility(const PromptPolicyInput& input) {
  ToolVisibilityResult result;

  if (input.visible_tools.empty()) {
    return result;
  }

  if (input.tool_visibility_rules.empty()) {
    return result;
  }

  std::unordered_map<std::string, std::string> exact_rules;
  std::unordered_map<std::string, std::string> domain_rules;

  for (const auto& raw_rule : input.tool_visibility_rules) {
    if (raw_rule.empty()) {
      continue;
    }

    const std::size_t exact_separator = raw_rule.find('=');
    const std::size_t domain_separator = raw_rule.find(':');

    if (exact_separator != std::string::npos && exact_separator > 0U &&
        exact_separator + 1U < raw_rule.size()) {
      exact_rules.emplace(raw_rule.substr(0U, exact_separator),
                          to_lower_copy(raw_rule.substr(exact_separator + 1U)));
      continue;
    }

    if (domain_separator != std::string::npos && domain_separator > 0U &&
        domain_separator + 1U < raw_rule.size()) {
      domain_rules.emplace(raw_rule.substr(0U, domain_separator),
                           to_lower_copy(raw_rule.substr(domain_separator + 1U)));
      continue;
    }

    result.disposition = PromptPolicyDisposition::Deny;
    result.reason = "invalid_tool_visibility_rule";
    return result;
  }

  for (const auto& visible_tool : input.visible_tools) {
    std::optional<std::string> matched_rule;
    std::optional<std::string> normalized_patch;

    if (const auto exact_rule = exact_rules.find(visible_tool); exact_rule != exact_rules.end()) {
      matched_rule = exact_rule->second;
      normalized_patch = visible_tool + "=" + exact_rule->second;
    } else {
      const std::string domain = tool_domain(visible_tool);
      if (const auto domain_rule = domain_rules.find(domain); domain_rule != domain_rules.end()) {
        matched_rule = domain_rule->second;
        normalized_patch = domain + ":" + domain_rule->second;
      }
    }

    if (!matched_rule.has_value()) {
      result.disposition = PromptPolicyDisposition::RequireRecompose;
      result.reason = "tool_visibility_mismatch";
      append_unique(result.patch, "hide:" + visible_tool);
      continue;
    }

    if (*matched_rule == "hidden" || *matched_rule == "none" || *matched_rule == "blocked") {
      result.disposition = PromptPolicyDisposition::RequireRecompose;
      result.reason = "tool_visibility_mismatch";
      append_unique(result.patch, "hide:" + visible_tool);
      continue;
    }

    append_unique(result.patch, *normalized_patch);
  }

  return result;
}

std::size_t token_end(const std::string& text, std::size_t start) {
  std::size_t end = start;
  while (end < text.size() && !std::isspace(static_cast<unsigned char>(text[end])) &&
         text[end] != ',' && text[end] != ';') {
    ++end;
  }

  return end;
}

bool replace_prefixed_token(std::string& text,
                            std::string_view prefix,
                            std::string_view replacement,
                            std::string_view label,
                            std::vector<std::string>& redactions) {
  bool changed = false;
  std::size_t search_from = 0U;

  while (true) {
    const std::size_t position = text.find(prefix, search_from);
    if (position == std::string::npos) {
      return changed;
    }

    const std::size_t end = token_end(text, position);
    text.replace(position, end - position, replacement);
    search_from = position + replacement.size();
    append_unique(redactions, std::string(label));
    changed = true;
  }
}

std::vector<std::string> apply_redactions(const std::vector<std::string>& messages,
                                          std::vector<std::string>& redactions) {
  std::vector<std::string> governed_messages = messages;

  for (auto& message : governed_messages) {
    replace_prefixed_token(message, "secret://", "[REDACTED_SECRET]", "secret_ref", redactions);
    replace_prefixed_token(message, "token=", "token=[REDACTED]", "token", redactions);
    replace_prefixed_token(message, "api_key=", "api_key=[REDACTED]", "api_key", redactions);
    replace_prefixed_token(message, "password=", "password=[REDACTED]", "password", redactions);
    replace_prefixed_token(message, "bearer ", "bearer [REDACTED]", "authorization", redactions);
  }

  return governed_messages;
}

bool has_warning(const PromptComposeResult& compose_result, std::string_view warning) {
  return compose_result.composition_warnings.has_value() &&
         contains_value(*compose_result.composition_warnings, warning);
}

PromptPolicyDecision make_decision(PromptPolicyDisposition disposition,
                                   std::string reason,
                                   std::vector<std::string> governed_messages = {},
                                   std::vector<std::string> redactions = {},
                                   std::vector<std::string> tool_visibility_patch = {}) {
  return PromptPolicyDecision{
      .disposition = disposition,
      .governed_messages = std::move(governed_messages),
      .redactions = std::move(redactions),
      .tool_visibility_patch = std::move(tool_visibility_patch),
      .reason = std::move(reason),
  };
}

}  // namespace

namespace dasall::llm::prompt {

PromptPolicy::PromptPolicy(std::shared_ptr<dasall::llm::TokenEstimator> token_estimator)
    : token_estimator_(std::move(token_estimator)) {}

bool PromptPolicy::init(const PromptPolicyConfig& config) {
  config_ = config;
  initialized_ = token_estimator_ != nullptr;
  return initialized_;
}

PromptPolicyDecision PromptPolicy::evaluate(const PromptComposeResult& compose_result,
                                            const PromptPolicyInput& input) const {
  if (!initialized_) {
    return make_decision(PromptPolicyDisposition::Deny, "policy_not_initialized");
  }

  if (!compose_result.messages.has_value() || compose_result.messages->empty()) {
    return make_decision(PromptPolicyDisposition::Deny, "missing_composed_messages");
  }

  if (!compose_result.selected_prompt_id.has_value() || !compose_result.selected_version.has_value() ||
      compose_result.selected_prompt_id->empty() || compose_result.selected_version->empty()) {
    return make_decision(PromptPolicyDisposition::Deny, "missing_selected_prompt");
  }

  const std::vector<std::string> trusted_sources = effective_trusted_sources(config_, input);
  if (trusted_sources.empty()) {
    return make_decision(PromptPolicyDisposition::Deny, "trusted_source_filter_missing");
  }

  if (input.selected_trusted_source.empty()) {
    return make_decision(PromptPolicyDisposition::Deny, "trusted_source_missing");
  }

  if (!contains_value(trusted_sources, input.selected_trusted_source)) {
    return make_decision(PromptPolicyDisposition::Deny, "trusted_source_denied");
  }

  const std::vector<std::string> allowlist = effective_allowlist(config_, input);
  if (allowlist.empty() && config_.deny_on_missing_allowlist) {
    return make_decision(PromptPolicyDisposition::Deny, "allowlist_missing");
  }

  if (!allowlist.empty() && !allowlist_matches(allowlist, compose_result, input)) {
    return make_decision(PromptPolicyDisposition::Deny, "prompt_release_not_allowed");
  }

  ToolVisibilityResult visibility = evaluate_tool_visibility(input);
  if (visibility.disposition == PromptPolicyDisposition::Deny) {
    return make_decision(PromptPolicyDisposition::Deny, visibility.reason, {}, {},
                         std::move(visibility.patch));
  }

  if (visibility.disposition == PromptPolicyDisposition::RequireRecompose) {
    return make_decision(PromptPolicyDisposition::RequireRecompose, visibility.reason, {}, {},
                         std::move(visibility.patch));
  }

  std::vector<std::string> redactions;
  std::vector<std::string> governed_messages = apply_redactions(*compose_result.messages, redactions);

  const std::uint32_t render_budget_tokens = input.render_budget_tokens > 0U
                                                 ? input.render_budget_tokens
                                                 : std::numeric_limits<std::uint32_t>::max();
  const TokenEstimate estimate = token_estimator_->estimate(governed_messages, render_budget_tokens, 0U);
  if ((input.render_budget_tokens > 0U && estimate.over_budget) ||
      (input.render_budget_tokens == 0U && has_warning(compose_result, "over_budget"))) {
    return make_decision(PromptPolicyDisposition::OverBudget, "render_budget_exceeded", {},
                         std::move(redactions), std::move(visibility.patch));
  }

  return make_decision(PromptPolicyDisposition::Allow, "allow", std::move(governed_messages),
                       std::move(redactions), std::move(visibility.patch));
}

}  // namespace dasall::llm::prompt