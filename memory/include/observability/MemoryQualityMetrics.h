#pragma once

#include <string_view>

namespace dasall::memory::observability::quality {

inline constexpr std::string_view kRecallAtKMetricName =
    "memory_quality_recall_at_k";
inline constexpr std::string_view kSummaryFaithfulnessMetricName =
    "memory_quality_summary_faithfulness_score";
inline constexpr std::string_view kFactConflictPrecisionMetricName =
    "memory_quality_fact_conflict_precision";
inline constexpr std::string_view kWritebackPartialRateMetricName =
    "memory_quality_writeback_partial_rate";
inline constexpr std::string_view kCompressionFallbackRateMetricName =
    "memory_quality_compression_fallback_rate";

inline constexpr std::string_view kQualityMetricUnitRatio = "1";

inline constexpr int kDefaultRecallAtK = 5;
inline constexpr double kSummaryFaithfulnessSloFloor = 0.85;
inline constexpr double kFactConflictPrecisionSloFloor = 0.90;
inline constexpr double kWritebackPartialRateSloCeiling = 0.10;
inline constexpr double kCompressionFallbackRateSloCeiling = 0.25;

inline constexpr std::string_view kRecallAtKMetricDescription =
    "fraction of ground-truth memory evidence retrieved within top-k context candidates";
inline constexpr std::string_view kSummaryFaithfulnessMetricDescription =
    "fraction of generated summary claims that are supported by retrieved memory context";
inline constexpr std::string_view kFactConflictPrecisionMetricDescription =
    "precision of conflict actions that match curated fact-conflict expectations";
inline constexpr std::string_view kWritebackPartialRateMetricDescription =
    "ratio of writeback operations that complete with partial=true";
inline constexpr std::string_view kCompressionFallbackRateMetricDescription =
    "ratio of compression attempts that fell back to the template path";

}  // namespace dasall::memory::observability::quality