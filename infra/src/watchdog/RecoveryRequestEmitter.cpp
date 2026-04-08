#include "watchdog/RecoveryRequestEmitter.h"

#include <cctype>
#include <string>
#include <string_view>
#include <utility>

namespace dasall::infra::watchdog {
namespace {

constexpr std::string_view kRecoveryRequestEmitterSourceRef =
    "RecoveryRequestEmitter";

[[nodiscard]] std::string ensure_trailing_slash(std::string prefix) {
  if (prefix.empty() || prefix.back() == '/') {
    return prefix;
  }

  prefix.push_back('/');
  return prefix;
}

}  // namespace

RecoveryRequestEmitter::RecoveryRequestEmitter(
    RecoveryRequestEmitterOptions options)
    : options_(std::move(options)) {
  options_.evidence_ref_prefix =
      ensure_trailing_slash(std::move(options_.evidence_ref_prefix));
}

RecoveryRequestEmissionResult RecoveryRequestEmitter::emit_recovery_hint(
    const TimeoutDecision& decision) const {
  if (!decision.has_required_fields()) {
    return RecoveryRequestEmissionResult::failure(
        contracts::ResultCode::ValidationFieldMissing,
        "recovery request emitter requires a valid TimeoutDecision before emitting advisory output",
        "watchdog.recovery_request.emit",
        std::string(kRecoveryRequestEmitterSourceRef));
  }

  if (decision.timeout_level != WatchdogTimeoutLevel::Critical &&
      decision.timeout_level != WatchdogTimeoutLevel::Fatal) {
    return RecoveryRequestEmissionResult::failure(
        contracts::ResultCode::ValidationFieldMissing,
        "recovery request emitter only accepts critical or fatal timeout decisions",
        "watchdog.recovery_request.emit",
        std::string(kRecoveryRequestEmitterSourceRef));
  }

  RecoveryHintRequest request{
      .reason_code = decision.reason_code,
      .target_ref = decision.entity_id,
      .suggested_action = suggested_action_for(decision),
      .evidence_ref = build_evidence_ref(decision),
  };

  if (!request.has_required_fields()) {
    return RecoveryRequestEmissionResult::failure(
        contracts::ResultCode::ValidationFieldMissing,
        "recovery request emitter could not build a valid advisory-only recovery request",
        "watchdog.recovery_request.emit",
        std::string(kRecoveryRequestEmitterSourceRef));
  }

  return RecoveryRequestEmissionResult::success(std::move(request));
}

std::string RecoveryRequestEmitter::sanitize_payload(std::string_view payload) const {
  std::string sanitized;
  sanitized.reserve(payload.size());

  for (const unsigned char ch : payload) {
    if (std::isalnum(ch) != 0 || ch == '_' || ch == '-') {
      sanitized.push_back(static_cast<char>(ch));
    } else {
      sanitized.push_back('_');
    }
  }

  if (sanitized.empty()) {
    return std::string("unknown");
  }

  return sanitized;
}

std::string RecoveryRequestEmitter::suggested_action_for(
    const TimeoutDecision& decision) const {
  if (decision.timeout_level == WatchdogTimeoutLevel::Fatal) {
    return options_.fatal_action;
  }

  return options_.critical_action;
}

std::string RecoveryRequestEmitter::build_evidence_ref(
    const TimeoutDecision& decision) const {
  return options_.evidence_ref_prefix + sanitize_payload(decision.entity_id) +
         "/" + std::string(watchdog_timeout_level_name(decision.timeout_level)) +
         "/miss/" + std::to_string(decision.consecutive_miss) +
         "/decision/" + sanitize_payload(decision.evidence_ref);
}

}  // namespace dasall::infra::watchdog