#include <deque>
#include <exception>
#include <iostream>
#include <memory>
#include <utility>

#include "secret/SecretHealthProbe.h"
#include "dasall/tests/support/TestAssertions.h"

namespace {

class ScriptedSecretHealthSignalProvider final
    : public dasall::infra::secret::ISecretHealthSignalProvider {
 public:
  void push(dasall::infra::secret::SecretHealthProbeSignals signals) {
    scripted_signals_.push_back(std::move(signals));
  }

  [[nodiscard]] dasall::infra::secret::SecretHealthProbeSignals sample() const override {
    if (scripted_signals_.empty()) {
      return dasall::infra::secret::SecretHealthProbeSignals{
          .backend_status = {
              .state = dasall::infra::secret::SecretBackendState::Available,
              .backend_ref = std::string("mock.primary"),
              .rate_limited = false,
              .read_only_fallback_ready = true,
              .last_error_code = std::nullopt,
              .detail_ref = std::string("status://secret/backend/mock.primary/healthy"),
          },
          .cache_stale = false,
          .active_lease_count = 2,
          .rotation_status = {
              .rotation_backlog = 0,
              .rollback_failures = 0,
              .degraded = false,
              .last_error_code = std::nullopt,
              .detail_ref = std::string("status://secret/rotation/ready"),
          },
      };
    }

    const auto signals = scripted_signals_.front();
    scripted_signals_.pop_front();
    return signals;
  }

 private:
  mutable std::deque<dasall::infra::secret::SecretHealthProbeSignals> scripted_signals_;
};

[[nodiscard]] dasall::infra::secret::SecretHealthProbeSignals make_ready_signals() {
  return dasall::infra::secret::SecretHealthProbeSignals{
      .backend_status = {
          .state = dasall::infra::secret::SecretBackendState::Available,
          .backend_ref = std::string("mock.primary"),
          .rate_limited = false,
          .read_only_fallback_ready = true,
          .last_error_code = std::nullopt,
          .detail_ref = std::string("status://secret/backend/mock.primary/healthy"),
      },
      .cache_stale = false,
      .active_lease_count = 2,
      .rotation_status = {
          .rotation_backlog = 0,
          .rollback_failures = 0,
          .degraded = false,
          .last_error_code = std::nullopt,
          .detail_ref = std::string("status://secret/rotation/ready"),
      },
  };
}

void test_secret_health_probe_reports_healthy_snapshot_when_all_signals_are_nominal() {
  using dasall::infra::secret::SecretHealthProbe;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  auto provider = std::make_shared<ScriptedSecretHealthSignalProvider>();
  provider->push(make_ready_signals());

  SecretHealthProbe probe(provider);
  const auto snapshot = probe.sample_secret_health();

  assert_true(snapshot.is_healthy(),
              "SecretHealthProbe should report a healthy secret snapshot when backend, cache and rotation signals are all nominal");
  assert_equal(2, static_cast<int>(snapshot.active_lease_count),
               "SecretHealthProbe should preserve the active lease count inside the secret-private health snapshot");
}

void test_secret_health_probe_marks_backend_down_as_degraded_and_unavailable() {
  using dasall::infra::secret::SecretBackendState;
  using dasall::infra::secret::SecretHealthProbe;
  using dasall::tests::support::assert_true;

  auto provider = std::make_shared<ScriptedSecretHealthSignalProvider>();
  auto signals = make_ready_signals();
  signals.backend_status.state = SecretBackendState::Unavailable;
  signals.backend_status.last_error_code = dasall::contracts::ResultCode::ProviderTimeout;
  signals.backend_status.detail_ref = std::string("status://secret/backend/mock.primary/unavailable");
  provider->push(signals);

  SecretHealthProbe probe(provider);
  const auto snapshot = probe.sample_secret_health();

  assert_true(!snapshot.backend_available && snapshot.degraded && !snapshot.is_healthy(),
              "SecretHealthProbe should surface backend down conditions as unavailable and degraded instead of reporting a false healthy state");
}

void test_secret_health_probe_marks_rotation_backlog_as_degraded() {
  using dasall::infra::secret::SecretHealthProbe;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  auto provider = std::make_shared<ScriptedSecretHealthSignalProvider>();
  auto signals = make_ready_signals();
  signals.rotation_status.rotation_backlog = 2;
  signals.rotation_status.degraded = true;
  signals.rotation_status.detail_ref = std::string("status://secret/rotation/grace_backlog");
  provider->push(signals);

  SecretHealthProbe probe(provider);
  const auto snapshot = probe.sample_secret_health();

  assert_true(snapshot.degraded && snapshot.has_rotation_backlog() && !snapshot.is_healthy(),
              "SecretHealthProbe should degrade when rotation backlog is still pending inside the secret rotation coordinator status");
  assert_equal(2, static_cast<int>(snapshot.rotation_backlog),
               "SecretHealthProbe should preserve the current rotation backlog count inside the secret-private snapshot");
}

void test_secret_health_probe_marks_cache_stale_as_degraded() {
  using dasall::infra::secret::SecretHealthProbe;
  using dasall::tests::support::assert_true;

  auto provider = std::make_shared<ScriptedSecretHealthSignalProvider>();
  auto signals = make_ready_signals();
  signals.cache_stale = true;
  provider->push(signals);

  SecretHealthProbe probe(provider);
  const auto snapshot = probe.sample_secret_health();

  assert_true(snapshot.cache_stale && snapshot.degraded && !snapshot.is_healthy(),
              "SecretHealthProbe should propagate cache_stale into the secret snapshot and degrade readiness until callers reacquire fresh metadata");
}

}  // namespace

int main() {
  try {
    test_secret_health_probe_reports_healthy_snapshot_when_all_signals_are_nominal();
    test_secret_health_probe_marks_backend_down_as_degraded_and_unavailable();
    test_secret_health_probe_marks_rotation_backlog_as_degraded();
    test_secret_health_probe_marks_cache_stale_as_degraded();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}