#include <exception>
#include <iostream>
#include <string>

#include "metrics/InstrumentRegistry.h"
#include "metrics/MetricsErrors.h"
#include "dasall/tests/support/TestAssertions.h"

namespace {

void test_instrument_registry_keeps_same_identity_registration_idempotent() {
  using dasall::infra::metrics::InstrumentRegistry;
  using dasall::infra::metrics::MetricIdentity;
  using dasall::infra::metrics::MetricType;
  using dasall::tests::support::assert_true;

  InstrumentRegistry registry;
  const MetricIdentity identity{
      .name = std::string("metrics.queue_depth"),
      .type = MetricType::Gauge,
      .unit = std::string("1"),
      .description = std::string("current queue depth"),
  };

  const auto first = registry.register_identity(identity);
  const auto second = registry.register_identity(identity);
  const auto found = registry.find_identity(identity.name);

  assert_true(first.ok && first.created && first.handle.is_valid(),
              "InstrumentRegistry should create a stable handle for the first valid metric identity registration");
  assert_true(second.ok && !second.created && second.handle.instrument_key == first.handle.instrument_key,
              "InstrumentRegistry should keep repeated same-identity registrations idempotent");
  assert_true(found.has_value() && found->instrument_key == first.handle.instrument_key && registry.size() == 1U,
              "InstrumentRegistry should keep one canonical handle per identity name after repeated registration");
}

void test_instrument_registry_rejects_same_name_semantic_conflicts() {
  using dasall::infra::metrics::InstrumentRegistry;
  using dasall::infra::metrics::MetricIdentity;
  using dasall::infra::metrics::MetricsErrorCode;
  using dasall::infra::metrics::MetricType;
  using dasall::infra::metrics::map_metrics_error_code;
  using dasall::tests::support::assert_true;

  InstrumentRegistry registry;
  const MetricIdentity canonical{
      .name = std::string("metrics.request_duration"),
      .type = MetricType::Histogram,
      .unit = std::string("ms"),
      .description = std::string("request duration"),
  };

  const auto first = registry.register_identity(canonical);
  const auto conflicting = registry.register_identity(MetricIdentity{
      .name = std::string("metrics.request_duration"),
      .type = MetricType::Counter,
      .unit = std::string("1"),
      .description = std::string("request count"),
  });
  const auto found = registry.find_identity(canonical.name);

  assert_true(first.ok,
              "InstrumentRegistry should accept the canonical metric identity before conflict checks");
  assert_true(!conflicting.ok && conflicting.references_only_contract_error_types() &&
                  conflicting.result_code.has_value() &&
                  *conflicting.result_code ==
                      map_metrics_error_code(MetricsErrorCode::IdentityInvalid).result_code,
              "InstrumentRegistry should reject same-name registrations when type/unit/description semantics diverge");
  assert_true(found.has_value() && found->instrument_key == first.handle.instrument_key && registry.size() == 1U,
              "InstrumentRegistry should keep the original canonical handle stable after a semantic conflict rejection");
}

}  // namespace

int main() {
  try {
    test_instrument_registry_keeps_same_identity_registration_idempotent();
    test_instrument_registry_rejects_same_name_semantic_conflicts();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}