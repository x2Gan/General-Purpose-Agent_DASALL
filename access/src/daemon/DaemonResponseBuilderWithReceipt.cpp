#include "daemon/DaemonResponseBuilderWithReceipt.h"

#include <memory>
#include <utility>

#include "../AsyncTaskRegistry.h"

namespace dasall::access::daemon {

DaemonResponseBuilderWithReceipt::DaemonResponseBuilderWithReceipt(
    std::shared_ptr<dasall::access::AsyncTaskRegistry> registry)
    : registry_(std::move(registry)) {}

std::shared_ptr<AsyncTaskReceipt> DaemonResponseBuilderWithReceipt::register_and_build_receipt(
    const RuntimeDispatchRequest& request,
    const RuntimeDispatchResult& runtime_result) const {
  // Check if accepted_async and registry available
  const bool is_accepted_async =
      runtime_result.disposition == AccessDisposition::AcceptedAsync && registry_;
  if (!is_accepted_async) {
    return nullptr;
  }

  // Call registry to generate receipt
  const auto receipt_opt = registry_->register_async_accept(request, runtime_result);
  if (!receipt_opt.has_value()) {
    return nullptr;
  }

  // Convert optional to shared_ptr
  return std::make_shared<AsyncTaskReceipt>(receipt_opt.value());
}

}  // namespace dasall::access::daemon

