#pragma once

#include <string>

#include "PolicyTypes.h"

namespace dasall::infra::policy {

class IPolicySnapshotStore {
 public:
  virtual ~IPolicySnapshotStore() = default;

  [[nodiscard]] virtual PolicyOpResult commit(const PolicySnapshot& snapshot) = 0;
  [[nodiscard]] virtual PolicySnapshot current() const = 0;
  [[nodiscard]] virtual PolicySnapshot last_known_good() const = 0;
  [[nodiscard]] virtual PolicySnapshot get_by_id(const std::string& snapshot_id) const = 0;
};

}  // namespace dasall::infra::policy