#include "execution/CompensationCatalog.h"

#include <algorithm>
#include <utility>

namespace dasall::services::internal {

namespace {

[[nodiscard]] std::vector<CompensationCatalogEntry> make_default_entries() {
  return {
      CompensationCatalogEntry{
          .capability_id = "cap.exec",
          .action = "toggle",
          .capability_version = "v1",
          .descriptor = CompensationDescriptor{
              .compensation_hints = {"switch.disable"},
              .idempotency_requirements = {
                  "reuse source_execution_id as compensation idempotency seed",
              },
              .ordering_constraints = {
                  "switch.disable only after provider ack for switch.enable",
              },
          },
      },
      CompensationCatalogEntry{
          .capability_id = "cap.exec",
          .action = "safe_mode.enter",
          .capability_version = "v1",
          .descriptor = CompensationDescriptor{
              .compensation_hints = {"safe_mode.exit"},
              .idempotency_requirements = {
                  "bind compensation request to original decision_ref and source_execution_id",
              },
              .ordering_constraints = {
                  "safe_mode.exit only after enter evidence is durable",
              },
          },
      },
  };
}

}  // namespace

CompensationCatalog::CompensationCatalog(std::vector<CompensationCatalogEntry> entries)
    : entries_(entries.empty() ? make_default_entries() : std::move(entries)) {}

CompensationDescriptor CompensationCatalog::lookup(std::string_view capability_id,
                                                   std::string_view action,
                                                   std::string_view capability_version) const {
  const auto entry = std::find_if(entries_.begin(),
                                  entries_.end(),
                                  [capability_id, action, capability_version](const auto& item) {
                                    return item.capability_id == capability_id &&
                                           item.action == action &&
                                           item.capability_version == capability_version;
                                  });
  if (entry != entries_.end()) {
    return entry->descriptor;
  }

  return CompensationDescriptor{};
}

std::vector<std::string> CompensationCatalog::flatten_hints(
    const CompensationDescriptor& descriptor) const {
  std::vector<std::string> flattened = descriptor.compensation_hints;

  for (const auto& requirement : descriptor.idempotency_requirements) {
    flattened.push_back("idempotency:" + requirement);
  }

  for (const auto& constraint : descriptor.ordering_constraints) {
    flattened.push_back("order:" + constraint);
  }

  return flattened;
}

}  // namespace dasall::services::internal