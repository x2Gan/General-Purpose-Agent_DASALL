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
#include "ipc/TuiIpcController.h"
#include "ipc/TuiIpcControllerTestHooks.h"
#include "support/TestAssertions.h"
#include "view/TuiModelSelector.h"

namespace {

using dasall::tests::support::assert_equal;
using dasall::tests::support::assert_true;
using dasall::tui::data::DaemonTuiDataSource;
using dasall::tui::data::NextTurnPreference;
using dasall::tui::data::TuiModelRouteProjection;
using dasall::tui::data::TuiRouteCatalogEntry;
using dasall::tui::data::TuiRouteCatalogRequest;
using dasall::tui::data::TuiRouteCatalogView;
using dasall::tui::data::TuiRoutePreferenceMode;
using dasall::tui::data::TuiSubmitTurnRequest;
using dasall::tui::data::TuiTurnReceipt;
using dasall::tui::ipc::TuiIpcControllerOptions;
using dasall::tui::ipc::TuiIpcOperation;
using dasall::tui::ipc::TuiIpcOutcome;
using dasall::tui::ipc::TuiIpcRequestEnvelope;
using dasall::tui::ipc::TuiIpcResponseEnvelope;
using dasall::tui::ipc::TuiIpcSubmitTurnPayload;
using dasall::tui::view::TuiModelSelector;

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
        make_platform_error("listen unused in next preference integration tests"));
  }

  dasall::platform::PlatformResult<dasall::platform::IpcChannelHandle> accept(
      const dasall::platform::IpcListenerHandle&,
      std::int32_t) override {
    return dasall::platform::PlatformResult<
        dasall::platform::IpcChannelHandle>::failure(
        make_platform_error("accept unused in next preference integration tests"));
  }

  dasall::platform::PlatformResult<dasall::platform::IpcChannelHandle> connect(
      const dasall::platform::IpcEndpoint&,
      std::int32_t) override {
    return dasall::platform::PlatformResult<
        dasall::platform::IpcChannelHandle>::success(
        dasall::platform::IpcChannelHandle{.native_fd = 129U});
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
        make_platform_error(
            "describe_peer unused in next preference integration tests"));
  }

  dasall::platform::PlatformResult<bool> close(
      const dasall::platform::IpcChannelHandle&) override {
    return dasall::platform::PlatformResult<bool>::success(true);
  }
};

[[nodiscard]] TuiIpcControllerOptions make_options() {
  TuiIpcControllerOptions options;
  options.socket_path = "/tmp/dasall-tui-next-preference.sock";
  return options;
}

[[nodiscard]] std::optional<TuiIpcRequestEnvelope> decode_sent_envelope(
    const std::vector<std::string>& payloads,
    const std::size_t index) {
  if (index >= payloads.size()) {
    return std::nullopt;
  }

  return dasall::tui::ipc::test::decode_request_envelope_for_test(payloads.at(index));
}

[[nodiscard]] TuiRouteCatalogRequest make_route_request(std::string session_id,
                                                        std::string request_id,
                                                        std::string trace_id) {
  return TuiRouteCatalogRequest{
      .session_id = std::move(session_id),
      .profile_id = std::optional<std::string>("desktop_full"),
      .selector_mode = std::optional<std::string>("next_turn"),
      .request_id = std::move(request_id),
      .trace_id = std::move(trace_id),
  };
}

[[nodiscard]] TuiTurnReceipt make_receipt(std::string request_id,
                                          std::string trace_id,
                                          std::string reason_code = {}) {
  TuiTurnReceipt receipt;
  receipt.request_id = std::move(request_id);
  receipt.trace_id = std::move(trace_id);
  receipt.session_id = "session-029";
  receipt.disposition = "accepted_async";
  receipt.receipt_ref = "receipt-029";
  receipt.submitted_at = "2026-05-24T09:29:01Z";
  receipt.summary_text = "next preference accepted for route evaluation";
  if (!reason_code.empty()) {
    receipt.reason_code = std::move(reason_code);
  }
  return receipt;
}

