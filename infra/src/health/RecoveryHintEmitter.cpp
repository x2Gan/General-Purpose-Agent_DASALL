#include "health/RecoveryHintEmitter.h"

#include <cctype>
#include <string>
#include <string_view>
#include <utility>

namespace dasall::infra {
namespace {

constexpr std::string_view kRecoveryHintEmitterSourceRef = "RecoveryHintEmitter";

[[nodiscard]] std::string ensure_trailing_slash(std::string prefix) {
  if (prefix.empty() || prefix.back() == '/') {
    return prefix;
  }

  prefix.push_back('/');
  return prefix;
}

[[nodiscard]] std::string state_segment(const HealthSnapshot& snapshot) {
  switch (snapshot.state()) {
    case HealthState::Degraded:
      return "degraded";
    case HealthState::Unhealthy:
      return "unhealthy";
    case HealthState::Healthy:
      return "healthy";
    case HealthState::Unknown:
      break;
  }

  return "unknown";
}

}  // namespace

RecoveryHintEmitter::RecoveryHintEmitter(RecoveryHintEmitterOptions options)
    : options_(std::move(options)) {
  options_.evidence_ref_prefix =
      ensure_trailing_slash(std::move(options_.evidence_ref_prefix));
}

RecoveryHintEmissionResult RecoveryHintEmitter::emit_hint(
    const HealthSnapshot& snapshot,
    std::string_view reason) const {
  if (!snapshot.has_consistent_state() || !snapshot.has_version_metadata()) {
    return RecoveryHintEmissionResult::failure(
        contracts::ResultCode::ValidationFieldMissing,
        "recovery hint emitter requires a consistent HealthSnapshot with version metadata",
        "health.recovery_hint.emit",
        std::string(kRecoveryHintEmitterSourceRef));
  }

  if (snapshot.state() != HealthState::Degraded &&
      snapshot.state() != HealthState::Unhealthy) {
    return RecoveryHintEmissionResult::failure(
        contracts::ResultCode::ValidationFieldMissing,
        "recovery hint emitter only accepts degraded or unhealthy snapshots",
        "health.recovery_hint.emit",
        std::string(kRecoveryHintEmitterSourceRef));
  }

  if (reason.empty()) {
    return RecoveryHintEmissionResult::failure(
        contracts::ResultCode::ValidationFieldMissing,
        "recovery hint emitter requires a non-empty reason for traceable advisory output",
        "health.recovery_hint.emit",
        std::string(kRecoveryHintEmitterSourceRef));
  }

  const std::string sanitized_reason = sanitize_hint_payload(reason);
  RecoveryHint hint{
      .reason_code = reason_code_for(snapshot),
      .severity = severity_for(snapshot),
      .suggested_action = suggested_action_for(snapshot),
      .evidence_ref = build_evidence_ref(snapshot, sanitized_reason),
  };

  if (!hint.has_required_fields()) {
    return RecoveryHintEmissionResult::failure(
        contracts::ResultCode::ValidationFieldMissing,
        "recovery hint emitter could not build a valid advisory payload",
        "health.recovery_hint.emit",
        std::string(kRecoveryHintEmitterSourceRef));
  }

  return RecoveryHintEmissionResult::success(std::move(hint));
}

std::string RecoveryHintEmitter::sanitize_hint_payload(std::string_view payload) const {
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

contracts::ResultCode RecoveryHintEmitter::reason_code_for(
    const HealthSnapshot& snapshot) const {
  if (snapshot.state() == HealthState::Unhealthy) {
    return contracts::ResultCode::RuntimeRetryExhausted;
  }

  return contracts::ResultCode::ProviderTimeout;
}

RecoveryHintSeverity RecoveryHintEmitter::severity_for(
    const HealthSnapshot& snapshot) const {
  if (snapshot.state() == HealthState::Unhealthy) {
    return RecoveryHintSeverity::Critical;
  }

  return RecoveryHintSeverity::Warning;
}

std::string RecoveryHintEmitter::suggested_action_for(
    const HealthSnapshot& snapshot) const {
  if (snapshot.state() == HealthState::Unhealthy) {
    return options_.unhealthy_action;
  }

  return options_.degraded_action;
}

std::string RecoveryHintEmitter::build_evidence_ref(
    const HealthSnapshot& snapshot,
    std::string_view sanitized_reason) const {
  return options_.evidence_ref_prefix + state_segment(snapshot) +
         "/version/" + std::to_string(snapshot.version) + "/components/" +
         build_component_segment(snapshot) + "/reason/" +
         std::string(sanitized_reason);
}

std::string RecoveryHintEmitter::build_component_segment(
    const HealthSnapshot& snapshot) const {
  if (snapshot.failed_components.empty()) {
    return std::string("none");
  }

  std::string segment;
  for (std::size_t index = 0; index < snapshot.failed_components.size(); ++index) {
    if (index > 0U) {
      segment += "__";
    }

    segment += sanitize_hint_payload(snapshot.failed_components[index]);
  }

  return segment;
}

}  // namespace dasall::infra