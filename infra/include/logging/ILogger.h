#pragma once

#include <ILogger.h>

namespace dasall::infra::logging {

using LogFlushDeadline = ::dasall::infra::LogFlushDeadline;
using LogWriteResult = ::dasall::infra::LogWriteResult;
using LogLevel = ::dasall::infra::LogLevel;

class ILogger : public ::dasall::infra::ILogger {
 public:
  ~ILogger() override = default;

  virtual LogWriteResult flush(const LogFlushDeadline& deadline) override = 0;
  virtual void set_level(LogLevel level) = 0;
};

}  // namespace dasall::infra::logging