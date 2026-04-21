#include "ingest/IngestionCoordinator.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <optional>
#include <set>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace dasall::knowledge::ingest {

namespace {

constexpr std::string_view kBatchIdPrefix = "batch:";
constexpr std::string_view kDocumentLineagePrefix = "doclineage:";
constexpr std::string_view kCorpusQuarantinePrefix = "corpus::";

constexpr std::array<std::uint32_t, 64> kSha256RoundConstants = {
    0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U, 0x3956c25bU, 0x59f111f1U,
    0x923f82a4U, 0xab1c5ed5U, 0xd807aa98U, 0x12835b01U, 0x243185beU, 0x550c7dc3U,
    0x72be5d74U, 0x80deb1feU, 0x9bdc06a7U, 0xc19bf174U, 0xe49b69c1U, 0xefbe4786U,
    0x0fc19dc6U, 0x240ca1ccU, 0x2de92c6fU, 0x4a7484aaU, 0x5cb0a9dcU, 0x76f988daU,
    0x983e5152U, 0xa831c66dU, 0xb00327c8U, 0xbf597fc7U, 0xc6e00bf3U, 0xd5a79147U,
    0x06ca6351U, 0x14292967U, 0x27b70a85U, 0x2e1b2138U, 0x4d2c6dfcU, 0x53380d13U,
    0x650a7354U, 0x766a0abbU, 0x81c2c92eU, 0x92722c85U, 0xa2bfe8a1U, 0xa81a664bU,
    0xc24b8b70U, 0xc76c51a3U, 0xd192e819U, 0xd6990624U, 0xf40e3585U, 0x106aa070U,
    0x19a4c116U, 0x1e376c08U, 0x2748774cU, 0x34b0bcb5U, 0x391c0cb3U, 0x4ed8aa4aU,
    0x5b9cca4fU, 0x682e6ff3U, 0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U,
    0x90befffaU, 0xa4506cebU, 0xbef9a3f7U, 0xc67178f2U,
};

[[nodiscard]] bool is_sha256_hex(std::string_view value) {
  return value.size() == 64U &&
         std::all_of(value.begin(), value.end(), [](unsigned char character) {
           return std::isxdigit(character) != 0;
         });
}

[[nodiscard]] std::uint32_t rotate_right(std::uint32_t value, std::uint32_t shift) {
  return (value >> shift) | (value << (32U - shift));
}

[[nodiscard]] std::uint32_t sha256_choose(std::uint32_t x,
                                          std::uint32_t y,
                                          std::uint32_t z) {
  return (x & y) ^ (~x & z);
}

[[nodiscard]] std::uint32_t sha256_majority(std::uint32_t x,
                                            std::uint32_t y,
                                            std::uint32_t z) {
  return (x & y) ^ (x & z) ^ (y & z);
}

[[nodiscard]] std::uint32_t sha256_big_sigma0(std::uint32_t value) {
  return rotate_right(value, 2U) ^ rotate_right(value, 13U) ^ rotate_right(value, 22U);
}

[[nodiscard]] std::uint32_t sha256_big_sigma1(std::uint32_t value) {
  return rotate_right(value, 6U) ^ rotate_right(value, 11U) ^ rotate_right(value, 25U);
}

[[nodiscard]] std::uint32_t sha256_small_sigma0(std::uint32_t value) {
  return rotate_right(value, 7U) ^ rotate_right(value, 18U) ^ (value >> 3U);
}

[[nodiscard]] std::uint32_t sha256_small_sigma1(std::uint32_t value) {
  return rotate_right(value, 17U) ^ rotate_right(value, 19U) ^ (value >> 10U);
}

[[nodiscard]] char hex_digit(std::uint8_t value) {
  return value < 10U ? static_cast<char>('0' + value)
                     : static_cast<char>('a' + (value - 10U));
}

[[nodiscard]] std::string sha256_hex(std::string_view input) {
  std::vector<std::uint8_t> bytes(input.begin(), input.end());
  const std::uint64_t bit_length = static_cast<std::uint64_t>(bytes.size()) * 8U;

  bytes.push_back(0x80U);
  while ((bytes.size() % 64U) != 56U) {
    bytes.push_back(0U);
  }
  for (int shift = 56; shift >= 0; shift -= 8) {
    bytes.push_back(static_cast<std::uint8_t>((bit_length >> shift) & 0xffU));
  }

  std::array<std::uint32_t, 8> state = {
      0x6a09e667U,
      0xbb67ae85U,
      0x3c6ef372U,
      0xa54ff53aU,
      0x510e527fU,
      0x9b05688cU,
      0x1f83d9abU,
      0x5be0cd19U,
  };
  std::array<std::uint32_t, 64> message_schedule{};

  for (std::size_t chunk_offset = 0; chunk_offset < bytes.size(); chunk_offset += 64U) {
    for (std::size_t word_index = 0; word_index < 16U; ++word_index) {
      const std::size_t base_index = chunk_offset + word_index * 4U;
      message_schedule[word_index] = (static_cast<std::uint32_t>(bytes[base_index]) << 24U) |
                                     (static_cast<std::uint32_t>(bytes[base_index + 1U]) << 16U) |
                                     (static_cast<std::uint32_t>(bytes[base_index + 2U]) << 8U) |
                                     static_cast<std::uint32_t>(bytes[base_index + 3U]);
    }
    for (std::size_t word_index = 16U; word_index < message_schedule.size(); ++word_index) {
      message_schedule[word_index] = sha256_small_sigma1(message_schedule[word_index - 2U]) +
                                     message_schedule[word_index - 7U] +
                                     sha256_small_sigma0(message_schedule[word_index - 15U]) +
                                     message_schedule[word_index - 16U];
    }

    std::uint32_t a = state[0];
    std::uint32_t b = state[1];
    std::uint32_t c = state[2];
    std::uint32_t d = state[3];
    std::uint32_t e = state[4];
    std::uint32_t f = state[5];
    std::uint32_t g = state[6];
    std::uint32_t h = state[7];

    for (std::size_t round = 0; round < message_schedule.size(); ++round) {
      const std::uint32_t temp1 =
          h + sha256_big_sigma1(e) + sha256_choose(e, f, g) + kSha256RoundConstants[round] +
          message_schedule[round];
      const std::uint32_t temp2 = sha256_big_sigma0(a) + sha256_majority(a, b, c);

      h = g;
      g = f;
      f = e;
      e = d + temp1;
      d = c;
      c = b;
      b = a;
      a = temp1 + temp2;
    }

    state[0] += a;
    state[1] += b;
    state[2] += c;
    state[3] += d;
    state[4] += e;
    state[5] += f;
    state[6] += g;
    state[7] += h;
  }

  std::string hex;
  hex.reserve(64U);
  for (const std::uint32_t word : state) {
    for (int shift = 28; shift >= 0; shift -= 4) {
      hex.push_back(hex_digit(static_cast<std::uint8_t>((word >> shift) & 0x0fU)));
    }
  }
  return hex;
}

[[nodiscard]] bool has_unique_non_empty_values(const std::vector<std::string>& values) {
  std::set<std::string, std::less<>> seen_values;
  for (const auto& value : values) {
    if (value.empty() || !seen_values.insert(value).second) {
      return false;
    }
  }
  return true;
}

[[nodiscard]] bool has_disjoint_change_lanes(const CorpusChangeSet& change_set) {
  std::set<std::string, std::less<>> seen_sources;
  const auto collect_lane = [&seen_sources](const auto& values) {
    return std::all_of(values.begin(), values.end(), [&seen_sources](const std::string& value) {
      return seen_sources.insert(value).second;
    });
  };

  return collect_lane(change_set.added_sources) && collect_lane(change_set.updated_sources) &&
         collect_lane(change_set.removed_sources);
}

void sort_and_deduplicate(std::vector<std::string>& values) {
  std::sort(values.begin(), values.end());
  values.erase(std::unique(values.begin(), values.end()), values.end());
}

[[nodiscard]] std::string normalize_source_root(std::string_view value) {
  std::string normalized(value);
  while (!normalized.empty() && normalized.back() == '/') {
    normalized.pop_back();
  }
  return normalized;
}

[[nodiscard]] bool source_uri_belongs_to_root(std::string_view source_uri,
                                              std::string_view root_uri) {
  const auto normalized_source = normalize_source_root(source_uri);
  const auto normalized_root = normalize_source_root(root_uri);
  if (normalized_root.empty()) {
    return false;
  }
  return normalized_source == normalized_root ||
         normalized_source.rfind(normalized_root + '/', 0) == 0;
}

[[nodiscard]] bool has_any_changes(const CorpusChangeSet& change_set) {
  return !change_set.added_sources.empty() || !change_set.updated_sources.empty() ||
         !change_set.removed_sources.empty();
}

void append_warning(std::vector<std::string>& warnings,
                    std::string_view prefix,
                    std::string_view subject,
                    std::string_view detail = {}) {
  std::string warning(prefix);
  if (!subject.empty()) {
    warning.push_back(':');
    warning.append(subject);
  }
  if (!detail.empty()) {
    warning.push_back(':');
    warning.append(detail);
  }
  warnings.push_back(std::move(warning));
}

}  // namespace

