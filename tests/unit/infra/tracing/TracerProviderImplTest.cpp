#include <exception>
#include <iostream>
#include <string>

#include "tracing/TraceErrors.h"
#include "tracing/TracerProviderImpl.h"
#include "dasall/tests/support/TestAssertions.h"

namespace {

[[nodiscard]] dasall::infra::tracing::TracerScope make_scope() {
  return dasall::infra::tracing::TracerScope{
      .name = std::string("infra.tracing"),
      .version = std::string("1.0.0"),
      .schema_url = std::string("https://opentelemetry.io/schemas/1.26.0"),
  };
}

[[nodiscard]] dasall::infra::tracing::TraceConfig make_config() {
  return dasall::infra::tracing::TraceConfig{
      .enabled = true,
      .provider_type = std::string("internal"),
      .force_flush_on_stop = true,
  };
}

void test_tracer_provider_impl_rejects_uninitialized_usage() {
  using dasall::infra::tracing::TraceErrorCode;
  using dasall::infra::tracing::TracerProviderImpl;
  using dasall::infra::tracing::map_trace_error_code;
  using dasall::tests::support::assert_true;

  TracerProviderImpl provider;

  const auto tracer = provider.get_tracer(make_scope());
  assert_true(!tracer,
              "TracerProviderImpl should reject get_tracer() before init() succeeds");

  const auto flush_result = provider.force_flush(100);
  assert_true(!flush_result.ok && flush_result.references_only_contract_error_types() &&
                  flush_result.result_code ==
                      map_trace_error_code(TraceErrorCode::ProviderNotReady).result_code,
              "TracerProviderImpl should expose provider-not-ready failures before init()");
  assert_true(provider.lifecycle_state_name() == "created",
              "TracerProviderImpl should remain in created state before init()");
}

void test_tracer_provider_impl_initializes_and_caches_tracer_scope() {
  using dasall::infra::tracing::TracerProviderImpl;
  using dasall::tests::support::assert_true;

  TracerProviderImpl provider;
  const auto init_result = provider.init(make_config());
  assert_true(init_result.ok && provider.lifecycle_state_name() == "initialized" &&
                  provider.last_config().has_value(),
              "TracerProviderImpl should accept the minimal internal TraceConfig and enter initialized state");

  const auto first_tracer = provider.get_tracer(make_scope());
  const auto second_tracer = provider.get_tracer(make_scope());
  assert_true(first_tracer && second_tracer && first_tracer == second_tracer &&
                  provider.tracer_count() == 1U && provider.last_scope().has_value(),
              "TracerProviderImpl should cache a single tracer instance per frozen TracerScope during the initialized lifecycle");

  const auto flush_result = provider.force_flush(250);
  assert_true(flush_result.ok,
              "TracerProviderImpl should expose a working force_flush() outlet after init()");
}

void test_tracer_provider_impl_surfaces_shutdown_timeout() {
  using dasall::infra::tracing::TraceErrorCode;
  using dasall::infra::tracing::TracerProviderImpl;
  using dasall::infra::tracing::map_trace_error_code;
  using dasall::tests::support::assert_true;

  TracerProviderImpl provider;
  assert_true(provider.init(make_config()).ok,
              "TracerProviderImpl should initialize before the shutdown timeout path is exercised");

  const auto timeout_result = provider.shutdown(0);
  assert_true(!timeout_result.ok && timeout_result.references_only_contract_error_types() &&
                  timeout_result.result_code ==
                      map_trace_error_code(TraceErrorCode::ShutdownTimeout).result_code &&
                  provider.lifecycle_state_name() == "initialized",
              "TracerProviderImpl should surface shutdown timeout observably without dropping initialized state");

  const auto shutdown_result = provider.shutdown(250);
  assert_true(shutdown_result.ok && provider.lifecycle_state_name() == "stopped" &&
                  provider.tracer_count() == 0U,
              "TracerProviderImpl should transition to stopped after a successful shutdown() and clear cached tracers");
}

}  // namespace

int main() {
  try {
    test_tracer_provider_impl_rejects_uninitialized_usage();
    test_tracer_provider_impl_initializes_and_caches_tracer_scope();
    test_tracer_provider_impl_surfaces_shutdown_timeout();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}