#pragma once

#include <chrono>
#include <cstddef>
#include <map>
#include <mutex>
#include <optional>
#include <string>

#include "AccessTypes.h"
#include "IAdmissionController.h"

namespace dasall::access {

// AdmissionController 统一收敛并发配额和幂等窗口判定。
// 它实现 IAdmissionController 接口，作为 access 主链 runtime 前的最后一道准入门。
class AdmissionController final : public IAdmissionController {
 public:
  explicit AdmissionController(AccessAdmissionView config = {});

  [[nodiscard]] AccessAdmissionResult admit(
      const RuntimeDispatchRequest& request) override;

  void release_ticket(const std::string& ticket_ref) override;

  void record_completion(const std::string& ticket_ref,
                         const RuntimeDispatchResult& result) override;

 private:
  struct InflightTicket {
    std::string ticket_ref;
    std::string signature;
    std::chrono::steady_clock::time_point issued_at;
  };

  struct IdempotencyRecord {
    std::string signature;
    std::optional<std::string> inflight_ticket_ref;
    bool completed = false;
    std::optional<std::string> replay_receipt_ref;
    std::chrono::steady_clock::time_point expires_at;
  };

  [[nodiscard]] std::string build_idempotency_signature(
      const RuntimeDispatchRequest& request) const;

  [[nodiscard]] std::optional<AccessAdmissionResult> check_idempotency(
      const std::string& signature,
      std::chrono::steady_clock::time_point now) const;

  [[nodiscard]] InflightTicket acquire_inflight_ticket(
      const std::string& signature,
      std::chrono::steady_clock::time_point now);

  void release_inflight_ticket(const std::string& ticket_ref);

  void prune_expired_records(std::chrono::steady_clock::time_point now);

  [[nodiscard]] std::string make_receipt_ref(const std::string& ticket_ref) const;

  AccessAdmissionView config_;
  mutable std::mutex mutex_;
  std::size_t ticket_counter_ = 0;
  std::map<std::string, InflightTicket> inflight_tickets_;
  std::map<std::string, IdempotencyRecord> idempotency_records_;
};

}  // namespace dasall::access
