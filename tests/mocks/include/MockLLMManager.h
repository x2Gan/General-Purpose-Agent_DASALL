#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "error/ErrorInfo.h"
#include "error/ResultCode.h"
#include "llm/LLMResponse.h"

#include "../../../llm/include/HealthStatus.h"
#include "../../../llm/include/ILLMManager.h"
#include "../../../llm/include/LLMGenerateRequest.h"
#include "../../../llm/include/LLMManagerResult.h"
#include "../../../llm/include/LLMSubsystemConfig.h"

namespace dasall::tests::mocks {

class MockLLMManager : public dasall::llm::ILLMManager {
 public:
  using InitHandler =
      std::function<bool(const dasall::llm::LLMSubsystemConfig&)>;
  using GenerateHandler =
      std::function<dasall::llm::LLMManagerResult(
          const dasall::llm::LLMGenerateRequest&)>;
  using StreamHandler =
      std::function<dasall::llm::LLMManagerResult(
          const dasall::llm::LLMGenerateRequest&, dasall::llm::IStreamObserver*)>;
  using HealthHandler = std::function<dasall::llm::HealthStatus()>;

  static dasall::llm::LLMManagerResult make_success_result(
      std::string content,
      std::string resolved_route,
      std::optional<std::string> request_id = std::nullopt) {
    dasall::contracts::LLMResponse response;
    response.request_id = std::move(request_id);
    response.llm_call_id = std::string{"mock-llm-call"};
    response.response_kind = dasall::contracts::LLMResponseKind::DirectResponse;
    response.content_payload = std::move(content);
    response.completed_at = 1712746800000LL;
    response.model_name = std::string{"mock.model"};
    response.prompt_id = std::string{"mock.prompt"};
    response.prompt_version = std::string{"v1"};
    response.finish_reason = std::string{"stop"};
    response.input_tokens = 16U;
    response.output_tokens = 8U;
    response.total_tokens = 24U;

    dasall::llm::LLMManagerResult result;
    result.response = std::move(response);
    result.resolved_route = std::move(resolved_route);
    result.attempted_routes = std::vector<std::string>{result.resolved_route};
    return result;
  }

  static dasall::llm::LLMManagerResult make_structured_stage_result(
      std::string_view stage,
      std::string payload,
      std::optional<std::string> request_id = std::nullopt) {
    return make_success_result(std::move(payload),
                               default_route_for_stage(stage),
                               std::move(request_id));
  }

  static dasall::llm::LLMManagerResult make_failure_result(
      dasall::contracts::ResultCode code,
      std::string message,
      dasall::llm::LLMFailureCategory category,
      std::string resolved_route,
      std::optional<std::string> request_id = std::nullopt) {
    dasall::contracts::ErrorInfo error;
    error.failure_type = dasall::contracts::classify_result_code(code);
    error.retryable = false;
    error.safe_to_replan = false;
    error.details = dasall::contracts::ErrorDetails{
        .code = static_cast<int>(code),
        .message = std::move(message),
        .stage = std::string{"mock.llm.manager"},
    };
    error.source_ref = dasall::contracts::ErrorSourceRefMinimal{
        .ref_type = std::string{"llm"},
        .ref_id = std::string{"mock.llm.manager"},
    };

    dasall::llm::LLMManagerResult result;
    result.code = code;
    result.error = std::move(error);
    result.resolved_route = std::move(resolved_route);
    if (!result.resolved_route.empty()) {
      result.attempted_routes = std::vector<std::string>{result.resolved_route};
    }
    result.failure_category = category;
    if (request_id.has_value()) {
      result.error->details.message.append(" request_id=");
      result.error->details.message.append(*request_id);
    }
    return result;
  }

  bool init(const dasall::llm::LLMSubsystemConfig& config) override {
    ++init_call_count_;
    last_init_config_ = config;

    if (init_handler_) {
      return init_handler_(config);
    }

    return init_result_;
  }

  dasall::llm::LLMManagerResult generate(
      const dasall::llm::LLMGenerateRequest& request) override {
    ++generate_call_count_;
    last_request_ = request;
    generate_requests_.push_back(request);

    if (generate_handler_) {
      return generate_handler_(request);
    }

    const auto staged_result = stage_results_.find(request.stage);
    if (staged_result != stage_results_.end()) {
      return staged_result->second;
    }

    if (generate_result_.has_value()) {
      return *generate_result_;
    }

    return make_success_result(resolve_default_content(request),
                               resolve_route(request),
                               request.request.request_id);
  }

  dasall::llm::LLMManagerResult stream_generate(
      const dasall::llm::LLMGenerateRequest& request,
      dasall::llm::IStreamObserver* observer) override {
    ++stream_generate_call_count_;
    last_stream_request_ = request;
    stream_generate_requests_.push_back(request);
    last_stream_observer_ = observer;

    if (stream_handler_) {
      return stream_handler_(request, observer);
    }

    return generate(request);
  }

