#include "ResultReplayCache.h"

namespace dasall::access {

ResultReplayCache::ResultReplayCache(
    const std::size_t capacity,
    const std::chrono::milliseconds ttl)
    : capacity_(capacity == 0 ? 1 : capacity),
      ttl_(ttl <= std::chrono::milliseconds::zero() ? std::chrono::minutes(10) : ttl) {}

void ResultReplayCache::put(std::string key, PublishEnvelope envelope) {
  std::lock_guard<std::mutex> lock(mutex_);

  const auto now = std::chrono::steady_clock::now();
  const auto expires_at = now + ttl_;

  const auto existed = entries_.find(key);
  if (existed != entries_.end()) {
    lru_keys_.erase(existed->second.lru_it);
    entries_.erase(existed);
  }

  lru_keys_.push_front(key);
  entries_.emplace(
      key,
      Entry{
          .envelope = std::move(envelope),
          .expires_at = expires_at,
          .lru_it = lru_keys_.begin(),
      });

  evict_if_needed_locked();
}

std::optional<PublishEnvelope> ResultReplayCache::lookup(const std::string& key) {
  std::lock_guard<std::mutex> lock(mutex_);

  const auto it = entries_.find(key);
  if (it == entries_.end()) {
    return std::nullopt;
  }

  if (is_expired_locked(it->second)) {
    lru_keys_.erase(it->second.lru_it);
    entries_.erase(it);
    return std::nullopt;
  }

  touch_lru_locked(key, it->second);
  return it->second.envelope;
}

bool ResultReplayCache::erase(const std::string& key) {
  std::lock_guard<std::mutex> lock(mutex_);

  const auto it = entries_.find(key);
  if (it == entries_.end()) {
    return false;
  }

  lru_keys_.erase(it->second.lru_it);
  entries_.erase(it);
  return true;
}

std::size_t ResultReplayCache::evict_expired() {
  std::lock_guard<std::mutex> lock(mutex_);

  std::size_t evicted = 0;
  for (auto it = entries_.begin(); it != entries_.end();) {
    if (!is_expired_locked(it->second)) {
      ++it;
      continue;
    }

    lru_keys_.erase(it->second.lru_it);
    it = entries_.erase(it);
    ++evicted;
  }

  return evicted;
}

std::size_t ResultReplayCache::size() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return entries_.size();
}

void ResultReplayCache::touch_lru_locked(const std::string& key, Entry& entry) {
  lru_keys_.erase(entry.lru_it);
  lru_keys_.push_front(key);
  entry.lru_it = lru_keys_.begin();
}

void ResultReplayCache::evict_if_needed_locked() {
  while (entries_.size() > capacity_) {
    const std::string& key = lru_keys_.back();
    const auto it = entries_.find(key);
    if (it != entries_.end()) {
      entries_.erase(it);
    }
    lru_keys_.pop_back();
  }
}

bool ResultReplayCache::is_expired_locked(const Entry& entry) const {
  return std::chrono::steady_clock::now() >= entry.expires_at;
}

}  // namespace dasall::access