[[nodiscard]] TuiRouteCatalogEntry make_entry(std::string provider_id,
                                              std::string model_id,
                                              std::string depth_tier,
                                              std::string verification_state,
                                              std::string health,
                                              const bool selectable,
                                              std::vector<std::string> disabled_reasons = {}) {
  TuiRouteCatalogEntry entry;
  entry.provider_id = std::move(provider_id);
  entry.model_id = std::move(model_id);
  entry.depth_tier = std::move(depth_tier);
  entry.verification_state = std::move(verification_state);
  entry.health = std::move(health);
  entry.profile_allowlisted = disabled_reasons.empty();
  entry.selectable = selectable;
  entry.disabled_reasons = std::move(disabled_reasons);
  return entry;
}

[[nodiscard]] TuiRouteCatalogView make_initial_route_catalog() {
  TuiRouteCatalogView route_catalog;
  route_catalog.current_route = TuiModelRouteProjection{
      .current_provider_id = "deepseek-prod",
      .current_model_id = "deepseek-chat",
      .current_depth_tier = "standard",
      .verification_state = "verified",
      .health = "healthy",
      .profile_allowlisted = true,
      .disabled_reasons = {},
      .next_preference = NextTurnPreference{
          .mode = TuiRoutePreferenceMode::Auto,
          .preferred_depth_tier = std::nullopt,
          .pinned_provider_id = std::nullopt,
          .pinned_model_id = std::nullopt,
          .user_visible_summary = "auto",
          .source = "daemon",
          .applies_to_next_turn_only = true,
      },
  };

  route_catalog.candidate_routes.push_back(make_entry(
      "deepseek-prod", "deepseek-chat", "standard", "verified", "healthy", true));
  route_catalog.candidate_routes.push_back(make_entry(
      "deepseek-prod", "deepseek-reasoner", "deep", "verified", "healthy", true));
  route_catalog.candidate_routes.push_back(make_entry(
      "provider-local",
      "deep-reasoner",
      "deep",
      "verified",
      "healthy",
      true));
  return route_catalog;
}

[[nodiscard]] TuiRouteCatalogView make_auto_echo_catalog() {
  return make_initial_route_catalog();
}

[[nodiscard]] TuiRouteCatalogView make_prefer_depth_echo_catalog() {
  TuiRouteCatalogView route_catalog;
  route_catalog.current_route = TuiModelRouteProjection{
      .current_provider_id = "deepseek-prod",
      .current_model_id = "deepseek-chat",
      .current_depth_tier = "standard",
      .verification_state = "verified",
      .health = "healthy",
      .profile_allowlisted = true,
      .disabled_reasons = {},
      .next_preference = NextTurnPreference{
          .mode = TuiRoutePreferenceMode::PreferDepth,
          .preferred_depth_tier = std::optional<std::string>("deep"),
          .pinned_provider_id = std::nullopt,
          .pinned_model_id = std::nullopt,
          .user_visible_summary = "prefer depth: deep",
          .source = "tui_model_selector",
          .applies_to_next_turn_only = true,
      },
  };
  route_catalog.candidate_routes = make_initial_route_catalog().candidate_routes;
  return route_catalog;
}

