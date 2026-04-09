#include <cstdint>
#include <exception>
#include <iostream>
#include <type_traits>

#include "IDataService.h"
#include "IExecutionService.h"
#include "ServiceTypes.h"
#include "support/TestAssertions.h"

namespace {

void test_service_public_headers_remain_reachable_from_include_root() {
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  using ExecuteSignature = dasall::services::ExecutionCommandResult (
      dasall::services::IExecutionService::*)(const dasall::services::ExecutionCommandRequest&);
  using QuerySignature =
      dasall::services::DataQueryResult (dasall::services::IDataService::*)(
          const dasall::services::DataQueryRequest&);
  using ListSignature =
      dasall::services::DataCatalogResult (dasall::services::IDataService::*)(
          const dasall::services::DataCatalogRequest&);

  static_assert(std::is_same_v<decltype(&dasall::services::IExecutionService::execute),
                               ExecuteSignature>,
                "IExecutionService should keep the stable execute signature");
  static_assert(std::is_same_v<decltype(&dasall::services::IDataService::query), QuerySignature>,
                "IDataService should keep the stable query signature");
  static_assert(std::is_same_v<decltype(&dasall::services::IDataService::list_capabilities),
                               ListSignature>,
                "IDataService should keep the stable list_capabilities signature");
  static_assert(std::is_abstract_v<dasall::services::IExecutionService>,
                "IExecutionService should remain abstract");
  static_assert(std::is_abstract_v<dasall::services::IDataService>,
                "IDataService should remain abstract");
  static_assert(std::is_same_v<decltype(dasall::services::ServiceCallContext{}.deadline_ms),
                               std::uint64_t>,
                "ServiceCallContext::deadline_ms should stay uint64_t");

  dasall::services::ExecutionCommandRequest command_request{};
  command_request.target.capability_id = "cap.header";
  command_request.action = "inspect";

  dasall::services::DataCatalogRequest catalog_request{};
  catalog_request.target_class = "edge_device";

  assert_equal(std::string("cap.header"), command_request.target.capability_id,
               "ExecutionCommandRequest should be reachable from ServiceTypes.h");
  assert_equal(std::string("edge_device"), catalog_request.target_class,
               "DataCatalogRequest should be reachable from ServiceTypes.h");
  assert_true(command_request.context.deadline_ms == 0U,
              "default-initialized ServiceCallContext should keep zero deadline sentinel");
}

}  // namespace

int main() {
  try {
    test_service_public_headers_remain_reachable_from_include_root();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}