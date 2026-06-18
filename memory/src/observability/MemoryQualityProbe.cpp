#include "observability/MemoryQualityProbe.h"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

#include "observability/MemoryObservability.h"
#include "observability/MemoryQualityMetrics.h"

namespace dasall::memory::observability {
namespace {

using quality::kCompressionFallbackRateMetricDescription;
using quality::kCompressionFallbackRateMetricName;
using quality::kDefaultRecallAtK;
using quality::kFactConflictPrecisionMetricDescription;
using quality::kFactConflictPrecisionMetricName;
using quality::kQualityMetricUnitRatio;
using quality::kRecallAtKMetricDescription;
using quality::kRecallAtKMetricName;
using quality::kSummaryFaithfulnessMetricDescription;
using quality::kSummaryFaithfulnessMetricName;
using quality::kWritebackPartialRateMetricDescription;
using quality::kWritebackPartialRateMetricName;

[[nodiscard]] double safe_ratio(const std::uint64_t numerator,
                                const std::uint64_t denominator) {
  if (denominator == 0U) {
    return 0.0;
  }
  return static_cast<double>(numerator) / static_cast<double>(denominator);
}

[[nodiscard]] std::string normalize_text(std::string_view text) {
  std::string normalized;
  normalized.reserve(text.size());
  bool previous_space = false;
  for (const char ch : text) {
    const auto code = static_cast<unsigned char>(ch);
    if (std::isalnum(code) != 0) {
      normalized.push_back(static_cast<char>(std::tolower(code)));
      previous_space = false;
      continue;
    }

    if (ch == '_' || ch == '-') {
      normalized.push_back(' ');
      previous_space = true;
      continue;
    }

    if (code >= 0x80U) {
      normalized.push_back(ch);
      previous_space = false;
      continue;
    }

    if (!previous_space && !normalized.empty()) {
      normalized.push_back(' ');
      previous_space = true;
    }
  }

  while (!normalized.empty() && normalized.back() == ' ') {
    normalized.pop_back();
  }
  return normalized;
}

[[nodiscard]] std::vector<std::string> split_claims(std::string_view text) {
  std::vector<std::string> claims;
  std::string current;
  for (const char ch : text) {
    const bool separator = ch == '\n' || ch == '.' || ch == ';' || ch == '!' ||
                           ch == '?' || ch == ',' || ch == ':';
    if (!separator) {
      current.push_back(ch);
      continue;
    }

    const auto normalized = normalize_text(current);
    if (!normalized.empty()) {
      claims.push_back(normalized);
    }
    current.clear();
  }

  const auto trailing = normalize_text(current);
  if (!trailing.empty()) {
    claims.push_back(trailing);
  }
  return claims;
}

[[nodiscard]] std::unordered_set<std::string> tokenize(std::string_view text) {
  std::unordered_set<std::string> tokens;
  std::istringstream stream(normalize_text(text));
  std::string token;
  while (stream >> token) {
    const bool keep_token = token.size() >= 3U ||
                            std::all_of(token.begin(), token.end(),
                                        [](const char ch) {
                                          return std::isdigit(
                                                     static_cast<unsigned char>(ch)) != 0;
                                        });
    if (keep_token) {
      tokens.insert(token);
    }
  }
  return tokens;
}

[[nodiscard]] bool text_matches(std::string_view lhs, std::string_view rhs) {
  const auto normalized_left = normalize_text(lhs);
  const auto normalized_right = normalize_text(rhs);
  if (normalized_left.empty() || normalized_right.empty()) {
    return false;
  }
  if (normalized_left == normalized_right) {
    return true;
  }
  if (normalized_left.find(normalized_right) != std::string::npos ||
      normalized_right.find(normalized_left) != std::string::npos) {
    return true;
  }

  const auto left_tokens = tokenize(normalized_left);
  const auto right_tokens = tokenize(normalized_right);
  if (left_tokens.empty() || right_tokens.empty()) {
    return false;
  }

  for (const auto& token : left_tokens) {
    if (!right_tokens.contains(token)) {
      return false;
    }
  }

  return true;
}

[[nodiscard]] std::vector<std::string> collect_supporting_context(
    const contracts::ContextPacket& packet) {
  std::vector<std::string> contexts;
  if (packet.retrieval_evidence.has_value()) {
    contexts.insert(contexts.end(),
                    packet.retrieval_evidence->begin(),
                    packet.retrieval_evidence->end());
  }
  if (packet.recent_history.has_value()) {
    contexts.insert(contexts.end(),
                    packet.recent_history->begin(),
                    packet.recent_history->end());
  }
  if (packet.latest_observation_digest_summary.has_value()) {
    contexts.push_back(*packet.latest_observation_digest_summary);
  }
  if (packet.belief_state_summary.has_value()) {
    contexts.push_back(*packet.belief_state_summary);
  }
  return contexts;
}

[[nodiscard]] bool contains_warning(const std::vector<std::string>& warnings,
                                    std::string_view expected) {
  return std::find(warnings.begin(), warnings.end(), expected) != warnings.end();
}

[[nodiscard]] bool contains_note_prefix(const std::vector<std::string>& notes,
                                        std::string_view prefix,
                                        std::string* matched = nullptr) {
  const auto it = std::find_if(notes.begin(), notes.end(),
                               [prefix](const std::string& note) {
                                 return note.rfind(prefix, 0U) == 0U;
                               });
  if (it == notes.end()) {
    return false;
  }
  if (matched != nullptr) {
    *matched = *it;
  }
  return true;
}

[[nodiscard]] MemoryTelemetryContext make_context_metric_context(
    const MemoryContextRequest& request) {
  return MemoryTelemetryContext{
      .request_id = request.request_id.empty() ? "memory-quality-context"
                                               : request.request_id,
      .session_id = request.session_id,
      .stage = "context_quality",
      .trace_id = request.trace_id,
      .profile_id = {},
  };
}

[[nodiscard]] MemoryTelemetryContext make_writeback_metric_context(
    const MemoryWritebackRequest& request) {
  return MemoryTelemetryContext{
      .request_id = request.request_id.empty()
                        ? request.turn.turn_id.value_or("memory-quality-writeback")
                        : request.request_id,
      .session_id = request.session_id,
      .stage = "writeback_quality",
      .trace_id = request.trace_id,
      .profile_id = {},
  };
}

void emit_ratio_sample(const std::shared_ptr<MemoryObservability>& observability,
                       const MemoryTelemetryContext& context,
                       std::string_view metric_name,
                       std::string_view description,
                       const double value,
                       std::vector<MemoryTelemetryField> fields) {
  if (observability == nullptr) {
    return;
  }

  observability->emit_metric_sample(
      MemoryMetricSample{
          .name = std::string(metric_name),
          .type = infra::metrics::MetricType::Gauge,
          .value = value,
          .unit = std::string(kQualityMetricUnitRatio),
          .description = std::string(description),
          .outcome = "success",
          .error_code = {},
      },
      context,
      std::move(fields));
}

}  // namespace

MemoryQualityProbe::MemoryQualityProbe(
    std::shared_ptr<MemoryObservability> observability)
    : observability_(std::move(observability)) {}

void MemoryQualityProbe::record_context_quality(
    const MemoryContextRequest& request,
  const ContextAssemblyResult& result,
  std::optional<std::string> full_summary_text,
  std::vector<std::string> supporting_contexts) const {
  const auto context = make_context_metric_context(request);

  if (!request.external_evidence.empty()) {
    const auto retrieved_evidence = result.context_packet.retrieval_evidence
                                        .value_or(std::vector<std::string>{});
    std::uint64_t matched_evidence = 0U;
    for (const auto& expected : request.external_evidence) {
      const auto matched = std::find_if(
          retrieved_evidence.begin(), retrieved_evidence.end(),
          [&expected](const std::string& candidate) {
            return text_matches(expected, candidate);
          });
      if (matched != retrieved_evidence.end()) {
        ++matched_evidence;
      }
    }

    emit_ratio_sample(
        observability_,
        context,
        kRecallAtKMetricName,
        kRecallAtKMetricDescription,
        safe_ratio(matched_evidence, request.external_evidence.size()),
        {
            MemoryTelemetryField{.key = "k",
                                 .value = std::to_string(std::min<std::size_t>(
                                     kDefaultRecallAtK,
                                     retrieved_evidence.size()))},
            MemoryTelemetryField{.key = "expected_count",
                                 .value = std::to_string(request.external_evidence.size())},
            MemoryTelemetryField{.key = "matched_count",
                                 .value = std::to_string(matched_evidence)},
        });
  }

  const auto& summary_text =
      full_summary_text.has_value() ? full_summary_text
                                    : result.context_packet.summary_memory;
  if (summary_text.has_value() && !summary_text->empty()) {
    const auto claims = split_claims(*summary_text);
    const auto contexts = supporting_contexts.empty()
                              ? collect_supporting_context(result.context_packet)
                              : supporting_contexts;
    std::uint64_t supported_claims = 0U;
    for (const auto& claim : claims) {
      const auto matched = std::find_if(
          contexts.begin(), contexts.end(),
          [&claim](const std::string& supporting_text) {
            return text_matches(claim, supporting_text);
          });
      if (matched != contexts.end()) {
        ++supported_claims;
      }
    }

    emit_ratio_sample(
        observability_,
        context,
        kSummaryFaithfulnessMetricName,
        kSummaryFaithfulnessMetricDescription,
        safe_ratio(supported_claims, claims.size()),
        {
            MemoryTelemetryField{.key = "claim_count",
                                 .value = std::to_string(claims.size())},
            MemoryTelemetryField{.key = "supported_claim_count",
                                 .value = std::to_string(supported_claims)},
        });
  }

  const bool compression_attempted =
      !result.compression_notes.empty() ||
      contains_warning(result.warnings, "compression_skipped");
  if (!compression_attempted) {
    return;
  }

  const bool fallback = contains_warning(result.warnings, "compression_skipped") ||
                        contains_warning(result.warnings, "summarizer_fallback") ||
                        contains_note_prefix(result.compression_notes, "strategy:template") ||
                        std::find(result.compression_notes.begin(),
                                  result.compression_notes.end(),
                                  "summarizer_fallback") !=
                            result.compression_notes.end();
    const auto snapshot = next_compression_snapshot(fallback);
  emit_ratio_sample(
      observability_,
      context,
      kCompressionFallbackRateMetricName,
      kCompressionFallbackRateMetricDescription,
      safe_ratio(snapshot.numerator, snapshot.denominator),
      {
        MemoryTelemetryField{.key = "compression_sample_total",
                   .value = std::to_string(snapshot.denominator)},
          MemoryTelemetryField{.key = "compression_fallback_total",
                   .value = std::to_string(snapshot.numerator)},
      });
}

void MemoryQualityProbe::record_writeback_quality(
    const MemoryWritebackRequest& request,
    const WritebackResult& result) const {
  const auto snapshot = next_writeback_snapshot(result.partial);
  emit_ratio_sample(
      observability_,
      make_writeback_metric_context(request),
      kWritebackPartialRateMetricName,
      kWritebackPartialRateMetricDescription,
    safe_ratio(snapshot.numerator, snapshot.denominator),
      {
      MemoryTelemetryField{.key = "writeback_total",
                 .value = std::to_string(snapshot.denominator)},
          MemoryTelemetryField{.key = "partial_total",
                 .value = std::to_string(snapshot.numerator)},
      });
}

void MemoryQualityProbe::record_conflict_quality(
    const MemoryWritebackRequest& request,
    const std::uint64_t attempted_conflicts,
    const std::uint64_t resolved_conflicts,
    const std::vector<ConflictRecord>& conflicts) const {
  if (attempted_conflicts == 0U || conflicts.empty()) {
    return;
  }

  const auto snapshot =
      next_conflict_snapshot(attempted_conflicts, resolved_conflicts);
  emit_ratio_sample(
      observability_,
      make_writeback_metric_context(request),
      kFactConflictPrecisionMetricName,
      kFactConflictPrecisionMetricDescription,
      safe_ratio(snapshot.numerator, snapshot.denominator),
      {
          MemoryTelemetryField{.key = "conflict_total",
                               .value = std::to_string(snapshot.denominator)},
          MemoryTelemetryField{.key = "resolved_conflict_total",
                               .value = std::to_string(snapshot.numerator)},
          MemoryTelemetryField{.key = "current_conflict_count",
                               .value = std::to_string(conflicts.size())},
      });
}

MemoryQualityProbe::RateSnapshot MemoryQualityProbe::next_writeback_snapshot(
    const bool partial) const {
  std::scoped_lock lock(mutex_);
  ++total_writebacks_;
  if (partial) {
    ++partial_writebacks_;
  }
  return RateSnapshot{.numerator = partial_writebacks_,
                      .denominator = total_writebacks_};
}

MemoryQualityProbe::RateSnapshot MemoryQualityProbe::next_conflict_snapshot(
    const std::uint64_t attempted,
    const std::uint64_t resolved) const {
  std::scoped_lock lock(mutex_);
  total_conflict_records_ += attempted;
  resolved_conflict_records_ += resolved;
  return RateSnapshot{.numerator = resolved_conflict_records_,
                      .denominator = total_conflict_records_};
}

MemoryQualityProbe::RateSnapshot MemoryQualityProbe::next_compression_snapshot(
    const bool fallback) const {
  std::scoped_lock lock(mutex_);
  ++total_compression_samples_;
  if (fallback) {
    ++fallback_compression_samples_;
  }
  return RateSnapshot{.numerator = fallback_compression_samples_,
                      .denominator = total_compression_samples_};
}

}  // namespace dasall::memory::observability
