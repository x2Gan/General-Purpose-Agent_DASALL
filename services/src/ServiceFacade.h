#pragma once

#include <functional>

#include "IDataService.h"
#include "IExecutionService.h"
#include "ServiceContextBuilder.h"

namespace dasall::services::internal {

struct ServiceFacadeDependencies {
  ServiceContextBuilder* context_builder = nullptr;

  std::function<ExecutionCommandResult(const ServiceCallContext&, const ExecutionCommandRequest&)>
      execute_command;
  std::function<ExecutionCommandResult(const ServiceCallContext&, const ExecutionCompensationRequest&)>
      compensate_command;
  std::function<ExecutionQueryResult(const ServiceCallContext&, const ExecutionQueryRequest&)>
      query_execution_state;
  std::function<ExecutionSubscriptionResult(
      const ServiceCallContext&, const ExecutionSubscriptionRequest&)>
      subscribe_execution_state;
  std::function<ExecutionDiagnoseResult(const ServiceCallContext&, const ExecutionDiagnoseRequest&)>
      diagnose_execution_target;
  std::function<DataQueryResult(const ServiceCallContext&, const DataQueryRequest&)> query_data;
  std::function<DataCatalogResult(const ServiceCallContext&, const DataCatalogRequest&)>
      list_data_capabilities;
};

class ServiceFacade final : public IExecutionService, public IDataService {
 public:
  explicit ServiceFacade(ServiceFacadeDependencies dependencies);

  ExecutionCommandResult execute(const ExecutionCommandRequest& request) override;
  ExecutionCommandResult compensate(const ExecutionCompensationRequest& request) override;
  ExecutionQueryResult query_state(const ExecutionQueryRequest& request) override;
  ExecutionSubscriptionResult subscribe(const ExecutionSubscriptionRequest& request) override;
  ExecutionDiagnoseResult diagnose(const ExecutionDiagnoseRequest& request) override;
  DataQueryResult query(const DataQueryRequest& request) override;
  DataCatalogResult list_capabilities(const DataCatalogRequest& request) override;

 private:
  ServiceFacadeDependencies dependencies_;
};

}  // namespace dasall::services::internal