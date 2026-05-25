#include <exception>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <unistd.h>

#include "AccessGatewayFactory.h"
#include "AsyncTaskRegistry.h"
#include "DaemonIntegrationHarness.h"
#include "data/DaemonTuiDataSource.h"
#include "support/TestAssertions.h"

namespace {

using namespace std::chrono_literals;

using dasall::access::AccessDisposition;
using dasall::access::AsyncTaskRegistry;
using dasall::access::DaemonAccessPipelineOptions;
using dasall::access::PublishEnvelope;
using dasall::access::RuntimeDispatchRequest;
using dasall::access::RuntimeDispatchResult;
using dasall::contracts::AgentResult;
using dasall::tests::integration::access_support::DaemonIntegrationHarness;
using dasall::tests::support::assert_equal;
using dasall::tests::support::assert_true;
using dasall::tui::data::DaemonTuiDataSource;
using dasall::tui::data::TuiCloseSessionRequest;
using dasall::tui::data::TuiCloseSessionResult;
using dasall::tui::data::TuiOpenSessionRequest;
using dasall::tui::data::TuiOpenSessionResult;
using dasall::tui::data::TuiPollEventsRequest;
using dasall::tui::data::TuiPollEventsResult;
using dasall::tui::data::TuiRouteCatalogRequest;
using dasall::tui::data::TuiRouteCatalogResult;
using dasall::tui::data::TuiSubmitTurnRequest;
using dasall::tui::data::TuiSubmitTurnResult;
using dasall::tui::ipc::TuiIpcControllerOptions;

[[nodiscard]] std::string describe_issue(
    const std::optional<dasall::tui::data::TuiDataSourceIssue>& issue) {
  if (!issue.has_value()) {
    return "issue=<none>";
  }

  return "issue=" + issue->reason_domain + "/" + issue->reason_code +
         ": " + issue->message;
}

[[nodiscard]] std::string current_local_actor_ref() {
  return "local://uid/" + std::to_string(::getuid());
}

[[nodiscard]] DaemonAccessPipelineOptions make_daemon_options(
    DaemonAccessPipelineOptions::RuntimeDispatchBackend runtime_dispatch_backend = {}) {
  DaemonAccessPipelineOptions options;
  options.bootstrap_config.allowed_protocols = {"ipc_uds", "tui_ipc.v1"};
  options.auth_view.trusted_local_subjects = {current_local_actor_ref()};
  options.daemon_profile_id = "daemon.tui.e2e";
  options.async_task_registry =
      std::make_shared<AsyncTaskRegistry>("tui-daemon-e2e-secret", 30s);
  options.runtime_dispatch_backend = std::move(runtime_dispatch_backend);
  if (!options.runtime_dispatch_backend) {
    options.runtime_dispatch_backend = [](const RuntimeDispatchRequest& request) {
    RuntimeDispatchResult result;
    result.disposition = AccessDisposition::AcceptedAsync;
    result.receipt_ref = std::string("receipt:") + request.packet.packet_id;
    return result;
    };
  }
  return options;
}

struct FormalTuiRoundtripArtifacts {
  TuiOpenSessionResult open_session;
  TuiRouteCatalogResult route_catalog;
  TuiSubmitTurnResult submit_turn;
  TuiPollEventsResult poll_events;
  TuiCloseSessionResult close_session;
};

class TuiDaemonBackedE2EHarness {
 public:
  explicit TuiDaemonBackedE2EHarness(
    DaemonAccessPipelineOptions options = make_daemon_options())
    : daemon_(std::move(options)),
  data_source_(make_controller_options(daemon_.socket_path())) {}

