#pragma once

#include <cstddef>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include "metrics/AggregationEngine.h"
#include "metrics/CardinalityGuard.h"
#include "metrics/IMeter.h"
#include "metrics/IMetricsProvider.h"
#include "metrics/InstrumentRegistry.h"
#include "metrics/MetricsSnapshots.h"
#include "metrics/MetricTypes.h"

namespace dasall::infra::metrics {

class MetricsFacade final : public IMetricsProvider {
 public:
  explicit MetricsFacade(std::size_t max_cardinality_per_metric = 200U);

  MetricsOperationStatus init(const MetricsProviderConfig& config) override;
  [[nodiscard]] std::shared_ptr<IMeter> get_meter(const MeterScope& scope) override;
  MetricsOperationStatus force_flush(const MetricsCallDeadline& timeout) override;
  MetricsOperationStatus shutdown(const MetricsCallDeadline& timeout) override;

  [[nodiscard]] AggregationSnapshot aggregation_snapshot() const;
  [[nodiscard]] MetricsModuleSnapshot module_snapshot() const;
  [[nodiscard]] std::string_view lifecycle_state_name() const;
  [[nodiscard]] std::size_t record_attempt_count() const;
  [[nodiscard]] const std::optional<MetricSample>& last_recorded_sample() const;
  [[nodiscard]] const std::optional<MeterScope>& last_scope() const;

 private:
  class FacadeMeter;

  enum class LifecycleState {
    Created,
    Initialized,
    Stopped,
  };

  using MeterMap = std::map<std::string, std::shared_ptr<FacadeMeter>>;

  friend class FacadeMeter;

  [[nodiscard]] MetricsOperationStatus invalid_transition(
      std::string_view operation,
      std::string_view expected_state) const;
  [[nodiscard]] MetricsOperationStatus record_sample(const MeterScope& scope,
                                                     const MetricSample& sample);
  [[nodiscard]] static std::string make_scope_key(const MeterScope& scope);

  LifecycleState lifecycle_state_ = LifecycleState::Created;
  std::size_t max_cardinality_per_metric_ = 200U;
  MetricsProviderConfig config_{};
  AggregationEngine aggregation_engine_{};
  CardinalityGuard cardinality_guard_{};
  InstrumentRegistry registry_;
  MeterMap meters_;
  std::optional<MeterScope> last_scope_;
  std::optional<MetricSample> last_recorded_sample_;
  std::size_t record_attempt_count_ = 0;
  MetricsModuleSnapshot module_snapshot_{};
};

}  // namespace dasall::infra::metrics