[[nodiscard]] TuiRouteCatalogView make_pin_fail_closed_echo_catalog() {
  TuiRouteCatalogView route_catalog;
  route_catalog.current_route = TuiModelRouteProjection{
      .current_provider_id = "deepseek-prod",
      .current_model_id = "deepseek-chat",
      .current_depth_tier = "standard",
      .verification_state = "verified",
      .health = "healthy",
      .profile_allowlisted = true,
      .disabled_reasons = {"route_unavailable", "provider_unhealthy"},
      .next_preference = NextTurnPreference{
          .mode = TuiRoutePreferenceMode::PinModel,
          .preferred_depth_tier = std::nullopt,
          .pinned_provider_id = std::optional<std::string>("provider-local"),
          .pinned_model_id = std::optional<std::string>("deep-reasoner"),
          .user_visible_summary = "pin model: provider-local/deep-reasoner",
          .source = "tui_model_selector",
          .applies_to_next_turn_only = true,
      },
  };

  route_catalog.candidate_routes.push_back(make_entry(
      "deepseek-prod", "deepseek-chat", "standard", "verified", "healthy", true));
  route_catalog.candidate_routes.push_back(make_entry(
      "provider-local",
      "deep-reasoner",
      "deep",
      "verified",
      "unhealthy",
      false,
      {"route_unavailable", "provider_unhealthy"}));
  return route_catalog;
}

[[nodiscard]] std::string encode_response(const TuiIpcResponseEnvelope& envelope) {
  return dasall::tui::ipc::test::encode_response_envelope_for_test(envelope);
}

void assert_submit_payload_matches_preference(
    const std::optional<TuiIpcRequestEnvelope>& request_envelope,
    const NextTurnPreference& expected_preference) {
  assert_true(request_envelope.has_value() &&
                  request_envelope->operation == TuiIpcOperation::SubmitTurn,
              "next preference integration should send the canonical submit_turn IPC operation");

  const auto* submit_payload =
      std::get_if<TuiIpcSubmitTurnPayload>(&request_envelope->payload);
  assert_true(submit_payload != nullptr,
              "next preference integration should encode a submit payload variant");
  assert_true(submit_payload->next_preference.mode == expected_preference.mode,
              "submit payload should preserve the selected next-turn mode");
  assert_equal(expected_preference.preferred_depth_tier.value_or(std::string()),
               submit_payload->next_preference.preferred_depth_tier.value_or(
                   std::string()),
               "submit payload should preserve the preferred depth tier");
  assert_equal(expected_preference.pinned_provider_id.value_or(std::string()),
               submit_payload->next_preference.pinned_provider_id.value_or(
                   std::string()),
               "submit payload should preserve the pinned provider id");
  assert_equal(expected_preference.pinned_model_id.value_or(std::string()),
               submit_payload->next_preference.pinned_model_id.value_or(
                   std::string()),
               "submit payload should preserve the pinned model id");
}

void auto_submit_roundtrip_keeps_effective_route_and_echoes_auto_preference() {
  auto ipc = std::make_shared<ScriptedIpc>();

  TuiIpcResponseEnvelope submit_response;
  submit_response.operation = TuiIpcOperation::SubmitTurn;
  submit_response.request_id = "req-submit-auto-029";
  submit_response.trace_id = "trace-submit-auto-029";
  submit_response.session_id = "session-029";
  submit_response.outcome = TuiIpcOutcome::Success;
  submit_response.payload = make_receipt("req-submit-auto-029",
                                         "trace-submit-auto-029");

  TuiIpcResponseEnvelope route_response;
  route_response.operation = TuiIpcOperation::RouteCatalog;
  route_response.request_id = "req-route-auto-029";
  route_response.trace_id = "trace-route-auto-029";
  route_response.session_id = "session-029";
  route_response.outcome = TuiIpcOutcome::Success;
  route_response.payload = make_auto_echo_catalog();

  ipc->response_texts = {
      encode_response(submit_response),
      encode_response(route_response),
  };

  const dasall::tui::ipc::test::ScopedIpcOverride override(ipc);
  DaemonTuiDataSource data_source(make_options());
  TuiModelSelector selector(make_initial_route_catalog());

  const NextTurnPreference preference =
      selector.apply_preference();

  const TuiSubmitTurnRequest submit_request{
      .session_id = "session-029",
      .user_input = "Explain current route selection",
      .next_preference = preference,
      .request_id = "req-submit-auto-029",
      .trace_id = "trace-submit-auto-029",
  };

  const auto submit_result = data_source.submit_turn(submit_request);
  const auto route_result = data_source.route_catalog(
      make_route_request("session-029", "req-route-auto-029", "trace-route-auto-029"));

  assert_true(submit_result.ok() && route_result.ok(),
              "auto preference integration should complete submit and route echo roundtrips");
  assert_submit_payload_matches_preference(decode_sent_envelope(ipc->sent_payloads, 0),
                                           preference);
  assert_equal(std::string("deepseek-prod"),
               route_result.route_catalog->current_route.current_provider_id,
               "auto preference should keep the echoed effective provider unchanged");
  assert_equal(std::string("deepseek-chat"),
               route_result.route_catalog->current_route.current_model_id,
               "auto preference should keep the echoed effective model unchanged");
  assert_true(route_result.route_catalog->current_route.disabled_reasons.empty(),
              "auto preference should not invent route-level fail-closed reasons");
  assert_equal(std::string("auto"),
               route_result.route_catalog->current_route.next_preference.user_visible_summary,
               "auto preference should echo the owner-projected auto summary");
}

