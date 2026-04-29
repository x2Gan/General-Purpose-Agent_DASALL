#pragma once

#include <functional>
#include <string>
#include <string_view>

namespace dasall::access {
class AsyncTaskRegistry;
}  // namespace dasall::access

namespace dasall::access::daemon {

struct DaemonTaskOwner {
  std::string actor_ref;
  std::string ownership_token;
};

enum class DaemonTaskQueryStatus {
  Active = 0,
  Completed = 1,
  Cancelled = 2,
  Missing = 3,
  Expired = 4,
  OwnerMismatch = 5,
  CancelForwardFailed = 6,
};

struct DaemonTaskQueryResult {
  DaemonTaskQueryStatus status = DaemonTaskQueryStatus::Missing;
  std::string task_status = "missing";
  std::string receipt_ref;
  std::string request_id;
};

class DaemonTaskQueryHandler final {
 public:
  using CancelBackend = std::function<bool(std::string_view request_id,
                                           std::string_view actor_ref)>;

  explicit DaemonTaskQueryHandler(dasall::access::AsyncTaskRegistry& registry)
      : registry_(registry) {}

  [[nodiscard]] DaemonTaskQueryResult handle_status(
      std::string_view receipt_ref,
      const DaemonTaskOwner& owner) const;

    [[nodiscard]] DaemonTaskQueryResult handle_cancel(
      std::string_view receipt_ref,
      const DaemonTaskOwner& owner,
      const CancelBackend& cancel_backend);

 private:
  dasall::access::AsyncTaskRegistry& registry_;
};

}  // namespace dasall::access::daemon
