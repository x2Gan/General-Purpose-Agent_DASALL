#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "tracing/TraceErrors.h"
#include "tracing/TraceTypes.h"

namespace dasall::infra::tracing {

inline constexpr std::string_view kTraceHealthDetailNamespace =
    "status://tracing/health";

struct TraceHealthSnapshot {
  TraceModuleSnapshot module_snapshot{.queue_depth = 0U,
                                      .dropped_total = 0U,
                                      .exporter_state = "uninitialized",
                                      .degraded = false};
  bool degraded_mode = false;
  std::uint64_t consecutive_failure_total = 0;
  std::uint64_t degrade_enter_total = 0;
  std::uint64_t recovery_success_total = 0;
  std::optional<TraceErrorCode> last_error_code;
  std::string last_failure_reason;
  std::string detail_ref =
      std::string(kTraceHealthDetailNamespace) + "/uninitialized";

  [[nodiscard]] bool is_valid() const {
    return module_snapshot.is_valid() && is_printable_ascii(last_failure_reason) &&
           !detail_ref.empty() && is_printable_ascii(detail_ref);
  }
};

class TraceHealthProbe {
 public:
  explicit TraceHealthProbe(std::uint32_t consecutive_failure_threshold = 2U);

  TraceOperationStatus observe_result(const TraceOperationStatus& pipeline_status,
                                      const TraceModuleSnapshot& module_snapshot);
  TraceOperationStatus enter_degraded(TraceErrorCode error_code,
                                      std::string reason,
                                      TraceModuleSnapshot module_snapshot);
  TraceOperationStatus recover_to_healthy(std::string reason,
                                          TraceModuleSnapshot module_snapshot);

  [[nodiscard]] bool is_degraded() const;
  [[nodiscard]] std::uint64_t consecutive_failure_total() const;
  [[nodiscard]] std::uint64_t degrade_enter_total() const;
  [[nodiscard]] std::uint64_t recovery_success_total() const;
  [[nodiscard]] const TraceHealthSnapshot& snapshot() const;
  [[nodiscard]] const std::string& last_failure_reason() const;
  [[nodiscard]] std::optional<TraceErrorCode> last_error_code() const;

 private:
  [[nodiscard]] TraceOperationStatus invalid_request(std::string message,
                                                     std::string stage) const;
  [[nodiscard]] static std::optional<TraceErrorCode> infer_error_code(
      const TraceOperationStatus& pipeline_status);
  [[nodiscard]] static std::string make_detail_ref(
      bool degraded_mode,
      std::optional<TraceErrorCode> error_code,
      std::string_view fallback_segment);

  std::uint32_t consecutive_failure_threshold_ = 2U;
  TraceHealthSnapshot snapshot_{};
};

}  // namespace dasall::infra::tracing