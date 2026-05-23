#include <cstdint>
#include <exception>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "PlatformError.h"
#include "data/DaemonTuiDataSource.h"
#include "ipc/TuiIpcControllerTestHooks.h"
#include "support/TestAssertions.h"

namespace {

using dasall::tests::support::assert_equal;
using dasall::tests::support::assert_true;
using dasall::tui::data::DaemonTuiDataSource;
using dasall::tui::data::TuiEventProjection;
using dasall::tui::data::TuiPollEventsRequest;
using dasall::tui::data::TuiStatusProjection;
using dasall::tui::data::TuiToolSummaryView;
using dasall::tui::ipc::TuiIpcControllerOptions;
using dasall::tui::ipc::TuiIpcOperation;
using dasall::tui::ipc::TuiIpcOutcome;
using dasall::tui::ipc::TuiIpcPollEventsBatch;
using dasall::tui::ipc::TuiIpcResponseEnvelope;

[[nodiscard]] dasall::platform::PlatformError make_platform_error(
    const std::string& detail) {
  return dasall::platform::PlatformError{
      .code = dasall::platform::PlatformErrorCode::InvalidArgument,
      .category = dasall::platform::PlatformErrorCategory::Validation,
      .retryable_hint = false,
      .syscall_name = {},
      .errno_value = std::nullopt,
      .detail = detail,
  };
}

dasall::platform::IpcPayload make_payload(std::string_view text) {
  dasall::platform::IpcPayload payload;
  payload.reserve(text.size());
  for (const char ch : text) {
    payload.push_back(static_cast<std::uint8_t>(ch));
  }
  return payload;
}

class ScriptedIpc final : public dasall::platform::IIPC {
 public:
  std::vector<std::string> response_texts;
  std::vector<std::string> sent_payloads;
  std::size_t receive_index = 0;

  dasall::platform::PlatformResult<dasall::platform::IpcListenerHandle> listen(
      const dasall::platform::IpcEndpoint&,
      const dasall::platform::ListenOptions&) override {
    return dasall::platform::PlatformResult<
        dasall::platform::IpcListenerHandle>::failure(
      make_platform_error("listen unused in status projection contract tests"));
  }

  dasall::platform::PlatformResult<dasall::platform::IpcChannelHandle> accept(
      const dasall::platform::IpcListenerHandle&,
      std::int32_t) override {
    return dasall::platform::PlatformResult<
        dasall::platform::IpcChannelHandle>::failure(
      make_platform_error("accept unused in status projection contract tests"));
  }

  dasall::platform::PlatformResult<dasall::platform::IpcChannelHandle> connect(
      const dasall::platform::IpcEndpoint&,
      std::int32_t) override {
    return dasall::platform::PlatformResult<
        dasall::platform::IpcChannelHandle>::success(
        dasall::platform::IpcChannelHandle{.native_fd = 125U});
  }

  dasall::platform::PlatformResult<dasall::platform::IpcSendResult> send(
      const dasall::platform::IpcChannelHandle&,
      const dasall::platform::IpcPayload& payload) override {
    sent_payloads.emplace_back(reinterpret_cast<const char*>(payload.data()),
                               payload.size());
    return dasall::platform::PlatformResult<
        dasall::platform::IpcSendResult>::success(
        dasall::platform::IpcSendResult{.bytes_sent = payload.size()});
  }

  dasall::platform::PlatformResult<dasall::platform::IpcReceiveResult> receive(
      const dasall::platform::IpcChannelHandle&,
      std::int32_t) override {
    dasall::platform::IpcReceiveResult result;
    if (receive_index < response_texts.size()) {
      result.data = make_payload(response_texts.at(receive_index++));
    }
    return dasall::platform::PlatformResult<
        dasall::platform::IpcReceiveResult>::success(std::move(result));
  }

  dasall::platform::PlatformResult<dasall::platform::PeerIdentitySnapshot>
  describe_peer(const dasall::platform::IpcChannelHandle&) override {
    return dasall::platform::PlatformResult<
        dasall::platform::PeerIdentitySnapshot>::failure(
      make_platform_error("describe_peer unused in status projection contract tests"));
  }

