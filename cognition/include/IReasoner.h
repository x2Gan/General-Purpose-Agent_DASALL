#pragma once

namespace dasall::cognition {

class IReasoner {
 public:
  virtual ~IReasoner() = default;

 protected:
  IReasoner() = default;
  IReasoner(const IReasoner&) = default;
  IReasoner& operator=(const IReasoner&) = default;
  IReasoner(IReasoner&&) = default;
  IReasoner& operator=(IReasoner&&) = default;
};

}  // namespace dasall::cognition
