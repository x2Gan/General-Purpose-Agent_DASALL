#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include "health/IHealthProbe.h"
#include "policy/PolicyErrors.h"
#include "policy/PolicyTypes.h"

namespace dasall::infra::policy {

inline constexpr std::string_view kPolicyHealthProbeName =
    "infra.policy.snapshot";
inline constexpr std::string_view kPolicyHealthProbeGroup = "readiness";
inline constexpr std::int64_t kPolicyHealthProbeIntervalMs = 5000;
inline constexpr std::int64_t kPolicyHealthProbeTimeoutMs = 100;
inline constexpr std::string_view kPolicyHealthDetailNamespace =
    "status://policy/health";

struct PolicyHealthSignals {
  PolicySnapshot current_snapshot;
  PolicySnapshot last_known_good_snapshot;
  bool safe_mode = false;
  std::uint32_t consecutive_patch_failures = 0;
  bool audit_bridge_degraded = false;
  bool metrics_bridge_degraded = false;
  std::optional<PolicyErrorCode> last_policy_error_code;
  std::string last_failure_reason;

  [[nodiscard]] bool has_current_snapshot() const {
    return current_snapshot.is_valid();
  }

  [[nodiscard]] bool has_last_known_good_snapshot() const {
    return last_known_good_snapshot.is_valid();
  }

  [[nodiscard]] std::uint64_t active_generation() const {
    if (has_current_snapshot()) {
      return current_snapshot.generation;
    }

    if (has_last_known_good_snapshot()) {
      return last_known_good_snapshot.generation;
    }

    return 0U;
  }

  [[nodiscard]] bool has_recent_failure() const {
    return safe_mode || consecutive_patch_failures > 0 ||
           last_policy_error_code.has_value() || !last_failure_reason.empty();
  }

  [[nodiscard]] bool has_consistent_values() const {
    if (last_policy_error_code.has_value() &&
        contracts::classify_result_code(
            map_policy_error_code(*last_policy_error_code).result_code) ==
            contracts::ResultCodeCategory::Unknown) {
      return false;
    }

    if (has_current_snapshot() && has_last_known_good_snapshot() &&
        last_known_good_snapshot.generation > current_snapshot.generation) {
      return false;
    }

    return true;
  }
};

enum class PolicyHealthSampleState {
  Ready = 0,
  Timeout,
  Invalid,
};

struct PolicyHealthSample {
  PolicyHealthSampleState state = PolicyHealthSampleState::Ready;
  PolicyHealthSignals signals{};
  std::int64_t latency_ms = 0;
  std::int64_t sampled_at_unix_ms = 0;
  std::string detail_ref = std::string(kPolicyHealthDetailNamespace) + "/sample";

  [[nodiscard]] bool has_consistent_values() const {
    if (latency_ms < 0 || sampled_at_unix_ms <= 0) {
      return false;
    }

    if (state == PolicyHealthSampleState::Ready) {
      return signals.has_consistent_values();
    }

    return !detail_ref.empty();
  }
};

class IPolicyHealthSignalProvider {
 public:
  virtual ~IPolicyHealthSignalProvider() = default;

  [[nodiscard]] virtual PolicyHealthSample sample(std::int64_t timeout_ms) = 0;
};

class PolicyHealthProbe final : public IHealthProbe {
 public:
  explicit PolicyHealthProbe(
      std::shared_ptr<IPolicyHealthSignalProvider> signal_provider);

  [[nodiscard]] const ProbeDescriptor& descriptor() const {
    return descriptor_;
  }

  [[nodiscard]] ProbeResult probe() override;

 private:
  [[nodiscard]] static ProbeDescriptor make_descriptor();
  [[nodiscard]] static ProbeStatus map_status(
      const PolicyHealthSignals& signals);
  [[nodiscard]] static std::string detail_ref_for_state(
      const PolicyHealthSignals& signals,
      ProbeStatus status);
  [[nodiscard]] ProbeResult make_failure_result(
      contracts::ResultCode error_code,
      ProbeStatus status,
      std::int64_t latency_ms,
      std::int64_t timestamp_ms,
      std::string detail_ref) const;

  std::shared_ptr<IPolicyHealthSignalProvider> signal_provider_;
  ProbeDescriptor descriptor_ = make_descriptor();
};

}  // namespace dasall::infra::policy