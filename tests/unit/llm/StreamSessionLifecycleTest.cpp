#include <deque>
#include <exception>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "support/TestAssertions.h"

#include "../../../llm/include/ILLMTransport.h"
#include "../../../llm/src/adapters/OpenAICompatibleAdapter.h"
#include "../../../llm/src/stream/IStreamObserver.h"
#include "../../../llm/src/stream/StreamSessionRegistry.h"

namespace {

using dasall::contracts::ErrorInfo;
using dasall::contracts::LLMRequest;
using dasall::contracts::LLMRequestMode;
using dasall::contracts::ResultCode;
using dasall::llm::AdapterCallResult;
using dasall::llm::ILLMTransport;
using dasall::llm::IStreamObserver;
using dasall::llm::LLMAdapterConfig;
using dasall::llm::LLMTransportHeader;
using dasall::llm::LLMTransportMethod;
using dasall::llm::LLMTransportRequest;
using dasall::llm::LLMTransportResponse;
using dasall::llm::OpenAICompatibleAdapter;
using dasall::llm::StreamObserverFeedback;
using dasall::llm::StreamSessionRef;
using dasall::llm::stream::StreamSessionMutationStatus;
using dasall::llm::stream::StreamSessionRegistry;
using dasall::llm::stream::StreamSessionRegistryConfig;
using dasall::llm::stream::StreamSessionState;
using dasall::tests::support::assert_equal;
using dasall::tests::support::assert_true;

class CapturingTransport final : public ILLMTransport {
 public:
  [[nodiscard]] LLMTransportResponse send(const LLMTransportRequest& request) override {
    ++call_count;
    last_request = request;
    return next_response;
  }

  LLMTransportResponse next_response;
  std::optional<LLMTransportRequest> last_request;
  int call_count = 0;
};

class RecordingStreamObserver final : public IStreamObserver {
 public:
  StreamObserverFeedback on_stream_session_started(
      const StreamSessionRef& session_ref) override {
    started_session_ids.push_back(session_ref.session_id);
    if (session_feedbacks.empty()) {
      return StreamObserverFeedback::success();
    }

    auto feedback = session_feedbacks.front();
    session_feedbacks.pop_front();
    return feedback;
  }

  StreamObserverFeedback on_stream_delta(std::string_view delta) override {
    deltas.emplace_back(delta);
    if (delta_feedbacks.empty()) {
      return StreamObserverFeedback::success();
    }

    auto feedback = delta_feedbacks.front();
    delta_feedbacks.pop_front();
    return feedback;
  }

  void on_stream_completed(const AdapterCallResult& result) override {
    completed_results.push_back(result);
  }

  void on_stream_failed(const ErrorInfo& error,
                        std::optional<ResultCode> result_code) override {
    failed_errors.push_back(error);
    failed_result_codes.push_back(result_code);
  }

