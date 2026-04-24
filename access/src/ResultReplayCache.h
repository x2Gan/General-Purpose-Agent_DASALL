#pragma once

#include <chrono>
#include <cstddef>
#include <list>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>

#include "AccessTypes.h"

namespace dasall::access {

class ResultReplayCache final {
 public:
  explicit ResultReplayCache(
      std::size_t capacity,
      std::chrono::milliseconds ttl = std::chrono::minutes(10));

  void put(std::string key, PublishEnvelope envelope);

  [[nodiscard]] std::optional<PublishEnvelope> lookup(const std::string& key);

  [[nodiscard]] bool erase(const std::string& key);

  [[nodiscard]] std::size_t evict_expired();

  [[nodiscard]] std::size_t size() const;

 private:
  struct Entry {
    PublishEnvelope envelope;
    std::chrono::steady_clock::time_point expires_at;
    std::list<std::string>::iterator lru_it;
  };

  void touch_lru_locked(const std::string& key, Entry& entry);
  void evict_if_needed_locked();
  bool is_expired_locked(const Entry& entry) const;

  std::size_t capacity_;
  std::chrono::milliseconds ttl_;
  mutable std::mutex mutex_;
  std::list<std::string> lru_keys_;
  std::unordered_map<std::string, Entry> entries_;
};

}  // namespace dasall::access
