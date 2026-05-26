#include <sqlite3.h>

#include <algorithm>
#include <cmath>
#include <cctype>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "IKnowledgeService.h"
#include "KnowledgeTypes.h"
#include "evidence/EvidenceAssembler.h"
#include "facade/KnowledgeService.h"
#include "health/FreshnessController.h"
#include "index/CorpusCatalog.h"
#include "index/IndexReader.h"
#include "query/CorpusRouter.h"
#include "query/QueryNormalizer.h"
#include "rerank/Reranker.h"
#include "retrieve/RecallCoordinator.h"
#include "retrieve/SparseRetriever.h"
#include "support/TestAssertions.h"

#ifndef DASALL_KNOWLEDGE_INTEGRATION_TEST_DIR
#define DASALL_KNOWLEDGE_INTEGRATION_TEST_DIR "/home/gangan/DASALL/tests/integration/knowledge"
#endif

namespace {

using dasall::knowledge::AuthorityLevel;
using dasall::knowledge::CorpusDescriptor;
using dasall::knowledge::IndexManifest;
using dasall::knowledge::KnowledgeConfigSnapshot;
using dasall::knowledge::KnowledgeQuery;
using dasall::knowledge::KnowledgeQueryKind;
using dasall::knowledge::RetrievalMode;
using dasall::knowledge::SourceFormat;
using dasall::knowledge::SourceKind;
using dasall::knowledge::TrustLevel;
using dasall::knowledge::evidence::EvidenceAssembler;
using dasall::knowledge::facade::KnowledgeServiceDeps;
using dasall::knowledge::facade::KnowledgeServiceFacade;
using dasall::knowledge::index::CorpusCatalog;
using dasall::knowledge::index::IndexReader;
using dasall::knowledge::index::IndexSnapshot;
using dasall::knowledge::query::CorpusRouter;
using dasall::knowledge::query::QueryNormalizePolicy;
using dasall::knowledge::query::QueryNormalizer;
using dasall::knowledge::rerank::Reranker;
using dasall::knowledge::retrieve::DenseRecallRequest;
using dasall::knowledge::retrieve::DenseRecallResult;
using dasall::knowledge::retrieve::RecallCoordinator;
using dasall::knowledge::retrieve::RecallCoordinatorDeps;
using dasall::knowledge::retrieve::RecallCoordinatorPolicy;
using dasall::knowledge::retrieve::SparseIndexSearchRequest;
using dasall::knowledge::retrieve::SparseIndexSearchResult;
using dasall::knowledge::retrieve::SparseRetriever;
using dasall::knowledge::retrieve::SparseRetrieverDeps;
using dasall::knowledge::retrieve::SparseSearchRow;
using dasall::tests::support::assert_equal;
using dasall::tests::support::assert_true;

struct ParsedYamlDocument {
  bool ok = false;
  std::string error;
  std::map<std::string, std::string> scalar_values;
  std::map<std::string, std::vector<std::string>> list_values;
};

struct QualityMetricBundle {
  double mrr_at_10 = 0.0;
  double ndcg_at_10 = 0.0;
  double recall_at_5 = 0.0;
  double recall_at_10 = 0.0;
};

struct AggregateThresholds {
  double min_mrr_at_10 = 0.0;
  double min_ndcg_at_10 = 0.0;
  double min_recall_at_5 = 0.0;
  double min_recall_at_10 = 0.0;
  double max_regression_pct = 0.0;
};

struct ModeQualityGate {
  bool enabled = false;
  AggregateThresholds thresholds;
  QualityMetricBundle baseline_metrics;
  int min_cases = 0;
  int min_hard_fail_cases = 0;
  int min_architecture_reference_cases = 0;
  int min_adr_normative_cases = 0;
  int min_ssot_normative_cases = 0;
};

struct CoverageRequirements {
  int min_total_cases = 0;
  int min_fact_lookup_cases = 0;
  int min_procedure_lookup_cases = 0;
  int min_diagnostic_context_cases = 0;
  int min_hard_fail_cases = 0;
  int min_architecture_reference_cases = 0;
  int min_adr_normative_cases = 0;
  int min_ssot_normative_cases = 0;
  int min_profile_policy_normative_cases = 0;
};

struct QualityCaseSpec {
  std::string case_id;
  KnowledgeQueryKind query_kind = KnowledgeQueryKind::FactLookup;
  std::string query_text;
  std::string primary_corpus_id;
  std::vector<RetrievalMode> allowed_modes;
  std::vector<std::string> expected_source_uris;
  std::vector<std::string> expected_chunk_refs;
  int required_top_k = 5;
  bool hard_fail = false;
  double min_recall_at_5 = 0.0;
  double min_mrr_at_10 = 0.0;
};

struct QualityManifest {
  int format_version = 0;
  std::string dataset_id;
  std::size_t retrieval_top_k = 10U;
  AggregateThresholds thresholds;
  QualityMetricBundle baseline_metrics;
  std::map<RetrievalMode, ModeQualityGate> mode_gates;
  CoverageRequirements coverage;
  bool context_metrics_enabled = false;
  std::vector<std::string> context_metric_fields;
  std::vector<QualityCaseSpec> cases;
};

struct QualityEvaluationReport {
  bool passed = false;
  int executed_cases = 0;
  int skipped_cases = 0;
  QualityMetricBundle aggregate_metrics;
  std::vector<std::string> failures;
};

[[nodiscard]] std::filesystem::path manifest_path() {
  return std::filesystem::path(DASALL_KNOWLEDGE_INTEGRATION_TEST_DIR) /
         "golden/retrieval_quality_v1.yaml";
}

[[nodiscard]] std::string trim_copy(std::string value) {
  const auto begin = value.find_first_not_of(" \t\r\n");
  if (begin == std::string::npos) {
    return "";
  }

  const auto end = value.find_last_not_of(" \t\r\n");
  return value.substr(begin, end - begin + 1U);
}

[[nodiscard]] std::size_t count_indent(const std::string& line) {
  std::size_t indent = 0U;
  while (indent < line.size() && (line[indent] == ' ' || line[indent] == '\t')) {
    ++indent;
  }

  return indent;
}

[[nodiscard]] std::string join_path(const std::vector<std::pair<std::size_t, std::string>>& path,
                                    const std::string& leaf_key) {
  std::string joined;
  bool first = true;

  for (const auto& node : path) {
    if (!first) {
      joined.push_back('.');
    }
    joined.append(node.second);
    first = false;
  }

  if (!leaf_key.empty()) {
    if (!first) {
      joined.push_back('.');
    }
    joined.append(leaf_key);
  }

  return joined;
}

[[nodiscard]] std::string strip_inline_comment(std::string value) {
  for (std::size_t index = 0U; index < value.size(); ++index) {
    if (value[index] != '#') {
      continue;
    }

    if (index == 0U || std::isspace(static_cast<unsigned char>(value[index - 1U])) != 0) {
      return trim_copy(value.substr(0U, index));
    }
  }

  return trim_copy(std::move(value));
}

[[nodiscard]] ParsedYamlDocument parse_yaml_file(const std::filesystem::path& path) {
  ParsedYamlDocument parsed;

  std::ifstream stream(path);
  if (!stream.is_open()) {
    parsed.error = "unable to open retrieval quality manifest";
    return parsed;
  }

  std::vector<std::pair<std::size_t, std::string>> path_stack;
  std::string raw_line;
  while (std::getline(stream, raw_line)) {
    const std::string trimmed = trim_copy(raw_line);
    if (trimmed.empty() || trimmed.starts_with('#')) {
      continue;
    }

    const std::size_t indent = count_indent(raw_line);
    if (trimmed.starts_with("- ")) {
      while (!path_stack.empty() && path_stack.back().first >= indent) {
        path_stack.pop_back();
      }

      if (path_stack.empty()) {
        parsed.error = "yaml list item is missing parent key";
        return parsed;
      }

      parsed.list_values[join_path(path_stack, "")].push_back(
          strip_inline_comment(trimmed.substr(2U)));
      continue;
    }

    const auto colon_position = trimmed.find(':');
    if (colon_position == std::string::npos) {
      parsed.error = "yaml line missing colon separator";
      return parsed;
    }

    const std::string key = trim_copy(trimmed.substr(0U, colon_position));
    const std::string value = strip_inline_comment(trimmed.substr(colon_position + 1U));

    while (!path_stack.empty() && path_stack.back().first >= indent) {
      path_stack.pop_back();
    }

    if (value.empty()) {
      path_stack.emplace_back(indent, key);
      continue;
    }

    parsed.scalar_values[join_path(path_stack, key)] = value;
  }

  parsed.ok = true;
  return parsed;
}

[[nodiscard]] std::string require_scalar(const ParsedYamlDocument& parsed, const std::string& key) {
  const auto iterator = parsed.scalar_values.find(key);
  if (iterator == parsed.scalar_values.end()) {
    throw std::runtime_error("retrieval quality manifest missing scalar key: " + key);
  }

  return iterator->second;
}

[[nodiscard]] std::vector<std::string> require_list(const ParsedYamlDocument& parsed,
                                                    const std::string& key) {
  const auto iterator = parsed.list_values.find(key);
  if (iterator == parsed.list_values.end()) {
    throw std::runtime_error("retrieval quality manifest missing list key: " + key);
  }

  return iterator->second;
}

[[nodiscard]] bool has_scalar(const ParsedYamlDocument& parsed, const std::string& key) {
  return parsed.scalar_values.find(key) != parsed.scalar_values.end();
}

[[nodiscard]] std::string retrieval_mode_name(RetrievalMode mode) {
  switch (mode) {
    case RetrievalMode::LexicalOnly:
      return "LexicalOnly";
    case RetrievalMode::DenseOnly:
      return "DenseOnly";
    case RetrievalMode::Hybrid:
      return "Hybrid";
  }

  throw std::runtime_error("retrieval quality manifest invalid retrieval mode enum");
}

void load_optional_mode_gate(const ParsedYamlDocument& parsed,
                             RetrievalMode mode,
                             QualityManifest& manifest) {
  const auto prefix = std::string("mode_gates.") + retrieval_mode_name(mode) + ".";
  if (!has_scalar(parsed, prefix + "min_mrr_at_10")) {
    return;
  }

  auto& gate = manifest.mode_gates[mode];
  gate.enabled = true;
    gate.thresholds.min_mrr_at_10 = std::stod(require_scalar(parsed, prefix + "min_mrr_at_10"));
    gate.thresholds.min_ndcg_at_10 = std::stod(require_scalar(parsed, prefix + "min_ndcg_at_10"));
    gate.thresholds.min_recall_at_5 = std::stod(require_scalar(parsed, prefix + "min_recall_at_5"));
    gate.thresholds.min_recall_at_10 = std::stod(require_scalar(parsed, prefix + "min_recall_at_10"));
    gate.thresholds.max_regression_pct = std::stod(require_scalar(parsed, prefix + "max_regression_pct"));
    gate.baseline_metrics.mrr_at_10 = std::stod(require_scalar(parsed, prefix + "baseline_mrr_at_10"));
    gate.baseline_metrics.ndcg_at_10 = std::stod(require_scalar(parsed, prefix + "baseline_ndcg_at_10"));
    gate.baseline_metrics.recall_at_5 = std::stod(require_scalar(parsed, prefix + "baseline_recall_at_5"));
    gate.baseline_metrics.recall_at_10 = std::stod(require_scalar(parsed, prefix + "baseline_recall_at_10"));
    gate.min_cases = std::stoi(require_scalar(parsed, prefix + "min_cases"));
    gate.min_hard_fail_cases = std::stoi(require_scalar(parsed, prefix + "min_hard_fail_cases"));
    gate.min_architecture_reference_cases =
      std::stoi(require_scalar(parsed, prefix + "min_architecture_reference_cases"));
    gate.min_adr_normative_cases =
      std::stoi(require_scalar(parsed, prefix + "min_adr_normative_cases"));
    gate.min_ssot_normative_cases =
      std::stoi(require_scalar(parsed, prefix + "min_ssot_normative_cases"));
}

[[nodiscard]] int parse_int(const std::string& value) {
  return std::stoi(value);
}

[[nodiscard]] double parse_double(const std::string& value) {
  return std::stod(value);
}

[[nodiscard]] bool parse_bool(const std::string& value) {
  if (value == "true") {
    return true;
  }
  if (value == "false") {
    return false;
  }

  throw std::runtime_error("retrieval quality manifest invalid boolean: " + value);
}

[[nodiscard]] KnowledgeQueryKind parse_query_kind(const std::string& value) {
  if (value == "FactLookup") {
    return KnowledgeQueryKind::FactLookup;
  }
  if (value == "ProcedureLookup") {
    return KnowledgeQueryKind::ProcedureLookup;
  }
  if (value == "DiagnosticContext") {
    return KnowledgeQueryKind::DiagnosticContext;
  }
  if (value == "PolicyEvidence") {
    return KnowledgeQueryKind::PolicyEvidence;
  }

  throw std::runtime_error("retrieval quality manifest invalid query kind: " + value);
}

[[nodiscard]] RetrievalMode parse_retrieval_mode(const std::string& value) {
  if (value == "LexicalOnly") {
    return RetrievalMode::LexicalOnly;
  }
  if (value == "DenseOnly") {
    return RetrievalMode::DenseOnly;
  }
  if (value == "Hybrid") {
    return RetrievalMode::Hybrid;
  }

  throw std::runtime_error("retrieval quality manifest invalid retrieval mode: " + value);
}

[[nodiscard]] std::vector<std::string> collect_case_ids(const ParsedYamlDocument& parsed) {
  std::set<std::string> case_ids;

  const auto collect_from_key = [&case_ids](const std::string& key) {
    constexpr std::string_view prefix = "cases.";
    if (!key.starts_with(prefix)) {
      return;
    }

    const std::string_view remainder = std::string_view(key).substr(prefix.size());
    const auto separator = remainder.find('.');
    if (separator == std::string_view::npos) {
      return;
    }

    case_ids.insert(std::string(remainder.substr(0U, separator)));
  };

  for (const auto& [key, value] : parsed.scalar_values) {
    static_cast<void>(value);
    collect_from_key(key);
  }

  for (const auto& [key, value] : parsed.list_values) {
    static_cast<void>(value);
    collect_from_key(key);
  }

  return std::vector<std::string>(case_ids.begin(), case_ids.end());
}

[[nodiscard]] QualityManifest load_quality_manifest() {
  const auto parsed = parse_yaml_file(manifest_path());
  if (!parsed.ok) {
    throw std::runtime_error(parsed.error);
  }

  QualityManifest manifest;
  manifest.format_version = parse_int(require_scalar(parsed, "format_version"));
  manifest.dataset_id = require_scalar(parsed, "dataset_id");
  manifest.retrieval_top_k = static_cast<std::size_t>(parse_int(require_scalar(parsed, "retrieval_top_k")));

  manifest.thresholds.min_mrr_at_10 = parse_double(require_scalar(parsed, "aggregate_thresholds.min_mrr_at_10"));
  manifest.thresholds.min_ndcg_at_10 = parse_double(require_scalar(parsed, "aggregate_thresholds.min_ndcg_at_10"));
  manifest.thresholds.min_recall_at_5 = parse_double(require_scalar(parsed, "aggregate_thresholds.min_recall_at_5"));
  manifest.thresholds.min_recall_at_10 = parse_double(require_scalar(parsed, "aggregate_thresholds.min_recall_at_10"));
  manifest.thresholds.max_regression_pct = parse_double(require_scalar(parsed, "aggregate_thresholds.max_regression_pct"));

  manifest.baseline_metrics.mrr_at_10 = parse_double(require_scalar(parsed, "baseline_metrics.mrr_at_10"));
  manifest.baseline_metrics.ndcg_at_10 = parse_double(require_scalar(parsed, "baseline_metrics.ndcg_at_10"));
  manifest.baseline_metrics.recall_at_5 = parse_double(require_scalar(parsed, "baseline_metrics.recall_at_5"));
  manifest.baseline_metrics.recall_at_10 = parse_double(require_scalar(parsed, "baseline_metrics.recall_at_10"));

  load_optional_mode_gate(parsed, RetrievalMode::Hybrid, manifest);
  load_optional_mode_gate(parsed, RetrievalMode::DenseOnly, manifest);

  manifest.coverage.min_total_cases = parse_int(require_scalar(parsed, "coverage.min_total_cases"));
  manifest.coverage.min_fact_lookup_cases = parse_int(require_scalar(parsed, "coverage.min_fact_lookup_cases"));
  manifest.coverage.min_procedure_lookup_cases = parse_int(require_scalar(parsed, "coverage.min_procedure_lookup_cases"));
  manifest.coverage.min_diagnostic_context_cases = parse_int(require_scalar(parsed, "coverage.min_diagnostic_context_cases"));
  manifest.coverage.min_hard_fail_cases = parse_int(require_scalar(parsed, "coverage.min_hard_fail_cases"));
  manifest.coverage.min_architecture_reference_cases = parse_int(require_scalar(parsed, "coverage.min_architecture_reference_cases"));
  manifest.coverage.min_adr_normative_cases = parse_int(require_scalar(parsed, "coverage.min_adr_normative_cases"));
  manifest.coverage.min_ssot_normative_cases = parse_int(require_scalar(parsed, "coverage.min_ssot_normative_cases"));
  manifest.coverage.min_profile_policy_normative_cases = parse_int(require_scalar(parsed, "coverage.min_profile_policy_normative_cases"));

  manifest.context_metrics_enabled = parse_bool(require_scalar(parsed, "context_metric_slots.enabled"));
  manifest.context_metric_fields = require_list(parsed, "context_metric_slots.fields");

  for (const auto& case_id : collect_case_ids(parsed)) {
    const std::string prefix = "cases." + case_id + ".";
    QualityCaseSpec spec;
    spec.case_id = case_id;
    spec.query_kind = parse_query_kind(require_scalar(parsed, prefix + "query_kind"));
    spec.query_text = require_scalar(parsed, prefix + "query_text");
    spec.primary_corpus_id = require_scalar(parsed, prefix + "primary_corpus_id");
    for (const auto& mode : require_list(parsed, prefix + "allowed_modes")) {
      spec.allowed_modes.push_back(parse_retrieval_mode(mode));
    }
    spec.expected_source_uris = require_list(parsed, prefix + "expected_source_uris");
    spec.expected_chunk_refs = require_list(parsed, prefix + "expected_chunk_refs");
    spec.required_top_k = parse_int(require_scalar(parsed, prefix + "required_top_k"));
    spec.hard_fail = parse_bool(require_scalar(parsed, prefix + "hard_fail"));
    spec.min_recall_at_5 = parse_double(require_scalar(parsed, prefix + "min_recall_at_5"));
    spec.min_mrr_at_10 = parse_double(require_scalar(parsed, prefix + "min_mrr_at_10"));
    manifest.cases.push_back(std::move(spec));
  }

  if (manifest.format_version != 1) {
    throw std::runtime_error("retrieval quality manifest unsupported format version");
  }
  if (manifest.dataset_id.empty() || manifest.retrieval_top_k == 0U || manifest.cases.empty()) {
    throw std::runtime_error("retrieval quality manifest missing required dataset fields");
  }

  return manifest;
}

[[nodiscard]] bool same_within_tolerance(double expected, double actual, double tolerance = 1e-6) {
  return std::fabs(expected - actual) <= tolerance;
}

void assert_metric_equal(double expected, double actual, const std::string& message) {
  if (!same_within_tolerance(expected, actual)) {
    throw std::runtime_error(message + ": expected=" + std::to_string(expected) +
                             " actual=" + std::to_string(actual));
  }
}

[[nodiscard]] bool mode_allowed(RetrievalMode active_mode,
                                const std::vector<RetrievalMode>& allowed_modes) {
  return std::find(allowed_modes.begin(), allowed_modes.end(), active_mode) !=
         allowed_modes.end();
}

[[nodiscard]] std::string extract_source_uri(const std::string& citation_ref) {
  const auto separator = citation_ref.find('#');
  if (separator == std::string::npos) {
    return citation_ref;
  }

  return citation_ref.substr(0U, separator);
}

[[nodiscard]] std::vector<std::string> dedupe_preserve_order(
    const std::vector<std::string>& values) {
  std::vector<std::string> deduped;
  for (const auto& value : values) {
    if (!value.empty() && std::find(deduped.begin(), deduped.end(), value) == deduped.end()) {
      deduped.push_back(value);
    }
  }

  return deduped;
}

[[nodiscard]] double compute_recall_at_k(const std::vector<std::string>& expected,
                                         const std::vector<std::string>& retrieved,
                                         std::size_t k) {
  if (expected.empty()) {
    return 0.0;
  }

  std::size_t hit_count = 0U;
  const auto limit = std::min(k, retrieved.size());
  for (const auto& expected_source : expected) {
    if (std::find(retrieved.begin(), retrieved.begin() + static_cast<std::ptrdiff_t>(limit),
                  expected_source) != retrieved.begin() + static_cast<std::ptrdiff_t>(limit)) {
      ++hit_count;
    }
  }

  return static_cast<double>(hit_count) / static_cast<double>(expected.size());
}

[[nodiscard]] double compute_mrr_at_k(const std::vector<std::string>& expected,
                                      const std::vector<std::string>& retrieved,
                                      std::size_t k) {
  const auto limit = std::min(k, retrieved.size());
  for (std::size_t index = 0U; index < limit; ++index) {
    if (std::find(expected.begin(), expected.end(), retrieved[index]) != expected.end()) {
      return 1.0 / static_cast<double>(index + 1U);
    }
  }

  return 0.0;
}

[[nodiscard]] double relevance_gain_for_index(std::size_t index) {
  if (index == 0U) {
    return 3.0;
  }
  if (index == 1U) {
    return 2.0;
  }

  return 1.0;
}

[[nodiscard]] double compute_ndcg_at_k(const std::vector<std::string>& expected,
                                       const std::vector<std::string>& retrieved,
                                       std::size_t k) {
  if (expected.empty()) {
    return 0.0;
  }

  std::map<std::string, double> gains;
  for (std::size_t index = 0U; index < expected.size(); ++index) {
    gains.emplace(expected[index], relevance_gain_for_index(index));
  }

  double dcg = 0.0;
  const auto dcg_limit = std::min(k, retrieved.size());
  for (std::size_t index = 0U; index < dcg_limit; ++index) {
    const auto iterator = gains.find(retrieved[index]);
    if (iterator == gains.end()) {
      continue;
    }

    dcg += iterator->second / std::log2(static_cast<double>(index + 2U));
  }

  double idcg = 0.0;
  const auto ideal_limit = std::min(k, expected.size());
  for (std::size_t index = 0U; index < ideal_limit; ++index) {
    idcg += relevance_gain_for_index(index) / std::log2(static_cast<double>(index + 2U));
  }

  if (idcg <= 0.0) {
    return 0.0;
  }

  return dcg / idcg;
}

[[nodiscard]] QualityMetricBundle compute_case_metrics(const QualityCaseSpec& spec,
                                                       const std::vector<std::string>& retrieved) {
  const auto deduped_retrieved = dedupe_preserve_order(retrieved);
  return QualityMetricBundle{
      .mrr_at_10 = compute_mrr_at_k(spec.expected_source_uris, deduped_retrieved, 10U),
      .ndcg_at_10 = compute_ndcg_at_k(spec.expected_source_uris, deduped_retrieved, 10U),
      .recall_at_5 = compute_recall_at_k(spec.expected_source_uris, deduped_retrieved, 5U),
      .recall_at_10 = compute_recall_at_k(spec.expected_source_uris, deduped_retrieved, 10U),
  };
}

[[nodiscard]] double effective_threshold(double absolute_threshold,
                                         double baseline_metric,
                                         double max_regression_pct) {
  const auto relative_threshold = baseline_metric * (1.0 - max_regression_pct / 100.0);
  return std::max(absolute_threshold, relative_threshold);
}

template <typename RetrievalRunner>
[[nodiscard]] QualityEvaluationReport evaluate_manifest(const QualityManifest& manifest,
                                                        RetrievalMode active_mode,
                                                        RetrievalRunner&& runner) {
  QualityEvaluationReport report;
  int fact_lookup_cases = 0;
  int procedure_lookup_cases = 0;
  int diagnostic_context_cases = 0;
  int hard_fail_cases = 0;
  int architecture_cases = 0;
  int adr_cases = 0;
  int ssot_cases = 0;
  int profile_cases = 0;
  int hard_fail_fact_cases = 0;
  int hard_fail_procedure_cases = 0;
  int hard_fail_diagnostic_cases = 0;

  double mrr_sum = 0.0;
  double ndcg_sum = 0.0;
  double recall_at_5_sum = 0.0;
  double recall_at_10_sum = 0.0;

  for (const auto& spec : manifest.cases) {
    if (!mode_allowed(active_mode, spec.allowed_modes)) {
      ++report.skipped_cases;
      continue;
    }

    const auto retrieved_sources = dedupe_preserve_order(runner(spec));
    const auto metrics = compute_case_metrics(spec, retrieved_sources);
    const auto first_expected = spec.expected_source_uris.front();
    const auto hard_fail_limit = std::min<std::size_t>(static_cast<std::size_t>(spec.required_top_k),
                                                       retrieved_sources.size());
    const bool hard_fail_satisfied =
        std::find(retrieved_sources.begin(),
                  retrieved_sources.begin() + static_cast<std::ptrdiff_t>(hard_fail_limit),
                  first_expected) !=
        retrieved_sources.begin() + static_cast<std::ptrdiff_t>(hard_fail_limit);

    if (spec.hard_fail && !hard_fail_satisfied) {
      report.failures.push_back("hard_fail miss: " + spec.case_id);
    }
    if (metrics.recall_at_5 + 1e-6 < spec.min_recall_at_5) {
      report.failures.push_back("recall@5 below case floor: " + spec.case_id);
    }
    if (metrics.mrr_at_10 + 1e-6 < spec.min_mrr_at_10) {
      report.failures.push_back("mrr@10 below case floor: " + spec.case_id);
    }

    ++report.executed_cases;
    mrr_sum += metrics.mrr_at_10;
    ndcg_sum += metrics.ndcg_at_10;
    recall_at_5_sum += metrics.recall_at_5;
    recall_at_10_sum += metrics.recall_at_10;

    switch (spec.query_kind) {
      case KnowledgeQueryKind::FactLookup:
        ++fact_lookup_cases;
        if (spec.hard_fail) {
          ++hard_fail_fact_cases;
        }
        break;
      case KnowledgeQueryKind::ProcedureLookup:
        ++procedure_lookup_cases;
        if (spec.hard_fail) {
          ++hard_fail_procedure_cases;
        }
        break;
      case KnowledgeQueryKind::DiagnosticContext:
        ++diagnostic_context_cases;
        if (spec.hard_fail) {
          ++hard_fail_diagnostic_cases;
        }
        break;
      case KnowledgeQueryKind::PolicyEvidence:
      case KnowledgeQueryKind::MultiHop:
        break;
    }

    if (spec.hard_fail) {
      ++hard_fail_cases;
    }

    if (spec.primary_corpus_id == "architecture_reference") {
      ++architecture_cases;
    } else if (spec.primary_corpus_id == "adr_normative") {
      ++adr_cases;
    } else if (spec.primary_corpus_id == "ssot_normative") {
      ++ssot_cases;
    } else if (spec.primary_corpus_id == "profile_policy_normative") {
      ++profile_cases;
    }
  }

  if (report.executed_cases == 0) {
    report.failures.push_back("no executable regression cases for active mode");
    return report;
  }

  report.aggregate_metrics = QualityMetricBundle{
      .mrr_at_10 = mrr_sum / static_cast<double>(report.executed_cases),
      .ndcg_at_10 = ndcg_sum / static_cast<double>(report.executed_cases),
      .recall_at_5 = recall_at_5_sum / static_cast<double>(report.executed_cases),
      .recall_at_10 = recall_at_10_sum / static_cast<double>(report.executed_cases),
  };

  if (report.executed_cases < manifest.coverage.min_total_cases) {
    report.failures.push_back("coverage miss: total cases");
  }
  if (fact_lookup_cases < manifest.coverage.min_fact_lookup_cases) {
    report.failures.push_back("coverage miss: fact lookup cases");
  }
  if (procedure_lookup_cases < manifest.coverage.min_procedure_lookup_cases) {
    report.failures.push_back("coverage miss: procedure lookup cases");
  }
  if (diagnostic_context_cases < manifest.coverage.min_diagnostic_context_cases) {
    report.failures.push_back("coverage miss: diagnostic context cases");
  }
  if (hard_fail_cases < manifest.coverage.min_hard_fail_cases) {
    report.failures.push_back("coverage miss: hard fail cases");
  }
  if (architecture_cases < manifest.coverage.min_architecture_reference_cases) {
    report.failures.push_back("coverage miss: architecture cases");
  }
  if (adr_cases < manifest.coverage.min_adr_normative_cases) {
    report.failures.push_back("coverage miss: adr cases");
  }
  if (ssot_cases < manifest.coverage.min_ssot_normative_cases) {
    report.failures.push_back("coverage miss: ssot cases");
  }
  if (profile_cases < manifest.coverage.min_profile_policy_normative_cases) {
    report.failures.push_back("coverage miss: profile policy cases");
  }
  if (hard_fail_fact_cases < 2) {
    report.failures.push_back("coverage miss: hard fail fact lookup distribution");
  }
  if (hard_fail_procedure_cases < 2) {
    report.failures.push_back("coverage miss: hard fail procedure distribution");
  }
  if (hard_fail_diagnostic_cases < 2) {
    report.failures.push_back("coverage miss: hard fail diagnostic distribution");
  }

  const auto mrr_threshold = effective_threshold(manifest.thresholds.min_mrr_at_10,
                                                 manifest.baseline_metrics.mrr_at_10,
                                                 manifest.thresholds.max_regression_pct);
  const auto ndcg_threshold = effective_threshold(manifest.thresholds.min_ndcg_at_10,
                                                  manifest.baseline_metrics.ndcg_at_10,
                                                  manifest.thresholds.max_regression_pct);
  const auto recall_at_5_threshold = effective_threshold(manifest.thresholds.min_recall_at_5,
                                                         manifest.baseline_metrics.recall_at_5,
                                                         manifest.thresholds.max_regression_pct);
  const auto recall_at_10_threshold = effective_threshold(manifest.thresholds.min_recall_at_10,
                                                          manifest.baseline_metrics.recall_at_10,
                                                          manifest.thresholds.max_regression_pct);

  if (report.aggregate_metrics.mrr_at_10 + 1e-6 < mrr_threshold) {
    report.failures.push_back("aggregate miss: mrr@10");
  }
  if (report.aggregate_metrics.ndcg_at_10 + 1e-6 < ndcg_threshold) {
    report.failures.push_back("aggregate miss: ndcg@10");
  }
  if (report.aggregate_metrics.recall_at_5 + 1e-6 < recall_at_5_threshold) {
    report.failures.push_back("aggregate miss: recall@5");
  }
  if (report.aggregate_metrics.recall_at_10 + 1e-6 < recall_at_10_threshold) {
    report.failures.push_back("aggregate miss: recall@10");
  }

  report.passed = report.failures.empty();
  return report;
}

template <typename RetrievalRunner>
[[nodiscard]] QualityEvaluationReport evaluate_mode_gate(const QualityManifest& manifest,
                                                         RetrievalMode active_mode,
                                                         RetrievalRunner&& runner) {
  QualityEvaluationReport report;
  const auto gate_iterator = manifest.mode_gates.find(active_mode);
  if (gate_iterator == manifest.mode_gates.end() || !gate_iterator->second.enabled) {
    report.failures.push_back("mode gate missing: " + retrieval_mode_name(active_mode));
    return report;
  }

  const auto& gate = gate_iterator->second;
  int hard_fail_cases = 0;
  int architecture_cases = 0;
  int adr_cases = 0;
  int ssot_cases = 0;

  double mrr_sum = 0.0;
  double ndcg_sum = 0.0;
  double recall_at_5_sum = 0.0;
  double recall_at_10_sum = 0.0;

  for (const auto& spec : manifest.cases) {
    if (!mode_allowed(active_mode, spec.allowed_modes)) {
      ++report.skipped_cases;
      continue;
    }

    const auto retrieved_sources = dedupe_preserve_order(runner(spec));
    const auto metrics = compute_case_metrics(spec, retrieved_sources);
    const auto first_expected = spec.expected_source_uris.front();
    const auto hard_fail_limit = std::min<std::size_t>(static_cast<std::size_t>(spec.required_top_k),
                                                       retrieved_sources.size());
    const bool hard_fail_satisfied =
        std::find(retrieved_sources.begin(),
                  retrieved_sources.begin() + static_cast<std::ptrdiff_t>(hard_fail_limit),
                  first_expected) !=
        retrieved_sources.begin() + static_cast<std::ptrdiff_t>(hard_fail_limit);

    if (spec.hard_fail && !hard_fail_satisfied) {
      report.failures.push_back("hard_fail miss: " + spec.case_id);
    }
    if (metrics.recall_at_5 + 1e-6 < spec.min_recall_at_5) {
      report.failures.push_back("recall@5 below case floor: " + spec.case_id);
    }
    if (metrics.mrr_at_10 + 1e-6 < spec.min_mrr_at_10) {
      report.failures.push_back("mrr@10 below case floor: " + spec.case_id);
    }

    ++report.executed_cases;
    mrr_sum += metrics.mrr_at_10;
    ndcg_sum += metrics.ndcg_at_10;
    recall_at_5_sum += metrics.recall_at_5;
    recall_at_10_sum += metrics.recall_at_10;

    if (spec.hard_fail) {
      ++hard_fail_cases;
    }
    if (spec.primary_corpus_id == "architecture_reference") {
      ++architecture_cases;
    } else if (spec.primary_corpus_id == "adr_normative") {
      ++adr_cases;
    } else if (spec.primary_corpus_id == "ssot_normative") {
      ++ssot_cases;
    }
  }

  if (report.executed_cases == 0) {
    report.failures.push_back("no executable regression cases for active mode");
    return report;
  }

  report.aggregate_metrics = QualityMetricBundle{
      .mrr_at_10 = mrr_sum / static_cast<double>(report.executed_cases),
      .ndcg_at_10 = ndcg_sum / static_cast<double>(report.executed_cases),
      .recall_at_5 = recall_at_5_sum / static_cast<double>(report.executed_cases),
      .recall_at_10 = recall_at_10_sum / static_cast<double>(report.executed_cases),
  };

  if (report.executed_cases < gate.min_cases) {
    report.failures.push_back("coverage miss: mode cases");
  }
  if (hard_fail_cases < gate.min_hard_fail_cases) {
    report.failures.push_back("coverage miss: mode hard fail cases");
  }
  if (architecture_cases < gate.min_architecture_reference_cases) {
    report.failures.push_back("coverage miss: mode architecture cases");
  }
  if (adr_cases < gate.min_adr_normative_cases) {
    report.failures.push_back("coverage miss: mode adr cases");
  }
  if (ssot_cases < gate.min_ssot_normative_cases) {
    report.failures.push_back("coverage miss: mode ssot cases");
  }

  const auto mrr_threshold = effective_threshold(gate.thresholds.min_mrr_at_10,
                                                 gate.baseline_metrics.mrr_at_10,
                                                 gate.thresholds.max_regression_pct);
  const auto ndcg_threshold = effective_threshold(gate.thresholds.min_ndcg_at_10,
                                                  gate.baseline_metrics.ndcg_at_10,
                                                  gate.thresholds.max_regression_pct);
  const auto recall_at_5_threshold = effective_threshold(gate.thresholds.min_recall_at_5,
                                                         gate.baseline_metrics.recall_at_5,
                                                         gate.thresholds.max_regression_pct);
  const auto recall_at_10_threshold = effective_threshold(gate.thresholds.min_recall_at_10,
                                                          gate.baseline_metrics.recall_at_10,
                                                          gate.thresholds.max_regression_pct);

  if (report.aggregate_metrics.mrr_at_10 + 1e-6 < mrr_threshold) {
    report.failures.push_back("aggregate miss: mrr@10");
  }
  if (report.aggregate_metrics.ndcg_at_10 + 1e-6 < ndcg_threshold) {
    report.failures.push_back("aggregate miss: ndcg@10");
  }
  if (report.aggregate_metrics.recall_at_5 + 1e-6 < recall_at_5_threshold) {
    report.failures.push_back("aggregate miss: recall@5");
  }
  if (report.aggregate_metrics.recall_at_10 + 1e-6 < recall_at_10_threshold) {
    report.failures.push_back("aggregate miss: recall@10");
  }

  report.passed = report.failures.empty();
  return report;
}

[[nodiscard]] int count_cases_for_mode(const QualityManifest& manifest, RetrievalMode active_mode) {
  return static_cast<int>(std::count_if(manifest.cases.begin(),
                                        manifest.cases.end(),
                                        [active_mode](const QualityCaseSpec& spec) {
                                          return mode_allowed(active_mode, spec.allowed_modes);
                                        }));
}

void execute_sql(sqlite3* connection, const std::string& sql) {
  char* error_message = nullptr;
  const int sqlite_status =
      sqlite3_exec(connection, sql.c_str(), nullptr, nullptr, &error_message);
  if (sqlite_status != SQLITE_OK) {
    const std::string message =
        error_message != nullptr ? error_message : "failed to execute sqlite statement";
    sqlite3_free(error_message);
    throw std::runtime_error(message);
  }

  sqlite3_free(error_message);
}

[[nodiscard]] std::string encode_tags(const std::vector<std::string>& tags) {
  std::string encoded = "|";
  for (const auto& tag : tags) {
    encoded += tag;
    encoded.push_back('|');
  }
  return encoded;
}

[[nodiscard]] std::vector<std::string> decode_tags(const std::string& encoded_tags) {
  std::vector<std::string> tags;
  std::string current_tag;
  for (const char character : encoded_tags) {
    if (character == '|') {
      if (!current_tag.empty()) {
        tags.push_back(current_tag);
        current_tag.clear();
      }
      continue;
    }

    current_tag.push_back(character);
  }

  if (!current_tag.empty()) {
    tags.push_back(std::move(current_tag));
  }

  return tags;
}

class SqliteLexicalIndexFixture {
 public:
  SqliteLexicalIndexFixture() {
    if (sqlite3_open(":memory:", &connection_) != SQLITE_OK) {
      throw std::runtime_error("failed to open sqlite in-memory database");
    }

    execute_sql(connection_,
                "CREATE VIRTUAL TABLE chunks_fts USING fts5("
                "corpus_id UNINDEXED,"
                "document_id UNINDEXED,"
                "chunk_id UNINDEXED,"
                "chunk_text,"
                "citation_ref UNINDEXED,"
                "updated_at UNINDEXED,"
                "authority_level UNINDEXED,"
                "language UNINDEXED,"
                "tags UNINDEXED,"
                "tokenize='porter unicode61 remove_diacritics 1'"
                ");");
  }

