#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "stream/StreamSessionRef.h"

#include "../adapters/AdapterCallResult.h"

namespace dasall::llm {

struct StreamObserverFeedback {
  bool proceed = true;
  std::optional<contracts::ResultCode> result_code;
  std::string message;
  bool retryable = false;

  [[nodiscard]] bool has_consistent_values() const {
    if (proceed) {
      return !result_code.has_value() && message.empty() && !retryable;
    }

    return result_code.has_value() && !message.empty();
  }

  [[nodiscard]] static StreamObserverFeedback success() {
    return StreamObserverFeedback{};
  }

  [[nodiscard]] static StreamObserverFeedback reject(contracts::ResultCode code,
                                                     std::string message,
                                                     bool retryable = false) {
    return StreamObserverFeedback{
        .proceed = false,
        .result_code = code,
        .message = std::move(message),
        .retryable = retryable,
    };
  }
};

class IStreamObserver {
 public:
  virtual ~IStreamObserver() = default;

  [[nodiscard]] virtual StreamObserverFeedback on_stream_session_started(
      const StreamSessionRef& session_ref) = 0;
  [[nodiscard]] virtual StreamObserverFeedback on_stream_delta(
      std::string_view delta) = 0;
  virtual void on_stream_completed(const AdapterCallResult& result) = 0;
  virtual void on_stream_failed(const contracts::ErrorInfo& error,
                                std::optional<contracts::ResultCode> result_code) = 0;
};

}  // namespace dasall::llm