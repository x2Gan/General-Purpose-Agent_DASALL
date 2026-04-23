#pragma once

#include <memory>
#include <string>

#include "AccessTypes.h"
#include "IIPC.h"

namespace dasall::access::daemon {

class DaemonProtocolAdapter {
 public:
  explicit DaemonProtocolAdapter(std::shared_ptr<dasall::platform::IIPC> ipc);

  [[nodiscard]] LocalPeerUidFact describe_local_peer_uid_fact(
      const dasall::platform::IpcChannelHandle& handle,
      std::string actor_ref) const;

 private:
  std::shared_ptr<dasall::platform::IIPC> ipc_;
};

}  // namespace dasall::access::daemon
