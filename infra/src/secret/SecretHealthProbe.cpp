#include "SecretHealthProbe.h"

namespace dasall::infra::secret {

SecretHealthProbe::SecretHealthProbe(
    std::shared_ptr<ISecretHealthSignalProvider> signal_provider)
    : signal_provider_(std::move(signal_provider)) {}

SecretHealthSnapshot SecretHealthProbe::sample_secret_health() const {
  if (!signal_provider_) {
    return SecretHealthSnapshot{
        .backend_available = false,
        .cache_stale = false,
        .active_lease_count = 0,
        .rotation_backlog = 0,
        .degraded = true,
    };
  }

  const SecretHealthProbeSignals signals = signal_provider_->sample();
  const bool backend_available = backend_is_available(signals.backend_status);
  const bool degraded = !signals.has_consistent_values() ||
                        !backend_available ||
                        backend_is_degraded(signals.backend_status) ||
                        signals.cache_stale ||
                        signals.rotation_status.degraded ||
                        signals.rotation_status.has_rotation_backlog();

  return SecretHealthSnapshot{
      .backend_available = backend_available,
      .cache_stale = signals.cache_stale,
      .active_lease_count = signals.active_lease_count,
      .rotation_backlog = signals.rotation_status.rotation_backlog,
      .degraded = degraded,
  };
}

bool SecretHealthProbe::backend_is_available(
    const SecretBackendStatus& backend_status) {
  return backend_status.state == SecretBackendState::Available ||
         backend_status.state == SecretBackendState::Degraded;
}

bool SecretHealthProbe::backend_is_degraded(
    const SecretBackendStatus& backend_status) {
  return backend_status.state == SecretBackendState::Degraded ||
         backend_status.rate_limited || backend_status.last_error_code.has_value();
}

}  // namespace dasall::infra::secret