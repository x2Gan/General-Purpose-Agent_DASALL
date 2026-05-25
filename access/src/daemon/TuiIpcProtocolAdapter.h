#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "AccessTypes.h"
#include "IAccessGateway.h"
#include "IIPC.h"

namespace dasall::access::daemon {

inline constexpr std::string_view kTuiIpcSchemaVersion = "tui_ipc.v1";

enum class TuiIpcOperation {
  OpenSession,
  SubmitTurn,
  PollEvents,
  RouteCatalog,
  CloseSession,
  Unknown,
};

enum class TuiIpcOutcome {
  Success,
  Failure,
};

enum class TuiIpcDecodeError {
  None,
  Malformed,
  SchemaMismatch,
  UnknownOperation,
  ValidationRejected,
};

enum class TuiRoutePreferenceMode {
  Auto,
  PreferDepth,
  PinModel,
};

struct TuiIpcNextTurnPreference {
  TuiRoutePreferenceMode mode = TuiRoutePreferenceMode::Auto;
  std::optional<std::string> preferred_depth_tier;
  std::optional<std::string> pinned_provider_id;
  std::optional<std::string> pinned_model_id;
  std::string user_visible_summary;
  std::string source;
  bool applies_to_next_turn_only = true;
};

struct TuiIpcOpenSessionPayload {
  std::optional<std::string> profile_id;
  std::optional<std::string> startup_mode_hint;
};

struct TuiIpcSubmitTurnPayload {
  std::string user_input;
  TuiIpcNextTurnPreference next_preference;
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

struct TuiIpcSessionView {
  std::string session_id;
  std::string profile_id;
  std::string daemon_readiness;
  std::string startup_mode;
  std::string started_at;
};

struct TuiIpcTurnReceipt {
  std::string request_id;
  std::string trace_id;
  std::string session_id;
  std::string disposition;
  std::string receipt_ref;
  std::string submitted_at;
  std::string summary_text;
  std::optional<std::string> response_text;
  std::optional<std::string> reason_code;
};

struct TuiIpcStatusProjection {
  std::string stage;
  std::string current_tool;
  std::string pending_interaction;
  std::string budget_summary;
  std::string recovery_summary;
  std::string health_summary;
  std::string safe_mode_summary;
};

struct TuiIpcToolSummary {
  std::string tool_name;
  std::string risk_summary;
  std::string observation_summary;
  std::optional<int> latency_ms;
  std::vector<std::string> badges;
};

struct TuiIpcModelRouteProjection {
  std::string current_provider_id;
  std::string current_model_id;
  std::string current_depth_tier;
  std::string verification_state;
  std::string health;
  bool profile_allowlisted = true;
  std::vector<std::string> disabled_reasons;
  TuiIpcNextTurnPreference next_preference;
};

struct TuiIpcRouteCatalogEntry {
  std::string provider_id;
  std::string model_id;
  std::string depth_tier;
  std::string verification_state;
  std::string health;
  bool profile_allowlisted = true;
  bool selectable = true;
  std::vector<std::string> disabled_reasons;
};

struct TuiIpcRouteCatalogView {
  TuiIpcModelRouteProjection current_route;
  std::vector<TuiIpcRouteCatalogEntry> candidate_routes;
  std::vector<std::string> disabled_reasons;
};

struct TuiIpcEventProjection {
  std::string event_cursor;
  std::string event_kind;
  std::string session_id;
  std::string timestamp;
  std::optional<TuiIpcStatusProjection> status_delta;
  std::optional<TuiIpcTurnReceipt> turn_receipt;
  std::optional<TuiIpcToolSummary> tool_summary;
  std::optional<std::string> banner_reason;
};

struct TuiIpcPollEventsBatch {
  std::vector<TuiIpcEventProjection> events;
  std::optional<std::string> next_cursor;
};

struct TuiIpcCloseSessionAck {
  bool closed = false;
};

using TuiIpcResponsePayload = std::variant<TuiIpcSessionView,
                                           TuiIpcTurnReceipt,
                                           TuiIpcPollEventsBatch,
                                           TuiIpcRouteCatalogView,
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

struct DecodedTuiIpcRequest {
  std::optional<TuiIpcRequestEnvelope> envelope;
  TuiIpcDecodeError error = TuiIpcDecodeError::None;
  TuiIpcOperation parsed_operation = TuiIpcOperation::Unknown;
  std::string request_id;
  std::string trace_id;
  std::optional<std::string> session_id;

  [[nodiscard]] bool ok() const {
    return envelope.has_value() && error == TuiIpcDecodeError::None;
  }
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

struct TuiIpcSessionState {
  TuiIpcSessionView session;
  TuiIpcRouteCatalogView route_catalog;
  std::uint64_t next_event_cursor = 0U;
  std::vector<TuiIpcEventProjection> pending_events;
};

struct TuiIpcSessionStore {
  std::mutex mutex;
  std::uint64_t next_session_id = 0U;
  std::map<std::string, TuiIpcSessionState> sessions;
};

class TuiIpcProtocolAdapter {
 public:
  explicit TuiIpcProtocolAdapter(std::shared_ptr<dasall::platform::IIPC> ipc);

  void set_active_channel(dasall::platform::IpcChannelHandle channel,
                          std::vector<std::uint8_t> payload);

  [[nodiscard]] bool payload_looks_like_tui_ipc() const;
  [[nodiscard]] DecodedTuiIpcRequest decode_tui_ipc_request() const;

  [[nodiscard]] TuiIpcResponseEnvelope dispatch_tui_ipc_operation(
      const DecodedTuiIpcRequest& decoded,
      dasall::access::IAccessGateway& gateway,
      TuiIpcSessionStore& session_store,
      std::string_view peer_ref,
      std::string_view effective_profile_id) const;

  [[nodiscard]] bool encode_tui_ipc_response(
      const TuiIpcResponseEnvelope& envelope) const;

 private:
  std::shared_ptr<dasall::platform::IIPC> ipc_;
  dasall::platform::IpcChannelHandle active_channel_;
  std::vector<std::uint8_t> active_payload_;
};

}  // namespace dasall::access::daemon