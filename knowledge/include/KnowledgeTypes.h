#pragma once

#include <cstdint>

namespace dasall::knowledge {

// Skeleton declarations land in 005 to stabilize include/CMake topology.
// Task 006 fills in the public ABI fields and enum values.
enum class KnowledgeQueryKind : std::uint8_t;
enum class RetrievalMode : std::uint8_t;
enum class FreshnessState : std::uint8_t;
enum class TrustLevel : std::uint8_t;

struct KnowledgeQuery {};
struct EvidenceSlice {};
struct EvidenceBundle {};
struct CorpusDescriptor {};
struct KnowledgeRetrieveResult {};
struct RefreshResult {};

}  // namespace dasall::knowledge