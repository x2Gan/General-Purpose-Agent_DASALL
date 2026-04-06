#include <exception>
#include <iostream>
#include <string>

#include "health/ProbeRegistry.h"
#include "dasall/tests/support/TestAssertions.h"

namespace {

class StaticHealthProbe final : public dasall::infra::IHealthProbe {
 public:
  [[nodiscard]] dasall::infra::ProbeResult probe() override {
    return dasall::infra::ProbeResult{
        .probe_name = std::string("probe"),
        .status = dasall::infra::ProbeStatus::Healthy,
        .latency_ms = 1,
        .error_code = std::nullopt,
        .detail_ref = std::string(),
        .timestamp = 1712361600000,
    };
  }
};

void test_probe_registry_rejects_duplicate_probe_names() {
  using dasall::contracts::ResultCode;
  using dasall::infra::HealthProbeRegistration;
  using dasall::infra::ProbeRegistry;
  using dasall::tests::support::assert_true;

  ProbeRegistry registry;
  StaticHealthProbe first_probe;
  StaticHealthProbe duplicate_probe;

  const auto registered = registry.register_probe(HealthProbeRegistration{
      .probe_name = std::string("logging_sink"),
      .probe_group = std::string("readiness"),
      .probe = &first_probe,
  });
  assert_true(registered.ok && registered.descriptor.has_required_fields() &&
                  registry.size() == 1U,
              "ProbeRegistry should accept the first valid probe registration and derive a valid descriptor");

  const auto duplicate = registry.register_probe(HealthProbeRegistration{
      .probe_name = std::string("logging_sink"),
      .probe_group = std::string("readiness"),
      .probe = &duplicate_probe,
  });
  assert_true(!duplicate.ok && duplicate.references_only_contract_error_types() &&
                  duplicate.result_code.has_value() &&
                  *duplicate.result_code == ResultCode::ValidationFieldMissing &&
                  registry.size() == 1U && registry.find_probe("logging_sink") == &first_probe,
              "ProbeRegistry should reject duplicate probe_name registrations and keep the first registered probe entry stable");
}

void test_probe_registry_lists_descriptors_by_group_and_unregisters_them() {
  using dasall::contracts::ResultCode;
  using dasall::infra::HealthProbeRegistration;
  using dasall::infra::ProbeRegistry;
  using dasall::tests::support::assert_true;

  ProbeRegistry registry;
  StaticHealthProbe liveness_probe;
  StaticHealthProbe readiness_probe;

  assert_true(registry.register_probe(HealthProbeRegistration{
                  .probe_name = std::string("config_center"),
                  .probe_group = std::string("liveness"),
                  .probe = &liveness_probe,
              }).ok,
              "ProbeRegistry should accept a liveness probe registration");
  assert_true(registry.register_probe(HealthProbeRegistration{
                  .probe_name = std::string("logging_sink"),
                  .probe_group = std::string("readiness"),
                  .probe = &readiness_probe,
              }).ok,
              "ProbeRegistry should accept a readiness probe registration");

  const auto liveness_descriptors = registry.list_by_group("liveness");
  const auto readiness_descriptors = registry.list_by_group("readiness");
  assert_true(liveness_descriptors.size() == 1U && readiness_descriptors.size() == 1U &&
                  liveness_descriptors.front().probe_name == "config_center" &&
                  readiness_descriptors.front().probe_name == "logging_sink",
              "ProbeRegistry should return group-filtered descriptor collections without cross-group leakage");

  const auto removed = registry.unregister_probe("config_center");
  assert_true(removed.ok && removed.removed && registry.size() == 1U &&
                  registry.list_by_group("liveness").empty(),
              "ProbeRegistry should remove a registered probe and update group queries consistently");

  const auto missing = registry.unregister_probe("missing_probe");
  assert_true(!missing.ok && missing.references_only_contract_error_types() &&
                  missing.result_code.has_value() &&
                  *missing.result_code == ResultCode::ValidationFieldMissing,
              "ProbeRegistry should expose an explicit validation failure when unregister_probe targets an unknown probe_name");
}

}  // namespace

int main() {
  try {
    test_probe_registry_rejects_duplicate_probe_names();
    test_probe_registry_lists_descriptors_by_group_and_unregisters_them();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}