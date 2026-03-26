#pragma once

#include <optional>
#include <string>
#include <string_view>

#include "agent/AgentRequest.h"
#include "task/WorkerLease.h"
#include "task/WorkerTask.h"

namespace dasall::infra {

struct InfraContext {
  static constexpr std::string_view kUnknownIdentifier = "unknown";

  std::string request_id = std::string(kUnknownIdentifier);
  std::string session_id = std::string(kUnknownIdentifier);
  std::string trace_id = std::string(kUnknownIdentifier);
  std::string task_id = std::string(kUnknownIdentifier);
  std::string parent_task_id = std::string(kUnknownIdentifier);
  std::string lease_id = std::string(kUnknownIdentifier);

  [[nodiscard]] static std::string normalize_identifier(
      const std::optional<std::string>& value) {
    if (!value.has_value() || value->empty()) {
      return std::string(kUnknownIdentifier);
    }

    return *value;
  }

  [[nodiscard]] static InfraContext from_contracts(
      const contracts::AgentRequest& request,
      const contracts::WorkerTask* worker_task = nullptr,
      const contracts::WorkerLease* worker_lease = nullptr) {
    InfraContext context;
    context.request_id = normalize_identifier(request.request_id);
    context.session_id = normalize_identifier(request.session_id);
    context.trace_id = normalize_identifier(request.trace_id);

    if (worker_task != nullptr) {
      context.task_id = normalize_identifier(worker_task->task_id);
      context.parent_task_id = normalize_identifier(worker_task->parent_task_id);
      context.lease_id = normalize_identifier(worker_task->lease_id);
    }

    if (worker_lease != nullptr && context.lease_id == kUnknownIdentifier) {
      context.lease_id = normalize_identifier(worker_lease->lease_id);
    }

    return context;
  }
};

}  // namespace dasall::infra