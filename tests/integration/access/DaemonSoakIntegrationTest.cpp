#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <exception>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <thread>

#include "DaemonInProcessFixture.h"
#include "DaemonLoadScenario.h"
#include "daemon/DaemonFrameCodec.h"
#include "agent/AgentResult.h"
#include "support/TestAssertions.h"

namespace {

using namespace std::chrono_literals;

using dasall::access::AccessDisposition;
using dasall::access::DaemonAccessPipelineOptions;
using dasall::access::PublishEnvelope;
using dasall::access::RuntimeDispatchRequest;
using dasall::access::RuntimeDispatchResult;
using dasall::access::daemon::DaemonAsyncPreference;
using dasall::access::daemon::UdsRequestFrame;
using dasall::tests::integration::access_support::DaemonInProcessFixture;
using dasall::tests::integration::access_support::DaemonLoadScenario;
using dasall::tests::support::assert_equal;
using dasall::tests::support::assert_true;

[[nodiscard]] RuntimeDispatchResult make_completed_result(
    const RuntimeDispatchRequest& request,
    const std::string& response_text) {
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
  agent_result.response_text = response_text;
  agent_result.task_completed = true;
  envelope.agent_result = std::move(agent_result);

  result.publish_envelope = std::move(envelope);
  return result;
}

[[nodiscard]] RuntimeDispatchResult make_publish_failure_result(
    const RuntimeDispatchRequest& request) {
  RuntimeDispatchResult result;
  result.disposition = AccessDisposition::Rejected;
  result.error_ref = "publish-failure-simulated";

  PublishEnvelope envelope;
  dasall::contracts::AgentResult agent_result;
  agent_result.request_id = request.request_context.at("request_id");
  agent_result.status = dasall::contracts::AgentResultStatus::Failed;
  agent_result.result_code = 500;
  agent_result.response_text = "publish-failure-simulated";
  agent_result.task_completed = false;
  envelope.agent_result = std::move(agent_result);

  result.publish_envelope = std::move(envelope);
  return result;
}

[[nodiscard]] dasall::apps::cli::DaemonClientResponse submit_unique_run_request(
    const DaemonInProcessFixture& fixture,
    const std::string& request_id,
  const std::string& payload,
  const std::int32_t deadline_ms = 50) {
  UdsRequestFrame frame;
  frame.request_id = request_id;
  frame.trace_id = request_id + "-trace";
  frame.session_hint = std::nullopt;
  frame.idempotency_key = request_id + "-idem";
  frame.command = "run";
  frame.args = {};
  frame.payload = payload;
  frame.async_preference = DaemonAsyncPreference::PreferSync;
  return fixture.send_frame(frame, deadline_ms);
}

[[nodiscard]] std::string describe_response(
    const dasall::apps::cli::DaemonClientResponse& response) {
  std::ostringstream stream;
  stream << "ok=" << (response.ok() ? "true" : "false")
         << ", completed=" << (response.is_completed() ? "true" : "false")
         << ", accepted_async=" << (response.is_accepted_async() ? "true" : "false")
         << ", transport_ok=" << (response.transport_ok ? "true" : "false")
         << ", parse_ok=" << (response.parse_ok ? "true" : "false")
         << ", peer_closed=" << (response.peer_closed ? "true" : "false");
  if (response.error_ref.has_value()) {
    stream << ", error_ref=" << *response.error_ref;
  }
  if (!response.failure_reason.empty()) {
    stream << ", failure_reason=" << response.failure_reason;
  }
  if (response.response_text.has_value()) {
    stream << ", response_text=" << *response.response_text;
  }
  return stream.str();
}

struct PublishProbe {
  std::atomic<int> calls{0};
};

struct BlockingDrainDispatch {
  std::mutex mutex;
  std::condition_variable cv;
  int active_requests = 0;
  bool release_requests = false;

  [[nodiscard]] RuntimeDispatchResult dispatch(const RuntimeDispatchRequest& request) {
    std::unique_lock<std::mutex> lock(mutex);
    ++active_requests;
    cv.notify_all();
    cv.wait(lock, [this]() { return release_requests; });
    --active_requests;
    lock.unlock();
    return make_completed_result(request, "drained");
  }

  [[nodiscard]] bool wait_for_active(const std::chrono::milliseconds timeout) {
    std::unique_lock<std::mutex> lock(mutex);
    return cv.wait_for(lock, timeout, [this]() { return active_requests >= 1; });
  }

