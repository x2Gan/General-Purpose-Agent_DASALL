#include "diagnostics/RedactionEngine.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <optional>
#include <string_view>
#include <utility>

#include "diagnostics/DiagnosticsErrors.h"

namespace dasall::infra::diagnostics {
namespace {

constexpr std::string_view kRedactionSourceRef = "RedactionEngine";

constexpr std::array<std::string_view, 7> kDenyListTokens{
    "secret",
    "password",
    "token",
    "authorization",
    "cookie",
    "apikey",
    "credential",
};

constexpr std::array<std::string_view, 8> kAllowedEvidenceSchemes{
    "logs://",
    "metrics://",
    "health://",
    "errors://",
    "command://",
    "snapshot://",
    "export://",
    "error://",
};

[[nodiscard]] std::string to_lower_copy(std::string_view value) {
  std::string lowered(value);
  std::transform(lowered.begin(), lowered.end(), lowered.begin(), [](unsigned char character) {
    return static_cast<char>(std::tolower(character));
  });
  return lowered;
}

[[nodiscard]] bool contains_deny_token(std::string_view value) {
  const std::string lowered = to_lower_copy(value);
  return std::any_of(kDenyListTokens.begin(), kDenyListTokens.end(), [&](std::string_view token) {
    return lowered.find(token) != std::string::npos;
  });
}

void replace_case_insensitive_all(std::string& value,
                                  std::string_view token,
                                  std::string_view replacement) {
  std::string lowered = to_lower_copy(value);
  const std::string lowered_token = to_lower_copy(token);
  const std::string lowered_replacement = to_lower_copy(replacement);

  std::size_t position = lowered.find(lowered_token);
  while (position != std::string::npos) {
    value.replace(position, token.size(), replacement);
    lowered.replace(position, token.size(), lowered_replacement);
    position = lowered.find(lowered_token, position + replacement.size());
  }
}

[[nodiscard]] std::string mask_deny_tokens(std::string value) {
  for (const auto token : kDenyListTokens) {
    replace_case_insensitive_all(value, token, "[REDACTED]");
  }

  return value;
}

[[nodiscard]] bool is_supported_profile(RedactionProfile profile) {
  return profile == RedactionProfile::Strict || profile == RedactionProfile::Compatibility;
}

[[nodiscard]] bool is_allowed_evidence_ref(std::string_view ref) {
  if (ref.rfind("raw://", 0) == 0 || ref.rfind("inline://", 0) == 0 || ref.rfind("data:", 0) == 0) {
    return false;
  }

  return std::any_of(kAllowedEvidenceSchemes.begin(),
                     kAllowedEvidenceSchemes.end(),
                     [&](std::string_view scheme) { return ref.rfind(scheme, 0) == 0; });
}

[[nodiscard]] std::string canonical_summary(std::string_view command_name) {
  if (command_name == "health.snapshot") {
    return "diagnostics redacted health snapshot";
  }

  if (command_name == "queue.stats") {
    return "diagnostics redacted queue stats";
  }

  return "diagnostics redacted thread dump";
}

[[nodiscard]] std::vector<std::string> strict_args(const DiagnosticsCommand& command) {
  if (command.command_name == "health.snapshot") {
    return {std::string("--summary")};
  }

  if (command.command_name == "queue.stats") {
    return {std::string("--queue=redacted")};
  }

  return {std::string("--limit=5")};
}

[[nodiscard]] std::optional<std::vector<std::string>> compat_args(const std::vector<std::string>& args) {
  std::vector<std::string> redacted_args;
  redacted_args.reserve(args.size());

  for (const auto& argument : args) {
    if (argument.rfind("--", 0) != 0) {
      return std::nullopt;
    }

    if (!contains_deny_token(argument)) {
      redacted_args.push_back(argument);
      continue;
    }

    const auto delimiter = argument.find('=');
    if (delimiter == std::string::npos) {
      return std::nullopt;
    }

    redacted_args.push_back(argument.substr(0, delimiter + 1) + "redacted");
  }

  return redacted_args;
}

[[nodiscard]] std::optional<std::vector<std::string>> redact_evidence_refs(
    const std::vector<std::string>& evidence_refs) {
  std::vector<std::string> redacted_refs;
  redacted_refs.reserve(evidence_refs.size());

  for (const auto& evidence_ref : evidence_refs) {
    if (!is_allowed_evidence_ref(evidence_ref)) {
      return std::nullopt;
    }

    redacted_refs.push_back(mask_deny_tokens(evidence_ref));
  }

  return redacted_refs;
}

}  // namespace

RedactionOutcome RedactionOutcome::success(DiagnosticsSnapshot snapshot) {
  return RedactionOutcome{
      .redacted = true,
      .snapshot = std::move(snapshot),
      .result_code = contracts::ResultCode::RuntimeRetryExhausted,
      .error = std::nullopt,
  };
}

RedactionOutcome RedactionOutcome::failure(contracts::ResultCode result_code,
                                           std::string message,
                                           std::string stage,
                                           std::string source_ref) {
  return RedactionOutcome{
      .redacted = false,
      .snapshot = {},
      .result_code = result_code,
      .error = contracts::ErrorInfo{
          .failure_type = contracts::classify_result_code(result_code),
          .retryable = false,
          .safe_to_replan = false,
          .details = contracts::ErrorDetails{
              .code = static_cast<int>(result_code),
              .message = std::move(message),
              .stage = std::move(stage),
          },
          .source_ref = contracts::ErrorSourceRefMinimal{
              .ref_type = "infra.diagnostics",
              .ref_id = std::move(source_ref),
          },
      },
  };
}

RedactionOutcome RedactionEngine::redact(DiagnosticsSnapshot snapshot) const {
  if (!is_supported_profile(snapshot.redaction_profile)) {
    return RedactionOutcome::failure(
        map_diagnostics_error_code(DiagnosticsErrorCode::RedactionFail).result_code,
        std::string("diagnostics redaction requires strict or compat profile"),
        std::string("diagnostics.redact"),
        std::string(kRedactionSourceRef));
  }

  if (snapshot.exporter_hint != "local_file") {
    return RedactionOutcome::failure(
        map_diagnostics_error_code(DiagnosticsErrorCode::RedactionFail).result_code,
        std::string("diagnostics redaction only permits local_file exporter hints"),
        std::string("diagnostics.redact"),
        std::string(kRedactionSourceRef));
  }

  const auto redacted_evidence_refs = redact_evidence_refs(snapshot.evidence_refs);
  if (!redacted_evidence_refs.has_value()) {
    return RedactionOutcome::failure(
        map_diagnostics_error_code(DiagnosticsErrorCode::RedactionFail).result_code,
        std::string("diagnostics redaction requires reference-only evidence refs with controlled schemes"),
        std::string("diagnostics.redact"),
        std::string(kRedactionSourceRef));
  }

  snapshot.command.actor_ref = "actor://redacted";
  snapshot.evidence_refs = *redacted_evidence_refs;

  if (snapshot.redaction_profile == RedactionProfile::Strict) {
    snapshot.command.args = strict_args(snapshot.command);
    snapshot.summary = canonical_summary(snapshot.command.command_name);
    return RedactionOutcome::success(std::move(snapshot));
  }

  const auto compat_redacted_args = compat_args(snapshot.command.args);
  if (!compat_redacted_args.has_value()) {
    return RedactionOutcome::failure(
        map_diagnostics_error_code(DiagnosticsErrorCode::RedactionFail).result_code,
        std::string("diagnostics compat redaction could not stabilize command args"),
        std::string("diagnostics.redact"),
        std::string(kRedactionSourceRef));
  }

  snapshot.command.args = *compat_redacted_args;
  snapshot.summary = mask_deny_tokens(snapshot.summary);
  if (snapshot.summary.empty() || contains_deny_token(snapshot.summary)) {
    return RedactionOutcome::failure(
        map_diagnostics_error_code(DiagnosticsErrorCode::RedactionFail).result_code,
        std::string("diagnostics compat redaction could not remove sensitive summary tokens"),
        std::string("diagnostics.redact"),
        std::string(kRedactionSourceRef));
  }

  return RedactionOutcome::success(std::move(snapshot));
}

}  // namespace dasall::infra::diagnostics