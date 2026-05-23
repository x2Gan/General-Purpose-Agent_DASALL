#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "data/ITuiDataSource.h"

namespace dasall::tui::ipc {

inline constexpr std::string_view kTuiIpcSchemaVersion = "tui_ipc.v1";
inline constexpr std::string_view kTuiDefaultDaemonSocketPath =
    "/run/dasall/daemon.sock";

enum class TuiIpcOperation {
  OpenSession,
  SubmitTurn,
  PollEvents,
  RouteCatalog,
  CloseSession,
};

enum class TuiIpcOutcome {
  Success,
  Failure,
};

struct TuiIpcTimeoutPolicy {
  std::uint32_t open_session_deadline_ms = 3000;
  std::uint32_t submit_turn_deadline_ms = 15000;
  std::uint32_t poll_events_deadline_ms = 3000;
  std::uint32_t route_catalog_deadline_ms = 3000;
  std::uint32_t close_session_deadline_ms = 3000;
};

struct TuiIpcControllerOptions {
  std::string socket_path = std::string(kTuiDefaultDaemonSocketPath);
  TuiIpcTimeoutPolicy timeout_policy;
};

struct TuiIpcOpenSessionPayload {
  std::optional<std::string> profile_id;
  std::optional<std::string> startup_mode_hint;
};

struct TuiIpcSubmitTurnPayload {
  std::string user_input;
  data::NextTurnPreference next_preference;
};

struct TuiIpcPollEventsPayload {
  std::optional<std::string> event_cursor;
};

struct TuiIpcRouteCatalogPayload {
  std::optional<std::string> profile_id;
  std::optional<std::string> selector_mode;
};

struct TuiIpcCloseSessionPayload {
  std::string close_reason;
};

using TuiIpcRequestPayload = std::variant<TuiIpcOpenSessionPayload,
                                          TuiIpcSubmitTurnPayload,
                                          TuiIpcPollEventsPayload,
                                          TuiIpcRouteCatalogPayload,
                                          TuiIpcCloseSessionPayload>;

struct TuiIpcPollEventsBatch {
  std::vector<data::TuiEventProjection> events;
  std::optional<std::string> next_cursor;
};

struct TuiIpcCloseSessionAck {
  bool closed = false;
};

using TuiIpcResponsePayload = std::variant<data::TuiSessionView,
                                           data::TuiTurnReceipt,
                                           TuiIpcPollEventsBatch,
                                           data::TuiRouteCatalogView,
                                           TuiIpcCloseSessionAck>;

struct TuiIpcRequestEnvelope {
  std::string schema_version = std::string(kTuiIpcSchemaVersion);
  TuiIpcOperation operation = TuiIpcOperation::OpenSession;
  std::string request_id;
  std::string trace_id;
  std::optional<std::string> session_id;
  std::uint32_t deadline_ms = 0;
  TuiIpcRequestPayload payload = TuiIpcOpenSessionPayload{};
};

struct TuiIpcResponseEnvelope {
  std::string schema_version = std::string(kTuiIpcSchemaVersion);
  TuiIpcOperation operation = TuiIpcOperation::OpenSession;
  std::string request_id;
  std::string trace_id;
  std::optional<std::string> session_id;
  TuiIpcOutcome outcome = TuiIpcOutcome::Success;
  std::optional<TuiIpcResponsePayload> payload;
  std::optional<std::string> reason_domain;
  std::optional<std::string> reason_code;
  std::optional<std::string> message;
  std::optional<bool> retryable;
  std::optional<std::string> error_ref;
  std::vector<std::pair<std::string, std::string>> metadata;

  [[nodiscard]] bool ok() const {
    return outcome == TuiIpcOutcome::Success && payload.has_value() &&
           !reason_domain.has_value() && !reason_code.has_value() &&
           !message.has_value() && !retryable.has_value() &&
           !error_ref.has_value();
  }