void prefer_depth_submit_echoes_effective_route_without_claiming_depth_enforcement() {
  auto ipc = std::make_shared<ScriptedIpc>();

  TuiIpcResponseEnvelope submit_response;
  submit_response.operation = TuiIpcOperation::SubmitTurn;
  submit_response.request_id = "req-submit-depth-029";
  submit_response.trace_id = "trace-submit-depth-029";
  submit_response.session_id = "session-029";
  submit_response.outcome = TuiIpcOutcome::Success;
  submit_response.payload = make_receipt("req-submit-depth-029",
                                         "trace-submit-depth-029");

  TuiIpcResponseEnvelope route_response;
  route_response.operation = TuiIpcOperation::RouteCatalog;
  route_response.request_id = "req-route-depth-029";
  route_response.trace_id = "trace-route-depth-029";
  route_response.session_id = "session-029";
  route_response.outcome = TuiIpcOutcome::Success;
  route_response.payload = make_prefer_depth_echo_catalog();

  ipc->response_texts = {
      encode_response(submit_response),
      encode_response(route_response),
  };

  const dasall::tui::ipc::test::ScopedIpcOverride override(ipc);
  DaemonTuiDataSource data_source(make_options());
  TuiModelSelector selector(make_initial_route_catalog());

  static_cast<void>(selector.open_selector(TuiRoutePreferenceMode::PreferDepth));
  assert_true(selector.choose_depth_tier("deep"),
              "prefer-depth integration should be able to select a known depth tier");
  const NextTurnPreference preference = selector.apply_preference();

  const TuiSubmitTurnRequest submit_request{
      .session_id = "session-029",
      .user_input = "Prefer a deeper model for the next turn",
      .next_preference = preference,
      .request_id = "req-submit-depth-029",
      .trace_id = "trace-submit-depth-029",
  };

  const auto submit_result = data_source.submit_turn(submit_request);
  const auto route_result = data_source.route_catalog(
      make_route_request("session-029", "req-route-depth-029", "trace-route-depth-029"));

  assert_true(submit_result.ok() && route_result.ok(),
              "prefer-depth integration should complete submit and route echo roundtrips");
  assert_submit_payload_matches_preference(decode_sent_envelope(ipc->sent_payloads, 0),
                                           preference);
  assert_equal(std::string("deep"),
               route_result.route_catalog->current_route.next_preference
                   .preferred_depth_tier.value_or(std::string()),
               "prefer-depth echo should preserve the advisory requested depth tier");
  assert_equal(std::string("standard"),
               route_result.route_catalog->current_route.current_depth_tier,
               "prefer-depth echo should surface the effective route depth rather than pretending the preference was enforced");
  assert_true(route_result.route_catalog->current_route.disabled_reasons.empty(),
              "prefer-depth misses should stay advisory instead of fail-closing the turn");
}

