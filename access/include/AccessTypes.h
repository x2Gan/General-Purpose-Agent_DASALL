#pragma once

#include <optional>
#include <string>

namespace dasall::access {

enum class AccessDisposition {
  Rejected = 0,
  Completed = 1,
  AcceptedAsync = 2,
  StreamAttached = 3,
};

struct InboundPacket {
  std::string packet_id;
  std::string entry_type;
  std::string protocol_kind;
  std::string peer_ref;
  std::string payload;
  bool async_preferred = false;
  bool stream_requested = false;
};

struct RuntimeDispatchRequest {
  InboundPacket packet;
  bool async_allowed = false;
  bool stream_requested = false;
};

struct PublishEnvelope {
  std::string request_id;
  std::string result_id;
  std::string payload;
  std::string protocol_kind;
};

struct RuntimeDispatchResult {
  AccessDisposition disposition = AccessDisposition::Rejected;
  std::optional<PublishEnvelope> publish_envelope;
  std::optional<std::string> receipt_ref;
  std::optional<std::string> error_ref;
};

}  // namespace dasall::access