  [[nodiscard]] FormalTuiRoundtripArtifacts run_formal_tui_roundtrip() {
    FormalTuiRoundtripArtifacts artifacts;

    artifacts.open_session = data_source_.open_session(TuiOpenSessionRequest{
        .profile_id = std::optional<std::string>("desktop_full"),
        .startup_mode_hint = std::optional<std::string>("full"),
        .request_id = "tui-open-041",
        .trace_id = "trace-tui-open-041",
    });
    assert_true(artifacts.open_session.ok() && artifacts.open_session.session.has_value(),
                "daemon-backed TUI E2E should open a real foreground session");

    const std::string session_id = artifacts.open_session.session->session_id;

    artifacts.route_catalog = data_source_.route_catalog(TuiRouteCatalogRequest{
        .session_id = std::optional<std::string>(session_id),
        .profile_id = std::optional<std::string>("desktop_full"),
        .selector_mode = std::optional<std::string>("next_turn"),
        .request_id = "tui-route-041",
        .trace_id = "trace-tui-route-041",
    });
    assert_true(artifacts.route_catalog.ok() &&
                    artifacts.route_catalog.route_catalog.has_value(),
                "daemon-backed TUI E2E should load route catalog over the real daemon socket");

    artifacts.submit_turn = data_source_.submit_turn(TuiSubmitTurnRequest{
        .session_id = session_id,
        .user_input = "queue daemon-backed tui roundtrip",
        .next_preference = artifacts.route_catalog.route_catalog->current_route.next_preference,
        .request_id = "tui-submit-041",
        .trace_id = "trace-tui-submit-041",
    });
    assert_true(artifacts.submit_turn.ok() && artifacts.submit_turn.receipt.has_value(),
          "daemon-backed TUI E2E should submit through the real daemon-backed data source; " +
            describe_issue(artifacts.submit_turn.issue));

    artifacts.poll_events = data_source_.poll_events(TuiPollEventsRequest{
        .session_id = session_id,
        .event_cursor = std::nullopt,
        .request_id = "tui-poll-041",
        .trace_id = "trace-tui-poll-041",
    });
    assert_true(artifacts.poll_events.ok(),
                "daemon-backed TUI E2E should poll events over the real daemon socket");

    artifacts.close_session = data_source_.close_session(TuiCloseSessionRequest{
        .session_id = session_id,
        .close_reason = "/exit",
        .request_id = "tui-close-041",
        .trace_id = "trace-tui-close-041",
    });
    assert_true(artifacts.close_session.ok(),
                "daemon-backed TUI E2E should close the foreground session over the real daemon socket");

    return artifacts;
  }

  void stop() {
    daemon_.stop();
  }

  [[nodiscard]] bool daemon_stopped_cleanly() const {
    return daemon_.daemon_stopped_cleanly();
  }

 private:
  [[nodiscard]] static TuiIpcControllerOptions make_controller_options(
      const std::string& socket_path) {
    TuiIpcControllerOptions options;
    options.socket_path = socket_path;
    return options;
  }

