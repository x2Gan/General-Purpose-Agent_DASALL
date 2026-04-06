#include <exception>
#include <iostream>
#include <string>
#include <vector>

#include "metrics/MetricsConfigPolicy.h"
#include "dasall/tests/support/TestAssertions.h"

namespace {

void test_metrics_config_policy_keeps_frozen_defaults() {
  using dasall::infra::metrics::MetricsConfigPolicy;
  using dasall::tests::support::assert_true;

  MetricsConfigPolicy policy;
  const auto& config = policy.default_config();

  assert_true(config.enabled && config.provider_type == "internal" &&
                  config.exporter_type == "noop" && config.reader_interval_ms == 5000U &&
                  config.exporter_timeout_ms == 30000U &&
                  config.label_allowlist.size() == 5U &&
                  config.has_allowlist_key("module") && config.has_allowlist_key("stage") &&
                  config.has_allowlist_key("profile") && config.has_allowlist_key("outcome") &&
                  config.has_allowlist_key("error_code") && config.histogram_config.is_valid() &&
                  config.audit_on_policy_change,
              "MetricsConfigPolicy should preserve the frozen 6.9 defaults for provider/exporter/interval/labels and histogram buckets");
}

void test_metrics_config_policy_merges_profile_deploy_runtime_in_order() {
  using dasall::infra::metrics::MetricsConfigPatch;
  using dasall::infra::metrics::MetricsConfigPolicy;
  using dasall::tests::support::assert_true;

  MetricsConfigPolicy policy;
  MetricsConfigPatch profile_patch;
  profile_patch.exporter_type = std::string("prom_text");
  profile_patch.reader_interval_ms = 2000U;
  profile_patch.label_allowlist = std::vector<std::string>{
    "error_code", "outcome", "profile", "stage", "module"};

  MetricsConfigPatch deploy_patch;
  deploy_patch.reader_interval_ms = 1000U;
  deploy_patch.exporter_timeout_ms = 12000U;

  MetricsConfigPatch runtime_patch;
  runtime_patch.exporter_type = std::string("noop");
  runtime_patch.exporter_timeout_ms = 9000U;
  runtime_patch.audit_on_policy_change = false;

  const auto resolved = policy.merge(profile_patch, deploy_patch, runtime_patch);

  assert_true(resolved.exporter_type == "noop" && resolved.reader_interval_ms == 1000U &&
                  resolved.exporter_timeout_ms == 9000U && !resolved.audit_on_policy_change &&
                  resolved.label_allowlist.front() == "error_code" && resolved.is_valid(),
              "MetricsConfigPolicy should apply default -> profile -> deploy -> runtime overrides in order without losing the frozen allowlist set");
}

void test_metrics_config_policy_rejects_non_monotonic_histogram_buckets() {
  using dasall::infra::metrics::MetricsConfigPolicy;
  using dasall::tests::support::assert_true;

  MetricsConfigPolicy policy;
  const auto result = policy.validate_histogram_buckets({0.1, 0.05, 0.2});

  assert_true(!result.accepted && result.references_only_contract_error_types(),
              "MetricsConfigPolicy should reject histogram buckets that break the frozen monotonic ordering constraint");
}

}  // namespace

int main() {
  try {
    test_metrics_config_policy_keeps_frozen_defaults();
    test_metrics_config_policy_merges_profile_deploy_runtime_in_order();
    test_metrics_config_policy_rejects_non_monotonic_histogram_buckets();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}