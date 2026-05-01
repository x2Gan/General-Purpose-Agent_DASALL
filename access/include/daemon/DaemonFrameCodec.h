#pragma once

#include <cstddef>
#include <string>
#include <string_view>

#include "AccessTypes.h"
#include "daemon/DaemonProtocolTypes.h"

namespace dasall::access::daemon {

struct DecodedDaemonRequestFrame {
  UdsRequestFrame frame;
  DaemonFrameDecodeError error = DaemonFrameDecodeError::None;

  [[nodiscard]] bool ok() const {
    return error == DaemonFrameDecodeError::None;
  }
};

struct DecodedDaemonResponseFrame {
  UdsResponseFrame frame;
  DaemonFrameDecodeError error = DaemonFrameDecodeError::None;

  [[nodiscard]] bool ok() const {
    return error == DaemonFrameDecodeError::None;
  }
};

[[nodiscard]] DecodedDaemonRequestFrame decode_request_frame(
    std::string_view payload,
    std::size_t max_payload_bytes = 1024U * 1024U);

[[nodiscard]] DecodedDaemonResponseFrame decode_response_frame(
    std::string_view payload,
    std::size_t max_payload_bytes = 1024U * 1024U);

[[nodiscard]] std::string encode_request_frame(const UdsRequestFrame& frame);

[[nodiscard]] std::string encode_response_frame(const UdsResponseFrame& frame);

[[nodiscard]] PublishEnvelope map_frame_error_to_publish_envelope(
    DaemonFrameDecodeError error,
    std::string_view request_id = {},
    std::string_view trace_id = {});

}  // namespace dasall::access::daemon