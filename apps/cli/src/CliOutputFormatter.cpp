#include "CliOutputFormatter.h"

#include <string>

namespace dasall::apps::cli {

namespace {

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

}  // namespace

std::string CliOutputFormatter::format_ping_success(
    const DaemonClientResponse& response) {
  return format_action_response("ping", response);
}

std::string CliOutputFormatter::format_readiness_success(
    const DaemonClientResponse& response) {
  return format_action_response("readiness", response);
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
  return format_action_response("submit", response);
}

std::string CliOutputFormatter::format_status_success(
    const DaemonClientResponse& response) {
  return format_action_response("status", response);
}

std::string CliOutputFormatter::format_cancel_success(
    const DaemonClientResponse& response) {
  return format_action_response("cancel", response);
}

std::string CliOutputFormatter::format_diag_success(
    const DaemonClientResponse& response) {
  return format_action_response("diag", response);
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
