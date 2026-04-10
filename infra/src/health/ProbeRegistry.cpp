#include "health/ProbeRegistry.h"

#include "health/HealthErrors.h"

#include <algorithm>
#include <string_view>
#include <utility>

namespace dasall::infra {
namespace {

constexpr std::string_view kProbeRegistrySourceRef = "ProbeRegistry";
constexpr std::int64_t kDefaultLivenessIntervalMs = 2000;
constexpr std::int64_t kDefaultReadinessIntervalMs = 5000;
constexpr std::int64_t kDefaultProbeTimeoutMs = 1000;

[[nodiscard]] ProbeDescriptor make_descriptor(
    const HealthProbeRegistration& registration) {
  const std::int64_t interval_ms = registration.probe_group == "liveness"
                                       ? kDefaultLivenessIntervalMs
                                       : kDefaultReadinessIntervalMs;

  return ProbeDescriptor{
      .probe_name = registration.probe_name,
      .group = registration.probe_group,
      .criticality = ProbeCriticality::NonCritical,
      .interval_ms = interval_ms,
      .timeout_ms = std::min(kDefaultProbeTimeoutMs, interval_ms),
  };
}

[[nodiscard]] ProbeRegistryRegisterResult make_register_failure(
    contracts::ResultCode result_code,
    std::string message,
    std::string stage) {
  return ProbeRegistryRegisterResult::failure(result_code,
                                              std::move(message),
                                              std::move(stage),
                                              std::string(kProbeRegistrySourceRef));
}

[[nodiscard]] ProbeRegistryRemoveResult make_remove_failure(
    contracts::ResultCode result_code,
    std::string message,
    std::string stage) {
  return ProbeRegistryRemoveResult::failure(result_code,
                                            std::move(message),
                                            std::move(stage),
                                            std::string(kProbeRegistrySourceRef));
}

}  // namespace

ProbeRegistryRegisterResult ProbeRegistry::register_probe(
    const HealthProbeRegistration& registration) {
  if (!registration.is_valid()) {
    return make_register_failure(
        contracts::ResultCode::ValidationFieldMissing,
        "probe registry requires explicit probe_name, probe_group, and probe placeholder",
        "health.registry.register");
  }

  if (!is_supported_health_probe_group(registration.probe_group)) {
    return make_register_failure(
        contracts::ResultCode::ValidationFieldMissing,
        "probe registry only accepts liveness/readiness probe groups",
        "health.registry.register");
  }

  if (entries_.find(registration.probe_name) != entries_.end()) {
    return make_register_failure(
        contracts::ResultCode::ValidationFieldMissing,
        "probe registry rejects duplicate probe_name registrations",
        "health.registry.register");
  }

  ProbeDescriptor descriptor = make_descriptor(registration);
  if (!descriptor.has_required_fields()) {
    return make_register_failure(
        contracts::ResultCode::ValidationFieldMissing,
        "probe registry could not derive a valid descriptor from the registration",
        "health.registry.register");
  }

  entries_.emplace(registration.probe_name,
                   ProbeEntry{
                       .descriptor = descriptor,
                       .probe = registration.probe,
                   });
  return ProbeRegistryRegisterResult::success(std::move(descriptor));
}

ProbeRegistryRemoveResult ProbeRegistry::unregister_probe(std::string_view probe_name) {
  if (probe_name.empty()) {
    return make_remove_failure(
        contracts::ResultCode::ValidationFieldMissing,
        "probe registry requires a non-empty probe_name for unregister_probe",
        "health.registry.unregister");
  }

  const auto entry = entries_.find(std::string(probe_name));
  if (entry == entries_.end()) {
    const auto mapping = map_health_error_code(HealthErrorCode::ProbeNotFound);
    return make_remove_failure(
        mapping.result_code,
        "probe registry cannot unregister an unknown probe_name",
        "health.registry.unregister");
  }

  ProbeDescriptor descriptor = entry->second.descriptor;
  entries_.erase(entry);
  return ProbeRegistryRemoveResult::success(std::move(descriptor));
}

std::vector<ProbeDescriptor> ProbeRegistry::list_by_group(std::string_view group) const {
  std::vector<ProbeDescriptor> descriptors;
  if (!is_supported_health_probe_group(group)) {
    return descriptors;
  }

  descriptors.reserve(entries_.size());
  for (const auto& entry : entries_) {
    if (entry.second.descriptor.group == group) {
      descriptors.push_back(entry.second.descriptor);
    }
  }

  return descriptors;
}

std::optional<ProbeDescriptor> ProbeRegistry::find_descriptor(
    std::string_view probe_name) const {
  const auto entry = entries_.find(std::string(probe_name));
  if (entry == entries_.end()) {
    return std::nullopt;
  }

  return entry->second.descriptor;
}

IHealthProbe* ProbeRegistry::find_probe(std::string_view probe_name) const {
  const auto entry = entries_.find(std::string(probe_name));
  if (entry == entries_.end()) {
    return nullptr;
  }

  return entry->second.probe;
}

std::size_t ProbeRegistry::size() const {
  return entries_.size();
}

}  // namespace dasall::infra