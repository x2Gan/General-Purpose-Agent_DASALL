#pragma once

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
};

struct DaemonTaskQueryResult {
  DaemonTaskQueryStatus status = DaemonTaskQueryStatus::Missing;
  std::string task_status = "missing";
  std::string receipt_ref;
};

class DaemonTaskQueryHandler final {
 public:
  explicit DaemonTaskQueryHandler(dasall::access::AsyncTaskRegistry& registry)
      : registry_(registry) {}

  [[nodiscard]] DaemonTaskQueryResult handle_status(
      std::string_view receipt_ref,
      const DaemonTaskOwner& owner) const;

 private:
  dasall::access::AsyncTaskRegistry& registry_;
};

}  // namespace dasall::access::daemon
