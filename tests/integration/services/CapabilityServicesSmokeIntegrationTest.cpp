#include <exception>
#include <iostream>
#include <string>

#include "CapabilityServicesLoopbackFixture.h"
#include "support/TestAssertions.h"

namespace {

using dasall::tests::mocks::CapabilityServicesLoopbackFixture;
using dasall::tests::support::assert_equal;
using dasall::tests::support::assert_true;

void test_capability_services_smoke_integration_registers_minimal_loopback_round_trip() {
  CapabilityServicesLoopbackFixture fixture;

  const auto execute_result = fixture.execution_service().execute(
      fixture.make_execute_request());
  const auto query_result = fixture.data_service().query(
      fixture.make_query_request());
  const auto catalog_result = fixture.data_service().list_capabilities(
      fixture.make_catalog_request());

  assert_true(!execute_result.error.has_value(),
              "smoke loopback execute should succeed without structured error");
  assert_true(!query_result.error.has_value(),
              "smoke loopback data query should succeed without structured error");
  assert_true(!catalog_result.error.has_value(),
              "smoke loopback catalog query should succeed without structured error");
  assert_true(execute_result.payload_json.find("\"applied\":true") != std::string::npos,
              "smoke execute should preserve the loopback action payload");
  assert_true(!execute_result.side_effects.empty(),
              "smoke execute should preserve side effect facts from the loopback adapter");
  assert_true(!query_result.from_cache,
              "first smoke query should be served from the live loopback path");
  assert_true(query_result.rows_json.find("\"projection\":\"status\"") !=
                  std::string::npos,
              "smoke query should preserve the requested projection in rows_json");
  assert_true(catalog_result.catalog_json.find("\"local_service\"") != std::string::npos,
              "smoke catalog query should advertise the local_service loopback route");
  assert_equal(3,
               static_cast<int>(fixture.local_requests().size()),
               "smoke integration should hit the local loopback adapter three times");
  assert_equal(0,
               static_cast<int>(fixture.remote_requests().size()),
               "smoke integration should not use remote fallback under default fixture policy");
  assert_equal(std::string("toggle"),
               fixture.local_requests().at(0).operation_name,
               "smoke execute should register the expected action name");
  assert_equal(std::string("status"),
               fixture.local_requests().at(1).operation_name,
               "smoke query should register the expected projection name");
  assert_equal(std::string("catalog.list"),
               fixture.local_requests().at(2).operation_name,
               "smoke catalog query should register the expected catalog operation name");
}

}  // namespace

int main() {
  try {
    test_capability_services_smoke_integration_registers_minimal_loopback_round_trip();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}