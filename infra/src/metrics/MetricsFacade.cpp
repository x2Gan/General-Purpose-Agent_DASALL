#include "metrics/MetricsFacade.h"

#include <string>
#include <utility>

#include "metrics/MetricsErrors.h"

namespace dasall::infra::metrics {
namespace {

constexpr std::string_view kMetricsFacadeSourceRef = "MetricsFacade";

[[nodiscard]] MetricsOperationStatus make_metrics_failure(
    MetricsErrorCode code,
    std::string message,
    std::string stage) {
  const auto mapping = map_metrics_error_code(code);
  return MetricsOperationStatus::failure(mapping.result_code,
                                         std::move(message),
                                         std::move(stage),
                                         std::string(kMetricsFacadeSourceRef) + ":" +
                                             std::string(metrics_error_code_name(code)));
}

}  // namespace

class MetricsFacade::FacadeMeter final : public IMeter {
 public:
  FacadeMeter(MetricsFacade& owner, MeterScope scope)
      : owner_(owner), scope_(std::move(scope)) {}

  [[nodiscard]] std::optional<InstrumentHandle> create_counter(
      const MetricIdentity& identity) override {
    return create_handle(identity, MetricType::Counter, "counter");
  }

  [[nodiscard]] std::optional<InstrumentHandle> create_gauge(
      const MetricIdentity& identity) override {
    return create_handle(identity, MetricType::Gauge, "gauge");
  }

  [[nodiscard]] std::optional<InstrumentHandle> create_histogram(
      const MetricIdentity& identity) override {
    return create_handle(identity, MetricType::Histogram, "histogram");
  }

  MetricsOperationStatus record(const MetricSample& sample) override {
    return owner_.record_sample(scope_, sample);
  }

 private:
  [[nodiscard]] std::optional<InstrumentHandle> create_handle(
      const MetricIdentity& identity,
      MetricType expected_type,
      std::string_view prefix) const {
    if (!identity.is_valid() || identity.type != expected_type) {
      return std::nullopt;
    }

    return InstrumentHandle{
        .instrument_key = std::string(prefix) + "://" + scope_.name + "/" + identity.name,
    };
  }

  MetricsFacade& owner_;
  MeterScope scope_;
};

MetricsFacade::MetricsFacade() = default;

MetricsOperationStatus MetricsFacade::init(const MetricsProviderConfig& config) {
  if (lifecycle_state_ != LifecycleState::Created) {
    return invalid_transition("init", "created");
  }

  if (!config.is_valid()) {
    return make_metrics_failure(
        MetricsErrorCode::ConfigInvalid,
        "metrics facade requires explicit provider/exporter types and positive reader/exporter intervals",
        "metrics.init");
  }

  config_ = config;
  meters_.clear();
  last_scope_.reset();
  last_recorded_sample_.reset();
  record_attempt_count_ = 0;
  lifecycle_state_ = LifecycleState::Initialized;
  return MetricsOperationStatus::success("metrics-facade://initialized");
}

std::shared_ptr<IMeter> MetricsFacade::get_meter(const MeterScope& scope) {
  if (lifecycle_state_ != LifecycleState::Initialized || !scope.is_valid()) {
    return {};
  }

  const auto scope_key = make_scope_key(scope);
  const auto existing = meters_.find(scope_key);
  if (existing != meters_.end()) {
    last_scope_ = scope;
    return existing->second;
  }

  auto meter = std::make_shared<FacadeMeter>(*this, scope);
  meters_.emplace(scope_key, meter);
  last_scope_ = scope;
  return meter;
}

MetricsOperationStatus MetricsFacade::force_flush(const MetricsCallDeadline& timeout) {
  if (lifecycle_state_ != LifecycleState::Initialized) {
    return make_metrics_failure(
        MetricsErrorCode::ProviderNotReady,
        "metrics facade must be initialized before force_flush()",
        "metrics.force_flush");
  }

  if (!timeout.is_valid()) {
    return make_metrics_failure(
        MetricsErrorCode::ConfigInvalid,
        "metrics force_flush deadline must be greater than zero",
        "metrics.force_flush");
  }

  return MetricsOperationStatus::success("metrics-facade://flushed");
}

MetricsOperationStatus MetricsFacade::shutdown(const MetricsCallDeadline& timeout) {
  if (lifecycle_state_ != LifecycleState::Initialized) {
    return make_metrics_failure(
        MetricsErrorCode::ProviderNotReady,
        "metrics facade must be initialized before shutdown()",
        "metrics.shutdown");
  }

  if (!timeout.is_valid()) {
    return make_metrics_failure(
        MetricsErrorCode::ConfigInvalid,
        "metrics shutdown deadline must be greater than zero",
        "metrics.shutdown");
  }

  lifecycle_state_ = LifecycleState::Stopped;
  meters_.clear();
  return MetricsOperationStatus::success("metrics-facade://stopped");
}

std::string_view MetricsFacade::lifecycle_state_name() const {
  switch (lifecycle_state_) {
    case LifecycleState::Created:
      return "created";
    case LifecycleState::Initialized:
      return "initialized";
    case LifecycleState::Stopped:
      return "stopped";
  }

  return "unknown";
}

std::size_t MetricsFacade::record_attempt_count() const {
  return record_attempt_count_;
}

const std::optional<MetricSample>& MetricsFacade::last_recorded_sample() const {
  return last_recorded_sample_;
}

const std::optional<MeterScope>& MetricsFacade::last_scope() const {
  return last_scope_;
}

MetricsOperationStatus MetricsFacade::invalid_transition(
    std::string_view operation,
    std::string_view expected_state) const {
  return make_metrics_failure(
      MetricsErrorCode::ProviderNotReady,
      "invalid metrics facade lifecycle transition for operation " +
          std::string(operation) + ": expected state " + std::string(expected_state) +
          ", actual state " + std::string(lifecycle_state_name()),
      "metrics.lifecycle");
}

MetricsOperationStatus MetricsFacade::record_sample(const MeterScope& scope,
                                                    const MetricSample& sample) {
  ++record_attempt_count_;

  if (lifecycle_state_ != LifecycleState::Initialized) {
    return make_metrics_failure(
        MetricsErrorCode::ProviderNotReady,
        "metrics facade must be initialized before record()",
        "metrics.record");
  }

  if (!scope.is_valid() || !sample.is_valid()) {
    return make_metrics_failure(
        MetricsErrorCode::IdentityInvalid,
        "metrics facade record() requires a valid meter scope and metric sample identity",
        "metrics.record");
  }

  last_scope_ = scope;
  last_recorded_sample_ = sample;
  return MetricsOperationStatus::success("metrics-facade://recorded");
}

std::string MetricsFacade::make_scope_key(const MeterScope& scope) {
  return scope.name + "|" + scope.version + "|" + scope.schema_url;
}

}  // namespace dasall::infra::metrics