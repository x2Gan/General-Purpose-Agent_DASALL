#include "CliOutputFormatter.h"

#include <string_view>
#include <string>

#include "AccessErrors.h"
#include "CliAccessErrorProjection.h"

namespace dasall::apps::cli {

namespace {

constexpr std::string_view kCliJsonSchemaVersion = "cli.output.v1";

[[nodiscard]] std::string disposition_text(const DaemonClientResponse& response) {
  using dasall::access::daemon::UdsResponseDisposition;

  switch (response.disposition) {
    case UdsResponseDisposition::Rejected:
      return "rejected";
    case UdsResponseDisposition::Completed:
      return "completed";
    case UdsResponseDisposition::AcceptedAsync:
      return "accepted_async";
    case UdsResponseDisposition::NotReady:
      return "not_ready";
  }

  return "rejected";
}

[[nodiscard]] std::string format_action_response(
    std::string_view action,
    const DaemonClientResponse& response) {
  std::string out = "[dasall_cli] ";
  out += std::string(action);
  out += ": ";
  out += disposition_text(response);

  if (response.receipt_ref.has_value()) {
    out += " receipt=";
    out += *response.receipt_ref;
  }
  if (response.error_ref.has_value()) {
    out += " error=";
    out += *response.error_ref;
  }
  if (response.response_text.has_value() && !response.response_text->empty()) {
    out += " response=";
    out += *response.response_text;
  }

  return out;
}

[[nodiscard]] std::string escape_json_string(std::string_view input) {
  std::string output;
  output.reserve(input.size());
  for (const unsigned char current : input) {
    switch (current) {
      case '\\':
        output += "\\\\";
        break;
      case '"':
        output += "\\\"";
        break;
      case '\n':
        output += "\\n";
        break;
      case '\r':
        output += "\\r";
        break;
      case '\t':
        output += "\\t";
        break;
      default:
        output.push_back(static_cast<char>(current));
        break;
    }
  }
  return output;
}

[[nodiscard]] std::string json_string(std::string_view input) {
  return std::string("\"") + escape_json_string(input) + "\"";
}

[[nodiscard]] std::string json_nullable_string(const std::optional<std::string>& input) {
  return input.has_value() ? json_string(*input) : "null";
}

[[nodiscard]] std::string json_nullable_text(std::string_view input) {
  return input.empty() ? "null" : json_string(input);
}

[[nodiscard]] std::string json_nullable_bool(const std::optional<bool>& input) {
  if (!input.has_value()) {
    return "null";
  }

  return *input ? "true" : "false";
}

[[nodiscard]] std::string json_nullable_int(const std::optional<int>& input) {
  return input.has_value() ? std::to_string(*input) : "null";
}

[[nodiscard]] std::string json_disposition(const DaemonClientResponse& response,
                                           const CliExitDecision& decision) {
  switch (decision.outcome_family) {
    case CliOutcomeFamily::DaemonUnavailable:
      return "daemon_unavailable";
    case CliOutcomeFamily::ProtocolError:
      return "protocol_error";
    case CliOutcomeFamily::Success:
    case CliOutcomeFamily::InvalidArguments:
    case CliOutcomeFamily::AccessDenied:
    case CliOutcomeFamily::BusinessFailure:
    case CliOutcomeFamily::RetryableFailure:
      return disposition_text(response);
  }

  return disposition_text(response);
}

[[nodiscard]] std::string json_error_kind(const CliExitDecision& decision) {
  switch (decision.outcome_family) {
    case CliOutcomeFamily::InvalidArguments:
      return "argument";
    case CliOutcomeFamily::DaemonUnavailable:
      return "transport";
    case CliOutcomeFamily::AccessDenied:
      return "access_denied";
    case CliOutcomeFamily::BusinessFailure:
      return "business";
    case CliOutcomeFamily::RetryableFailure:
      return "timeout_or_cancel";
    case CliOutcomeFamily::ProtocolError:
      return "protocol";
    case CliOutcomeFamily::Success:
      return std::string();
  }

  return std::string();
}

[[nodiscard]] bool should_emit_result(const CliExitDecision& decision) {
  return decision.outcome_family == CliOutcomeFamily::Success;
}

[[nodiscard]] std::string format_result_object(
    const DaemonClientResponse& response,
    const CliExitDecision& decision) {
  if (!should_emit_result(decision)) {
    return "null";
  }

  std::string output = "{";
  output += "\"response_text\":";
  output += json_nullable_string(response.response_text);
  output += ",\"task_completed\":";
  output += json_nullable_bool(response.task_completed);
  output += '}';
  return output;
}

[[nodiscard]] std::string format_error_object(
    const DaemonClientResponse& response,
    const CliExitDecision& decision) {
  if (decision.outcome_family == CliOutcomeFamily::Success) {
    return "null";
  }

  const auto access_error_code = response.error_ref.has_value()
                                     ? classify_access_error_ref(*response.error_ref)
                                     : std::nullopt;
  const auto access_error_descriptor = access_error_code.has_value()
                                           ? std::optional(
                                                 dasall::access::describe_access_error(
                                                     *access_error_code))
                                           : std::nullopt;

  std::string output = "{";
  output += "\"kind\":" + json_string(json_error_kind(decision));
  output += ",\"reason\":" +
            json_nullable_text(decision.diagnostic_hint);
  output += ",\"error_ref\":" + json_nullable_string(response.error_ref);
  output += ",\"access_error_code\":" +
            json_nullable_int(access_error_code.has_value()
                                  ? std::optional(static_cast<int>(*access_error_code))
                                  : std::nullopt);
  output += ",\"access_error_domain\":" +
            (access_error_descriptor.has_value()
                 ? json_string(dasall::access::access_error_domain_name(
                       access_error_descriptor->domain))
                 : std::string("null"));
  output += ",\"retryable\":";
  output += access_error_descriptor.has_value()
                ? (access_error_descriptor->retryable ? "true" : "false")
                : "null";
  output += '}';
  return output;
}

}  // namespace

std::string CliOutputFormatter::format_command_human_output(
    std::string_view command,
    const DaemonClientResponse& response) {
  return format_action_response(command, response);
}

std::string CliOutputFormatter::format_json_output(
    std::string_view command,
    const DaemonClientResponse& response,
    const CliExitDecision& decision) {
  std::string output = "{";
  output += "\"schema_version\":" + json_string(kCliJsonSchemaVersion);
  output += ",\"command\":" + json_string(command);
  output += ",\"request_id\":" + json_nullable_text(response.request_id);
  output += ",\"trace_id\":" + json_nullable_text(response.trace_id);
  output += ",\"session_id\":" + json_nullable_string(response.session_id);
  output += ",\"disposition\":" +
            json_string(json_disposition(response, decision));
  output += ",\"receipt_ref\":" + json_nullable_string(response.receipt_ref);
  output += ",\"result\":" + format_result_object(response, decision);
  output += ",\"error\":" + format_error_object(response, decision);
  output += ",\"warnings\":[]";
  output += ",\"exit_code\":" + std::to_string(decision.exit_code);
  output += '}';
  return output;
}

std::string CliOutputFormatter::format_ping_success(
    const DaemonClientResponse& response) {
  return format_command_human_output("ping", response);
}

std::string CliOutputFormatter::format_readiness_success(
    const DaemonClientResponse& response) {
  return format_command_human_output("readiness", response);
}

std::string CliOutputFormatter::format_ping_failure(std::string_view reason) {
  std::string out = "[dasall_cli] daemon ping: FAILED";
  if (!reason.empty()) {
    out += " - ";
    out += reason;
  }
  return out;
}

std::string CliOutputFormatter::format_submit_success(
    const DaemonClientResponse& response) {
  return format_command_human_output("run", response);
}

std::string CliOutputFormatter::format_status_success(
    const DaemonClientResponse& response) {
  return format_command_human_output("status", response);
}

std::string CliOutputFormatter::format_cancel_success(
    const DaemonClientResponse& response) {
  return format_command_human_output("cancel", response);
}

std::string CliOutputFormatter::format_diag_success(
    const DaemonClientResponse& response) {
  return format_command_human_output("diag", response);
}

std::string CliOutputFormatter::format_error(std::string_view reason) {
  std::string out = "[dasall_cli] error";
  if (!reason.empty()) {
    out += ": ";
    out += reason;
  }
  return out;
}

}  // namespace dasall::apps::cli
