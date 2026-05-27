#pragma once

#include "logging/ILogger.h"

namespace dasall::infra::logging {

class ILogSink {
 public:
  virtual ~ILogSink() = default;

  virtual LogWriteResult write(const LogEvent& event) = 0;
  virtual LogWriteResult flush(const LogFlushDeadline& deadline) = 0;
};

}  // namespace dasall::infra::logging