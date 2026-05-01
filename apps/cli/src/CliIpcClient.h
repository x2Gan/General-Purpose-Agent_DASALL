#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include "IIPC.h"
#include "daemon/DaemonProtocolTypes.h"

namespace dasall::apps::cli {

struct DaemonClientResponse {
  bool transport_ok = false;
  bool parse_ok = false;
  bool peer_closed = false;
  std::string failure_reason;
  std::string raw_response;
  std::string request_id;
  std::string trace_id;
  dasall::access::daemon::UdsResponseDisposition disposition =
      dasall::access::daemon::UdsResponseDisposition::Rejected;
  std::optional<std::string> session_id;
  std::optional<int> exit_code_hint;
  std::optional<std::string> receipt_ref;
  std::optional<std::string> error_ref;
  std::optional<std::string> response_text;
  std::optional<bool> task_completed;

  [[nodiscard]] bool ok() const {
    return transport_ok && parse_ok && !peer_closed;
  }

  [[nodiscard]] bool is_completed() const {
    return disposition ==
           dasall::access::daemon::UdsResponseDisposition::Completed;
  }

  [[nodiscard]] bool is_accepted_async() const {
    return disposition ==
           dasall::access::daemon::UdsResponseDisposition::AcceptedAsync;
  }

  [[nodiscard]] bool is_not_ready() const {
    return disposition ==
           dasall::access::daemon::UdsResponseDisposition::NotReady;
  }
};

class CliIpcClient {
 public:
  CliIpcClient(std::shared_ptr<dasall::platform::IIPC> ipc,
               dasall::platform::IpcEndpoint endpoint,
               std::int32_t connect_deadline_ms = 1000);

  [[nodiscard]] DaemonClientResponse ping_daemon() const;
  [[nodiscard]] DaemonClientResponse submit(std::string_view payload) const;
  [[nodiscard]] DaemonClientResponse query_status(
      std::string_view receipt_ref,
      std::string_view ownership_token,
      std::string_view actor_ref = {}) const;
  [[nodiscard]] DaemonClientResponse cancel(
      std::string_view receipt_ref,
      std::string_view ownership_token,
      std::string_view actor_ref = {}) const;
  [[nodiscard]] DaemonClientResponse read_readiness() const;
  [[nodiscard]] DaemonClientResponse run_diagnostics(
      std::string_view command_name) const;

 private:
  [[nodiscard]] DaemonClientResponse send_request(
      const dasall::access::daemon::UdsRequestFrame& frame) const;

  std::shared_ptr<dasall::platform::IIPC> ipc_;
  dasall::platform::IpcEndpoint endpoint_;
  std::int32_t connect_deadline_ms_ = 1000;
};

}  // namespace dasall::apps::cli
