#include <exception>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <cstdlib>
#include <unistd.h>

#include "AccessGatewayFactory.h"
#include "AsyncTaskRegistry.h"
#include "CliBinaryTestSupport.h"
#include "DaemonIntegrationHarness.h"
#include "support/TestAssertions.h"

#ifndef DASALL_TUI_BINARY_PATH
#error "DASALL_TUI_BINARY_PATH must be defined"
#endif

#ifndef DASALL_REPOSITORY_ROOT
#error "DASALL_REPOSITORY_ROOT must be defined"
#endif

namespace {

namespace fs = std::filesystem;

using namespace std::chrono_literals;

constexpr char kScriptedSmokeModeEnv[] = "DASALL_TUI_SCRIPTED_SMOKE";
constexpr char kScriptedSmokePromptEnv[] = "DASALL_TUI_SCRIPTED_SMOKE_PROMPT";
constexpr char kScriptedSmokeProfileEnv[] = "DASALL_TUI_SCRIPTED_SMOKE_PROFILE_ID";
constexpr char kDaemonSocketEnv[] = "DASALL_TUI_DAEMON_SOCKET";

using dasall::access::AccessDisposition;
using dasall::access::AsyncTaskRegistry;
using dasall::access::DaemonAccessPipelineOptions;
using dasall::access::RuntimeDispatchRequest;
using dasall::access::RuntimeDispatchResult;
using dasall::tests::integration::access_support::DaemonIntegrationHarness;
using dasall::tests::integration::access_support::ProcessResult;
using dasall::tests::integration::access_support::run_process_capture_split;
using dasall::tests::support::assert_equal;
using dasall::tests::support::assert_true;

class ScopedEnvironmentOverride {
 public:
  ScopedEnvironmentOverride(std::string name, std::string value)
      : name_(std::move(name)) {
    if (const char* previous = ::getenv(name_.c_str()); previous != nullptr) {
      previous_value_ = previous;
    }
    ::setenv(name_.c_str(), value.c_str(), 1);
  }

  ~ScopedEnvironmentOverride() {
    if (previous_value_.has_value()) {
      ::setenv(name_.c_str(), previous_value_->c_str(), 1);
      return;
    }

    ::unsetenv(name_.c_str());
  }

