#include "IInfrastructureService.h"

namespace dasall::infra {

namespace {

constexpr std::string_view kFacadeSourceRef = "InfraServiceFacade";

}  // namespace

InfraOperationResult InfraServiceFacade::init(const InfrastructureConfig& config) {
  if (lifecycle_state_ != LifecycleState::Created) {
    return invalid_transition("init", "created");
  }

  if (config.profile.empty()) {
    return InfraOperationResult::failure(contracts::ResultCode::ValidationFieldMissing,
                                         "infrastructure config profile must not be empty",
                                         "infra.init",
                                         std::string(kFacadeSourceRef));
  }

  last_config_ = config;
  lifecycle_state_ = LifecycleState::Initialized;
  return InfraOperationResult::success();
}

InfraOperationResult InfraServiceFacade::start() {
  if (lifecycle_state_ != LifecycleState::Initialized) {
    return invalid_transition("start", "initialized");
  }

  lifecycle_state_ = LifecycleState::Started;
  return InfraOperationResult::success();
}

InfraOperationResult InfraServiceFacade::stop(std::uint32_t timeout_ms) {
  if (lifecycle_state_ != LifecycleState::Started) {
    return invalid_transition("stop", "started");
  }

  if (timeout_ms == 0) {
    return InfraOperationResult::failure(contracts::ResultCode::ValidationFieldMissing,
                                         "stop timeout must be greater than zero",
                                         "infra.stop",
                                         std::string(kFacadeSourceRef));
  }

  lifecycle_state_ = LifecycleState::Stopped;
  return InfraOperationResult::success();
}

InfraOperationResult InfraServiceFacade::execute(const InfraCommandRequest& command) {
  if (lifecycle_state_ != LifecycleState::Started) {
    return invalid_transition("execute", "started");
  }

  if (!command.is_valid()) {
    return InfraOperationResult::failure(contracts::ResultCode::ValidationFieldMissing,
                                         "infra command name must not be empty",
                                         "infra.execute",
                                         std::string(kFacadeSourceRef));
  }

  return InfraOperationResult::success();
}

std::string_view InfraServiceFacade::lifecycle_state_name() const {
  switch (lifecycle_state_) {
    case LifecycleState::Created:
      return "created";
    case LifecycleState::Initialized:
      return "initialized";
    case LifecycleState::Started:
      return "started";
    case LifecycleState::Stopped:
      return "stopped";
  }

  return "unknown";
}

InfraOperationResult InfraServiceFacade::invalid_transition(
    std::string_view operation,
    std::string_view expected_state) const {
  return InfraOperationResult::failure(
      contracts::ResultCode::RuntimeRetryExhausted,
      "invalid lifecycle transition for operation " + std::string(operation) +
          ": expected state " + std::string(expected_state) +
          ", actual state " + std::string(lifecycle_state_name()),
      "infra.lifecycle",
      std::string(kFacadeSourceRef));
}

}  // namespace dasall::infra