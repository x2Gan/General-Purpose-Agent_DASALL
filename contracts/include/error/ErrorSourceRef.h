#pragma once

#include <string>
#include <vector>

namespace dasall::contracts {

// ErrorSourceRefEntry represents one structured source reference item.
// - primary=true means this item is the unique main attribution target.
// - primary=false means this item is a related attribution link.
struct ErrorSourceRefEntry {
  bool primary = false;
  std::string ref_type;
  std::string ref_id;
};

// ErrorSourceRefSet groups source references for one ErrorInfo instance.
// Exactly one item is expected to be primary, and all items must satisfy
// ref_type/ref_id validity constraints enforced by ErrorSourceGuards.
struct ErrorSourceRefSet {
  std::vector<ErrorSourceRefEntry> refs;
};

}  // namespace dasall::contracts
