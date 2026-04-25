#pragma once

namespace dasall::cognition {

class IReflectionEngine {
 public:
  virtual ~IReflectionEngine() = default;

 protected:
  IReflectionEngine() = default;
  IReflectionEngine(const IReflectionEngine&) = default;
  IReflectionEngine& operator=(const IReflectionEngine&) = default;
  IReflectionEngine(IReflectionEngine&&) = default;
  IReflectionEngine& operator=(IReflectionEngine&&) = default;
};

}  // namespace dasall::cognition
