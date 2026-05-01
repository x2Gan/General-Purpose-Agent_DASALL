#include "CliIpcClient.h"

#include <cstdint>
#include <string>
#include <utility>

#include "daemon/DaemonFrameCodec.h"

namespace dasall::apps::cli {

namespace {

[[nodiscard]] dasall::platform::IpcPayload to_ipc_payload(
    std::string_view payload_text) {
  dasall::platform::IpcPayload ipc_payload;
  ipc_payload.reserve(payload_text.size());
  for (const char c : payload_text) {
    ipc_payload.push_back(static_cast<std::uint8_t>(c));
  }
  return ipc_payload;
}

[[nodiscard]] std::string from_ipc_payload(
    const dasall::platform::IpcPayload& payload) {
  return std::string(reinterpret_cast<const char*>(payload.data()),
                     payload.size());
}

[[nodiscard]] std::string make_request_id(std::string_view command_name) {
  return std::string("cli-") + std::string(command_name);
}

[[nodiscard]] dasall::access::daemon::UdsRequestFrame make_request_frame(
    std::string_view command_name) {
  dasall::access::daemon::UdsRequestFrame frame;
  frame.request_id = make_request_id(command_name);
  frame.trace_id = frame.request_id + "-trace";
  frame.command = std::string(command_name);
  return frame;
}

void populate_response_fields(
    const dasall::access::daemon::UdsResponseFrame& frame,
    DaemonClientResponse& response) {
  response.request_id = frame.request_id;
  response.trace_id = frame.trace_id;
  response.disposition = frame.disposition;
  response.session_id = frame.session_id;
  response.exit_code_hint = frame.exit_code_hint;
  response.receipt_ref = frame.receipt_ref;
  response.error_ref = frame.error_ref;
  if (frame.agent_result.has_value()) {
    response.response_text = frame.agent_result->response_text;
    response.task_completed = frame.agent_result->task_completed;
  }
}

}  // namespace

CliIpcClient::CliIpcClient(std::shared_ptr<dasall::platform::IIPC> ipc,
                           dasall::platform::IpcEndpoint endpoint,
                           const std::int32_t connect_deadline_ms)
    : ipc_(std::move(ipc)),
      endpoint_(std::move(endpoint)),
      connect_deadline_ms_(connect_deadline_ms) {}

DaemonClientResponse CliIpcClient::ping_daemon() const {
  return send_request(make_request_frame("ping"));
}

DaemonClientResponse CliIpcClient::submit(const std::string_view payload) const {
  auto frame = make_request_frame("run");
  frame.payload = std::string(payload);
  return send_request(frame);
}

DaemonClientResponse CliIpcClient::query_status(
    const std::string_view receipt_ref,
    const std::string_view ownership_token,
    const std::string_view actor_ref) const {
  auto frame = make_request_frame("status");
  frame.args.emplace("receipt_ref", std::string(receipt_ref));
  frame.args.emplace("ownership_token", std::string(ownership_token));
  if (!actor_ref.empty()) {
    frame.args.emplace("actor_ref", std::string(actor_ref));
  }
  return send_request(frame);
}

DaemonClientResponse CliIpcClient::cancel(
    const std::string_view receipt_ref,
    const std::string_view ownership_token,
    const std::string_view actor_ref) const {
  auto frame = make_request_frame("cancel");
  frame.args.emplace("receipt_ref", std::string(receipt_ref));
  frame.args.emplace("ownership_token", std::string(ownership_token));
  if (!actor_ref.empty()) {
    frame.args.emplace("actor_ref", std::string(actor_ref));
  }
  return send_request(frame);
}

DaemonClientResponse CliIpcClient::read_readiness() const {
  return send_request(make_request_frame("readiness"));
}

DaemonClientResponse CliIpcClient::run_diagnostics(
    const std::string_view command_name) const {
  auto frame = make_request_frame("diag");
  frame.payload = std::string("command_name=") + std::string(command_name);
  return send_request(frame);
}

DaemonClientResponse CliIpcClient::send_request(
    const dasall::access::daemon::UdsRequestFrame& frame) const {
  DaemonClientResponse response;

  if (!ipc_ || !endpoint_.has_consistent_values() || connect_deadline_ms_ < 0) {
    response.failure_reason = "invalid cli ipc client configuration";
    return response;
  }

  const auto channel = ipc_->connect(endpoint_, connect_deadline_ms_);
  if (!channel.ok() || !channel.value.has_value()) {
    response.failure_reason = channel.error.has_value()
                                  ? channel.error->detail
                                  : "daemon connect failed";
    return response;
  }

  const auto request_text = dasall::access::daemon::encode_request_frame(frame);
  const auto ipc_payload = to_ipc_payload(request_text);

  const auto sent = ipc_->send(*channel.value, ipc_payload);
  if (!sent.ok()) {
    (void)ipc_->close(*channel.value);
    response.failure_reason = sent.error.has_value()
                                  ? sent.error->detail
                                  : "daemon send failed";
    return response;
  }

  const auto received = ipc_->receive(*channel.value, connect_deadline_ms_);
  (void)ipc_->close(*channel.value);

  if (!received.ok() || !received.value.has_value()) {
    response.failure_reason = received.error.has_value()
                                  ? received.error->detail
                                  : "daemon receive failed";
    return response;
  }

  response.transport_ok = true;
  response.peer_closed = received.value->peer_closed;
  response.raw_response = from_ipc_payload(received.value->data);

  if (response.peer_closed) {
    response.failure_reason = "daemon closed channel before returning a response";
    return response;
  }

  if (response.raw_response.empty()) {
    response.failure_reason = "daemon returned an empty response frame";
    return response;
  }

  const auto decoded = dasall::access::daemon::decode_response_frame(
      response.raw_response);
  if (!decoded.ok()) {
    response.failure_reason = "daemon returned an invalid response frame";
    return response;
  }

  response.parse_ok = true;
  populate_response_fields(decoded.frame, response);
  return response;
}

}  // namespace dasall::apps::cli
