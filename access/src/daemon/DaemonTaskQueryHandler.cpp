#include "daemon/DaemonTaskQueryHandler.h"

#include <string>

#include "../AsyncTaskRegistry.h"

namespace dasall::access::daemon {

DaemonTaskQueryResult DaemonTaskQueryHandler::handle_status(
    const std::string_view receipt_ref,
    const DaemonTaskOwner& owner) const {
  const std::string id{receipt_ref};
  const auto initial_qr = registry_.query_receipt(id);

  using QueryStatus = dasall::access::AsyncTaskRegistry::QueryStatus;
  if (initial_qr.status == QueryStatus::NotFound) {
    return DaemonTaskQueryResult{
        .status = DaemonTaskQueryStatus::Missing,
        .task_status = "missing",
        .receipt_ref = id,
        .request_id = std::string(),
    };
  }

  if (initial_qr.status == QueryStatus::Expired) {
    return DaemonTaskQueryResult{
        .status = DaemonTaskQueryStatus::Expired,
        .task_status = "expired",
        .receipt_ref = id,
        .request_id = std::string(),
    };
  }

  if (!registry_.validate_ownership(id, owner.actor_ref, owner.ownership_token)) {
    return DaemonTaskQueryResult{
        .status = DaemonTaskQueryStatus::OwnerMismatch,
        .task_status = "mismatch",
        .receipt_ref = id,
        .request_id = std::string(),
    };
  }

  const auto& receipt = initial_qr.receipt.value();
  const std::string registry_status = receipt.initial_status.value_or("pending");

  if (registry_status == "completed") {
    return DaemonTaskQueryResult{
        .status = DaemonTaskQueryStatus::Completed,
        .task_status = "completed",
        .receipt_ref = id,
        .request_id = receipt.request_id,
    };
  }

  if (registry_status == "cancelled") {
    return DaemonTaskQueryResult{
        .status = DaemonTaskQueryStatus::Cancelled,
        .task_status = "cancelled",
        .receipt_ref = id,
        .request_id = receipt.request_id,
    };
  }

  return DaemonTaskQueryResult{
      .status = DaemonTaskQueryStatus::Active,
      .task_status = "active",
      .receipt_ref = id,
      .request_id = receipt.request_id,
  };
}

DaemonTaskQueryResult DaemonTaskQueryHandler::handle_cancel(
    const std::string_view receipt_ref,
    const DaemonTaskOwner& owner,
    const CancelBackend& cancel_backend) {
  const std::string id{receipt_ref};
  const auto status_result = handle_status(receipt_ref, owner);

  if (status_result.status == DaemonTaskQueryStatus::Missing ||
      status_result.status == DaemonTaskQueryStatus::Expired ||
      status_result.status == DaemonTaskQueryStatus::OwnerMismatch ||
      status_result.status == DaemonTaskQueryStatus::Cancelled) {
    return status_result;
  }

  if (!cancel_backend || status_result.request_id.empty()) {
    return DaemonTaskQueryResult{
        .status = DaemonTaskQueryStatus::CancelForwardFailed,
        .task_status = "cancel_forward_failed",
        .receipt_ref = id,
        .request_id = status_result.request_id,
    };
  }

  if (!cancel_backend(status_result.request_id, owner.actor_ref)) {
    return DaemonTaskQueryResult{
        .status = DaemonTaskQueryStatus::CancelForwardFailed,
        .task_status = "cancel_forward_failed",
        .receipt_ref = id,
        .request_id = status_result.request_id,
    };
  }

  if (!registry_.mark_completed(id, "cancelled")) {
    return DaemonTaskQueryResult{
        .status = DaemonTaskQueryStatus::Expired,
        .task_status = "expired",
        .receipt_ref = id,
        .request_id = status_result.request_id,
    };
  }

  return DaemonTaskQueryResult{
      .status = DaemonTaskQueryStatus::Cancelled,
      .task_status = "cancelled",
      .receipt_ref = id,
      .request_id = status_result.request_id,
  };
}

}  // namespace dasall::access::daemon