  [[nodiscard]] bool has_consistent_values() const {
    const bool reason_pair_is_consistent =
        reason_domain.has_value() == reason_code.has_value();
    if (!reason_pair_is_consistent) {
      return false;
    }

    if (outcome == TuiIpcOutcome::Success) {
      return payload.has_value() && !reason_domain.has_value() &&
             !reason_code.has_value() && !message.has_value() &&
             !retryable.has_value() && !error_ref.has_value();
    }

    return !payload.has_value() && reason_domain.has_value() &&
           reason_code.has_value();
  }
};

[[nodiscard]] constexpr std::string_view to_string(
    const TuiIpcOperation operation) noexcept {
  switch (operation) {
    case TuiIpcOperation::OpenSession:
      return "open_session";
    case TuiIpcOperation::SubmitTurn:
      return "submit_turn";
    case TuiIpcOperation::PollEvents:
      return "poll_events";
    case TuiIpcOperation::RouteCatalog:
      return "route_catalog";
    case TuiIpcOperation::CloseSession:
      return "close_session";
  }

  return "open_session";
}

[[nodiscard]] inline TuiIpcRequestEnvelope make_request_envelope(
    const data::TuiOpenSessionRequest& request,
    const std::uint32_t deadline_ms) {
  TuiIpcRequestEnvelope envelope;
  envelope.operation = TuiIpcOperation::OpenSession;
  envelope.request_id = request.request_id;
  envelope.trace_id = request.trace_id;
  envelope.deadline_ms = deadline_ms;
  envelope.payload = TuiIpcOpenSessionPayload{
      .profile_id = request.profile_id,
      .startup_mode_hint = request.startup_mode_hint,
  };
  return envelope;
}

[[nodiscard]] inline TuiIpcRequestEnvelope make_request_envelope(
    const data::TuiSubmitTurnRequest& request,
    const std::uint32_t deadline_ms) {
  TuiIpcRequestEnvelope envelope;
  envelope.operation = TuiIpcOperation::SubmitTurn;
  envelope.request_id = request.request_id;
  envelope.trace_id = request.trace_id;
  envelope.session_id = request.session_id;
  envelope.deadline_ms = deadline_ms;
  envelope.payload = TuiIpcSubmitTurnPayload{
      .user_input = request.user_input,
      .next_preference = request.next_preference,
  };
  return envelope;
}

[[nodiscard]] inline TuiIpcRequestEnvelope make_request_envelope(
    const data::TuiPollEventsRequest& request,
    const std::uint32_t deadline_ms) {
  TuiIpcRequestEnvelope envelope;
  envelope.operation = TuiIpcOperation::PollEvents;
  envelope.request_id = request.request_id;
  envelope.trace_id = request.trace_id;
  envelope.session_id = request.session_id;
  envelope.deadline_ms = deadline_ms;
  envelope.payload = TuiIpcPollEventsPayload{
      .event_cursor = request.event_cursor,
  };
  return envelope;
}

[[nodiscard]] inline TuiIpcRequestEnvelope make_request_envelope(
    const data::TuiRouteCatalogRequest& request,
    const std::uint32_t deadline_ms) {
  TuiIpcRequestEnvelope envelope;
  envelope.operation = TuiIpcOperation::RouteCatalog;
  envelope.request_id = request.request_id;
  envelope.trace_id = request.trace_id;
  envelope.session_id = request.session_id;
  envelope.deadline_ms = deadline_ms;
  envelope.payload = TuiIpcRouteCatalogPayload{
      .profile_id = request.profile_id,
      .selector_mode = request.selector_mode,
  };
  return envelope;
}

[[nodiscard]] inline TuiIpcRequestEnvelope make_request_envelope(
    const data::TuiCloseSessionRequest& request,
    const std::uint32_t deadline_ms) {
  TuiIpcRequestEnvelope envelope;
  envelope.operation = TuiIpcOperation::CloseSession;
  envelope.request_id = request.request_id;
  envelope.trace_id = request.trace_id;
  envelope.session_id = request.session_id;
  envelope.deadline_ms = deadline_ms;
  envelope.payload = TuiIpcCloseSessionPayload{
      .close_reason = request.close_reason,
  };
  return envelope;
}

class TuiIpcController {
 public:
  explicit TuiIpcController(
      TuiIpcControllerOptions options = TuiIpcControllerOptions{});

  data::TuiOpenSessionResult open_session(
      const data::TuiOpenSessionRequest& request);
  data::TuiSubmitTurnResult submit_turn(
      const data::TuiSubmitTurnRequest& request);
  data::TuiPollEventsResult poll_events(
      const data::TuiPollEventsRequest& request);
  data::TuiRouteCatalogResult query_route_catalog(
      const data::TuiRouteCatalogRequest& request);
  data::TuiCloseSessionResult close_session(
      const data::TuiCloseSessionRequest& request);

 private:
  TuiIpcControllerOptions options_;
};

}  // namespace dasall::tui::ipc