bool IndexUpdateBatch::has_consistent_values() const {
  std::set<std::string, std::less<>> chunk_ids;
  for (const auto& chunk : chunk_records) {
    if (!chunk.has_consistent_values() || !chunk_ids.insert(chunk.chunk_id).second) {
      return false;
    }
  }

  return batch_id.rfind(kBatchIdPrefix, 0) == 0 && is_sha256_hex(batch_id.substr(6U)) &&
         has_unique_non_empty_values(removed_document_ids) &&
         has_unique_non_empty_values(warnings);
}

IngestionCoordinator::IngestionCoordinator(IngestionCoordinatorDeps deps,
                                           ChunkPolicy chunk_policy)
    : deps_(std::move(deps)),
      chunk_policy_(std::move(chunk_policy)) {
  if (!deps_.load_catalog_snapshot || !deps_.load_inventory || !deps_.repository_root ||
      !deps_.now_ms) {
    throw std::invalid_argument("ingestion_coordinator_deps_incomplete");
  }
  if (!chunk_policy_.has_consistent_values()) {
    throw std::invalid_argument("chunk_policy_invalid");
  }
}

IndexUpdateBatch IngestionCoordinator::build_update_batch(
    const CorpusChangeSet& change_set) const {
  if (!change_set.has_consistent_values() || !has_disjoint_change_lanes(change_set)) {
    throw std::invalid_argument("corpus_change_set_invalid");
  }

  auto scan_result = scan_and_canonicalize(change_set);

  IndexUpdateBatch batch;
  batch.chunk_records = build_chunk_records(scan_result.documents);
  batch.removed_document_ids = std::move(scan_result.removed_document_ids);
  batch.warnings = std::move(scan_result.warnings);

  sort_and_deduplicate(batch.removed_document_ids);
  sort_and_deduplicate(batch.warnings);
  batch.batch_id = build_batch_id(batch);
  return batch;
}