void pin_model_submit_fail_closes_with_route_reason_and_without_silent_fallback() {
  auto ipc = std::make_shared<ScriptedIpc>();

  TuiIpcResponseEnvelope submit_response;
  submit_response.operation = TuiIpcOperation::SubmitTurn;
  submit_response.request_id = "req-submit-pin-029";
  submit_response.trace_id = "trace-submit-pin-029";
  submit_response.session_id = "session-029";
  submit_response.outcome = TuiIpcOutcome::Success;
  submit_response.payload = make_receipt("req-submit-pin-029",
                                         "trace-submit-pin-029",
                                         "route_unavailable");

  TuiIpcResponseEnvelope route_response;
  route_response.operation = TuiIpcOperation::RouteCatalog;
  route_response.request_id = "req-route-pin-029";
  route_response.trace_id = "trace-route-pin-029";
  route_response.session_id = "session-029";
  route_response.outcome = TuiIpcOutcome::Success;
  route_response.payload = make_pin_fail_closed_echo_catalog();

  ipc->response_texts = {
      encode_response(submit_response),
      encode_response(route_response),
  };

  const dasall::tui::ipc::test::ScopedIpcOverride override(ipc);
  DaemonTuiDataSource data_source(make_options());
  TuiModelSelector selector(make_initial_route_catalog());

  static_cast<void>(selector.open_selector(TuiRoutePreferenceMode::PinModel));
  assert_true(selector.choose_model("provider-local", "deep-reasoner"),
              "pin-model integration should be able to select a currently cataloged model before submit");
  const NextTurnPreference preference = selector.apply_preference();

  const TuiSubmitTurnRequest submit_request{
      .session_id = "session-029",
      .user_input = "Pin the next turn to provider-local/deep-reasoner",
      .next_preference = preference,
      .request_id = "req-submit-pin-029",
      .trace_id = "trace-submit-pin-029",
  };

  const auto submit_result = data_source.submit_turn(submit_request);
  const auto route_result = data_source.route_catalog(
      make_route_request("session-029", "req-route-pin-029", "trace-route-pin-029"));

  assert_true(submit_result.ok() && route_result.ok(),
              "pin-model integration should still receive owner projections for fail-closed evaluation");
  assert_submit_payload_matches_preference(decode_sent_envelope(ipc->sent_payloads, 0),
                                           preference);
  assert_equal(std::string("route_unavailable"),
               submit_result.receipt->reason_code.value_or(std::string()),
               "pin-model fail-closed should surface the stable route.* reason family through the turn receipt");
  assert_equal(std::string("deepseek-prod"),
               route_result.route_catalog->current_route.current_provider_id,
               "pin-model fail-closed should preserve the effective provider instead of silently claiming the requested pin succeeded");
  assert_equal(std::string("deepseek-chat"),
               route_result.route_catalog->current_route.current_model_id,
               "pin-model fail-closed should preserve the effective model instead of silently falling back to another route");
  assert_equal(std::string("provider-local"),
               route_result.route_catalog->current_route.next_preference
                   .pinned_provider_id.value_or(std::string()),
               "pin-model echo should preserve the requested provider in the projected next preference");
  assert_equal(std::string("deep-reasoner"),
               route_result.route_catalog->current_route.next_preference
                   .pinned_model_id.value_or(std::string()),
               "pin-model echo should preserve the requested model in the projected next preference");
  assert_true(!route_result.route_catalog->current_route.disabled_reasons.empty(),
              "pin-model fail-closed should surface route-level disabled reasons rather than suppressing them");
}

}  // namespace

int main() {
  try {
    auto_submit_roundtrip_keeps_effective_route_and_echoes_auto_preference();
    prefer_depth_submit_echoes_effective_route_without_claiming_depth_enforcement();
    pin_model_submit_fail_closes_with_route_reason_and_without_silent_fallback();
  } catch (const std::exception& exception) {
    std::cerr << exception.what() << '\n';
    return 1;
  }

  return 0;
}