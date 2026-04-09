#pragma once

#include "ServiceTypes.h"

namespace dasall::services {

class IExecutionService {
public:
	virtual ~IExecutionService() = default;

	virtual ExecutionCommandResult execute(const ExecutionCommandRequest& request) = 0;
	virtual ExecutionCommandResult compensate(const ExecutionCompensationRequest& request) = 0;
	virtual ExecutionQueryResult query_state(const ExecutionQueryRequest& request) = 0;
	virtual ExecutionSubscriptionResult subscribe(const ExecutionSubscriptionRequest& request) = 0;
	virtual ExecutionDiagnoseResult diagnose(const ExecutionDiagnoseRequest& request) = 0;
};

}  // namespace dasall::services