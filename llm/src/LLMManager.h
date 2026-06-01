#pragma once

#include <atomic>
#include <cstdint>
#include <optional>
#include <memory>
#include <string>
#include <string_view>

#include "ILLMManager.h"
#include "LLMSubsystemConfig.h"
#include "adapters/AdapterCallResult.h"
#include "route/AdapterRegistry.h"

namespace dasall::llm {

class UsageAggregator;

namespace execution {
class ResponseNormalizer;
}

namespace observability {
class LLMAuditBridge;
class LLMMetricsBridge;
class LLMTraceBridge;
}

namespace prompt {
class IPromptPipeline;
}

namespace provider {
class ProviderCatalogRepository;
struct ProviderCatalogSnapshot;
}

namespace route {
class ModelRouter;
}

namespace stream {
class StreamSessionRegistry;
}

enum class LLMCallExecutionFailureReason {
  NotInitialized = 0,
  RouteUnavailable = 1,
  RouteBlocked = 2,
  ConcurrencyRejected = 3,
  Timeout = 4,
  AdapterFailure = 5,
};

struct LLMCallExecutionResult {
  std::string route_key;
  std::uint32_t attempts_started = 0U;
  std::optional<AdapterCallResult> adapter_result;
  std::optional<contracts::ErrorInfo> error;
  std::optional<contracts::ResultCode> result_code;
  std::optional<LLMCallExecutionFailureReason> failure_reason;

  [[nodiscard]] bool succeeded() const;
  [[nodiscard]] bool has_consistent_values() const;
};

class LLMCallExecutor {
 public:
  bool init(const LLMSubsystemConfig& config);

  [[nodiscard]] LLMCallExecutionResult execute_unary(std::string_view route_key,
                                                     const contracts::LLMRequest& request,
                                                     route::AdapterRegistry& registry);
  [[nodiscard]] LLMCallExecutionResult execute_stream(
      std::string_view route_key,
      const contracts::LLMRequest& request,
      route::AdapterRegistry& registry,
      stream::StreamSessionRegistry& session_registry,
      IStreamObserver* observer);

  [[nodiscard]] std::uint32_t active_call_count() const;
  [[nodiscard]] bool is_initialized() const;

 private:
  LLMSubsystemConfig config_{};
  std::atomic<std::uint32_t> active_call_count_{0U};
  bool initialized_ = false;
};

class LLMManager final : public ILLMManager {
 public:
  LLMManager();
  LLMManager(std::shared_ptr<prompt::IPromptPipeline> prompt_pipeline,
             std::shared_ptr<route::ModelRouter> model_router,
             std::shared_ptr<route::AdapterRegistry> adapter_registry,
             std::shared_ptr<LLMCallExecutor> call_executor,
             std::shared_ptr<execution::ResponseNormalizer> response_normalizer,
             std::shared_ptr<UsageAggregator> usage_aggregator,
             std::shared_ptr<const provider::ProviderCatalogSnapshot> provider_catalog_snapshot = nullptr,
             std::shared_ptr<stream::StreamSessionRegistry> stream_session_registry = nullptr,
             std::shared_ptr<observability::LLMMetricsBridge> metrics_bridge = nullptr,
             std::shared_ptr<observability::LLMTraceBridge> trace_bridge = nullptr,
             std::shared_ptr<observability::LLMAuditBridge> audit_bridge = nullptr);

  bool init(const LLMSubsystemConfig& config) override;
  [[nodiscard]] LLMManagerResult generate(const LLMGenerateRequest& request) override;
  [[nodiscard]] LLMManagerResult stream_generate(const LLMGenerateRequest& request,
                                                 IStreamObserver* observer) override;
  [[nodiscard]] bool abandon_call(std::string_view llm_call_id) override;
  [[nodiscard]] HealthStatus health_check() const override;

  [[nodiscard]] bool is_initialized() const;

 private:
  LLMSubsystemConfig config_{};
  std::shared_ptr<provider::ProviderCatalogRepository> provider_catalog_repository_;
  std::shared_ptr<const provider::ProviderCatalogSnapshot> provider_catalog_snapshot_;
  std::shared_ptr<prompt::IPromptPipeline> prompt_pipeline_;
  std::shared_ptr<route::ModelRouter> model_router_;
  std::shared_ptr<route::AdapterRegistry> adapter_registry_;
  std::shared_ptr<LLMCallExecutor> call_executor_;
  std::shared_ptr<execution::ResponseNormalizer> response_normalizer_;
  std::shared_ptr<UsageAggregator> usage_aggregator_;
  std::shared_ptr<stream::StreamSessionRegistry> stream_session_registry_;
  std::shared_ptr<observability::LLMMetricsBridge> metrics_bridge_;
  std::shared_ptr<observability::LLMTraceBridge> trace_bridge_;
  std::shared_ptr<observability::LLMAuditBridge> audit_bridge_;
  bool initialized_ = false;
};

}  // namespace dasall::llm