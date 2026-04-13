#pragma once

#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "llm/LLMRequest.h"
#include "llm/LLMResponse.h"

#include "../../../llm/include/ILLMAdapter.h"
#include "../../../llm/include/stream/StreamSessionRef.h"
#include "../../../llm/src/adapters/AdapterCallResult.h"

namespace dasall::tests::mocks {

class MockLLMAdapter : public dasall::llm::ILLMAdapter {
 public:
  using Handler = std::function<std::string(const std::string&)>;
  using InitHandler = std::function<bool(const dasall::llm::LLMAdapterConfig&)>;
  using GenerateHandler =
      std::function<dasall::llm::AdapterCallResult(const dasall::contracts::LLMRequest&)>;
  using StreamHandler = std::function<dasall::llm::StreamSessionRef(
      const dasall::contracts::LLMRequest&, dasall::llm::IStreamObserver*)>;
  using HealthHandler = std::function<dasall::llm::HealthStatus()>;

  bool init(const dasall::llm::LLMAdapterConfig& config) override {
    ++init_call_count_;
    last_init_config_ = config;

    if (init_handler_) {
      return init_handler_(config);
    }

    return init_result_;
  }

  dasall::llm::AdapterCallResult generate(
      const dasall::contracts::LLMRequest& request) override {
    ++generate_call_count_;
    last_request_ = request;
    last_prompt_ = extract_prompt(request);

    if (generate_handler_) {
      return generate_handler_(request);
    }

    if (generate_result_.has_value()) {
      return *generate_result_;
    }

    return make_success_result(resolve_legacy_response(last_prompt_));
  }

  dasall::llm::StreamSessionRef stream_generate(
      const dasall::contracts::LLMRequest& request,
      dasall::llm::IStreamObserver* observer) override {
    ++stream_generate_call_count_;
    last_stream_request_ = request;
    last_stream_observer_ = observer;

    if (stream_handler_) {
      return stream_handler_(request, observer);
    }

    return stream_session_;
  }

  dasall::llm::HealthStatus health_check() override {
    ++health_check_call_count_;

    if (health_handler_) {
      return health_handler_();
    }

    return health_status_;
  }

  void set_handler(Handler handler) { handler_ = std::move(handler); }

  void set_init_handler(InitHandler handler) { init_handler_ = std::move(handler); }

  void set_init_result(bool init_result) {
    init_result_ = init_result;
    init_handler_ = {};
  }

  void set_generate_handler(GenerateHandler handler) {
    generate_handler_ = std::move(handler);
    generate_result_.reset();
  }

  void set_generate_result(dasall::llm::AdapterCallResult result) {
    generate_result_ = std::move(result);
    generate_handler_ = {};
  }

  void set_stream_handler(StreamHandler handler) { stream_handler_ = std::move(handler); }

  void set_stream_session(dasall::llm::StreamSessionRef session) {
    stream_session_ = std::move(session);
    stream_handler_ = {};
  }

  void set_health_handler(HealthHandler handler) { health_handler_ = std::move(handler); }

  void set_health_status(dasall::llm::HealthStatus status) {
    health_status_ = std::move(status);
    health_handler_ = {};
  }

  std::string invoke(const std::string& prompt) {
    const auto result = generate(make_legacy_request(prompt));
    if (result.response.has_value() && result.response->content_payload.has_value()) {
      return *result.response->content_payload;
    }

    return {};
  }

  void set_default_response(std::string response) { default_response_ = std::move(response); }
  int init_call_count() const { return init_call_count_; }
  int call_count() const { return generate_call_count_; }
  int generate_call_count() const { return generate_call_count_; }
  int stream_generate_call_count() const { return stream_generate_call_count_; }
  int health_check_call_count() const { return health_check_call_count_; }
  const std::string& last_prompt() const { return last_prompt_; }
  const std::optional<dasall::llm::LLMAdapterConfig>& last_init_config() const {
    return last_init_config_;
  }
  const std::optional<dasall::contracts::LLMRequest>& last_request() const {
    return last_request_;
  }
  const std::optional<dasall::contracts::LLMRequest>& last_stream_request() const {
    return last_stream_request_;
  }
  dasall::llm::IStreamObserver* last_stream_observer() const {
    return last_stream_observer_;
  }

 private:
  static dasall::contracts::LLMRequest make_legacy_request(const std::string& prompt) {
    dasall::contracts::LLMRequest request;
    request.request_mode = dasall::contracts::LLMRequestMode::Unary;
    request.messages = std::vector<std::string>{prompt};
    return request;
  }

  static std::string extract_prompt(const dasall::contracts::LLMRequest& request) {
    if (!request.messages.has_value() || request.messages->empty()) {
      return {};
    }

    return request.messages->front();
  }

  static dasall::llm::AdapterCallResult make_success_result(std::string content) {
    dasall::contracts::LLMResponse response;
    response.response_kind = dasall::contracts::LLMResponseKind::DirectResponse;
    response.content_payload = std::move(content);

    dasall::llm::AdapterCallResult result;
    result.response = std::move(response);
    return result;
  }

  std::string resolve_legacy_response(const std::string& prompt) const {
    if (handler_) {
      return handler_(prompt);
    }

    return default_response_;
  }

  Handler handler_;
  InitHandler init_handler_;
  GenerateHandler generate_handler_;
  StreamHandler stream_handler_;
  HealthHandler health_handler_;
  std::optional<dasall::llm::LLMAdapterConfig> last_init_config_;
  std::optional<dasall::contracts::LLMRequest> last_request_;
  std::optional<dasall::contracts::LLMRequest> last_stream_request_;
  std::optional<dasall::llm::AdapterCallResult> generate_result_;
  dasall::llm::IStreamObserver* last_stream_observer_{nullptr};
  bool init_result_{true};
  std::string default_response_{"mock-llm-response"};
  std::string last_prompt_;
  dasall::llm::StreamSessionRef stream_session_{.session_id = "mock-stream-session"};
  dasall::llm::HealthStatus health_status_{
      .ready = true,
      .degraded = false,
      .message = "mock adapter healthy",
  };
  int init_call_count_{0};
  int generate_call_count_{0};
  int stream_generate_call_count_{0};
  int health_check_call_count_{0};
};

}  // namespace dasall::tests::mocks
