#pragma once

#include <optional>

#include "error/ResultCode.h"

namespace dasall::memory {

class IStoreTransaction {
 public:
  virtual ~IStoreTransaction() = default;

  [[nodiscard]] virtual std::optional<contracts::ResultCode> commit() = 0;
  virtual void rollback() noexcept = 0;
};

}  // namespace dasall::memory