IngestionCoordinator::ScanAndCanonicalizeResult IngestionCoordinator::scan_and_canonicalize(
    const CorpusChangeSet& change_set) const {
  const auto snapshot = deps_.load_catalog_snapshot();
  if (!snapshot.has_consistent_values()) {
    throw std::invalid_argument("corpus_catalog_snapshot_invalid");
  }

  ScanAndCanonicalizeResult result;
  const auto target_corpora = select_target_corpora(change_set, snapshot);
  if (target_corpora.empty()) {
    append_warning(result.warnings,
                   has_any_changes(change_set) ? "no_matching_corpora" : "no_trusted_corpora",
                   "ingest");
    return result;
  }

  if (has_any_changes(change_set)) {
    const auto all_descriptors = snapshot.list_all();
    const auto append_unmatched = [&](const std::vector<std::string>& sources) {
      for (const auto& source_uri : sources) {
        const bool matched = std::any_of(all_descriptors.begin(), all_descriptors.end(),
                                         [&source_uri](const CorpusDescriptor& descriptor) {
                                           return descriptor.trust_level == TrustLevel::Trusted &&
                                                  source_uri_belongs_to_root(source_uri,
                                                                             descriptor.source_uri);
                                         });
        if (!matched) {
          append_warning(result.warnings, "unmatched_change_source", source_uri);
        }
      }
    };
    append_unmatched(change_set.added_sources);
    append_unmatched(change_set.updated_sources);
    append_unmatched(change_set.removed_sources);
  }

  SourceScannerDeps scanner_deps;
  scanner_deps.lookup_corpus = [snapshot](std::string_view corpus_id) {
    return snapshot.find_by_id(corpus_id);
  };
  scanner_deps.load_inventory = deps_.load_inventory;
  scanner_deps.repository_root = deps_.repository_root;
  scanner_deps.now_ms = deps_.now_ms;

  const SourceScanner scanner(std::move(scanner_deps));
  const Canonicalizer canonicalizer({.repository_root = deps_.repository_root()});
  const bool full_refresh = !has_any_changes(change_set);

  for (const auto& descriptor : target_corpora) {
    CorpusScanPlan plan;
    plan.corpus_id = descriptor.corpus_id;
    plan.root_uri = descriptor.source_uri;
    plan.source_kind = descriptor.source_kind;
    plan.include_globs = descriptor.include_globs;
    plan.exclude_globs = descriptor.exclude_globs;
    plan.allowed_formats = descriptor.allowed_formats;
    plan.full_scan = full_refresh;

    const auto delta = scanner.scan(plan);
    for (const auto& source_id : delta.removed_source_ids) {
      result.removed_document_ids.push_back(build_document_lineage_id(source_id));
    }
    for (const auto& source_id : delta.quarantined_source_ids) {
      append_warning(result.warnings, "source_quarantine", source_id);
      if (source_id.rfind(kCorpusQuarantinePrefix, 0) != 0) {
        result.removed_document_ids.push_back(build_document_lineage_id(source_id));
      }
    }
    for (const auto& source : delta.updated) {
      result.removed_document_ids.push_back(build_document_lineage_id(source.source_id));
    }

    std::vector<SourceRecord> changed_sources;
    changed_sources.reserve(delta.added.size() + delta.updated.size());
    changed_sources.insert(changed_sources.end(), delta.added.begin(), delta.added.end());
    changed_sources.insert(changed_sources.end(), delta.updated.begin(), delta.updated.end());

    for (const auto& source : changed_sources) {
      const auto canonicalize_result = canonicalizer.canonicalize(source);
      for (const auto& warning : canonicalize_result.warnings) {
        append_warning(result.warnings, "canonicalize_warning", source.source_id, warning);
      }

      if (!canonicalize_result.ok || !canonicalize_result.document.has_value()) {
        if (canonicalize_result.quarantine_reason.has_value()) {
          append_warning(result.warnings, "canonicalize_quarantine", source.source_id,
                         *canonicalize_result.quarantine_reason);
        }
        continue;
      }

      result.documents.push_back(*canonicalize_result.document);
    }
  }

  std::sort(result.documents.begin(), result.documents.end(),
            [](const CanonicalDocument& left, const CanonicalDocument& right) {
              if (left.source_id != right.source_id) {
                return left.source_id < right.source_id;
              }
              return left.document_id < right.document_id;
            });
  return result;
}

