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
using dasall::tui::ipc::TuiIpcControllerOptions;
using dasall::tui::ipc::TuiIpcOperation;
using dasall::tui::ipc::TuiIpcOutcome;
using dasall::tui::ipc::TuiIpcResponseEnvelope;
using dasall::tui::view::TuiModelSelector;
using dasall::tui::view::TuiModelSelectorOption;

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
        make_platform_error("listen unused in selector daemon tests"));
  }

  dasall::platform::PlatformResult<dasall::platform::IpcChannelHandle> accept(
      const dasall::platform::IpcListenerHandle&,
      std::int32_t) override {
    return dasall::platform::PlatformResult<
        dasall::platform::IpcChannelHandle>::failure(
        make_platform_error("accept unused in selector daemon tests"));
  }

  dasall::platform::PlatformResult<dasall::platform::IpcChannelHandle> connect(
      const dasall::platform::IpcEndpoint&,
      std::int32_t) override {
    return dasall::platform::PlatformResult<
        dasall::platform::IpcChannelHandle>::success(
        dasall::platform::IpcChannelHandle{.native_fd = 128U});
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
        make_platform_error("describe_peer unused in selector daemon tests"));
  }

  dasall::platform::PlatformResult<bool> close(
      const dasall::platform::IpcChannelHandle&) override {
    return dasall::platform::PlatformResult<bool>::success(true);
  }
};

[[nodiscard]] TuiIpcControllerOptions make_options() {
  TuiIpcControllerOptions options;
  options.socket_path = "/tmp/dasall-tui-selector-daemon.sock";
  return options;
}

[[nodiscard]] std::optional<dasall::tui::ipc::TuiIpcRequestEnvelope>
decode_sent_envelope(const std::vector<std::string>& payloads, std::size_t index) {
  return dasall::tui::ipc::test::decode_request_envelope_for_test(payloads.at(index));
}

[[nodiscard]] const TuiModelSelectorOption* find_option(
    const std::vector<TuiModelSelectorOption>& options,
    const std::string& provider_id,
    const std::string& model_id) {
  for (const TuiModelSelectorOption& option : options) {
    if (option.provider_id == provider_id && option.model_id == model_id) {
      return &option;
    }
  }

  return nullptr;
}

[[nodiscard]] TuiRouteCatalogView make_route_catalog() {
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
          .mode = TuiRoutePreferenceMode::PinModel,
          .preferred_depth_tier = std::nullopt,
          .pinned_provider_id = std::optional<std::string>("deepseek-prod"),
          .pinned_model_id = std::optional<std::string>("deepseek-chat"),
          .user_visible_summary = "pin model: deepseek-prod/deepseek-chat",
          .source = "daemon",
          .applies_to_next_turn_only = true,
      },
  };

  TuiRouteCatalogEntry current_entry;
  current_entry.provider_id = "deepseek-prod";
  current_entry.model_id = "deepseek-chat";
  current_entry.depth_tier = "standard";
  current_entry.verification_state = "verified";
  current_entry.health = "healthy";
  current_entry.profile_allowlisted = true;
  current_entry.selectable = true;
  route_catalog.candidate_routes.push_back(current_entry);

  TuiRouteCatalogEntry reasoning_entry;
  reasoning_entry.provider_id = "deepseek-prod";
  reasoning_entry.model_id = "deepseek-reasoner";
  reasoning_entry.depth_tier = "deep";
  reasoning_entry.verification_state = "verified";
  reasoning_entry.health = "healthy";
  reasoning_entry.profile_allowlisted = true;
  reasoning_entry.selectable = true;
  route_catalog.candidate_routes.push_back(reasoning_entry);

  TuiRouteCatalogEntry disabled_entry;
  disabled_entry.provider_id = "provider-local";
  disabled_entry.model_id = "deep-reasoner";
  disabled_entry.depth_tier = "deep";
  disabled_entry.verification_state = "pending";
  disabled_entry.health = "degraded";
  disabled_entry.profile_allowlisted = false;
  disabled_entry.selectable = false;
  disabled_entry.disabled_reasons = {
      "verification_pending",
      "provider_unhealthy",
      "allowlist_blocked",
  };
  route_catalog.candidate_routes.push_back(disabled_entry);

  return route_catalog;
}

void selector_consumes_daemon_route_catalog_projection_fields() {
  auto ipc = std::make_shared<ScriptedIpc>();

  TuiIpcResponseEnvelope route_response;
  route_response.operation = TuiIpcOperation::RouteCatalog;
  route_response.request_id = "req-route-028";
  route_response.trace_id = "trace-route-028";
  route_response.session_id = "session-028";
  route_response.outcome = TuiIpcOutcome::Success;
  route_response.payload = make_route_catalog();
  ipc->response_texts.push_back(
      dasall::tui::ipc::test::encode_response_envelope_for_test(route_response));

  const dasall::tui::ipc::test::ScopedIpcOverride override(ipc);
  DaemonTuiDataSource data_source(make_options());

  const TuiRouteCatalogRequest request{
      .session_id = std::optional<std::string>("session-028"),
      .profile_id = std::optional<std::string>("desktop_full"),
      .selector_mode = std::optional<std::string>("pin_model"),
      .request_id = "req-route-028",
      .trace_id = "trace-route-028",
  };

  const auto route_result = data_source.route_catalog(request);
  assert_true(route_result.ok() && route_result.has_consistent_values(),
              "daemon selector test should retrieve a route catalog through DaemonTuiDataSource");

  const auto request_envelope = decode_sent_envelope(ipc->sent_payloads, 0);
  assert_true(request_envelope.has_value() &&
                  request_envelope->operation == TuiIpcOperation::RouteCatalog,
              "daemon selector test should send the canonical route_catalog IPC operation");

  const auto selector_payload =
      std::get<dasall::tui::ipc::TuiIpcRouteCatalogPayload>(request_envelope->payload);
  assert_equal(std::string("pin_model"),
               selector_payload.selector_mode.value_or(std::string()),
               "route_catalog should preserve selector_mode hints for daemon-side filtering");

  TuiModelSelector selector(*route_result.route_catalog);
  const auto pin_options = selector.open_selector(TuiRoutePreferenceMode::PinModel);

  const TuiModelSelectorOption* current_option =
      find_option(pin_options, "deepseek-prod", "deepseek-chat");
  assert_true(current_option != nullptr,
              "selector should expose the current daemon route as a pin option");
  assert_equal(std::string("deepseek-prod/deepseek-chat [verified healthy depth=standard]"),
               current_option->display_label,
               "selector should consume verification and health summaries from daemon route projections");

  const TuiModelSelectorOption* disabled_option =
      find_option(pin_options, "provider-local", "deep-reasoner");
  assert_true(disabled_option != nullptr && !disabled_option->selectable,
              "selector should keep disabled daemon routes visible but fail-closed");
  assert_equal(std::string("provider-local/deep-reasoner [pending degraded depth=deep]"),
               disabled_option->display_label,
               "selector should surface daemon route verification and health state in disabled labels");
  assert_equal(std::string("not verified, provider unhealthy, profile disallows route"),
               selector.render_disabled_reason(disabled_option->disabled_reasons),
               "selector should map daemon disabled reasons to stable user-facing text");
}

}  // namespace

int main() {
  try {
    selector_consumes_daemon_route_catalog_projection_fields();
  } catch (const std::exception& exception) {
    std::cerr << "[TuiModelSelectorDaemonTest] FAILED: " << exception.what() << '\n';
    return 1;
  }

  return 0;
}