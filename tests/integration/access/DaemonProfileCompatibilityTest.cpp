#include <exception>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>

#include "DaemonIntegrationHarness.h"
#include "DaemonProfileProjection.h"
#include "ProfileCatalog.h"

namespace {

using dasall::access::AccessDisposition;
using dasall::access::DaemonAccessPipelineOptions;
using dasall::access::PublishEnvelope;
using dasall::access::RuntimeDispatchRequest;
using dasall::access::RuntimeDispatchResult;
using dasall::profiles::DaemonProfileProjection;
using dasall::profiles::DaemonProfileProjectionRequest;
using dasall::profiles::ProfileCatalog;
using dasall::tests::integration::access_support::DaemonIntegrationHarness;
using dasall::tests::support::assert_equal;
using dasall::tests::support::assert_true;

[[nodiscard]] std::filesystem::path repository_root() {
  return std::filesystem::path(__FILE__).parent_path().parent_path().parent_path().parent_path();
}

void baseline_profiles_keep_diag_disabled_without_forking_unary_flow() {
  const ProfileCatalog catalog(repository_root() / "profiles");
  const auto listed = catalog.list_profiles();
  assert_true(listed.ok(),
              "daemon profile compatibility should be able to enumerate baseline profiles");
  assert_equal(5, static_cast<int>(listed.profiles.size()),
               "daemon profile compatibility should cover five baseline profiles");

  const DaemonProfileProjection projection(catalog);
  for (const auto& descriptor : listed.profiles) {
    const auto projected = projection.load(DaemonProfileProjectionRequest{
        .profile_id = descriptor.profile_id,
    });
    assert_true(projected.ok() && projected.settings.has_value(),
                "daemon profile compatibility should project daemon settings for every baseline profile");

    int runtime_calls = 0;
    DaemonAccessPipelineOptions options;
    options.bootstrap_config.allowed_protocols = {"ipc_uds"};
    options.auth_view.trusted_local_subjects = {"local://uid/1000"};
    options.daemon_profile_id = projected.settings->effective_profile_id;
    options.daemon_diagnostics_enabled = projected.settings->diag_enabled;
    options.runtime_dispatch_backend =
        [&runtime_calls, profile_id = projected.settings->effective_profile_id](
            const RuntimeDispatchRequest& request) {
          ++runtime_calls;

          RuntimeDispatchResult result;
          result.disposition = AccessDisposition::Completed;

          PublishEnvelope envelope;
          envelope.request_id = request.request_context.at("request_id");
          envelope.trace_id = request.request_context.contains("trace_id")
                                  ? request.request_context.at("trace_id")
                                  : envelope.request_id + "-trace";
          envelope.protocol_kind = request.packet.protocol_kind;
          envelope.protocol_status_hint = "200";

          dasall::contracts::AgentResult agent_result;
          agent_result.request_id = envelope.request_id;
          agent_result.response_text = std::string("profile=") + profile_id;
          agent_result.task_completed = true;
          envelope.agent_result = std::move(agent_result);
          result.publish_envelope = std::move(envelope);
          return result;
        };

    dasall::apps::daemon::DaemonBootstrapConfig config;
    config.listen_backlog = projected.settings->listen_backlog;
    config.dispatch_timeout_ms = projected.settings->dispatch_timeout_ms;
    config.diag_enabled = projected.settings->diag_enabled;
    config.watchdog_enabled = projected.settings->watchdog_enabled;

    DaemonIntegrationHarness harness(std::move(options), config);

    const auto run_response = harness.make_client().submit("profile compatibility unary");
    assert_true(run_response.ok() && run_response.is_completed(),
                "daemon profile compatibility should keep unary path available across baseline profiles");
    assert_true(run_response.response_text.has_value(),
                "daemon profile compatibility should preserve runtime response text");
    assert_true(run_response.response_text->find(projected.settings->effective_profile_id) !=
                    std::string::npos,
                "daemon profile compatibility should preserve effective profile id in runtime response");

    const auto diag_response = harness.make_client().run_diagnostics("health.snapshot");
    assert_true(diag_response.ok(),
                "daemon profile compatibility should parse diagnostics rejection response");
    assert_true(diag_response.error_ref.has_value(),
                "daemon profile compatibility should surface diagnostics gate rejection");
    assert_equal(std::string("diag_disabled"), *diag_response.error_ref,
                 "baseline profiles should keep daemon diagnostics disabled by default");
    assert_equal(1, runtime_calls,
                 "daemon profile compatibility should not fork unary flow when diagnostics stay disabled");

    harness.stop();
    assert_true(harness.daemon_stopped_cleanly(),
                "daemon profile compatibility should stop daemon cleanly for every baseline profile");
  }
}

}  // namespace

int main() {
  try {
    baseline_profiles_keep_diag_disabled_without_forking_unary_flow();
  } catch (const std::exception& ex) {
    std::cerr << "[DaemonProfileCompatibilityTest] FAILED: " << ex.what() << '\n';
    return 1;
  }

  return 0;
}