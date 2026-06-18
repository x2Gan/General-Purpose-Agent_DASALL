#pragma once

#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "context/ContextAssemblyResult.h"
#include "context/MemoryContextRequest.h"
#include "writeback/MemoryWritebackRequest.h"
#include "writeback/WritebackResult.h"

namespace dasall::memory::observability {

class MemoryObservability;

class MemoryQualityProbe {
 public:
  struct RateSnapshot {
    std::uint64_t numerator = 0;
    std::uint64_t denominator = 0;
  };

  explicit MemoryQualityProbe(
      std::shared_ptr<MemoryObservability> observability = nullptr);

  void record_context_quality(const MemoryContextRequest& request,
                  const ContextAssemblyResult& result,
                  std::optional<std::string> full_summary_text = std::nullopt,
                  std::vector<std::string> supporting_contexts = {}) const;
  void record_writeback_quality(const MemoryWritebackRequest& request,
                                const WritebackResult& result) const;
  void record_conflict_quality(const MemoryWritebackRequest& request,
                               std::uint64_t attempted_conflicts,
                               std::uint64_t resolved_conflicts,
                               const std::vector<ConflictRecord>& conflicts) const;

 private:
  [[nodiscard]] RateSnapshot next_writeback_snapshot(bool partial) const;
  [[nodiscard]] RateSnapshot next_conflict_snapshot(std::uint64_t attempted,
                                                    std::uint64_t resolved) const;
  [[nodiscard]] RateSnapshot next_compression_snapshot(bool fallback) const;

  std::shared_ptr<MemoryObservability> observability_;
  mutable std::mutex mutex_;
  mutable std::uint64_t total_writebacks_ = 0;
  mutable std::uint64_t partial_writebacks_ = 0;
  mutable std::uint64_t total_conflict_records_ = 0;
  mutable std::uint64_t resolved_conflict_records_ = 0;
  mutable std::uint64_t total_compression_samples_ = 0;
  mutable std::uint64_t fallback_compression_samples_ = 0;
};

}  // namespace dasall::memory::observability