  dasall::llm::HealthStatus health_check() const override {
    ++health_check_call_count_;

    if (health_handler_) {
      return health_handler_();
    }

    return health_status_;
  }

  void set_init_handler(InitHandler handler) { init_handler_ = std::move(handler); }

  void set_init_result(bool init_result) {
    init_result_ = init_result;
    init_handler_ = {};
  }

  void set_generate_handler(GenerateHandler handler) {
    generate_handler_ = std::move(handler);
    generate_result_.reset();
    stage_results_.clear();
  }

  void set_generate_result(dasall::llm::LLMManagerResult result) {
    generate_result_ = std::move(result);
    generate_handler_ = {};
    stage_results_.clear();
  }

  void set_stage_result(std::string stage, dasall::llm::LLMManagerResult result) {
    stage_results_[std::move(stage)] = std::move(result);
    generate_handler_ = {};
    generate_result_.reset();
  }

  void set_structured_stage_payload(
      std::string stage,
      std::string payload,
      std::optional<std::string> request_id = std::nullopt) {
    auto result = make_structured_stage_result(stage, std::move(payload), std::move(request_id));
    set_stage_result(std::move(stage), std::move(result));
  }

  void clear_stage_results() { stage_results_.clear(); }

  void clear_recorded_requests() {
    last_request_.reset();
    last_stream_request_.reset();
    generate_requests_.clear();
    stream_generate_requests_.clear();
  }

  void set_stream_handler(StreamHandler handler) { stream_handler_ = std::move(handler); }

  void set_health_handler(HealthHandler handler) { health_handler_ = std::move(handler); }

  void set_health_status(dasall::llm::HealthStatus status) {
    health_status_ = std::move(status);
    health_handler_ = {};
  }

  void set_default_content(std::string default_content) {
    default_content_ = std::move(default_content);
  }

  [[nodiscard]] int init_call_count() const { return init_call_count_; }
  [[nodiscard]] int generate_call_count() const { return generate_call_count_; }
  [[nodiscard]] int stream_generate_call_count() const {
    return stream_generate_call_count_;
  }
  [[nodiscard]] int health_check_call_count() const {
    return health_check_call_count_;
  }
  [[nodiscard]] const std::optional<dasall::llm::LLMSubsystemConfig>& last_init_config()
      const {
    return last_init_config_;
  }
  [[nodiscard]] const std::optional<dasall::llm::LLMGenerateRequest>& last_request() const {
    return last_request_;
  }
  [[nodiscard]] const std::vector<dasall::llm::LLMGenerateRequest>& generate_requests()
      const {
    return generate_requests_;
  }
  [[nodiscard]] const std::optional<dasall::llm::LLMGenerateRequest>&
  last_stream_request() const {
    return last_stream_request_;
  }
  [[nodiscard]] const std::vector<dasall::llm::LLMGenerateRequest>& stream_generate_requests()
      const {
    return stream_generate_requests_;
  }
  [[nodiscard]] dasall::llm::IStreamObserver* last_stream_observer() const {
    return last_stream_observer_;
  }

 private:
  static std::string default_route_for_stage(std::string_view stage) {
    if (!stage.empty()) {
      return std::string{"mock.route."} + std::string{stage};
    }

    return std::string{"mock.route.default"};
  }

  static std::string resolve_route(const dasall::llm::LLMGenerateRequest& request) {
    if (request.request.model_route.has_value() && !request.request.model_route->empty()) {
      return *request.request.model_route;
    }

    if (!request.stage.empty()) {
      return default_route_for_stage(request.stage);
    }

    return default_route_for_stage(std::string_view{});
  }

  std::string resolve_default_content(
      const dasall::llm::LLMGenerateRequest& request) const {
    if (!default_content_.empty()) {
      return default_content_;
    }

    if (!request.stage.empty()) {
      return std::string{"mock-content-for-"} + request.stage;
    }

    return std::string{"mock-content"};
  }

  InitHandler init_handler_;
  GenerateHandler generate_handler_;
  StreamHandler stream_handler_;
  HealthHandler health_handler_;
  std::optional<dasall::llm::LLMSubsystemConfig> last_init_config_;
  std::optional<dasall::llm::LLMGenerateRequest> last_request_;
  std::optional<dasall::llm::LLMGenerateRequest> last_stream_request_;
  std::vector<dasall::llm::LLMGenerateRequest> generate_requests_;
  std::vector<dasall::llm::LLMGenerateRequest> stream_generate_requests_;
  std::optional<dasall::llm::LLMManagerResult> generate_result_;
  std::unordered_map<std::string, dasall::llm::LLMManagerResult> stage_results_;
  dasall::llm::IStreamObserver* last_stream_observer_{nullptr};
  bool init_result_{true};
  std::string default_content_;
  dasall::llm::HealthStatus health_status_{
      .ready = true,
      .degraded = false,
      .message = "mock llm manager healthy",
  };
  int init_call_count_{0};
  int generate_call_count_{0};
  int stream_generate_call_count_{0};
  mutable int health_check_call_count_{0};
};

}  // namespace dasall::tests::mocks
