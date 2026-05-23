#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <type_traits>

#include "ipc/TuiIpcController.h"
#include "support/TestAssertions.h"

#ifndef DASALL_TUI_IPC_CONTROLLER_HEADER
#define DASALL_TUI_IPC_CONTROLLER_HEADER \
  "/home/gangan/DASALL/apps/tui/src/ipc/TuiIpcController.h"
#endif

namespace {

using dasall::tests::support::assert_equal;
using dasall::tests::support::assert_true;
using dasall::tui::data::NextTurnPreference;
using dasall::tui::data::TuiCloseSessionRequest;
using dasall::tui::data::TuiEventProjection;
using dasall::tui::data::TuiOpenSessionRequest;
using dasall::tui::data::TuiPollEventsRequest;
using dasall::tui::data::TuiRouteCatalogRequest;
using dasall::tui::data::TuiRoutePreferenceMode;
using dasall::tui::data::TuiRouteCatalogView;
using dasall::tui::data::TuiSessionView;
using dasall::tui::data::TuiSubmitTurnRequest;
using dasall::tui::data::TuiTurnReceipt;
using dasall::tui::ipc::TuiIpcCloseSessionAck;
using dasall::tui::ipc::TuiIpcCloseSessionPayload;
using dasall::tui::ipc::TuiIpcController;
using dasall::tui::ipc::TuiIpcControllerOptions;
using dasall::tui::ipc::TuiIpcOpenSessionPayload;
using dasall::tui::ipc::TuiIpcOperation;
using dasall::tui::ipc::TuiIpcOutcome;
using dasall::tui::ipc::TuiIpcPollEventsBatch;
using dasall::tui::ipc::TuiIpcPollEventsPayload;
using dasall::tui::ipc::TuiIpcResponseEnvelope;
using dasall::tui::ipc::TuiIpcRouteCatalogPayload;
using dasall::tui::ipc::TuiIpcSubmitTurnPayload;
using dasall::tui::ipc::kTuiDefaultDaemonSocketPath;
using dasall::tui::ipc::kTuiIpcSchemaVersion;
using dasall::tui::ipc::make_request_envelope;
using dasall::tui::ipc::to_string;

[[nodiscard]] std::string read_text_file(const std::filesystem::path& path) {
  std::ifstream input(path);
  return std::string(std::istreambuf_iterator<char>(input),
                     std::istreambuf_iterator<char>());
}

void controller_surface_matches_detailed_design() {
  using OpenSignature =
      dasall::tui::data::TuiOpenSessionResult (TuiIpcController::*)(
          const dasall::tui::data::TuiOpenSessionRequest&);
  using SubmitSignature =
      dasall::tui::data::TuiSubmitTurnResult (TuiIpcController::*)(
          const dasall::tui::data::TuiSubmitTurnRequest&);
  using PollSignature =
      dasall::tui::data::TuiPollEventsResult (TuiIpcController::*)(
          const dasall::tui::data::TuiPollEventsRequest&);
  using RouteSignature =
      dasall::tui::data::TuiRouteCatalogResult (TuiIpcController::*)(
          const dasall::tui::data::TuiRouteCatalogRequest&);
  using CloseSignature =
      dasall::tui::data::TuiCloseSessionResult (TuiIpcController::*)(
          const dasall::tui::data::TuiCloseSessionRequest&);

  static_assert(
      std::is_same_v<OpenSignature, decltype(&TuiIpcController::open_session)>);
  static_assert(
      std::is_same_v<SubmitSignature, decltype(&TuiIpcController::submit_turn)>);
  static_assert(
      std::is_same_v<PollSignature, decltype(&TuiIpcController::poll_events)>);
  static_assert(std::is_same_v<RouteSignature,
                               decltype(&TuiIpcController::query_route_catalog)>);
  static_assert(std::is_same_v<CloseSignature,
                               decltype(&TuiIpcController::close_session)>);
}

void request_envelope_builders_preserve_operation_specific_fields() {
  NextTurnPreference preference;
  preference.mode = TuiRoutePreferenceMode::PinModel;
  preference.pinned_provider_id = std::string("deepseek-prod");
  preference.pinned_model_id = std::string("deepseek-chat");
  preference.user_visible_summary = "pin deepseek-prod/deepseek-chat";

  const TuiOpenSessionRequest open_request{
      .profile_id = std::string("desktop_full"),
      .startup_mode_hint = std::string("full"),
      .request_id = "req-open",
      .trace_id = "trace-open",
  };
  const auto open_envelope = make_request_envelope(open_request, 3000);
  const auto* open_payload =
      std::get_if<TuiIpcOpenSessionPayload>(&open_envelope.payload);
  assert_true(open_payload != nullptr,
              "open_session should map to TuiIpcOpenSessionPayload");
  assert_equal(std::string(kTuiIpcSchemaVersion),
               open_envelope.schema_version,
               "open_session should freeze the TUI IPC schema version");
  assert_equal(std::string("open_session"),
               std::string(to_string(open_envelope.operation)),
               "open_session should keep the canonical operation name");
  assert_true(!open_envelope.session_id.has_value(),
              "open_session should not require an existing session_id");
  assert_true(open_envelope.deadline_ms == 3000,
              "open_session should preserve its explicit deadline");
  assert_equal(std::string("desktop_full"),
               open_payload->profile_id.value_or(std::string()),
               "open_session should carry profile_id in the request payload");

  const TuiSubmitTurnRequest submit_request{
      .session_id = "session-001",
      .user_input = "Summarize the current readiness state",
      .next_preference = preference,
      .request_id = "req-submit",
      .trace_id = "trace-submit",
  };
  const auto submit_envelope = make_request_envelope(submit_request, 15000);
  const auto* submit_payload =
      std::get_if<TuiIpcSubmitTurnPayload>(&submit_envelope.payload);
  assert_true(submit_payload != nullptr,
              "submit_turn should map to TuiIpcSubmitTurnPayload");
  assert_equal(std::string("submit_turn"),
               std::string(to_string(submit_envelope.operation)),
               "submit_turn should keep the canonical operation name");
  assert_equal(std::string("session-001"),
               submit_envelope.session_id.value_or(std::string()),
               "submit_turn should route the caller session_id through the envelope");
  assert_equal(std::string("Summarize the current readiness state"),
               submit_payload->user_input,
               "submit_turn should keep user_input in the payload body");
  assert_equal(std::string("deepseek-prod"),
               submit_payload->next_preference.pinned_provider_id.value_or(
                   std::string()),
               "submit_turn should carry the next-turn provider pin");

  const TuiPollEventsRequest poll_request{
      .session_id = "session-001",
      .event_cursor = std::string("cursor-001"),
      .request_id = "req-poll",
      .trace_id = "trace-poll",
  };
  const auto poll_envelope = make_request_envelope(poll_request, 3000);
  const auto* poll_payload =
      std::get_if<TuiIpcPollEventsPayload>(&poll_envelope.payload);
  assert_true(poll_payload != nullptr,
              "poll_events should map to TuiIpcPollEventsPayload");
  assert_equal(std::string("poll_events"),
               std::string(to_string(poll_envelope.operation)),
               "poll_events should keep the canonical operation name");
  assert_equal(std::string("cursor-001"),
               poll_payload->event_cursor.value_or(std::string()),
               "poll_events should preserve the prior event cursor");

  const TuiRouteCatalogRequest route_request{
      .session_id = std::string("session-001"),
      .profile_id = std::string("desktop_full"),
      .selector_mode = std::string("next_turn"),
      .request_id = "req-route",
      .trace_id = "trace-route",
  };
  const auto route_envelope = make_request_envelope(route_request, 3000);
  const auto* route_payload =
      std::get_if<TuiIpcRouteCatalogPayload>(&route_envelope.payload);
  assert_true(route_payload != nullptr,
              "route_catalog should map to TuiIpcRouteCatalogPayload");
  assert_equal(std::string("route_catalog"),
               std::string(to_string(route_envelope.operation)),
               "route_catalog should keep the canonical operation name");
  assert_equal(std::string("next_turn"),
               route_payload->selector_mode.value_or(std::string()),
               "route_catalog should preserve selector_mode for route filtering");

  const TuiCloseSessionRequest close_request{
      .session_id = "session-001",
      .close_reason = "/exit",
      .request_id = "req-close",
      .trace_id = "trace-close",
  };
  const auto close_envelope = make_request_envelope(close_request, 3000);
  const auto* close_payload =
      std::get_if<TuiIpcCloseSessionPayload>(&close_envelope.payload);
  assert_true(close_payload != nullptr,
              "close_session should map to TuiIpcCloseSessionPayload");
  assert_equal(std::string("close_session"),
               std::string(to_string(close_envelope.operation)),
               "close_session should keep the canonical operation name");
  assert_equal(std::string("/exit"),
               close_payload->close_reason,
               "close_session should preserve the caller-visible close reason");
}

void response_envelopes_keep_success_and_failure_contracts_disjoint() {
  TuiSessionView session;
  session.session_id = "session-001";
  session.profile_id = "desktop_full";
  session.daemon_readiness = "ready";
  session.startup_mode = "full";
  session.started_at = "2026-05-23T09:00:00Z";

  TuiIpcResponseEnvelope open_success;
  open_success.operation = TuiIpcOperation::OpenSession;
  open_success.request_id = "req-open";
  open_success.trace_id = "trace-open";
  open_success.outcome = TuiIpcOutcome::Success;
  open_success.payload = session;
  assert_true(open_success.ok() && open_success.has_consistent_values(),
              "open_session success should carry a TuiSessionView payload only");

  TuiTurnReceipt receipt;
  receipt.request_id = "req-submit";
  receipt.trace_id = "trace-submit";
  receipt.session_id = session.session_id;
  receipt.disposition = "accepted_async";
  receipt.receipt_ref = "receipt-001";
  receipt.submitted_at = "2026-05-23T09:00:01Z";
  receipt.summary_text = "turn accepted";

  TuiIpcResponseEnvelope submit_success;
  submit_success.operation = TuiIpcOperation::SubmitTurn;
  submit_success.request_id = receipt.request_id;
  submit_success.trace_id = receipt.trace_id;
  submit_success.session_id = receipt.session_id;
  submit_success.outcome = TuiIpcOutcome::Success;
  submit_success.payload = receipt;
  assert_true(submit_success.ok() && submit_success.has_consistent_values(),
              "submit_turn success should carry a TuiTurnReceipt payload only");

  TuiEventProjection event;
  event.event_cursor = "cursor-002";
  event.event_kind = "status.updated";
  event.session_id = session.session_id;
  event.timestamp = "2026-05-23T09:00:02Z";

  TuiIpcPollEventsBatch poll_batch;
  poll_batch.events.push_back(event);
  poll_batch.next_cursor = std::string("cursor-002");

  TuiIpcResponseEnvelope poll_success;
  poll_success.operation = TuiIpcOperation::PollEvents;
  poll_success.request_id = "req-poll";
  poll_success.trace_id = "trace-poll";
  poll_success.session_id = session.session_id;
  poll_success.outcome = TuiIpcOutcome::Success;
  poll_success.payload = poll_batch;
  assert_true(poll_success.ok() && poll_success.has_consistent_values(),
              "poll_events success should carry the event batch payload only");

  TuiRouteCatalogView route_catalog;
  route_catalog.current_route.current_provider_id = "deepseek-prod";
  route_catalog.current_route.current_model_id = "deepseek-chat";
  route_catalog.current_route.current_depth_tier = "standard";
  route_catalog.current_route.next_preference.user_visible_summary = "auto";

  TuiIpcResponseEnvelope route_success;
  route_success.operation = TuiIpcOperation::RouteCatalog;
  route_success.request_id = "req-route";
  route_success.trace_id = "trace-route";
  route_success.session_id = session.session_id;
  route_success.outcome = TuiIpcOutcome::Success;
  route_success.payload = route_catalog;
  assert_true(route_success.ok() && route_success.has_consistent_values(),
              "route_catalog success should carry the route catalog payload only");

  TuiIpcCloseSessionAck close_ack;
  close_ack.closed = true;

  TuiIpcResponseEnvelope close_success;
  close_success.operation = TuiIpcOperation::CloseSession;
  close_success.request_id = "req-close";
  close_success.trace_id = "trace-close";
  close_success.session_id = session.session_id;
  close_success.outcome = TuiIpcOutcome::Success;
  close_success.payload = close_ack;
  assert_true(close_success.ok() && close_success.has_consistent_values(),
              "close_session success should carry the close ack payload only");

  TuiIpcResponseEnvelope permission_denied;
  permission_denied.operation = TuiIpcOperation::SubmitTurn;
  permission_denied.request_id = "req-submit";
  permission_denied.trace_id = "trace-submit";
  permission_denied.session_id = session.session_id;
  permission_denied.outcome = TuiIpcOutcome::Failure;
  permission_denied.reason_domain = std::string("transport");
  permission_denied.reason_code = std::string("permission_denied");
  permission_denied.message =
      std::string("permission denied for /run/dasall/daemon.sock");
  permission_denied.retryable = false;
  permission_denied.error_ref = std::string("error-001");
  permission_denied.metadata.emplace_back("socket_path",
                                          "/run/dasall/daemon.sock");
  assert_true(!permission_denied.ok() &&
                  permission_denied.has_consistent_values(),
              "failure envelopes should keep reason domain/code machine-readable");

  TuiIpcResponseEnvelope incomplete_failure;
  incomplete_failure.operation = TuiIpcOperation::PollEvents;
  incomplete_failure.request_id = "req-poll";
  incomplete_failure.trace_id = "trace-poll";
  incomplete_failure.outcome = TuiIpcOutcome::Failure;
  incomplete_failure.reason_domain = std::string("protocol");
  assert_true(!incomplete_failure.has_consistent_values(),
              "failure envelopes should reject missing reason_code");

  TuiIpcResponseEnvelope ambiguous_failure;
  ambiguous_failure.operation = TuiIpcOperation::PollEvents;
  ambiguous_failure.request_id = "req-poll";
  ambiguous_failure.trace_id = "trace-poll";
  ambiguous_failure.outcome = TuiIpcOutcome::Failure;
  ambiguous_failure.reason_domain = std::string("protocol");
  ambiguous_failure.reason_code = std::string("malformed_response");
  ambiguous_failure.payload = poll_batch;
  assert_true(!ambiguous_failure.has_consistent_values(),
              "failure envelopes should reject payload-plus-error ambiguity");
}

void controller_options_freeze_socket_path_and_per_operation_deadlines() {
  const TuiIpcControllerOptions options;

  assert_equal(std::string(kTuiDefaultDaemonSocketPath),
               options.socket_path,
               "TuiIpcControllerOptions should default to the daemon socket path");
  assert_true(options.timeout_policy.open_session_deadline_ms > 0,
              "open_session should keep a positive timeout");
  assert_true(options.timeout_policy.submit_turn_deadline_ms >
                  options.timeout_policy.poll_events_deadline_ms,
              "submit_turn should allow a longer deadline than poll_events");
  assert_true(options.timeout_policy.route_catalog_deadline_ms > 0 &&
                  options.timeout_policy.close_session_deadline_ms > 0,
              "route_catalog and close_session should keep positive deadlines");
}

void controller_header_avoids_owner_private_includes_and_cli_projection_reuse() {
  const std::string header_text =
      read_text_file(std::filesystem::path{DASALL_TUI_IPC_CONTROLLER_HEADER});

  assert_true(header_text.find("access/") == std::string::npos,
              "TuiIpcController should not include access private headers");
  assert_true(header_text.find("runtime/") == std::string::npos,
              "TuiIpcController should not include runtime private headers");
  assert_true(header_text.find("llm/") == std::string::npos,
              "TuiIpcController should not include llm private headers");
  assert_true(header_text.find("profiles/") == std::string::npos,
              "TuiIpcController should not include profile private headers");
  assert_true(header_text.find("DaemonClientResponse") == std::string::npos,
              "TuiIpcController should not reuse CLI projection envelopes");
  assert_true(header_text.find("UdsRequestFrame") == std::string::npos,
              "TuiIpcController should not bind directly to raw daemon carriers");
  assert_true(header_text.find("UdsResponseFrame") == std::string::npos,
              "TuiIpcController should not bind directly to raw daemon carriers");
  assert_true(header_text.find("AgentRequest") == std::string::npos,
              "TuiIpcController should not bind to access shared request owners");
  assert_true(header_text.find("RuntimeDispatchRequest") == std::string::npos,
              "TuiIpcController should not bind to runtime dispatch owners");
  assert_true(header_text.find("ftxui") == std::string::npos,
              "TuiIpcController should not leak renderer dependencies into IPC mapping");
}

}  // namespace

int main() {
  try {
    controller_surface_matches_detailed_design();
    request_envelope_builders_preserve_operation_specific_fields();
    response_envelopes_keep_success_and_failure_contracts_disjoint();
    controller_options_freeze_socket_path_and_per_operation_deadlines();
    controller_header_avoids_owner_private_includes_and_cli_projection_reuse();
  } catch (const std::exception& exception) {
    std::cerr << "[TuiDaemonProjectionMappingTest] FAILED: " << exception.what()
              << '\n';
    return 1;
  }

  return 0;
}