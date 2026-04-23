#pragma once

#include <optional>
#include <string>

namespace dasall::access {

// AccessGateway 生命周期状态枚举
enum class AccessGatewayState {
  Uninitialized = 0,    // 未初始化：init() 前的初始状态
  Initializing = 1,     // 正在初始化：注册 adapter、pipeline 阶段
  Ready = 2,            // 就绪：唯一可接受新请求的状态
  Draining = 3,         // 排空中：shutdown() 已触发，不再接受新请求
  ShutDown = 4,         // 已终止：所有资源已释放，网关已停止
};

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