  ~SqliteLexicalIndexFixture() {
    if (connection_ != nullptr) {
      sqlite3_close(connection_);
    }
  }

  void insert_row(std::string corpus_id,
                  std::string document_id,
                  std::string chunk_id,
                  std::string chunk_text,
                  std::string citation_ref,
                  std::int64_t updated_at,
                  AuthorityLevel authority_level,
                  std::string language,
                  std::vector<std::string> tags) {
    constexpr auto insert_sql =
        "INSERT INTO chunks_fts("
        "corpus_id, document_id, chunk_id, chunk_text, citation_ref, updated_at, authority_level, language, tags"
        ") VALUES(?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9);";

    sqlite3_stmt* statement = nullptr;
    if (sqlite3_prepare_v2(connection_, insert_sql, -1, &statement, nullptr) != SQLITE_OK) {
      throw std::runtime_error("failed to prepare lexical fixture insert");
    }

    sqlite3_bind_text(statement, 1, corpus_id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(statement, 2, document_id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(statement, 3, chunk_id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(statement, 4, chunk_text.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(statement, 5, citation_ref.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(statement, 6, updated_at);
    sqlite3_bind_int(statement, 7, static_cast<int>(authority_level));
    sqlite3_bind_text(statement, 8, language.c_str(), -1, SQLITE_TRANSIENT);
    const auto encoded_tags = encode_tags(tags);
    sqlite3_bind_text(statement, 9, encoded_tags.c_str(), -1, SQLITE_TRANSIENT);

    if (sqlite3_step(statement) != SQLITE_DONE) {
      sqlite3_finalize(statement);
      throw std::runtime_error("failed to insert lexical fixture row");
    }

    sqlite3_finalize(statement);
  }

  [[nodiscard]] SparseIndexSearchResult search(const SparseIndexSearchRequest& request) const {
    std::string sql =
        "SELECT corpus_id, document_id, chunk_id, chunk_text, citation_ref, updated_at, authority_level, language, tags, bm25(chunks_fts) "
        "FROM chunks_fts WHERE chunks_fts MATCH ?1";

    int bind_index = 2;
    if (!request.allowed_corpus_ids.empty()) {
      sql += " AND corpus_id IN (";
      for (std::size_t index = 0U; index < request.allowed_corpus_ids.size(); ++index) {
        if (index > 0U) {
          sql += ", ";
        }
        sql += "?" + std::to_string(bind_index++);
      }
      sql += ")";
    }

    sql += " AND CAST(authority_level AS INTEGER) <= ?" + std::to_string(bind_index++);

    if (request.required_language.has_value()) {
      sql += " AND language = ?" + std::to_string(bind_index++);
    }

    for (std::size_t index = 0U; index < request.required_tags.size(); ++index) {
      sql += " AND instr(tags, ?" + std::to_string(bind_index++) + ") > 0";
    }

    sql += " ORDER BY bm25(chunks_fts) LIMIT ?" + std::to_string(bind_index++);

    sqlite3_stmt* statement = nullptr;
    if (sqlite3_prepare_v2(connection_, sql.c_str(), -1, &statement, nullptr) != SQLITE_OK) {
      SparseIndexSearchResult result;
      result.ok = false;
      result.error = dasall::knowledge::make_knowledge_error_info(
          dasall::knowledge::KnowledgeErrorCode::IndexUnavailable,
          "failed to prepare retrieval quality search statement",
          "retrieval_quality_regression.search");
      return result;
    }

    int parameter_index = 1;
    sqlite3_bind_text(statement,
                      parameter_index++,
                      request.expression.match_expression.c_str(),
                      -1,
                      SQLITE_TRANSIENT);

    for (const auto& corpus_id : request.allowed_corpus_ids) {
      sqlite3_bind_text(statement, parameter_index++, corpus_id.c_str(), -1, SQLITE_TRANSIENT);
    }

    sqlite3_bind_int(statement,
                     parameter_index++,
                     static_cast<int>(request.minimum_authority_level));

    if (request.required_language.has_value()) {
      sqlite3_bind_text(statement,
                        parameter_index++,
                        request.required_language->c_str(),
                        -1,
                        SQLITE_TRANSIENT);
    }

    for (const auto& required_tag : request.required_tags) {
      const auto encoded_tag = "|" + required_tag + "|";
      sqlite3_bind_text(statement,
                        parameter_index++,
                        encoded_tag.c_str(),
                        -1,
                        SQLITE_TRANSIENT);
    }

    sqlite3_bind_int(statement, parameter_index++, static_cast<int>(request.top_k));

    SparseIndexSearchResult result;
    result.ok = true;
    while (sqlite3_step(statement) == SQLITE_ROW) {
      const auto score = std::max(0.0F, static_cast<float>(-sqlite3_column_double(statement, 9)));
      const auto* language_text =
          reinterpret_cast<const char*>(sqlite3_column_text(statement, 7));
      const auto* tags_text = reinterpret_cast<const char*>(sqlite3_column_text(statement, 8));
      result.rows.push_back(SparseSearchRow{
          .corpus_id = reinterpret_cast<const char*>(sqlite3_column_text(statement, 0)),
          .document_id = reinterpret_cast<const char*>(sqlite3_column_text(statement, 1)),
          .chunk_id = reinterpret_cast<const char*>(sqlite3_column_text(statement, 2)),
          .score = score,
          .chunk_text = reinterpret_cast<const char*>(sqlite3_column_text(statement, 3)),
          .citation_ref = reinterpret_cast<const char*>(sqlite3_column_text(statement, 4)),
          .updated_at = sqlite3_column_int64(statement, 5),
          .authority_level = static_cast<AuthorityLevel>(sqlite3_column_int(statement, 6)),
          .language = language_text != nullptr ? std::optional<std::string>(language_text)
                                               : std::nullopt,
          .tags = tags_text != nullptr ? decode_tags(tags_text) : std::vector<std::string>{},
      });
    }

    sqlite3_finalize(statement);
    return result;
  }

 private:
  sqlite3* connection_ = nullptr;
};

[[nodiscard]] QueryNormalizePolicy make_normalize_policy() {
  QueryNormalizePolicy policy;
  policy.max_query_text_bytes = 512U;
  policy.max_lexical_terms = 16U;
  policy.max_top_k = 10U;
  policy.max_context_projection_items = 10U;
  policy.allowed_corpora = {
      "architecture_reference",
      "adr_normative",
      "ssot_normative",
      "profile_policy_normative",
  };
  return policy;
}

[[nodiscard]] KnowledgeConfigSnapshot make_config(RetrievalMode active_mode) {
  KnowledgeConfigSnapshot config;
  config.knowledge_enabled = true;
  config.vector_enabled = active_mode != RetrievalMode::LexicalOnly;
  config.retrieval_mode_default = active_mode;
  config.evidence_budget_tokens = 4096U;
  config.max_context_projection_items = 10U;
  config.catalog_refresh_interval_ms = 30000;
  config.catalog_expire_after_ms = 120000;
  config.allow_stale_read = false;
  config.failure_backoff_ms = 1000;
  config.request_deadline_ms = 1500;
  config.allow_budget_degrade = false;
  config.max_parallel_recall = active_mode == RetrievalMode::Hybrid ? 2U : 1U;
  config.sparse_recall_timeout_ms = 450;
  config.dense_recall_timeout_ms = 450;
  config.ingest_timeout_ms = 2000;
  return config;
}

[[nodiscard]] std::string corpus_source_root(const std::string& corpus_id) {
  if (corpus_id == "architecture_reference") {
    return "docs/architecture/";
  }
  if (corpus_id == "adr_normative") {
    return "docs/adr/";
  }
  if (corpus_id == "ssot_normative") {
    return "docs/ssot/";
  }
  if (corpus_id == "profile_policy_normative") {
    return "profiles/quality/";
  }

  throw std::runtime_error("unknown corpus id in retrieval quality harness: " + corpus_id);
}

[[nodiscard]] std::string corpus_display_name(const std::string& corpus_id) {
  if (corpus_id == "architecture_reference") {
    return "Architecture Reference";
  }
  if (corpus_id == "adr_normative") {
    return "ADR Normative";
  }
  if (corpus_id == "ssot_normative") {
    return "SSOT Normative";
  }
  if (corpus_id == "profile_policy_normative") {
    return "Profile Policy Normative";
  }

  throw std::runtime_error("unknown corpus display name id: " + corpus_id);
}

[[nodiscard]] AuthorityLevel corpus_authority_level(const std::string& corpus_id) {
  return corpus_id == "architecture_reference" ? AuthorityLevel::Reference
                                                 : AuthorityLevel::Normative;
}

[[nodiscard]] SourceKind corpus_source_kind(const std::string& corpus_id) {
  return corpus_id == "profile_policy_normative" ? SourceKind::ConfigSnapshot
                                                   : SourceKind::File;
}

[[nodiscard]] std::vector<SourceFormat> corpus_formats(const std::string& corpus_id) {
  return corpus_id == "profile_policy_normative" ? std::vector<SourceFormat>{SourceFormat::Yaml}
                                                   : std::vector<SourceFormat>{SourceFormat::Markdown};
}

[[nodiscard]] std::vector<std::string> corpus_tags(const std::string& corpus_id) {
  if (corpus_id == "architecture_reference") {
    return {"architecture", "quality"};
  }
  if (corpus_id == "adr_normative") {
    return {"adr", "quality"};
  }
  if (corpus_id == "ssot_normative") {
    return {"ssot", "quality"};
  }
  if (corpus_id == "profile_policy_normative") {
    return {"profile", "quality"};
  }

  throw std::runtime_error("unknown corpus tags id: " + corpus_id);
}

[[nodiscard]] CorpusDescriptor make_descriptor(const std::string& corpus_id) {
  CorpusDescriptor descriptor;
  descriptor.corpus_id = corpus_id;
  descriptor.display_name = corpus_display_name(corpus_id);
  descriptor.source_uri = corpus_source_root(corpus_id);
  descriptor.trust_level = TrustLevel::Trusted;
  descriptor.authority_level = corpus_authority_level(corpus_id);
  descriptor.source_kind = corpus_source_kind(corpus_id);
  descriptor.allowed_formats = corpus_formats(corpus_id);
  descriptor.include_globs = descriptor.source_kind == SourceKind::ConfigSnapshot
                                 ? std::vector<std::string>{"*.yaml"}
                                 : std::vector<std::string>{"*.md"};
  descriptor.exclude_globs = {};
  descriptor.supported_modes = corpus_id == "profile_policy_normative"
                                 ? std::vector<RetrievalMode>{RetrievalMode::LexicalOnly}
                                 : std::vector<RetrievalMode>{RetrievalMode::LexicalOnly,
                                                              RetrievalMode::Hybrid,
                                                              RetrievalMode::DenseOnly};
  descriptor.active_snapshot_id = "snapshot-retrieval-quality-v1";
  descriptor.last_updated_ms = 1713744000000;
  descriptor.tags = corpus_tags(corpus_id);
  descriptor.metadata = {
      {"baseline_class", "knowledge_quality"},
      {"owner_module", "knowledge"},
      {"refresh_strategy", "manual"},
      {"default_language", "zh-CN"},
  };
  return descriptor;
}

[[nodiscard]] std::string build_chunk_text(const QualityCaseSpec& spec) {
  return spec.query_text +
         " curated regression evidence keeps retrieval quality anchored to " + spec.case_id +
         " within corpus " + spec.primary_corpus_id;
}

class RetrievalQualityHarness {
 public:
  RetrievalQualityHarness(const QualityManifest& manifest, RetrievalMode active_mode)
      : manifest_(manifest),
        active_mode_(active_mode),
        sparse_retriever_(nullptr) {
    populate_sqlite_fixture();

    auto corpus_catalog = std::make_unique<CorpusCatalog>();
    std::vector<CorpusDescriptor> descriptors = {
        make_descriptor("architecture_reference"),
        make_descriptor("adr_normative"),
        make_descriptor("ssot_normative"),
        make_descriptor("profile_policy_normative"),
    };
    assert_true(corpus_catalog->replace_all(descriptors),
                "retrieval quality harness should install corpus descriptors");

    auto index_reader = std::make_unique<IndexReader>(make_snapshot());
    auto* index_reader_ptr = index_reader.get();

    sparse_retriever_ = std::make_unique<SparseRetriever>(SparseRetrieverDeps{
        .search_index = [index_reader_ptr](const SparseIndexSearchRequest& request) {
          return index_reader_ptr->search_sparse(request);
        },
    });

    RecallCoordinatorDeps recall_deps;
    recall_deps.sparse_lane = [this](const dasall::knowledge::retrieve::SparseRetrieveRequest& request) {
      return sparse_retriever_->retrieve(request);
    };
    recall_deps.dense_lane = [](const DenseRecallRequest&) {
      return DenseRecallResult{};
    };
    recall_deps.dense_lane = [this](const DenseRecallRequest& request) {
      return dense_retrieve(request);
    };

    KnowledgeServiceDeps deps;
    deps.query_normalizer = std::make_unique<QueryNormalizer>(make_normalize_policy());
    deps.corpus_catalog = std::move(corpus_catalog);
    deps.index_reader = std::move(index_reader);
    deps.freshness_controller = std::make_unique<dasall::knowledge::FreshnessController>();
    deps.corpus_router = std::make_unique<CorpusRouter>();
    deps.recall_coordinator = std::make_unique<RecallCoordinator>(
        std::move(recall_deps),
        RecallCoordinatorPolicy{
            .max_parallel_recall = 1U,
            .sparse_lane_timeout_ms = 450,
            .dense_lane_timeout_ms = 450,
        });
    deps.reranker = std::make_unique<Reranker>();
    deps.evidence_assembler = std::make_unique<EvidenceAssembler>();
    deps.now_ms = [this] { return now_ms_; };

    knowledge_service_ = std::make_unique<KnowledgeServiceFacade>(std::move(deps));
    assert_true(knowledge_service_->init(make_config(active_mode_)),
                "retrieval quality harness should initialize the knowledge service");
  }

  [[nodiscard]] std::vector<std::string> retrieve_sources(const QualityCaseSpec& spec) const {
    KnowledgeQuery query;
    query.request_id = "req-" + spec.case_id;
    query.query_text = spec.query_text;
    query.query_kind = spec.query_kind;
    query.preferred_mode = active_mode_;
    query.allowed_corpora = {spec.primary_corpus_id};
    query.top_k = manifest_.retrieval_top_k;
    query.max_context_projection_items = manifest_.retrieval_top_k;

    const auto result = knowledge_service_->retrieve(query);
    assert_true(result.ok,
                "retrieval quality harness should execute retrieval successfully for " +
                    spec.case_id);
    assert_true(result.has_consistent_values(),
                "retrieval quality harness should keep public result invariants for " +
                    spec.case_id);
    assert_true(result.mode == active_mode_,
          "retrieval quality harness should stay on the requested mode for " +
            spec.case_id);
    assert_true(result.evidence.has_value(),
                "retrieval quality harness should return evidence for " + spec.case_id);
    assert_true(!result.evidence->degraded,
                "retrieval quality harness should not degrade curated baseline retrieval for " +
                    spec.case_id);

    std::vector<std::string> sources;
    for (const auto& slice : result.evidence->slices) {
      sources.push_back(extract_source_uri(slice.citation_ref));
    }
    return dedupe_preserve_order(sources);
  }

 private:
  [[nodiscard]] const QualityCaseSpec* find_case_spec(std::string_view query_text) const {
    const auto iterator = std::find_if(manifest_.cases.begin(),
                                       manifest_.cases.end(),
                                       [query_text](const QualityCaseSpec& spec) {
                                         return spec.query_text == query_text;
                                       });
    return iterator == manifest_.cases.end() ? nullptr : &(*iterator);
  }

  [[nodiscard]] DenseRecallResult dense_retrieve(const DenseRecallRequest& request) const {
    DenseRecallResult result;
    const auto* spec = find_case_spec(request.normalized_query.normalized_text);
    if (spec == nullptr) {
      result.ok = false;
      result.failure_reason_codes = {"dense_fixture_missing"};
      return result;
    }

    if (spec->primary_corpus_id == "profile_policy_normative") {
      result.ok = false;
      result.failure_reason_codes = {"dense_fixture_missing"};
      return result;
    }

    dasall::knowledge::retrieve::RecallHit hit;
    hit.corpus_id = spec->primary_corpus_id;
    hit.document_id = spec->case_id;
    hit.chunk_id = spec->case_id + "#dense";
    hit.score = 0.97F;
    hit.raw_snippet = build_chunk_text(*spec);
    hit.citation_ref = spec->expected_chunk_refs.front();
    hit.updated_at = 1713744000600;
    hit.authority_level = corpus_authority_level(spec->primary_corpus_id);
    hit.tags = corpus_tags(spec->primary_corpus_id);

    result.ok = true;
    result.hits.push_back(std::move(hit));
    return result;
  }

  [[nodiscard]] std::shared_ptr<const IndexSnapshot> make_snapshot() const {
    auto snapshot = std::make_shared<IndexSnapshot>();
    snapshot->manifest = IndexManifest{
        .format_version = 1U,
        .lexical_backend = "sqlite_fts5",
        .tokenizer_profile = "porter unicode61 remove_diacritics 1",
        .snapshot_id = "snapshot-retrieval-quality-v1",
        .built_at = 1713744000000,
        .effective_at = 1713744001000,
        .document_count = manifest_.cases.size(),
        .chunk_count = manifest_.cases.size(),
        .vector_enabled = active_mode_ != RetrievalMode::LexicalOnly,
    };
    snapshot->checksum = "checksum-retrieval-quality-v1";
    snapshot->search = [this](const SparseIndexSearchRequest& request) {
      return sqlite_index_.search(request);
    };
    return snapshot;
  }

  void populate_sqlite_fixture() {
    for (const auto& spec : manifest_.cases) {
      sqlite_index_.insert_row(spec.primary_corpus_id,
                               spec.case_id,
                               spec.case_id,
                               build_chunk_text(spec),
                               spec.expected_chunk_refs.front(),
                               1713744000500,
                               corpus_authority_level(spec.primary_corpus_id),
                               "zh-CN",
                               corpus_tags(spec.primary_corpus_id));
    }
  }

  QualityManifest manifest_;
  RetrievalMode active_mode_ = RetrievalMode::LexicalOnly;
  SqliteLexicalIndexFixture sqlite_index_;
  std::int64_t now_ms_ = 1713744002000;
  std::unique_ptr<SparseRetriever> sparse_retriever_;
  std::unique_ptr<dasall::knowledge::IKnowledgeService> knowledge_service_;
};

void test_retrieval_quality_regression_passes_curated_manifest() {
  const auto manifest = load_quality_manifest();
  RetrievalQualityHarness harness(manifest, RetrievalMode::LexicalOnly);

  const auto report = evaluate_manifest(
      manifest,
      RetrievalMode::LexicalOnly,
      [&harness](const QualityCaseSpec& spec) { return harness.retrieve_sources(spec); });

  assert_true(report.passed,
              "retrieval quality curated manifest should satisfy aggregate and hard-fail gates");
  assert_equal(count_cases_for_mode(manifest, RetrievalMode::LexicalOnly),
               report.executed_cases,
               "retrieval quality curated manifest should execute all lexical-only cases");
  assert_equal(static_cast<int>(manifest.cases.size()) - report.executed_cases,
               report.skipped_cases,
               "retrieval quality curated manifest should skip only non-lexical mode cases");
  assert_metric_equal(1.0, report.aggregate_metrics.mrr_at_10,
                      "retrieval quality curated manifest should keep perfect MRR@10");
  assert_metric_equal(1.0, report.aggregate_metrics.ndcg_at_10,
                      "retrieval quality curated manifest should keep perfect NDCG@10");
  assert_metric_equal(1.0, report.aggregate_metrics.recall_at_5,
                      "retrieval quality curated manifest should keep perfect Recall@5");
  assert_metric_equal(1.0, report.aggregate_metrics.recall_at_10,
                      "retrieval quality curated manifest should keep perfect Recall@10");
}

void test_retrieval_quality_regression_rejects_hard_fail_regression() {
  const auto manifest = load_quality_manifest();
  RetrievalQualityHarness harness(manifest, RetrievalMode::LexicalOnly);

  const auto report = evaluate_manifest(
      manifest,
      RetrievalMode::LexicalOnly,
      [&harness](const QualityCaseSpec& spec) {
        if (spec.case_id == "fact_arch_01") {
          return std::vector<std::string>{"docs/architecture/ARCH-QUALITY-02.md"};
        }
        return harness.retrieve_sources(spec);
      });

  assert_true(!report.passed,
              "retrieval quality gate should fail when a hard-fail case loses its first expected source");
  assert_true(std::find(report.failures.begin(),
                        report.failures.end(),
                        std::string("hard_fail miss: fact_arch_01")) != report.failures.end(),
              "retrieval quality gate should surface the hard-fail case id in its failure summary");
}

void test_retrieval_quality_regression_passes_hybrid_mode_gate() {
  const auto manifest = load_quality_manifest();
  RetrievalQualityHarness harness(manifest, RetrievalMode::Hybrid);

  const auto report = evaluate_mode_gate(
      manifest,
      RetrievalMode::Hybrid,
      [&harness](const QualityCaseSpec& spec) { return harness.retrieve_sources(spec); });

  assert_true(report.passed,
              "retrieval quality hybrid mode gate should satisfy aggregate and hard-fail requirements");
  assert_equal(count_cases_for_mode(manifest, RetrievalMode::Hybrid),
               report.executed_cases,
               "retrieval quality hybrid mode gate should execute all hybrid cases");
  assert_metric_equal(1.0, report.aggregate_metrics.mrr_at_10,
                      "retrieval quality hybrid mode gate should keep perfect MRR@10");
  assert_metric_equal(1.0, report.aggregate_metrics.ndcg_at_10,
                      "retrieval quality hybrid mode gate should keep perfect NDCG@10");
}

void test_retrieval_quality_regression_passes_dense_only_mode_gate() {
  const auto manifest = load_quality_manifest();
  RetrievalQualityHarness harness(manifest, RetrievalMode::DenseOnly);

  const auto report = evaluate_mode_gate(
      manifest,
      RetrievalMode::DenseOnly,
      [&harness](const QualityCaseSpec& spec) { return harness.retrieve_sources(spec); });

  assert_true(report.passed,
              "retrieval quality dense-only mode gate should satisfy aggregate and hard-fail requirements");
  assert_equal(count_cases_for_mode(manifest, RetrievalMode::DenseOnly),
               report.executed_cases,
               "retrieval quality dense-only mode gate should execute all dense-only cases");
  assert_metric_equal(1.0, report.aggregate_metrics.recall_at_5,
                      "retrieval quality dense-only mode gate should keep perfect Recall@5");
  assert_metric_equal(1.0, report.aggregate_metrics.recall_at_10,
                      "retrieval quality dense-only mode gate should keep perfect Recall@10");
}

void test_retrieval_quality_regression_rejects_dense_only_hard_fail_regression() {
  const auto manifest = load_quality_manifest();
  RetrievalQualityHarness harness(manifest, RetrievalMode::DenseOnly);

  const auto report = evaluate_mode_gate(
      manifest,
      RetrievalMode::DenseOnly,
      [&harness](const QualityCaseSpec& spec) {
        if (spec.case_id == "diag_adr_dense_01") {
          return std::vector<std::string>{"docs/adr/ADR-QUALITY-08.md"};
        }
        return harness.retrieve_sources(spec);
      });

  assert_true(!report.passed,
              "retrieval quality dense-only gate should fail when a dense hard-fail case loses its expected source");
  assert_true(std::find(report.failures.begin(),
                        report.failures.end(),
                        std::string("hard_fail miss: diag_adr_dense_01")) != report.failures.end(),
              "retrieval quality dense-only gate should surface the failing case id in its failure summary");
}

}  // namespace

int main() {
  try {
    test_retrieval_quality_regression_passes_curated_manifest();
    test_retrieval_quality_regression_rejects_hard_fail_regression();
    test_retrieval_quality_regression_passes_hybrid_mode_gate();
    test_retrieval_quality_regression_passes_dense_only_mode_gate();
    test_retrieval_quality_regression_rejects_dense_only_hard_fail_regression();
  } catch (const std::exception& exception) {
    std::cerr << exception.what() << '\n';
    return 1;
  }

  return 0;
}