#pragma once

#include <string_view>

#include "health/IHealthPolicy.h"

namespace dasall::infra {

struct HealthEvaluatorOptions {
  std::size_t degraded_threshold = 1;
};

class HealthEvaluator final : public IHealthPolicy {
 public:
  explicit HealthEvaluator(HealthEvaluatorOptions options = {});

  [[nodiscard]] HealthPolicyEvaluationResult evaluate(
      ProbeResultView results) const override;
  [[nodiscard]] std::string_view policy_version() const override;
  [[nodiscard]] HealthTransition evaluate_transition(const HealthSnapshot& previous,
                                                     const HealthSnapshot& current) const;

 private:
  [[nodiscard]] static std::int64_t current_time_unix_ms();
  [[nodiscard]] static std::string_view state_name(HealthState state);

  HealthEvaluatorOptions options_;
};

}  // namespace dasall::infra