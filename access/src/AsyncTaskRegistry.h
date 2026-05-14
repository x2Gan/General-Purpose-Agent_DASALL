#pragma once

#include <chrono>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

#include "AccessTypes.h"

namespace dasall::access {

class AsyncTaskRegistry final {
 public:
    struct OwnershipTokenKey {
        std::string key_id;
        std::string secret;

        [[nodiscard]] bool is_valid() const {
            return !key_id.empty() && !secret.empty();
        }
    };

  enum class QueryStatus {
    Found = 0,
    NotFound = 1,
    Expired = 2,
  };

  struct QueryResult {
    QueryStatus status = QueryStatus::NotFound;
    std::optional<AsyncTaskReceipt> receipt;
  };

  explicit AsyncTaskRegistry(
      std::string ownership_secret,
      std::chrono::milliseconds receipt_ttl = std::chrono::minutes(10));

  explicit AsyncTaskRegistry(
      OwnershipTokenKey current_key,
      std::optional<OwnershipTokenKey> previous_key = std::nullopt,
      std::chrono::milliseconds receipt_ttl = std::chrono::minutes(10));

  [[nodiscard]] std::optional<AsyncTaskReceipt> register_async_accept(
      const RuntimeDispatchRequest& request,
      const RuntimeDispatchResult& result);

  [[nodiscard]] QueryResult query_receipt(const std::string& receipt_id);

  [[nodiscard]] bool validate_ownership(
      const std::string& receipt_id,
      std::string_view actor_ref,
      std::string_view ownership_token);

  [[nodiscard]] bool mark_completed(
      const std::string& receipt_id,
      std::string_view task_status);

  [[nodiscard]] std::size_t prune_expired();

  [[nodiscard]] std::size_t size() const;

    [[nodiscard]] bool enabled() const;

    void rotate_ownership_keys(
            OwnershipTokenKey current_key,
            std::optional<OwnershipTokenKey> previous_key = std::nullopt);

 private:
  struct StoredReceipt {
    AsyncTaskReceipt receipt;
    std::string task_status = "pending";
  };

  [[nodiscard]] static std::string build_receipt_id(
      const RuntimeDispatchRequest& request,
      const RuntimeDispatchResult& result);

  [[nodiscard]] std::string build_ownership_token(
      std::string_view receipt_id,
      std::string_view actor_ref,
      std::string_view request_id,
      std::int64_t issued_at_unix_ms,
      std::int64_t expires_at_unix_ms) const;

  [[nodiscard]] static bool constant_time_equals(
      std::string_view lhs,
      std::string_view rhs);

  [[nodiscard]] std::size_t prune_expired_locked(
      std::chrono::steady_clock::time_point now);

  void erase_expired_locked(const std::string& receipt_id);

  std::chrono::milliseconds receipt_ttl_;
  mutable std::mutex mutex_;
  std::unordered_map<std::string, StoredReceipt> receipts_;
    OwnershipTokenKey current_key_;
    std::optional<OwnershipTokenKey> previous_key_;
};

}  // namespace dasall::access
