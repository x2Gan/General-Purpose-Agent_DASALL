#pragma once

#include <memory>

#include "IStoreTransaction.h"

namespace dasall::memory {

class ITransactionalStore {
 public:
  virtual ~ITransactionalStore() = default;

  [[nodiscard]] virtual std::unique_ptr<IStoreTransaction> begin_immediate() = 0;
};

}  // namespace dasall::memory