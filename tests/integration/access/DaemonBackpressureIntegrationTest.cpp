#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <exception>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "DaemonInProcessFixture.h"
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

[[nodiscard]] dasall::apps::cli::DaemonClientResponse submit_unique_run_request(
    const DaemonInProcessFixture& fixture,
    const std::string& request_id,
    const std::string& payload) {
  UdsRequestFrame frame;
  frame.request_id = request_id;
  frame.trace_id = request_id + "-trace";
  frame.session_hint = std::nullopt;
  frame.idempotency_key = request_id + "-idem";
  frame.command = "run";
  frame.args = {};
  frame.payload = payload;
  frame.async_preference = DaemonAsyncPreference::PreferSync;
  return fixture.send_frame(frame);
}

struct BlockingDispatchBackend {
  std::mutex mutex;
  std::condition_variable cv;
  int active_requests = 0;
  int max_active_requests = 0;
  bool release_requests = false;

  [[nodiscard]] RuntimeDispatchResult dispatch(const RuntimeDispatchRequest& request) {
    std::unique_lock<std::mutex> lock(mutex);
    ++active_requests;
    max_active_requests = std::max(max_active_requests, active_requests);
    cv.notify_all();
    cv.wait(lock, [this]() { return release_requests; });
    --active_requests;
    lock.unlock();
    return make_completed_result(request, request.packet.payload);
  }

  [[nodiscard]] bool wait_for_active(const int expected,
                                     const std::chrono::milliseconds timeout) {
    std::unique_lock<std::mutex> lock(mutex);
    return cv.wait_for(lock, timeout, [this, expected]() {
      return active_requests >= expected;
    });
  }

  void release_all() {
    {
      std::lock_guard<std::mutex> lock(mutex);
      release_requests = true;
    }
    cv.notify_all();
  }
};

void admission_backpressure_rejects_over_limit_concurrency() {
  BlockingDispatchBackend dispatch_backend;

  DaemonAccessPipelineOptions options;
  options.bootstrap_config.allowed_protocols = {"ipc_uds"};
  options.auth_view.trusted_local_subjects = {"local://uid/1000"};
  options.admission_view.max_inflight_requests = 2;
  options.admission_view.idempotency_window_ms = 60000;
  options.daemon_profile_id = "daemon.backpressure.integration";
  options.runtime_dispatch_backend = [&dispatch_backend](const RuntimeDispatchRequest& request) {
    return dispatch_backend.dispatch(request);
  };

  dasall::apps::daemon::DaemonBootstrapConfig config;
  config.dispatch_workers = 4U;

  DaemonInProcessFixture fixture(std::move(options), config);

  std::vector<dasall::apps::cli::DaemonClientResponse> responses(4);
  std::vector<std::thread> workers;
  workers.reserve(responses.size());

  std::mutex start_mutex;
  std::condition_variable ready_cv;
  std::condition_variable start_cv;
  std::size_t ready_count = 0U;
  bool start = false;
  std::atomic<int> finished_workers{0};

  for (std::size_t index = 0U; index < responses.size(); ++index) {
    workers.emplace_back([&, index]() {
      {
        std::unique_lock<std::mutex> lock(start_mutex);
        ++ready_count;
        ready_cv.notify_one();
        start_cv.wait(lock, [&start]() { return start; });
      }
      responses[index] = submit_unique_run_request(
          fixture,
          "backpressure-" + std::to_string(index),
          "parallel-" + std::to_string(index));
      ++finished_workers;
    });
  }

  {
    std::unique_lock<std::mutex> lock(start_mutex);
    ready_cv.wait(lock, [&]() { return ready_count == responses.size(); });
    start = true;
  }
  start_cv.notify_all();

  const auto cleanup = [&]() {
    dispatch_backend.release_all();
    for (auto& worker : workers) {
      if (worker.joinable()) {
        worker.join();
      }
    }
    fixture.stop();
  };

  try {
    assert_true(dispatch_backend.wait_for_active(2, 500ms),
                "daemon backpressure should allow two inflight requests before rejecting overflow");
    for (int attempt = 0; attempt < 50 && finished_workers.load() < 2; ++attempt) {
      std::this_thread::sleep_for(10ms);
    }
    assert_true(finished_workers.load() >= 2,
                "daemon backpressure should reject overflow requests while the first two remain inflight");
    assert_true(fixture.active_connection_count() >= 2U,
                "daemon backpressure should expose active connection count while requests are inflight");

    cleanup();

    int completed = 0;
    int rejected = 0;
    for (const auto& response : responses) {
      assert_true(response.ok(),
                  "daemon backpressure should always return a parseable response envelope");
      if (response.is_completed()) {
        ++completed;
        continue;
      }

      assert_true(response.error_ref.has_value(),
                  "daemon backpressure rejections should surface a stable error_ref");
      assert_equal(std::string("concurrency_limit_exceeded"), *response.error_ref,
                   "daemon backpressure should reject overflow through AdmissionController");
      ++rejected;
    }

    assert_equal(2, completed,
                 "daemon backpressure should admit requests up to the inflight limit");
    assert_equal(2, rejected,
                 "daemon backpressure should reject requests beyond the inflight limit");
    assert_true(dispatch_backend.max_active_requests >= 2,
                "daemon backpressure should exercise concurrent dispatch workers instead of single-thread fallback");
    assert_equal(static_cast<std::size_t>(0U), fixture.active_connection_count(),
                 "daemon backpressure should return active connection count to baseline after drain");
    assert_true(fixture.daemon_stopped_cleanly(),
                "daemon backpressure fixture should stop cleanly after releasing blocked requests");
  } catch (...) {
    cleanup();
    throw;
  }
}

void payload_budget_rejection_does_not_reach_runtime_dispatch() {
  std::atomic<int> runtime_calls{0};

  DaemonAccessPipelineOptions options;
  options.bootstrap_config.allowed_protocols = {"ipc_uds"};
  options.auth_view.trusted_local_subjects = {"local://uid/1000"};
  options.publish_view.max_payload_bytes = 8U;
  options.daemon_profile_id = "daemon.payload.limit.integration";
  options.runtime_dispatch_backend = [&runtime_calls](const RuntimeDispatchRequest& request) {
    ++runtime_calls;
    return make_completed_result(request, request.packet.payload);
  };

  DaemonInProcessFixture fixture(std::move(options));

  const auto response = fixture.make_client().submit("payload-exceeds-budget");
  assert_true(response.ok(),
              "daemon payload gate should still return a parseable rejection response");
  assert_true(response.error_ref.has_value(),
              "daemon payload gate should surface payload limit rejection");
  assert_equal(std::string("payload_size_limit_exceeded"), *response.error_ref,
               "daemon payload gate should reject over-budget payloads before runtime dispatch");
  assert_equal(0, runtime_calls.load(),
               "daemon payload gate should not enter runtime dispatch when request validator rejects");
  assert_equal(static_cast<std::size_t>(0U), fixture.active_connection_count(),
               "daemon payload gate should leave no active connections after rejection");

  fixture.stop();
  assert_true(fixture.daemon_stopped_cleanly(),
              "daemon payload gate fixture should stop cleanly after rejection path validation");
}

}  // namespace

int main() {
  try {
    admission_backpressure_rejects_over_limit_concurrency();
    payload_budget_rejection_does_not_reach_runtime_dispatch();
  } catch (const std::exception& ex) {
    std::cerr << "[DaemonBackpressureIntegrationTest] FAILED: " << ex.what() << '\n';
    return 1;
  }

  return 0;
}