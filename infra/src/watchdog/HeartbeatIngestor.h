#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>

#include "error/ErrorInfo.h"
#include "error/ResultCode.h"
#include "watchdog/HeartbeatRegistry.h"
#include "watchdog/HeartbeatSample.h"
#include "watchdog/WatchdogErrors.h"

namespace dasall::infra::watchdog {

struct HeartbeatIngestResult {
  bool ok = false;
  std::optional<WatchdogErrorCode> watchdog_code;
  std::optional<contracts::ResultCode> result_code;
  std::optional<contracts::ErrorInfo> error;
  HeartbeatSample sample{};
  bool accepted = false;

  [[nodiscard]] static HeartbeatIngestResult success(HeartbeatSample sample) {
    return HeartbeatIngestResult{
        .ok = true,
        .watchdog_code = std::nullopt,
        .result_code = std::nullopt,
        .error = std::nullopt,
        .sample = std::move(sample),
        .accepted = true,
    };
  }

  [[nodiscard]] static HeartbeatIngestResult failure(
      std::optional<WatchdogErrorCode> watchdog_code,
      contracts::ResultCode result_code,
      std::string message,
      std::string stage,
      std::string source_ref) {
    return HeartbeatIngestResult{
        .ok = false,
        .watchdog_code = watchdog_code,
        .result_code = result_code,
        .error = contracts::ErrorInfo{
            .failure_type = contracts::classify_result_code(result_code),
            .retryable = false,
            .safe_to_replan = false,
            .details = contracts::ErrorDetails{
                .code = static_cast<int>(result_code),
                .message = std::move(message),
                .stage = std::move(stage),
            },
            .source_ref = contracts::ErrorSourceRefMinimal{
                .ref_type = "infra.watchdog",
                .ref_id = std::move(source_ref),
            },
        },
        .sample = {},
        .accepted = false,
    };
  }

  [[nodiscard]] bool references_only_contract_error_types() const {
    if (!result_code.has_value() && !error.has_value()) {
      return ok && accepted;
    }

    return result_code.has_value() && error.has_value() &&
           error->failure_type.has_value() &&
           *error->failure_type == contracts::classify_result_code(*result_code);
  }
};

struct HeartbeatSampleQueryResult {
  bool ok = false;
  std::optional<WatchdogErrorCode> watchdog_code;
  std::optional<contracts::ResultCode> result_code;
  std::optional<contracts::ErrorInfo> error;
  HeartbeatSample sample{};
  bool has_sample = false;

  [[nodiscard]] static HeartbeatSampleQueryResult success(HeartbeatSample sample) {
    return HeartbeatSampleQueryResult{
        .ok = true,
        .watchdog_code = std::nullopt,
        .result_code = std::nullopt,
        .error = std::nullopt,
        .sample = std::move(sample),
        .has_sample = true,
    };
  }

  [[nodiscard]] static HeartbeatSampleQueryResult failure(
      std::optional<WatchdogErrorCode> watchdog_code,
      contracts::ResultCode result_code,
      std::string message,
      std::string stage,
      std::string source_ref) {
    return HeartbeatSampleQueryResult{
        .ok = false,
        .watchdog_code = watchdog_code,
        .result_code = result_code,
        .error = contracts::ErrorInfo{
            .failure_type = contracts::classify_result_code(result_code),
            .retryable = false,
            .safe_to_replan = false,
            .details = contracts::ErrorDetails{
                .code = static_cast<int>(result_code),
                .message = std::move(message),
                .stage = std::move(stage),
            },
            .source_ref = contracts::ErrorSourceRefMinimal{
                .ref_type = "infra.watchdog",
                .ref_id = std::move(source_ref),
            },
        },
        .sample = {},
        .has_sample = false,
    };
  }

  [[nodiscard]] bool references_only_contract_error_types() const {
    if (!result_code.has_value() && !error.has_value()) {
      return ok && has_sample;
    }

    return result_code.has_value() && error.has_value() &&
           error->failure_type.has_value() &&
           *error->failure_type == contracts::classify_result_code(*result_code);
  }
};

struct HeartbeatIngestStatus {
  std::uint64_t accepted_total = 0;
  std::uint64_t stale_drop_total = 0;
  std::uint64_t rejected_total = 0;
  std::string last_entity_id;
  std::optional<WatchdogErrorCode> last_watchdog_code;
  std::optional<contracts::ResultCode> last_result_code;

  [[nodiscard]] bool is_valid() const {
    if (last_result_code.has_value() &&
        contracts::classify_result_code(*last_result_code) ==
            contracts::ResultCodeCategory::Unknown) {
      return false;
    }

    if (!last_result_code.has_value() && last_watchdog_code.has_value()) {
      return false;
    }

    return true;
  }
};

class HeartbeatIngestor {
 public:
  explicit HeartbeatIngestor(const HeartbeatRegistry* registry = nullptr,
                             std::size_t max_tracked_entities = 1024U)
      : registry_(registry), max_tracked_entities_(max_tracked_entities) {}

  void bind_registry(const HeartbeatRegistry* registry);
  void set_max_tracked_entities(std::size_t max_tracked_entities);

  [[nodiscard]] HeartbeatIngestResult ingest(const HeartbeatSample& sample);
  [[nodiscard]] HeartbeatSampleQueryResult latest_sample(
      std::string_view entity_id) const;
  [[nodiscard]] HeartbeatIngestStatus status() const;
  [[nodiscard]] std::size_t tracked_entity_count() const;

  void forget_entity(std::string_view entity_id);

 private:
  using SampleMap = std::map<std::string, HeartbeatSample>;

  void record_success(std::string_view entity_id);
  void record_failure(std::string_view entity_id,
                      std::optional<WatchdogErrorCode> watchdog_code,
                      std::optional<contracts::ResultCode> result_code,
                      bool stale_drop);

  const HeartbeatRegistry* registry_ = nullptr;
  std::size_t max_tracked_entities_ = 1024U;
  SampleMap latest_samples_;
  std::uint64_t accepted_total_ = 0;
  std::uint64_t stale_drop_total_ = 0;
  std::uint64_t rejected_total_ = 0;
  std::string last_entity_id_;
  std::optional<WatchdogErrorCode> last_watchdog_code_;
  std::optional<contracts::ResultCode> last_result_code_;
};

}  // namespace dasall::infra::watchdog