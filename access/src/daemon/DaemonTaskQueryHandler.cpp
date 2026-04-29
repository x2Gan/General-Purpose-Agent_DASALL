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
    };
  }

  if (initial_qr.status == QueryStatus::Expired) {
    return DaemonTaskQueryResult{
        .status = DaemonTaskQueryStatus::Expired,
        .task_status = "expired",
        .receipt_ref = id,
    };
  }

  if (!registry_.validate_ownership(id, owner.actor_ref, owner.ownership_token)) {
    return DaemonTaskQueryResult{
        .status = DaemonTaskQueryStatus::OwnerMismatch,
        .task_status = "mismatch",
        .receipt_ref = id,
    };
  }

  const auto& receipt = initial_qr.receipt.value();
  const std::string registry_status = receipt.initial_status.value_or("pending");

  if (registry_status == "completed") {
    return DaemonTaskQueryResult{
        .status = DaemonTaskQueryStatus::Completed,
        .task_status = "completed",
        .receipt_ref = id,
    };
  }

  if (registry_status == "cancelled") {
    return DaemonTaskQueryResult{
        .status = DaemonTaskQueryStatus::Cancelled,
        .task_status = "cancelled",
        .receipt_ref = id,
    };
  }

  return DaemonTaskQueryResult{
      .status = DaemonTaskQueryStatus::Active,
      .task_status = "active",
      .receipt_ref = id,
  };
}

}  // namespace dasall::access::daemon