  dasall::platform::PlatformResult<bool> close(
      const dasall::platform::IpcChannelHandle&) override {
    return dasall::platform::PlatformResult<bool>::success(true);
  }
};

[[nodiscard]] TuiIpcControllerOptions make_options() {
  TuiIpcControllerOptions options;
  options.socket_path = "/tmp/dasall-tui-status-projection.sock";
  return options;
}

[[nodiscard]] TuiPollEventsRequest make_poll_request() {
  return TuiPollEventsRequest{.session_id = "session-025",
                              .event_cursor = std::string("cursor-024"),
                              .request_id = "req-poll-025",
                              .trace_id = "trace-poll-025"};
}

void status_projection_contract_preserves_poll_refresh_fields() {
  auto ipc = std::make_shared<ScriptedIpc>();

  TuiStatusProjection status;
  status.stage = "tool_calling";
  status.current_tool = "tool.search";
  status.pending_interaction = "confirm external tool";
  status.budget_summary = "Budget 58% remaining";
  status.recovery_summary = "Accepted safe replay window after tool timeout.";
  status.health_summary = "degraded";
  status.safe_mode_summary = "guarded";

  TuiToolSummaryView tool_summary;
  tool_summary.tool_name = "tool.search";
  tool_summary.risk_summary = "elevated network latency";
  tool_summary.observation_summary =
      "Collected three planning references for the next turn.";
  tool_summary.latency_ms = 237;
  tool_summary.badges = {"read_only", "slow_path"};

  TuiEventProjection event;
  event.event_cursor = "cursor-025";
  event.event_kind = "status.updated";
  event.session_id = "session-025";
  event.timestamp = "2026-05-23T16:25:00Z";
  event.status_delta = status;
  event.tool_summary = tool_summary;
  event.banner_reason = std::string("operator attention required");

  TuiIpcResponseEnvelope response;
  response.operation = TuiIpcOperation::PollEvents;
  response.request_id = "req-poll-025";
  response.trace_id = "trace-poll-025";
  response.session_id = "session-025";
  response.outcome = TuiIpcOutcome::Success;
  response.payload = TuiIpcPollEventsBatch{
      .events = {event},
      .next_cursor = std::string("cursor-025"),
  };

  ipc->response_texts.push_back(
      dasall::tui::ipc::test::encode_response_envelope_for_test(response));

  const dasall::tui::ipc::test::ScopedIpcOverride override(ipc);
  DaemonTuiDataSource data_source(make_options());

  const auto result = data_source.poll_events(make_poll_request());

  assert_true(result.ok() && result.has_consistent_values(),
              "poll_events should keep status projection refresh machine-readable");
  assert_equal(std::string("cursor-025"),
               result.next_cursor.value_or(std::string()),
               "poll_events should preserve the daemon next cursor");
  assert_equal(1,
               static_cast<int>(result.events.size()),
               "poll_events should surface one status refresh event");
  assert_true(result.events.front().status_delta.has_value(),
              "status refresh events should keep the status projection payload");
  assert_true(result.events.front().tool_summary.has_value(),
              "status refresh events should keep the tool summary payload");

  const auto& refreshed_status = *result.events.front().status_delta;
  assert_equal(std::string("tool_calling"),
               refreshed_status.stage,
               "stage should survive daemon poll decoding unchanged");
  assert_equal(std::string("tool.search"),
               refreshed_status.current_tool,
               "current tool should survive daemon poll decoding unchanged");
  assert_equal(std::string("confirm external tool"),
               refreshed_status.pending_interaction,
               "pending interaction should survive daemon poll decoding unchanged");
  assert_equal(std::string("Budget 58% remaining"),
               refreshed_status.budget_summary,
               "budget summary should survive daemon poll decoding unchanged");
  assert_equal(std::string("Accepted safe replay window after tool timeout."),
               refreshed_status.recovery_summary,
               "recovery summary should survive daemon poll decoding unchanged");
  assert_equal(std::string("degraded"),
               refreshed_status.health_summary,
               "health summary should survive daemon poll decoding unchanged");
  assert_equal(std::string("guarded"),
               refreshed_status.safe_mode_summary,
               "safe mode summary should survive daemon poll decoding unchanged");

  const auto& refreshed_tool_summary = *result.events.front().tool_summary;
  assert_equal(std::string("tool.search"),
               refreshed_tool_summary.tool_name,
               "tool summary should preserve the active tool label");
  assert_equal(std::string("elevated network latency"),
               refreshed_tool_summary.risk_summary,
               "tool summary should preserve the risk summary");
  assert_equal(std::string("Collected three planning references for the next turn."),
               refreshed_tool_summary.observation_summary,
               "tool summary should preserve the observation summary");
  assert_equal(237,
               refreshed_tool_summary.latency_ms.value_or(-1),
               "tool summary should preserve latency metadata");
  assert_equal(2,
               static_cast<int>(refreshed_tool_summary.badges.size()),
               "tool summary should preserve badge count");
}

void status_projection_contract_keeps_user_surface_projected() {
  auto ipc = std::make_shared<ScriptedIpc>();

  TuiIpcResponseEnvelope response;
  response.operation = TuiIpcOperation::PollEvents;
  response.request_id = "req-poll-025";
  response.trace_id = "trace-poll-025";
  response.session_id = "session-025";
  response.outcome = TuiIpcOutcome::Success;
  response.payload = TuiIpcPollEventsBatch{.events = {},
                                           .next_cursor = std::string("cursor-026")};
  ipc->response_texts.push_back(
      dasall::tui::ipc::test::encode_response_envelope_for_test(response));

  const dasall::tui::ipc::test::ScopedIpcOverride override(ipc);
  DaemonTuiDataSource data_source(make_options());
  const auto result = data_source.poll_events(make_poll_request());

  assert_true(result.ok() && result.has_consistent_values(),
              "empty status refresh batches should remain valid results");
  assert_true(result.events.empty(),
              "empty batches should stay empty instead of synthesizing internal dump events");
}

}  // namespace

int main() {
  try {
    status_projection_contract_preserves_poll_refresh_fields();
    status_projection_contract_keeps_user_surface_projected();
  } catch (const std::exception& exception) {
    std::cerr << "[TuiStatusProjectionContractTest] FAILED: " << exception.what()
              << '\n';
    return 1;
  }

  return 0;
}
