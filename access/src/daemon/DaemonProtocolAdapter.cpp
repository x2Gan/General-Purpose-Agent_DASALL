#include "daemon/DaemonProtocolAdapter.h"

#include <utility>

namespace dasall::access::daemon {

DaemonProtocolAdapter::DaemonProtocolAdapter(
    std::shared_ptr<dasall::platform::IIPC> ipc)
    : ipc_(std::move(ipc)) {}

LocalPeerUidFact DaemonProtocolAdapter::describe_local_peer_uid_fact(
    const dasall::platform::IpcChannelHandle& handle,
    std::string actor_ref) const {
  LocalPeerUidFact fact;
  fact.actor_ref = std::move(actor_ref);

  if (!ipc_) {
    return fact;
  }

  const auto peer_snapshot = ipc_->describe_peer(handle);
  if (!peer_snapshot.ok() || !peer_snapshot.value.has_value()) {
    return fact;
  }

  fact.peer_uid = peer_snapshot.value->peer_uid;
  fact.peer_gid = peer_snapshot.value->peer_gid;
  fact.peer_pid = peer_snapshot.value->peer_pid;
  fact.is_local_socket_peer = peer_snapshot.value->is_local_socket_peer;
  fact.eligible_for_local_trusted =
      fact.is_local_socket_peer && fact.peer_uid != 0U;

  return fact;
}

}  // namespace dasall::access::daemon
