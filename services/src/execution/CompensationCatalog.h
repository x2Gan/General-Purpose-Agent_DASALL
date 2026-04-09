#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace dasall::services::internal {

struct CompensationDescriptor {
  std::vector<std::string> compensation_hints;
  std::vector<std::string> idempotency_requirements;
  std::vector<std::string> ordering_constraints;

  [[nodiscard]] bool empty() const {
    return compensation_hints.empty() && idempotency_requirements.empty() &&
           ordering_constraints.empty();
  }
};

struct CompensationCatalogEntry {
  std::string capability_id;
  std::string action;
  std::string capability_version;
  CompensationDescriptor descriptor;
};

class CompensationCatalog {
 public:
  explicit CompensationCatalog(std::vector<CompensationCatalogEntry> entries = {});

  [[nodiscard]] CompensationDescriptor lookup(std::string_view capability_id,
                                              std::string_view action,
                                              std::string_view capability_version) const;
  [[nodiscard]] std::vector<std::string> flatten_hints(
      const CompensationDescriptor& descriptor) const;

 private:
  std::vector<CompensationCatalogEntry> entries_;
};

}  // namespace dasall::services::internal