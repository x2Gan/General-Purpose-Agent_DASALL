#include "metrics/InstrumentRegistry.h"

#include <string>
#include <string_view>

#include "metrics/MetricsErrors.h"

namespace dasall::infra::metrics {
namespace {

constexpr std::string_view kInstrumentRegistrySourceRef = "InstrumentRegistry";

[[nodiscard]] InstrumentRegistrationResult make_registration_failure(
    MetricsErrorCode code,
    std::string message,
    std::string stage) {
  const auto mapping = map_metrics_error_code(code);
  return InstrumentRegistrationResult::failure(
      mapping.result_code,
      std::move(message),
      std::move(stage),
      std::string(kInstrumentRegistrySourceRef) + ":" +
          std::string(metrics_error_code_name(code)));
}

[[nodiscard]] bool identities_are_equivalent(const MetricIdentity& lhs,
                                             const MetricIdentity& rhs) {
  return lhs.name == rhs.name && lhs.type == rhs.type && lhs.unit == rhs.unit &&
         lhs.description == rhs.description;
}

[[nodiscard]] std::string instrument_type_prefix(MetricType type) {
  switch (type) {
    case MetricType::Counter:
      return "counter";
    case MetricType::Gauge:
      return "gauge";
    case MetricType::Histogram:
      return "histogram";
    case MetricType::UpDownCounter:
      return "updown-counter";
  }

  return "metric";
}

[[nodiscard]] InstrumentHandle make_handle(const MetricIdentity& identity) {
  return InstrumentHandle{
      .instrument_key = instrument_type_prefix(identity.type) + "://" + identity.name,
  };
}

}  // namespace

InstrumentRegistrationResult InstrumentRegistry::register_identity(
    const MetricIdentity& identity) {
  if (!identity.is_valid()) {
    return make_registration_failure(
        MetricsErrorCode::IdentityInvalid,
        "instrument registry requires a valid metric identity before registration",
        "metrics.registry.register");
  }

  const auto existing = entries_.find(identity.name);
  if (existing == entries_.end()) {
    auto handle = make_handle(identity);
    entries_.emplace(identity.name,
                     InstrumentEntry{
                         .identity = identity,
                         .handle = handle,
                     });
    return InstrumentRegistrationResult::success(std::move(handle), true);
  }

  if (!identities_are_equivalent(existing->second.identity, identity)) {
    return make_registration_failure(
        MetricsErrorCode::IdentityInvalid,
        "instrument registry rejects same-name registrations when metric semantics differ",
        "metrics.registry.register");
  }

  return InstrumentRegistrationResult::success(existing->second.handle, false);
}

std::optional<InstrumentHandle> InstrumentRegistry::find_identity(
    std::string_view metric_name) const {
  if (metric_name.empty()) {
    return std::nullopt;
  }

  const auto existing = entries_.find(std::string(metric_name));
  if (existing == entries_.end()) {
    return std::nullopt;
  }

  return existing->second.handle;
}

std::size_t InstrumentRegistry::size() const {
  return entries_.size();
}

}  // namespace dasall::infra::metrics