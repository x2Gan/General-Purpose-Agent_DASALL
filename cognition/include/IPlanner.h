#pragma once

namespace dasall::cognition {

class IPlanner {
 public:
  virtual ~IPlanner() = default;

 protected:
  IPlanner() = default;
  IPlanner(const IPlanner&) = default;
  IPlanner& operator=(const IPlanner&) = default;
  IPlanner(IPlanner&&) = default;
  IPlanner& operator=(IPlanner&&) = default;
};

}  // namespace dasall::cognition
