#include <exception>
#include <iostream>

#include "AccessGatewayFactory.h"
#include "HealthProbeHandler.h"
#include "support/TestAssertions.h"

namespace {

void readiness_reflects_real_gateway_initialization() {
  using dasall::access::AccessDisposition;
  using dasall::access::GatewayAccessPipelineOptions;
  using dasall::access::RuntimeDispatchRequest;
  using dasall::access::RuntimeDispatchResult;
  using dasall::access::gateway::HealthProbeHandler;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  auto unavailable_gateway = dasall::access::create_gateway_access_gateway();
  assert_true(!unavailable_gateway->init(),
              "gateway without runtime backend must fail closed at init");

  HealthProbeHandler unavailable_health;
  unavailable_health.set_started(false);
  unavailable_health.set_ready(unavailable_gateway->is_ready());

  const auto unavailable_ready = unavailable_health.handle_ready();
  const auto unavailable_startup = unavailable_health.handle_startup();
  assert_equal(503,
               unavailable_ready.status_code,
               "readiness probe should stay NOT_READY when gateway init fails");
  assert_equal(503,
               unavailable_startup.status_code,
               "startup probe should stay STARTING when gateway init fails");

  GatewayAccessPipelineOptions options;
  options.bootstrap_config.allowed_protocols = {"http"};
  options.runtime_dispatch_backend = [](const RuntimeDispatchRequest&) {
    RuntimeDispatchResult result;
    result.disposition = AccessDisposition::Completed;
    return result;
  };

  auto ready_gateway = dasall::access::create_gateway_access_gateway(std::move(options));
  assert_true(ready_gateway->init(),
              "gateway with configured runtime backend should initialize");

  HealthProbeHandler ready_health;
  ready_health.set_started(true);
  ready_health.set_ready(ready_gateway->is_ready());

  const auto ready_result = ready_health.handle_ready();
  const auto startup_result = ready_health.handle_startup();
  assert_equal(200,
               ready_result.status_code,
               "readiness probe should report READY after real gateway init");
  assert_equal(200,
               startup_result.status_code,
               "startup probe should report STARTED after real gateway init");
}

}  // namespace

int main() {
  try {
    readiness_reflects_real_gateway_initialization();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << '\n';
    return 1;
  }

  return 0;
}