 private:
  std::string name_;
  std::optional<std::string> previous_value_;
};

[[nodiscard]] const fs::path& repository_root() {
  static const fs::path root{DASALL_REPOSITORY_ROOT};
  return root;
}

[[nodiscard]] std::string current_local_actor_ref() {
  return "local://uid/" + std::to_string(::getuid());
}

[[nodiscard]] DaemonAccessPipelineOptions make_daemon_options() {
  DaemonAccessPipelineOptions options;
  options.bootstrap_config.allowed_protocols = {"ipc_uds", "tui_ipc.v1"};
  options.auth_view.trusted_local_subjects = {current_local_actor_ref()};
  options.daemon_profile_id = "daemon.tui.scripted-smoke";
  options.async_task_registry =
      std::make_shared<AsyncTaskRegistry>("tui-scripted-smoke-secret", 30s);
  options.runtime_dispatch_backend = [](const RuntimeDispatchRequest& request) {
    RuntimeDispatchResult result;
    result.disposition = AccessDisposition::AcceptedAsync;
    result.receipt_ref = std::string("receipt:") + request.packet.packet_id;
    return result;
  };
  return options;
}

[[nodiscard]] ProcessResult run_tui_binary() {
  return run_process_capture_split({DASALL_TUI_BINARY_PATH}, repository_root());
}

void formal_entrypoint_supports_scripted_daemon_backed_smoke_mode() {
  DaemonIntegrationHarness harness(make_daemon_options());

  ScopedEnvironmentOverride smoke_mode(kScriptedSmokeModeEnv, "daemon_roundtrip");
  ScopedEnvironmentOverride smoke_prompt(kScriptedSmokePromptEnv,
                                         "queue daemon-backed tui roundtrip");
  ScopedEnvironmentOverride smoke_profile(kScriptedSmokeProfileEnv, "desktop_full");
  ScopedEnvironmentOverride socket_override(kDaemonSocketEnv, harness.socket_path());

  const ProcessResult result = run_tui_binary();

  assert_equal(0,
               result.exit_code,
               "scripted smoke mode should keep the formal TUI entrypoint green; stdout=" +
                   result.stdout_text + " stderr=" + result.stderr_text);
  assert_true(result.stderr_text.empty(),
              "scripted smoke mode should not emit stderr on the success path; stderr=" +
                  result.stderr_text);
  assert_true(result.stdout_text.find("\"mode\":\"daemon_roundtrip\"") !=
                  std::string::npos,
              "scripted smoke mode should identify the daemon_roundtrip smoke payload; stdout=" +
                  result.stdout_text);
  assert_true(result.stdout_text.find("\"shutdown_clean\":true") != std::string::npos,
              "scripted smoke mode should close the foreground session cleanly; stdout=" +
                  result.stdout_text);
  assert_true(result.stdout_text.find("\"session_closed_cleanly\":true") !=
                  std::string::npos,
              "scripted smoke mode should report a clean close_session acknowledgement; stdout=" +
                  result.stdout_text);
  assert_true(result.stdout_text.find("\"profile_id\":\"desktop_full\"") !=
                  std::string::npos,
              "scripted smoke mode should preserve the requested profile id in the emitted payload; stdout=" +
                  result.stdout_text);
  assert_true(result.stdout_text.find("\"daemon_readiness\":\"ready\"") !=
                  std::string::npos,
              "scripted smoke mode should surface ready daemon_readiness from open_session; stdout=" +
                  result.stdout_text);
  assert_true(result.stdout_text.find("\"current_provider_id\":\"daemon-local\"") !=
                  std::string::npos,
              "scripted smoke mode should surface the daemon-backed current provider projection; stdout=" +
                  result.stdout_text);
  assert_true(result.stdout_text.find("\"current_model_id\":\"dasall-core\"") !=
                  std::string::npos,
              "scripted smoke mode should surface the daemon-backed current model projection; stdout=" +
                  result.stdout_text);
  assert_true(result.stdout_text.find("\"status_stage\":\"accepted_async\"") !=
                  std::string::npos,
              "scripted smoke mode should poll the accepted_async event status into the payload; stdout=" +
                  result.stdout_text);
  assert_true(result.stdout_text.find("\"status_current_tool\":\"access.submit\"") !=
                  std::string::npos,
              "scripted smoke mode should preserve access.submit as the current tool; stdout=" +
                  result.stdout_text);
  assert_true(result.stdout_text.find(
                  "\"latest_transcript_content\":\"queued for daemon-backed execution\"") !=
                  std::string::npos,
              "scripted smoke mode should surface the queued daemon-backed receipt summary; stdout=" +
                  result.stdout_text);
  assert_true(result.stdout_text.find("\"latest_banner_title\":\"Turn submitted\"") !=
                  std::string::npos,
              "scripted smoke mode should preserve the submit confirmation banner; stdout=" +
                  result.stdout_text);
  assert_true(result.stdout_text.find("\"rendered_screen_contains_route\":true") !=
                  std::string::npos,
              "scripted smoke mode should keep the rendered screen aligned with the daemon-backed route projection; stdout=" +
                  result.stdout_text);

  harness.stop();
  assert_true(harness.daemon_stopped_cleanly(),
              "scripted smoke mode should leave the in-process daemon harness stoppable after the roundtrip");
}

}  // namespace

int main() {
  try {
    formal_entrypoint_supports_scripted_daemon_backed_smoke_mode();
  } catch (const std::exception& exception) {
    std::cerr << exception.what() << '\n';
    return 1;
  }

  return 0;
}