  std::deque<StreamObserverFeedback> session_feedbacks;
  std::deque<StreamObserverFeedback> delta_feedbacks;
  std::vector<std::string> started_session_ids;
  std::vector<std::string> deltas;
  std::vector<AdapterCallResult> completed_results;
  std::vector<ErrorInfo> failed_errors;
  std::vector<std::optional<ResultCode>> failed_result_codes;
};

LLMAdapterConfig make_openai_config() {
  return LLMAdapterConfig{
      .adapter_id = "deepseek-prod",
      .adapter_family = "openai_compatible",
      .provider_instance_id = "deepseek-prod",
      .base_url = "https://api.deepseek.example/v1",
      .base_url_alias = "deepseek/default",
      .auth_ref = "secret://llm/providers/deepseek-prod",
      .header_refs = {"header://llm/providers/deepseek-org"},
      .activation_flag = true,
      .snapshot_version = "2026.05.15",
      .timeout_ms = 4000U,
      .max_retries = 1U,
      .capability_tags = {"cloud", "reasoning", "streaming"},
  };
}

LLMRequest make_stream_request() {
  LLMRequest request;
  request.request_id = "req-036-stream";
  request.llm_call_id = "call-036-stream";
  request.model_route = "deepseek-prod/deepseek-chat";
  request.request_mode = LLMRequestMode::Streaming;
  request.messages = std::vector<std::string>{"system: stream carefully",
                                              "user: emit deltas"};
  request.created_at = 1712966404000LL;
  request.prompt_id = "prompt.stream.guard";
  request.prompt_version = "2026-05-15.1";
  request.response_format = "json_object";
  request.max_output_tokens = 128U;
  return request;
}

std::optional<std::string> find_header_value(const LLMTransportRequest& request,
                                             std::string_view name) {
  for (const auto& header : request.headers) {
    if (header.name == name) {
      return header.value;
    }
  }

  return std::nullopt;
}

void test_stream_session_registry_tracks_terminal_cancel_and_cleanup() {
  StreamSessionRegistry registry;
  assert_true(registry.init(StreamSessionRegistryConfig{
                  .max_active_sessions = 2U,
                  .max_buffered_chars = 8U,
                  .session_ttl_ms = 1000U,
              }),
              "StreamSessionRegistry should initialize with explicit lifecycle limits");

  const StreamSessionRef session_ref{.session_id = "stream-session-1"};
  const auto accepted = registry.accept(session_ref, "deepseek-prod/deepseek-chat");
  assert_true(accepted.ok && accepted.state == StreamSessionState::Accepted,
              "StreamSessionRegistry should accept a new stream session into Accepted state");
  assert_equal(1, static_cast<int>(registry.active_session_count()),
               "StreamSessionRegistry should count accepted sessions toward active capacity");

  const auto active = registry.mark_active(session_ref.session_id);
  assert_true(active.ok && active.state == StreamSessionState::Active,
              "StreamSessionRegistry should promote accepted sessions to Active");

  const auto appended = registry.append_delta(session_ref.session_id, 4U);
  assert_true(appended.ok && appended.state == StreamSessionState::Active,
              "StreamSessionRegistry should accumulate streamed characters while Active");

  const auto cancel_requested = registry.request_cancel(session_ref.session_id);
  assert_true(cancel_requested.ok &&
                  cancel_requested.state == StreamSessionState::CancelRequested,
              "StreamSessionRegistry should keep cancel requests explicit until a terminal edge is observed");

  const auto completed = registry.mark_completed(session_ref.session_id);
  assert_true(completed.ok && completed.state == StreamSessionState::Cancelled,
              "StreamSessionRegistry should fail closed by translating completed-after-cancel into Cancelled");

  const auto snapshot = registry.find(session_ref.session_id);
  assert_true(snapshot.has_value() && snapshot->buffered_chars == 4U,
              "StreamSessionRegistry should preserve buffered character accounting until cleanup");
  assert_true(snapshot->state == StreamSessionState::Cancelled,
              "StreamSessionRegistry should expose the terminal Cancelled state through find()");

  assert_true(registry.cleanup(session_ref.session_id),
              "StreamSessionRegistry should clean up terminal sessions");
  assert_true(!registry.find(session_ref.session_id).has_value(),
              "StreamSessionRegistry should erase cleaned terminal sessions");
}

void test_stream_session_registry_fails_closed_on_buffer_overflow() {
  StreamSessionRegistry registry;
  assert_true(registry.init(StreamSessionRegistryConfig{
                  .max_active_sessions = 1U,
                  .max_buffered_chars = 8U,
                  .session_ttl_ms = 1000U,
              }),
              "StreamSessionRegistry overflow coverage should initialize with a small buffer limit");

  const StreamSessionRef session_ref{.session_id = "stream-session-overflow"};
  assert_true(registry.accept(session_ref, "deepseek-prod/deepseek-chat").ok,
              "StreamSessionRegistry should accept the overflow coverage session");

  const auto overflow = registry.append_delta(session_ref.session_id, 9U);
  assert_true(!overflow.ok && overflow.status == StreamSessionMutationStatus::Overflow,
              "StreamSessionRegistry should reject deltas that exceed the configured buffer limit");
  assert_true(overflow.state == StreamSessionState::Failed,
              "StreamSessionRegistry should move overflowed sessions into Failed to remain fail closed");
}

void test_openai_adapter_stream_generate_emits_session_delta_and_terminal_result() {
  auto transport = std::make_shared<CapturingTransport>();
  transport->next_response = LLMTransportResponse{
      .status_code = 200U,
      .body = R"(data: {"id":"chatcmpl-stream-1","model":"deepseek-chat","choices":[{"delta":{"content":"hello "}}]}

data: {"choices":[{"delta":{"content":"world"},"finish_reason":"stop"}],"usage":{"prompt_tokens":12,"completion_tokens":3,"total_tokens":15}}

data: [DONE]

)",
      .error_message = {},
  };

  OpenAICompatibleAdapter adapter(transport);
  assert_true(adapter.init(make_openai_config()),
              "OpenAICompatibleAdapter should initialize before streaming lifecycle coverage");

  RecordingStreamObserver observer;
  const auto session_ref = adapter.stream_generate(make_stream_request(), &observer);

  assert_equal(std::string("deepseek-prod:call-036-stream"), session_ref.session_id,
               "OpenAICompatibleAdapter should project a stable stream session id from adapter_id and llm_call_id");
  assert_true(transport->last_request.has_value(),
              "OpenAICompatibleAdapter streaming should materialize a transport request");
  assert_equal(1, transport->call_count,
               "OpenAICompatibleAdapter streaming should invoke transport exactly once per request");
  assert_true(transport->last_request->method == LLMTransportMethod::Post,
              "OpenAICompatibleAdapter streaming should call chat completions via POST");
  assert_true(find_header_value(*transport->last_request, "Accept") ==
                  std::optional<std::string>("text/event-stream"),
              "OpenAICompatibleAdapter streaming should request text/event-stream responses");
  assert_true(transport->last_request->body.find("\"stream\":true") != std::string::npos,
              "OpenAICompatibleAdapter streaming should set stream=true in the request body");
  assert_true(transport->last_request->body.find("\"stream_options\":{\"include_usage\":true}") !=
                  std::string::npos,
              "OpenAICompatibleAdapter streaming should request terminal usage payloads for aggregation");

