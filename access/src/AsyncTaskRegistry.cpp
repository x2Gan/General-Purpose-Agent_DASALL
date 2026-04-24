#include "AsyncTaskRegistry.h"

#include <cstdint>
#include <functional>
#include <sstream>

namespace dasall::access {

namespace {

[[nodiscard]] std::string to_hex(std::uint64_t value) {
  std::ostringstream stream;
  stream << std::hex;
  stream.width(16);
  stream.fill('0');
  stream << value;
  return stream.str();
}

[[nodiscard]] std::string context_or_default(
    const RuntimeDispatchRequest& request,
    const std::string& key,
    const std::string& fallback) {
  const auto it = request.request_context.find(key);
  if (it == request.request_context.end() || it->second.empty()) {
    return fallback;
  }

  return it->second;
}

}  // namespace

AsyncTaskRegistry::AsyncTaskRegistry(
    std::string ownership_secret,
    const std::chrono::milliseconds receipt_ttl)
    : ownership_secret_(std::move(ownership_secret)),
      receipt_ttl_(receipt_ttl) {
  if (ownership_secret_.empty()) {
    ownership_secret_ = "access-static-secret-v1";
  }

  if (receipt_ttl_ <= std::chrono::milliseconds::zero()) {
    receipt_ttl_ = std::chrono::minutes(10);
  }
}

std::optional<AsyncTaskReceipt> AsyncTaskRegistry::register_async_accept(
    const RuntimeDispatchRequest& request,
    const RuntimeDispatchResult& result) {
  if (result.disposition != AccessDisposition::AcceptedAsync) {
    return std::nullopt;
  }

  const auto receipt_id = build_receipt_id(request, result);
  const auto request_id = context_or_default(request, "request_id", request.packet.packet_id);
  const auto session_id = context_or_default(request, "session_id", "session:" + request_id);

  AsyncTaskReceipt receipt;
  receipt.receipt_id = receipt_id;
  receipt.request_id = request_id;
  receipt.session_id = session_id;
  receipt.actor_ref = request.subject_identity.actor_ref;
  receipt.task_ref = result.receipt_ref.value_or("task:" + request_id);
  receipt.expires_at = std::chrono::steady_clock::now() + receipt_ttl_;
  receipt.ownership_token = build_ownership_token(
      receipt.receipt_id,
      receipt.actor_ref,
      receipt.request_id);
  receipt.initial_status = std::string("pending");

  std::lock_guard<std::mutex> lock(mutex_);
  receipts_[receipt.receipt_id] = StoredReceipt{
      .receipt = receipt,
      .task_status = "pending",
  };

  return receipt;
}

AsyncTaskRegistry::QueryResult AsyncTaskRegistry::query_receipt(
    const std::string& receipt_id) {
  std::lock_guard<std::mutex> lock(mutex_);

  const auto it = receipts_.find(receipt_id);
  if (it == receipts_.end()) {
    return QueryResult{.status = QueryStatus::NotFound, .receipt = std::nullopt};
  }

  const auto now = std::chrono::steady_clock::now();
  if (now >= it->second.receipt.expires_at) {
    erase_expired_locked(receipt_id);
    return QueryResult{.status = QueryStatus::Expired, .receipt = std::nullopt};
  }

  return QueryResult{.status = QueryStatus::Found, .receipt = it->second.receipt};
}

bool AsyncTaskRegistry::validate_ownership(
    const std::string& receipt_id,
    const std::string_view actor_ref,
    const std::string_view ownership_token) {
  std::lock_guard<std::mutex> lock(mutex_);

  const auto it = receipts_.find(receipt_id);
  if (it == receipts_.end()) {
    return false;
  }

  const auto now = std::chrono::steady_clock::now();
  if (now >= it->second.receipt.expires_at) {
    erase_expired_locked(receipt_id);
    return false;
  }

  if (it->second.receipt.actor_ref != actor_ref) {
    return false;
  }

  // 采用常量时序比较，避免通过提前返回暴露 token 差异位置。
  return constant_time_equals(it->second.receipt.ownership_token, ownership_token);
}

bool AsyncTaskRegistry::mark_completed(
    const std::string& receipt_id,
    const std::string_view task_status) {
  std::lock_guard<std::mutex> lock(mutex_);

  const auto it = receipts_.find(receipt_id);
  if (it == receipts_.end()) {
    return false;
  }

  const auto now = std::chrono::steady_clock::now();
  if (now >= it->second.receipt.expires_at) {
    erase_expired_locked(receipt_id);
    return false;
  }

  it->second.task_status = std::string(task_status);
  it->second.receipt.initial_status = it->second.task_status;
  return true;
}

std::size_t AsyncTaskRegistry::size() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return receipts_.size();
}

std::string AsyncTaskRegistry::build_receipt_id(
    const RuntimeDispatchRequest& request,
    const RuntimeDispatchResult& result) {
  if (result.receipt_ref.has_value() && !result.receipt_ref->empty()) {
    return *result.receipt_ref;
  }

  const auto request_id = context_or_default(request, "request_id", request.packet.packet_id);
  return "receipt:" + request_id;
}

std::string AsyncTaskRegistry::build_ownership_token(
    const std::string_view receipt_id,
    const std::string_view actor_ref,
    const std::string_view request_id) const {
  // v1 采用部署静态 secret + 稳定哈希串联；后续可无缝替换为正式 HMAC 实现。
  const std::string material = ownership_secret_ + "|" +
                               std::string(receipt_id) + "|" +
                               std::string(actor_ref) + "|" +
                               std::string(request_id);
  const std::uint64_t hash_value = std::hash<std::string>{}(material);
  return to_hex(hash_value);
}

bool AsyncTaskRegistry::constant_time_equals(
    const std::string_view lhs,
    const std::string_view rhs) {
  const std::size_t max_size = lhs.size() > rhs.size() ? lhs.size() : rhs.size();
  std::uint8_t diff = static_cast<std::uint8_t>(lhs.size() ^ rhs.size());

  for (std::size_t index = 0; index < max_size; ++index) {
    const std::uint8_t left = index < lhs.size()
                                  ? static_cast<std::uint8_t>(lhs[index])
                                  : static_cast<std::uint8_t>(0U);
    const std::uint8_t right = index < rhs.size()
                                   ? static_cast<std::uint8_t>(rhs[index])
                                   : static_cast<std::uint8_t>(0U);
    diff = static_cast<std::uint8_t>(diff | static_cast<std::uint8_t>(left ^ right));
  }

  return diff == 0U;
}

void AsyncTaskRegistry::erase_expired_locked(const std::string& receipt_id) {
  receipts_.erase(receipt_id);
}

}  // namespace dasall::access
