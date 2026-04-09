#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "ServiceTypes.h"

namespace dasall::services::internal {

class ServiceMetricsBridge;

struct ExecutionSubscriptionEvent {
  std::uint64_t sequence = 0;
  std::string payload_json;
};

struct ExecutionSubscriptionStreamState {
  std::deque<ExecutionSubscriptionEvent> buffer;
  std::uint64_t next_sequence = 1;
  std::uint32_t dropped_count = 0;
  bool resync_required = false;
};

struct ExecutionSubscriptionHubDependencies {
  std::size_t max_buffered_events = 64;
  ServiceMetricsBridge* metrics_bridge = nullptr;
};

class ExecutionSubscriptionHub {
 public:
  explicit ExecutionSubscriptionHub(ExecutionSubscriptionHubDependencies dependencies);

  void publish(const CapabilityTargetRef& target,
               const std::string& stream_kind,
               const std::vector<std::string>& events_json_batch);

  [[nodiscard]] ExecutionSubscriptionResult subscribe(const ServiceCallContext& context,
                                                      const ExecutionSubscriptionRequest& request);

 private:
  [[nodiscard]] std::string make_stream_key(const CapabilityTargetRef& target,
                                            const std::string& stream_kind) const;
  [[nodiscard]] ExecutionSubscriptionResult make_validation_failure(
      const std::string& message,
      const std::string& stage,
      const std::string& ref_id) const;
  [[nodiscard]] ExecutionSubscriptionResult make_overflow_result(
      const std::optional<std::string>& next_cursor,
      std::string events_json,
      std::uint32_t dropped_count,
      const std::string& stream_key) const;

  ExecutionSubscriptionHubDependencies dependencies_;
  std::mutex mutex_;
  std::unordered_map<std::string, ExecutionSubscriptionStreamState> streams_;
};

}  // namespace dasall::services::internal