  assert_equal(1, static_cast<int>(observer.started_session_ids.size()),
               "OpenAICompatibleAdapter streaming should emit exactly one session-start callback");
  assert_equal(std::string("deepseek-prod:call-036-stream"),
               observer.started_session_ids.front(),
               "OpenAICompatibleAdapter streaming should forward the concrete session id to observers");
  assert_equal(2, static_cast<int>(observer.deltas.size()),
               "OpenAICompatibleAdapter streaming should forward each parsed delta to observers");
  assert_equal(std::string("hello "), observer.deltas.at(0),
               "OpenAICompatibleAdapter streaming should preserve the first content delta");
  assert_equal(std::string("world"), observer.deltas.at(1),
               "OpenAICompatibleAdapter streaming should preserve the terminal content delta");
  assert_equal(1, static_cast<int>(observer.completed_results.size()),
               "OpenAICompatibleAdapter streaming should emit one terminal completed callback");
  assert_true(observer.failed_errors.empty(),
              "OpenAICompatibleAdapter streaming should not emit failure callbacks on the happy path");

  const auto& completed = observer.completed_results.front();
  assert_true(completed.response.has_value() && completed.usage.has_value(),
              "OpenAICompatibleAdapter streaming should surface a terminal response and usage fragment");
  assert_equal(std::string("hello world"), *completed.response->content_payload,
               "OpenAICompatibleAdapter streaming should aggregate parsed content deltas into the terminal payload");
  assert_equal(std::string("stop"), *completed.response->finish_reason,
               "OpenAICompatibleAdapter streaming should preserve the provider finish_reason");
  assert_true(completed.usage->prompt_tokens == 12U &&
                  completed.usage->completion_tokens == 3U &&
                  completed.usage->total_tokens == 15U,
              "OpenAICompatibleAdapter streaming should preserve terminal usage counts for downstream aggregation");
  assert_equal(std::string("chatcmpl-stream-1"),
               completed.provider_diagnostics.provider_trace_id,
               "OpenAICompatibleAdapter streaming should preserve provider trace ids in adapter diagnostics");
}

void test_openai_adapter_stream_generate_reports_observer_rejection() {
  auto transport = std::make_shared<CapturingTransport>();
  transport->next_response = LLMTransportResponse{
      .status_code = 200U,
      .body = R"(data: {"id":"chatcmpl-stream-2","model":"deepseek-chat","choices":[{"delta":{"content":"reject-me"}}]}

data: [DONE]

)",
      .error_message = {},
  };

  OpenAICompatibleAdapter adapter(transport);
  assert_true(adapter.init(make_openai_config()),
              "OpenAICompatibleAdapter should initialize before observer rejection coverage");

  RecordingStreamObserver observer;
  observer.delta_feedbacks.push_back(
      StreamObserverFeedback::reject(ResultCode::ValidationFieldMissing,
                                     "downstream observer rejected delta"));

  const auto session_ref = adapter.stream_generate(make_stream_request(), &observer);

  assert_equal(std::string("deepseek-prod:call-036-stream"), session_ref.session_id,
               "OpenAICompatibleAdapter should still expose the started session id when a downstream observer rejects a delta");
  assert_equal(1, static_cast<int>(observer.failed_errors.size()),
               "OpenAICompatibleAdapter should translate observer rejection into one terminal failure callback");
  assert_true(observer.completed_results.empty(),
              "OpenAICompatibleAdapter should stop before completed when a downstream observer rejects a delta");
  assert_true(observer.failed_result_codes.front().has_value() &&
                  *observer.failed_result_codes.front() == ResultCode::ValidationFieldMissing,
              "OpenAICompatibleAdapter should preserve the observer-supplied ResultCode on stream failure");
  assert_equal(std::string("llm.openai_compatible.stream_generate"),
               observer.failed_errors.front().details.stage,
               "OpenAICompatibleAdapter should anchor stream observer failures on the streaming adapter stage");
}

}  // namespace

int main() {
  try {
    test_stream_session_registry_tracks_terminal_cancel_and_cleanup();
    test_stream_session_registry_fails_closed_on_buffer_overflow();
    test_openai_adapter_stream_generate_emits_session_delta_and_terminal_result();
    test_openai_adapter_stream_generate_reports_observer_rejection();
  } catch (const std::exception& error) {
    std::cerr << "StreamSessionLifecycleTest failed: " << error.what() << '\n';
    return 1;
  }

  return 0;
}
