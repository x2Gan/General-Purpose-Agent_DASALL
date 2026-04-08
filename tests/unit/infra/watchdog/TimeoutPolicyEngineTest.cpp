#include <cstdint>
#include <exception>
#include <iostream>
#include <memory>
#include <string>

#include "watchdog/HeartbeatSample.h"
#include "watchdog/IWatchdogService.h"
#include "watchdog/TimeoutDecision.h"
#include "watchdog/TimeoutPolicyEngine.h"
#include "support/TestAssertions.h"

namespace {

[[nodiscard]] std::shared_ptr<const dasall::infra::watchdog::HeartbeatSample>
make_candidate(std::uint64_t seq_no) {
  using dasall::infra::watchdog::HeartbeatSample;

  return std::make_shared<HeartbeatSample>(HeartbeatSample{
      .entity_id = std::string("runtime.main_loop"),
      .heartbeat_ts = 1711958400000,
      .deadline_ts = 1711958415000,
      .seq_no = seq_no,
  });
}

[[nodiscard]] std::shared_ptr<const dasall::infra::watchdog::HeartbeatSample>
make_history_sample() {
  using dasall::infra::watchdog::HeartbeatSample;

  return std::make_shared<HeartbeatSample>(HeartbeatSample{
      .entity_id = std::string("runtime.peer"),
      .heartbeat_ts = 1711958400500,
      .deadline_ts = 1711958415500,
      .seq_no = 1,
  });
}

[[nodiscard]] std::shared_ptr<const dasall::infra::watchdog::TimeoutDecision>
make_prior_decision(dasall::infra::watchdog::WatchdogTimeoutLevel level,
                    std::uint32_t consecutive_miss) {
  using dasall::infra::watchdog::TimeoutDecision;

  return std::make_shared<TimeoutDecision>(TimeoutDecision{
      .entity_id = std::string("runtime.main_loop"),
      .timeout_level = level,
      .consecutive_miss = consecutive_miss,
      .reason_code = dasall::contracts::ResultCode::ProviderTimeout,
      .evidence_ref = std::string("watchdog://decision/prior"),
  });
}

void test_timeout_policy_engine_keeps_first_miss_inside_grace_window_as_warning() {
  using dasall::infra::watchdog::TimeoutHistoryWindow;
  using dasall::infra::watchdog::TimeoutPolicyEngine;
  using dasall::infra::watchdog::WatchdogServiceConfig;
  using dasall::infra::watchdog::WatchdogTimeoutLevel;
  using dasall::tests::support::assert_true;

  WatchdogServiceConfig config;
  config.scan_interval_ms = 500;
  config.grace_ms = 1000;
  config.consecutive_miss_threshold = 3;

  TimeoutHistoryWindow history;
  history.recent_samples.push_back(make_history_sample());

  TimeoutPolicyEngine engine(config);
  const auto result = engine.evaluate(make_candidate(1), history);

  assert_true(result.ok && result.has_decision(),
              "TimeoutPolicyEngine should produce a TimeoutDecision for a bindable first-miss candidate");
  assert_true(result.decision->timeout_level == WatchdogTimeoutLevel::Warning,
              "TimeoutPolicyEngine should keep the first miss inside the grace scan budget at warning level");
  assert_true(result.decision->consecutive_miss == 1,
              "TimeoutPolicyEngine should start consecutive_miss at one for the first observed timeout candidate");
}

void test_timeout_policy_engine_escalates_warning_to_critical_at_threshold() {
  using dasall::infra::watchdog::TimeoutHistoryWindow;
  using dasall::infra::watchdog::TimeoutPolicyEngine;
  using dasall::infra::watchdog::WatchdogServiceConfig;
  using dasall::infra::watchdog::WatchdogTimeoutLevel;
  using dasall::tests::support::assert_true;

  WatchdogServiceConfig config;
  config.scan_interval_ms = 500;
  config.grace_ms = 500;
  config.consecutive_miss_threshold = 3;

  TimeoutHistoryWindow history;
  history.recent_samples.push_back(make_history_sample());
  history.prior_decisions.push_back(
      make_prior_decision(WatchdogTimeoutLevel::Warning, 2));

  TimeoutPolicyEngine engine(config);
  const auto result = engine.evaluate(make_candidate(2), history);

  assert_true(result.ok && result.has_decision(),
              "TimeoutPolicyEngine should keep producing decisions when prior warning history exists");
  assert_true(result.decision->timeout_level == WatchdogTimeoutLevel::Critical,
              "TimeoutPolicyEngine should escalate warning history to critical once the miss threshold is reached outside grace");
  assert_true(result.decision->consecutive_miss == 3,
              "TimeoutPolicyEngine should increment consecutive_miss from the last same-entity decision");
}

void test_timeout_policy_engine_escalates_repeated_critical_to_fatal() {
  using dasall::infra::watchdog::TimeoutHistoryWindow;
  using dasall::infra::watchdog::TimeoutPolicyEngine;
  using dasall::infra::watchdog::WatchdogServiceConfig;
  using dasall::infra::watchdog::WatchdogTimeoutLevel;
  using dasall::tests::support::assert_true;

  WatchdogServiceConfig config;
  config.scan_interval_ms = 500;
  config.grace_ms = 500;
  config.consecutive_miss_threshold = 3;

  TimeoutHistoryWindow history;
  history.recent_samples.push_back(make_history_sample());
  history.prior_decisions.push_back(
      make_prior_decision(WatchdogTimeoutLevel::Critical, 3));

  TimeoutPolicyEngine engine(config);
  const auto result = engine.evaluate(make_candidate(3), history);

  assert_true(result.ok && result.has_decision(),
              "TimeoutPolicyEngine should accept a candidate that follows a prior critical timeout decision");
  assert_true(result.decision->timeout_level == WatchdogTimeoutLevel::Fatal,
              "TimeoutPolicyEngine should escalate repeated critical timeout history to fatal");
  assert_true(result.decision->reason_code ==
                  dasall::contracts::ResultCode::ProviderTimeout,
              "TimeoutPolicyEngine should keep timeout decisions inside the contracts provider timeout category");
}

void test_timeout_policy_engine_supports_critical_only_policy_after_grace() {
  using dasall::infra::watchdog::TimeoutHistoryWindow;
  using dasall::infra::watchdog::TimeoutPolicyEngine;
  using dasall::infra::watchdog::WatchdogServiceConfig;
  using dasall::infra::watchdog::WatchdogTimeoutLevel;
  using dasall::infra::watchdog::WatchdogTimeoutLevelPolicy;
  using dasall::tests::support::assert_true;

  WatchdogServiceConfig config;
  config.scan_interval_ms = 500;
  config.grace_ms = 500;
  config.consecutive_miss_threshold = 3;
  config.timeout_level_policy =
      WatchdogTimeoutLevelPolicy::CriticalOnly;

  TimeoutHistoryWindow history;
  history.recent_samples.push_back(make_history_sample());
  history.prior_decisions.push_back(
      make_prior_decision(WatchdogTimeoutLevel::Warning, 1));

  TimeoutPolicyEngine engine(config);
  const auto result = engine.evaluate(make_candidate(4), history);

  assert_true(result.ok && result.has_decision(),
              "TimeoutPolicyEngine should evaluate critical_only policy with the same candidate/history bindings");
  assert_true(result.decision->timeout_level == WatchdogTimeoutLevel::Critical,
              "TimeoutPolicyEngine should skip the extended warning phase once grace is exhausted under critical_only policy");
}

void test_timeout_policy_engine_rejects_invalid_candidate_or_unbound_history() {
  using dasall::infra::watchdog::HeartbeatSample;
  using dasall::infra::watchdog::TimeoutHistoryWindow;
  using dasall::infra::watchdog::TimeoutPolicyEngine;
  using dasall::tests::support::assert_true;

  TimeoutPolicyEngine engine;

  const auto missing_history = engine.evaluate(make_candidate(5), TimeoutHistoryWindow{});
  assert_true(!missing_history.ok,
              "TimeoutPolicyEngine should reject candidates when no bindable history window is provided");
  assert_true(missing_history.references_only_contract_error_types(),
              "TimeoutPolicyEngine history binding failures should remain expressible through contracts ResultCode/ErrorInfo");

  auto invalid_candidate = std::make_shared<HeartbeatSample>();
  TimeoutHistoryWindow bindable_history;
  bindable_history.recent_samples.push_back(make_history_sample());

  const auto invalid = engine.evaluate(invalid_candidate, bindable_history);
  assert_true(!invalid.ok,
              "TimeoutPolicyEngine should reject placeholder candidates that do not satisfy HeartbeatSample invariants");
  assert_true(invalid.references_only_contract_error_types(),
              "TimeoutPolicyEngine candidate validation failures should stay inside the contracts categories");
}

}  // namespace

int main() {
  try {
    test_timeout_policy_engine_keeps_first_miss_inside_grace_window_as_warning();
    test_timeout_policy_engine_escalates_warning_to_critical_at_threshold();
    test_timeout_policy_engine_escalates_repeated_critical_to_fatal();
    test_timeout_policy_engine_supports_critical_only_policy_after_grace();
    test_timeout_policy_engine_rejects_invalid_candidate_or_unbound_history();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}