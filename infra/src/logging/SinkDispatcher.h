#pragma once

#include <cstddef>
#include <optional>

#include "AsyncQueueController.h"
#include "LoggingFacade.h"
#include "LoggingPipelineTypes.h"

namespace dasall::infra::logging {

class SinkDispatcher final : public ILogDispatchBackend {
 public:
  SinkDispatcher();
  explicit SinkDispatcher(AsyncQueueOptions queue_options);

  LogWriteResult dispatch(const LogEvent& event) override;
  LogWriteResult flush(const LogFlushDeadline& deadline) override;

  [[nodiscard]] bool has_last_record() const {
    return last_record_.has_value();
  }

  [[nodiscard]] const RoutedLogRecord& last_record() const {
    return *last_record_;
  }

  [[nodiscard]] SinkRoute last_route() const {
    return has_last_record() ? last_record_->route : SinkRoute::BasicFile;
  }

  [[nodiscard]] std::size_t dispatched_record_count() const {
    return dispatched_record_count_;
  }

  [[nodiscard]] std::size_t dispatched_record_count(SinkRoute route) const;

  [[nodiscard]] std::size_t queue_depth() const {
    return queue_controller_.queue_depth();
  }

  [[nodiscard]] std::uint64_t dropped_total() const {
    return queue_controller_.dropped_total();
  }

  [[nodiscard]] std::uint64_t blocked_write_attempt_total() const {
    return queue_controller_.blocked_write_attempt_total();
  }

 private:
  [[nodiscard]] static SinkRoute select_route(const LogEvent& event);

  AsyncQueueController queue_controller_;
  std::optional<RoutedLogRecord> last_record_;
  std::size_t dispatched_record_count_ = 0;
  std::size_t basic_route_dispatch_count_ = 0;
  std::size_t audit_route_dispatch_count_ = 0;
};

}  // namespace dasall::infra::logging