  DaemonIntegrationHarness daemon_;
  DaemonTuiDataSource data_source_;
};

void formal_tui_roundtrip_uses_real_daemon_socket_and_projects_receipt_event() {
  TuiDaemonBackedE2EHarness harness;
  const FormalTuiRoundtripArtifacts artifacts = harness.run_formal_tui_roundtrip();

  assert_equal(std::string("desktop_full"),
               artifacts.open_session.session->profile_id,
               "daemon-backed TUI E2E should preserve the requested profile id in open_session");
  assert_equal(std::string("ready"),
               artifacts.open_session.session->daemon_readiness,
               "daemon-backed TUI E2E should surface ready daemon_readiness after opening the session");
  assert_equal(std::string("daemon-local"),
               artifacts.route_catalog.route_catalog->current_route.current_provider_id,
               "daemon-backed TUI E2E should return the daemon-local current provider projection");
  assert_equal(std::string("dasall-core"),
               artifacts.route_catalog.route_catalog->current_route.current_model_id,
               "daemon-backed TUI E2E should return the daemon-backed current model projection");
  assert_equal(2, static_cast<int>(artifacts.route_catalog.route_catalog->candidate_routes.size()),
               "daemon-backed TUI E2E should surface the stable two-entry route catalog baseline");
  assert_true(artifacts.route_catalog.route_catalog->disabled_reasons.empty(),
              "daemon-backed TUI E2E should keep route catalog enabled when effective profile id is present");

  assert_equal(std::string("accepted_async"),
               artifacts.submit_turn.receipt->disposition,
               "daemon-backed TUI E2E should preserve accepted_async submit disposition");
  assert_equal(std::string("receipt:tui-submit-041"),
               artifacts.submit_turn.receipt->receipt_ref,
               "daemon-backed TUI E2E should preserve the daemon access receipt reference through submit_turn");
  assert_equal(std::string("queued for daemon-backed execution"),
               artifacts.submit_turn.receipt->summary_text,
               "daemon-backed TUI E2E should surface the stable queued summary text on submit receipt");

  assert_equal(1, static_cast<int>(artifacts.poll_events.events.size()),
               "daemon-backed TUI E2E should return a single queued submit receipt event on the first poll");
  assert_true(artifacts.poll_events.next_cursor.has_value() &&
                  !artifacts.poll_events.next_cursor->empty(),
              "daemon-backed TUI E2E should return a next_cursor after delivering the queued event batch");
  const auto& event = artifacts.poll_events.events.front();
  assert_equal(std::string("turn.receipt"),
               event.event_kind,
               "daemon-backed TUI E2E should surface the stable turn.receipt event kind");
  assert_true(event.turn_receipt.has_value(),
              "daemon-backed TUI E2E should embed the submit receipt inside the polled event batch");
  assert_equal(std::string("accepted_async"),
               event.turn_receipt->disposition,
               "daemon-backed TUI E2E should preserve receipt disposition inside poll_events");
  assert_true(event.status_delta.has_value(),
              "daemon-backed TUI E2E should include status projection alongside the submit receipt event");
  assert_equal(std::string("accepted_async"),
               event.status_delta->stage,
               "daemon-backed TUI E2E should project accepted_async into the event status stage");
  assert_equal(std::string("access.submit"),
               event.status_delta->current_tool,
               "daemon-backed TUI E2E should project access.submit as the current tool");
  assert_true(event.tool_summary.has_value(),
              "daemon-backed TUI E2E should include tool summary metadata inside the event batch");
  assert_equal(std::string("access.submit"),
               event.tool_summary->tool_name,
               "daemon-backed TUI E2E should preserve access.submit tool summary name");
  assert_equal(std::string("queued for daemon-backed execution"),
               event.tool_summary->observation_summary,
               "daemon-backed TUI E2E should preserve the queued observation summary in tool summary");
  assert_true(!event.tool_summary->badges.empty() &&
                  event.tool_summary->badges.front() == "local_daemon",
              "daemon-backed TUI E2E should tag the event tool summary with the local_daemon badge");

  assert_true(artifacts.close_session.closed,
              "daemon-backed TUI E2E should acknowledge close_session after the roundtrip");

  harness.stop();
  assert_true(harness.daemon_stopped_cleanly(),
              "daemon-backed TUI E2E should stop the in-process daemon cleanly after the roundtrip");
}

void formal_tui_roundtrip_projects_completed_response_text() {
  TuiDaemonBackedE2EHarness harness(make_daemon_options(
      [](const RuntimeDispatchRequest& request) {
        RuntimeDispatchResult result;
        result.disposition = AccessDisposition::Completed;

        AgentResult agent_result;
        agent_result.response_text = std::string("daemon-backed final answer");

        PublishEnvelope envelope;
        envelope.result_id = std::string("result:") + request.packet.packet_id;
        envelope.agent_result = std::move(agent_result);
        result.publish_envelope = std::move(envelope);
        return result;
      }));
  const FormalTuiRoundtripArtifacts artifacts = harness.run_formal_tui_roundtrip();

  assert_equal(std::string("completed"),
               artifacts.submit_turn.receipt->disposition,
               "completed daemon-backed TUI E2E should preserve completed submit disposition");
  assert_true(artifacts.submit_turn.receipt->response_text.has_value(),
              "completed daemon-backed TUI E2E should surface response_text on submit receipt");
  assert_equal(std::string("daemon-backed final answer"),
               *artifacts.submit_turn.receipt->response_text,
               "completed daemon-backed TUI E2E should preserve response_text on submit receipt");

  assert_equal(1, static_cast<int>(artifacts.poll_events.events.size()),
               "completed daemon-backed TUI E2E should return a single completed receipt event on first poll");
  const auto& event = artifacts.poll_events.events.front();
  assert_true(event.turn_receipt.has_value(),
              "completed daemon-backed TUI E2E should embed the completed receipt inside the event batch");
  assert_true(event.turn_receipt->response_text.has_value(),
              "completed daemon-backed TUI E2E should preserve response_text inside poll_events");
  assert_equal(std::string("daemon-backed final answer"),
               *event.turn_receipt->response_text,
               "completed daemon-backed TUI E2E should preserve response_text value inside poll_events");
  assert_true(event.status_delta.has_value(),
              "completed daemon-backed TUI E2E should include terminal status projection");
  assert_equal(std::string("completed"),
               event.status_delta->stage,
               "completed daemon-backed TUI E2E should project completed into the event status stage");

  harness.stop();
  assert_true(harness.daemon_stopped_cleanly(),
              "completed daemon-backed TUI E2E should stop the in-process daemon cleanly");
}

}  // namespace

int main() {
  try {
    formal_tui_roundtrip_uses_real_daemon_socket_and_projects_receipt_event();
    formal_tui_roundtrip_projects_completed_response_text();
  } catch (const std::exception& exception) {
    std::cerr << exception.what() << '\n';
    return 1;
  }

  return 0;
}