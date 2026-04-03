#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

#include "LoggingFacade.h"

namespace dasall::infra::logging {

enum class SinkRoute {
  BasicFile,
  Audit,
};

[[nodiscard]] std::string_view sink_route_name(SinkRoute route);

struct RoutedLogRecord {
  SinkRoute route = SinkRoute::BasicFile;
  LogEvent event;
};

class SinkDispatcher final : public ILogDispatchBackend {
 public:
  LogWriteResult dispatch(const LogEvent& event) override;
  LogWriteResult flush(const LogFlushDeadline& deadline) override;

  [[nodiscard]] bool has_last_record() const {
    return !records_.empty();
  }

  [[nodiscard]] const RoutedLogRecord& last_record() const {
    return records_.back();
  }

  [[nodiscard]] SinkRoute last_route() const {
    return has_last_record() ? records_.back().route : SinkRoute::BasicFile;
  }

  [[nodiscard]] std::size_t dispatched_record_count() const {
    return records_.size();
  }

  [[nodiscard]] std::size_t dispatched_record_count(SinkRoute route) const;

  [[nodiscard]] std::uint32_t last_flush_timeout_ms() const {
    return last_flush_timeout_ms_;
  }

 private:
  [[nodiscard]] static SinkRoute select_route(const LogEvent& event);

  std::vector<RoutedLogRecord> records_;
  std::uint32_t last_flush_timeout_ms_ = 0;
};

}  // namespace dasall::infra::logging