  void release_all() {
    {
      std::lock_guard<std::mutex> lock(mutex);
      release_requests = true;
    }
    cv.notify_all();
  }
};

void repeated_parallel_waves_do_not_leak_connections_when_publish_emit_fails() {
  PublishProbe publish_probe;

  DaemonAccessPipelineOptions options;
  options.bootstrap_config.allowed_protocols = {"ipc_uds"};
  options.auth_view.trusted_local_subjects = {"local://uid/1000"};
  options.admission_view.max_inflight_requests = 16;
  options.admission_view.idempotency_window_ms = 60000;
  options.daemon_profile_id = "daemon.soak.integration";
  options.publish_backend = [&publish_probe](const PublishEnvelope&) {
    ++publish_probe.calls;
    return false;
  };
  options.runtime_dispatch_backend = [](const RuntimeDispatchRequest& request) {
    if (request.packet.payload.rfind("fail:", 0U) == 0U) {
      return make_publish_failure_result(request);
    }
    return make_completed_result(request, request.packet.payload);
  };

  dasall::apps::daemon::DaemonBootstrapConfig config;
  config.dispatch_workers = 4U;

  DaemonInProcessFixture fixture(std::move(options), config);

  constexpr std::size_t kWaveCount = 6U;
  constexpr std::size_t kRequestsPerWave = 8U;
  int completed_count = 0;
  int rejected_count = 0;

  for (std::size_t wave = 0U; wave < kWaveCount; ++wave) {
    const auto results = DaemonLoadScenario::run_parallel(
        fixture,
        kRequestsPerWave,
        [wave](const DaemonInProcessFixture& fixture, const std::size_t index) {
          const bool should_fail = (index % 2U) == 1U;
          const std::string payload = should_fail
                                          ? "fail:wave-" + std::to_string(wave) + "-" +
                                                std::to_string(index)
                                          : "ok:wave-" + std::to_string(wave) + "-" +
                                                std::to_string(index);
          return submit_unique_run_request(
              fixture,
              "soak-" + std::to_string(wave) + "-" + std::to_string(index),
              payload);
        });

    for (const auto& result : results) {
      assert_true(result.response.ok(),
                  "daemon soak gate should keep every response parseable across repeated parallel waves");
      if ((result.index % 2U) == 0U) {
        assert_true(result.response.is_completed(),
                    "daemon soak gate should keep completed requests on the happy path");
        assert_true(result.response.response_text.has_value(),
                    "daemon soak gate should preserve runtime response_text for completed requests");
        ++completed_count;
        continue;
      }

      assert_true(result.response.error_ref.has_value(),
                  "daemon soak gate should surface error_ref for publish-failure responses");
      assert_equal(std::string("publish-failure-simulated"), *result.response.error_ref,
                   "daemon soak gate should keep rejected publish failure responses parseable even when emit backend fails");
      ++rejected_count;
    }

    assert_equal(static_cast<std::size_t>(0U), fixture.active_connection_count(),
                 "daemon soak gate should return active connection count to baseline after each wave");
  }

  assert_equal(static_cast<int>((kWaveCount * kRequestsPerWave) / 2U), completed_count,
               "daemon soak gate should complete every happy-path request across all waves");
  assert_equal(static_cast<int>((kWaveCount * kRequestsPerWave) / 2U), rejected_count,
               "daemon soak gate should surface every deterministic publish-failure request across all waves");
  assert_equal(rejected_count, publish_probe.calls.load(),
               "daemon soak gate should attempt publish once per rejected publish-failure response");

  fixture.stop();
  assert_true(fixture.daemon_stopped_cleanly(),
              "daemon soak fixture should stop cleanly after repeated parallel waves");
}

void draining_stops_accepting_new_requests_and_releases_resources() {
  BlockingDrainDispatch dispatch_backend;

  DaemonAccessPipelineOptions options;
  options.bootstrap_config.allowed_protocols = {"ipc_uds"};
  options.auth_view.trusted_local_subjects = {"local://uid/1000"};
  options.admission_view.max_inflight_requests = 4;
  options.admission_view.idempotency_window_ms = 60000;
  options.daemon_profile_id = "daemon.drain.integration";
  options.runtime_dispatch_backend = [&dispatch_backend](const RuntimeDispatchRequest& request) {
    return dispatch_backend.dispatch(request);
  };

  dasall::apps::daemon::DaemonBootstrapConfig config;
  config.dispatch_workers = 2U;

  DaemonInProcessFixture fixture(std::move(options), config);

  dasall::apps::cli::DaemonClientResponse inflight_response;
  std::thread inflight_thread([&]() {
    inflight_response = submit_unique_run_request(
        fixture,
        "drain-inflight",
      "drain-inflight",
      500);
  });

  assert_true(dispatch_backend.wait_for_active(500ms),
              "daemon drain gate should observe one active inflight request before shutdown");
  assert_true(fixture.active_connection_count() >= 1U,
              "daemon drain gate should expose an active connection while shutdown is pending");

  std::thread stop_thread([&fixture]() {
    fixture.stop(200ms);
  });

  std::this_thread::sleep_for(20ms);
  const auto late_response = submit_unique_run_request(
      fixture,
      "late-during-drain",
      "late-during-drain",
      50);
  assert_true(!late_response.ok() ||
                  (late_response.error_ref.has_value() &&
                   *late_response.error_ref == "gateway_not_ready_or_shutting_down"),
              "daemon drain gate should refuse new requests once draining begins");

  dispatch_backend.release_all();

  if (inflight_thread.joinable()) {
    inflight_thread.join();
  }
  if (stop_thread.joinable()) {
    stop_thread.join();
  }

  assert_true(inflight_response.ok() && inflight_response.is_completed(),
              "daemon drain gate should let the already inflight request finish cleanly: " +
                  describe_response(inflight_response));
  assert_equal(static_cast<std::size_t>(0U), fixture.active_connection_count(),
               "daemon drain gate should release all active connections after shutdown completes");
  assert_true(fixture.daemon_stopped_cleanly(),
              "daemon drain gate should stop daemon cleanly after inflight request drains");
}

}  // namespace

int main() {
  try {
    repeated_parallel_waves_do_not_leak_connections_when_publish_emit_fails();
    draining_stops_accepting_new_requests_and_releases_resources();
  } catch (const std::exception& ex) {
    std::cerr << "[DaemonSoakIntegrationTest] FAILED: " << ex.what() << '\n';
    return 1;
  }

  return 0;
}