std::vector<ChunkRecord> IngestionCoordinator::build_chunk_records(
    const std::vector<CanonicalDocument>& documents) const {
  const Chunker chunker(chunk_policy_);
  std::vector<ChunkRecord> records;

  for (const auto& document : documents) {
    auto chunk_records = chunker.chunk(document);
    const auto lineage_id = build_document_lineage_id(document.source_id);
    for (auto& chunk_record : chunk_records) {
      chunk_record.metadata.insert_or_assign("document_lineage_id", lineage_id);
      records.push_back(std::move(chunk_record));
    }
  }

  std::sort(records.begin(), records.end(), [](const ChunkRecord& left, const ChunkRecord& right) {
    if (left.source_id != right.source_id) {
      return left.source_id < right.source_id;
    }
    if (left.span_begin != right.span_begin) {
      return left.span_begin < right.span_begin;
    }
    return left.chunk_id < right.chunk_id;
  });
  return records;
}

std::vector<CorpusDescriptor> IngestionCoordinator::select_target_corpora(
    const CorpusChangeSet& change_set,
    const index::CorpusCatalogSnapshot& snapshot) const {
  auto descriptors = snapshot.list_all();
  std::vector<CorpusDescriptor> selected;
  selected.reserve(descriptors.size());

  if (!has_any_changes(change_set)) {
    for (const auto& descriptor : descriptors) {
      if (descriptor.trust_level == TrustLevel::Trusted) {
        selected.push_back(descriptor);
      }
    }
  } else {
    std::vector<std::string> changed_sources;
    changed_sources.reserve(change_set.added_sources.size() + change_set.updated_sources.size() +
                            change_set.removed_sources.size());
    changed_sources.insert(changed_sources.end(), change_set.added_sources.begin(),
                           change_set.added_sources.end());
    changed_sources.insert(changed_sources.end(), change_set.updated_sources.begin(),
                           change_set.updated_sources.end());
    changed_sources.insert(changed_sources.end(), change_set.removed_sources.begin(),
                           change_set.removed_sources.end());

    for (const auto& descriptor : descriptors) {
      if (descriptor.trust_level != TrustLevel::Trusted) {
        continue;
      }
      const bool impacted = std::any_of(changed_sources.begin(), changed_sources.end(),
                                        [&descriptor](const std::string& source_uri) {
                                          return source_uri_belongs_to_root(source_uri,
                                                                            descriptor.source_uri);
                                        });
      if (impacted) {
        selected.push_back(descriptor);
      }
    }
  }

  std::sort(selected.begin(), selected.end(),
            [](const CorpusDescriptor& left, const CorpusDescriptor& right) {
              return left.corpus_id < right.corpus_id;
            });
  return selected;
}

std::string IngestionCoordinator::build_batch_id(const IndexUpdateBatch& batch) const {
  std::string seed;
  for (const auto& chunk : batch.chunk_records) {
    seed.append(chunk.chunk_id);
    seed.push_back('\n');
  }
  for (const auto& removed_document_id : batch.removed_document_ids) {
    seed.append(removed_document_id);
    seed.push_back('\n');
  }
  for (const auto& warning : batch.warnings) {
    seed.append(warning);
    seed.push_back('\n');
  }
  return std::string(kBatchIdPrefix) + sha256_hex(seed);
}

std::string IngestionCoordinator::build_document_lineage_id(std::string_view source_id) const {
  return std::string(kDocumentLineagePrefix) + sha256_hex(source_id);
}

}  // namespace dasall::knowledge::ingest