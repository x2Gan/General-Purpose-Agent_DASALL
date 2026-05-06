#pragma once

#include <optional>
#include <string>

namespace dasall::contracts {

// ---------------------------------------------------------------------------
// RetrievalEvidenceRef is the additive + optional shared evidence projection
// frozen by RetrievalEvidenceProjectionV1.
//
// It preserves the minimum provenance / freshness / citation surface across
// knowledge -> runtime -> memory -> contracts without lifting EvidenceSlice or
// EvidenceBundle into shared contracts.
// ---------------------------------------------------------------------------
struct RetrievalEvidenceRef {
  std::string evidence_ref;
  std::string source_ref;
  std::string source_kind;
  std::string summary_text;
  std::string trust_level;
  std::string freshness;
  std::optional<std::string> anchor_locator;

  [[nodiscard]] bool has_consistent_values() const {
    if (evidence_ref.empty() || source_ref.empty() || source_kind.empty() ||
        summary_text.empty() || trust_level.empty() || freshness.empty()) {
      return false;
    }

    if (anchor_locator.has_value() && anchor_locator->empty()) {
      return false;
    }

    return true;
  }
};

}  // namespace dasall::contracts