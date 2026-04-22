#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>

namespace dasall::runtime {

class CancellationToken {
 public:
  CancellationToken() : state_(std::make_shared<State>()) {}

  void cancel() const {
    state_->cancelled.store(true, std::memory_order_release);
  }

  void bind_deadline(std::chrono::system_clock::time_point deadline) const {
    state_->deadline_epoch_ms.store(to_epoch_ms(deadline), std::memory_order_release);
  }

  [[nodiscard]] bool has_deadline() const {
    return state_->deadline_epoch_ms.load(std::memory_order_acquire) >= 0;
  }

  [[nodiscard]] std::optional<std::chrono::system_clock::time_point> deadline() const {
    const auto deadline_ms = state_->deadline_epoch_ms.load(std::memory_order_acquire);
    if (deadline_ms < 0) {
      return std::nullopt;
    }

    return std::chrono::system_clock::time_point{std::chrono::milliseconds{deadline_ms}};
  }

  [[nodiscard]] bool is_cancelled() const {
    if (state_->cancelled.load(std::memory_order_acquire)) {
      return true;
    }

    const auto deadline_ms = state_->deadline_epoch_ms.load(std::memory_order_acquire);
    if (deadline_ms >= 0 && current_time_ms() >= deadline_ms) {
      state_->cancelled.store(true, std::memory_order_release);
      return true;
    }

    return false;
  }

 private:
  struct State {
    std::atomic<bool> cancelled{false};
    std::atomic<std::int64_t> deadline_epoch_ms{-1};
  };

  static std::int64_t current_time_ms() {
    const auto now = std::chrono::system_clock::now().time_since_epoch();
    return std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
  }

  static std::int64_t to_epoch_ms(std::chrono::system_clock::time_point deadline) {
    return std::chrono::duration_cast<std::chrono::milliseconds>(deadline.time_since_epoch())
        .count();
  }

  std::shared_ptr<State> state_;
};

}  // namespace dasall::runtime