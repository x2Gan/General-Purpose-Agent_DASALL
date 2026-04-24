#include "AdmissionController.h"

#include <chrono>
#include <cstddef>
#include <optional>
#include <string>
#include <utility>

namespace dasall::access {

AdmissionController::AdmissionController(AccessAdmissionView config)
    : config_(std::move(config)) {}

AccessAdmissionResult AdmissionController::admit(
    const RuntimeDispatchRequest& request) {
  const auto now = std::chrono::steady_clock::now();
  std::lock_guard<std::mutex> lock(mutex_);

  // Admission 判定前先清理过期幂等记录，防止窗口外数据污染当前决策。
  prune_expired_records(now);

  if (config_.max_inflight_requests <= 0 ||
      inflight_tickets_.size() >= static_cast<std::size_t>(config_.max_inflight_requests)) {
    AccessAdmissionResult result;
    result.admitted = false;
    result.reject_reason = std::string("concurrency_limit_exceeded");
    return result;
  }

  const std::string signature = build_idempotency_signature(request);
  if (signature.empty()) {
    AccessAdmissionResult result;
    result.admitted = false;
    result.reject_reason = std::string("admission_rejected");
    return result;
  }

  if (const auto existing = check_idempotency(signature, now); existing.has_value()) {
    return *existing;
  }

  const InflightTicket ticket = acquire_inflight_ticket(signature, now);

  auto& record = idempotency_records_[signature];
  record.signature = signature;
  record.inflight_ticket_ref = ticket.ticket_ref;
  record.completed = false;
  record.replay_receipt_ref = std::nullopt;
  record.expires_at = now + std::chrono::milliseconds(config_.idempotency_window_ms);

  AccessAdmissionResult result;
  result.admitted = true;
  result.ticket_ref = ticket.ticket_ref;
  return result;
}

void AdmissionController::release_ticket(const std::string& ticket_ref) {
  std::lock_guard<std::mutex> lock(mutex_);
  release_inflight_ticket(ticket_ref);
}

void AdmissionController::record_completion(const std::string& ticket_ref,
                                            const RuntimeDispatchResult& result) {
  std::lock_guard<std::mutex> lock(mutex_);

  const auto inflight_it = inflight_tickets_.find(ticket_ref);
  if (inflight_it == inflight_tickets_.end()) {
    return;
  }

  const std::string signature = inflight_it->second.signature;
  const auto record_it = idempotency_records_.find(signature);
  if (record_it != idempotency_records_.end()) {
    record_it->second.completed = true;
    record_it->second.inflight_ticket_ref = std::nullopt;
    record_it->second.replay_receipt_ref =
        result.receipt_ref.value_or(make_receipt_ref(ticket_ref));
  }

  inflight_tickets_.erase(inflight_it);
}

std::string AdmissionController::build_idempotency_signature(
    const RuntimeDispatchRequest& request) const {
  // 优先使用显式 idempotency key；若缺失则退回到入口关键字段拼接签名。
  const auto idempotency_key_it = request.request_context.find("idempotency_key");
  if (idempotency_key_it != request.request_context.end() &&
      !idempotency_key_it->second.empty()) {
    return request.packet.entry_type + "|" + request.packet.protocol_kind + "|" +
           request.subject_identity.actor_ref + "|" + idempotency_key_it->second;
  }

  if (request.packet.packet_id.empty() || request.subject_identity.actor_ref.empty()) {
    return std::string();
  }

  return request.packet.entry_type + "|" + request.packet.protocol_kind + "|" +
         request.subject_identity.actor_ref + "|" + request.packet.packet_id;
}

std::optional<AccessAdmissionResult> AdmissionController::check_idempotency(
    const std::string& signature,
    const std::chrono::steady_clock::time_point now) const {
  const auto record_it = idempotency_records_.find(signature);
  if (record_it == idempotency_records_.end()) {
    return std::nullopt;
  }

  const IdempotencyRecord& record = record_it->second;
  if (record.expires_at < now) {
    return std::nullopt;
  }

  if (record.completed) {
    AccessAdmissionResult replay_result;
    replay_result.admitted = false;
    replay_result.replay_hit = true;
    replay_result.replay_receipt_ref = record.replay_receipt_ref;
    replay_result.reject_reason = std::string("idempotency_replay_hit");
    return replay_result;
  }

  if (record.inflight_ticket_ref.has_value()) {
    AccessAdmissionResult conflict_result;
    conflict_result.admitted = false;
    conflict_result.conflict_hit = true;
    conflict_result.ticket_ref = record.inflight_ticket_ref;
    conflict_result.reject_reason = std::string("idempotency_conflict");
    return conflict_result;
  }

  return std::nullopt;
}

AdmissionController::InflightTicket AdmissionController::acquire_inflight_ticket(
    const std::string& signature,
    const std::chrono::steady_clock::time_point now) {
  ++ticket_counter_;
  InflightTicket ticket;
  ticket.ticket_ref = "ticket-" + std::to_string(ticket_counter_);
  ticket.signature = signature;
  ticket.issued_at = now;
  inflight_tickets_[ticket.ticket_ref] = ticket;
  return ticket;
}

void AdmissionController::release_inflight_ticket(const std::string& ticket_ref) {
  const auto inflight_it = inflight_tickets_.find(ticket_ref);
  if (inflight_it == inflight_tickets_.end()) {
    return;
  }

  const std::string signature = inflight_it->second.signature;
  inflight_tickets_.erase(inflight_it);

  // release_ticket 通常对应 dispatch 前失败或提前终止，直接清理对应幂等记录。
  const auto record_it = idempotency_records_.find(signature);
  if (record_it != idempotency_records_.end() && !record_it->second.completed) {
    idempotency_records_.erase(record_it);
  }
}

void AdmissionController::prune_expired_records(
    const std::chrono::steady_clock::time_point now) {
  for (auto it = idempotency_records_.begin(); it != idempotency_records_.end();) {
    if (it->second.expires_at < now && !it->second.inflight_ticket_ref.has_value()) {
      it = idempotency_records_.erase(it);
      continue;
    }

    ++it;
  }
}

std::string AdmissionController::make_receipt_ref(const std::string& ticket_ref) const {
  return "receipt-for-" + ticket_ref;
}

}  // namespace dasall::access
