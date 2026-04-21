#include "ingest/Chunker.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cctype>
#include <cstdint>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <vector>

namespace dasall::knowledge::ingest {

namespace {

constexpr std::string_view kChunkIdPrefix = "chunk:";

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

[[nodiscard]] bool is_heading_line(std::string_view value) {
  std::size_t hashes = 0U;
  while (hashes < value.size() && value[hashes] == '#') {
    ++hashes;
  }
  return hashes > 0U && hashes <= 6U &&
         (hashes == value.size() || value[hashes] == ' ' || value[hashes] == '\t');
}

[[nodiscard]] std::string trim_copy(std::string_view value) {
  const auto begin = value.find_first_not_of(" \t\n\r");
  if (begin == std::string_view::npos) {
    return {};
  }
  const auto end = value.find_last_not_of(" \t\n\r");
  return std::string(value.substr(begin, end - begin + 1U));
}

[[nodiscard]] std::vector<std::size_t> collect_breakpoints(std::string_view text) {
  std::vector<std::size_t> breakpoints{text.size()};
  std::size_t line_start = 0U;

  while (line_start < text.size()) {
    const auto line_end = text.find('\n', line_start);
    const auto line_limit = line_end == std::string_view::npos ? text.size() : line_end;
    const auto line = text.substr(line_start, line_limit - line_start);
    const auto trimmed = trim_copy(line);

    if (!trimmed.empty() && is_heading_line(trimmed) && line_start != 0U) {
      breakpoints.push_back(line_start);
    }

    if (trimmed.empty()) {
      std::size_t blank_sequence_end = line_end == std::string_view::npos ? text.size() : line_end + 1U;
      std::size_t cursor = blank_sequence_end;
      while (cursor < text.size()) {
        const auto next_end = text.find('\n', cursor);
        const auto next_limit = next_end == std::string_view::npos ? text.size() : next_end;
        const auto next_trimmed = trim_copy(text.substr(cursor, next_limit - cursor));
        if (!next_trimmed.empty()) {
          break;
        }
        blank_sequence_end = next_end == std::string_view::npos ? text.size() : next_end + 1U;
        cursor = blank_sequence_end;
      }
      breakpoints.push_back(blank_sequence_end);
    }

    line_start = line_end == std::string_view::npos ? text.size() : line_end + 1U;
  }

  std::sort(breakpoints.begin(), breakpoints.end());
  breakpoints.erase(std::unique(breakpoints.begin(), breakpoints.end()), breakpoints.end());
  return breakpoints;
}

[[nodiscard]] std::optional<std::size_t> choose_breakpoint(
    const std::vector<std::size_t>& breakpoints,
    std::size_t min_end,
    std::size_t limit) {
  auto it = std::upper_bound(breakpoints.begin(), breakpoints.end(), limit);
  while (it != breakpoints.begin()) {
    --it;
    if (*it > min_end) {
      return *it;
    }
  }
  return std::nullopt;
}

[[nodiscard]] std::optional<std::size_t> choose_soft_break(std::string_view text,
                                                           std::size_t min_end,
                                                           std::size_t limit) {
  if (limit <= min_end || limit > text.size()) {
    return std::nullopt;
  }
  for (std::size_t position = limit; position > min_end; --position) {
    if (std::isspace(static_cast<unsigned char>(text[position - 1U])) != 0) {
      return position;
    }
  }
  return std::nullopt;
}

[[nodiscard]] std::size_t adjust_overlap_start(std::string_view text,
                                               std::size_t candidate,
                                               std::size_t end) {
  if (candidate == 0U || candidate >= end || end > text.size()) {
    return candidate;
  }

  std::size_t cursor = candidate;
  while (cursor < end && std::isspace(static_cast<unsigned char>(text[cursor])) == 0) {
    ++cursor;
  }
  while (cursor < end && std::isspace(static_cast<unsigned char>(text[cursor])) != 0) {
    ++cursor;
  }
  return cursor < end ? cursor : candidate;
}

[[nodiscard]] std::size_t estimate_tokens(std::string_view text) {
  std::size_t token_count = 0U;
  bool in_token = false;
  for (const unsigned char character : text) {
    if (std::isspace(character) != 0) {
      in_token = false;
      continue;
    }
    if (!in_token) {
      ++token_count;
      in_token = true;
    }
  }
  return std::max<std::size_t>(1U, token_count);
}

[[nodiscard]] bool has_chunkable_document_fields(const CanonicalDocument& document) {
  if (document.document_id.rfind("doc:", 0) != 0 ||
      !is_sha256_hex(document.document_id.substr(4U)) || document.corpus_id.empty() ||
      document.source_id.empty() || document.source_uri.empty() || document.title.empty() ||
      !is_sha256_hex(document.source_hash) || document.version.empty() ||
      document.updated_at_ms < 0 || document.language.empty()) {
    return false;
  }

  return document.metadata.contains("document_class") && document.metadata.contains("section_path") &&
         dasall::knowledge::detail::has_unique_values(document.tags);
}

[[nodiscard]] std::string make_citation_ref(const CanonicalDocument& document,
                                            const TextSpan& span) {
  return document.source_uri + "#char=" + std::to_string(span.begin) + "-" +
         std::to_string(span.end);
}

}  // namespace

bool ChunkPolicy::has_consistent_values() const {
  const bool strategy_known = strategy == ChunkStrategy::FixedSize ||
                              strategy == ChunkStrategy::Semantic ||
                              strategy == ChunkStrategy::DocumentAware;
  return strategy_known && target_chunk_chars > 0U && max_chunk_chars >= target_chunk_chars &&
         overlap_chars < max_chunk_chars && min_chunk_chars > 0U &&
         min_chunk_chars <= target_chunk_chars;
}

bool TextSpan::has_consistent_values() const {
  return end > begin;
}

bool ChunkRecord::has_consistent_values() const {
  if (chunk_id.rfind(kChunkIdPrefix, 0) != 0 || !is_sha256_hex(chunk_id.substr(6U)) ||
      document_id.empty() || corpus_id.empty() || source_id.empty() || source_uri.empty() ||
      chunk_text.empty() || version.empty() || updated_at_ms < 0 || language.empty() ||
      token_estimate == 0U || span_end <= span_begin || chunk_text.size() != span_end - span_begin ||
      citation_ref.empty() || !dasall::knowledge::detail::has_unique_values(tags) ||
      !dasall::knowledge::detail::has_unique_values(adjacent_chunk_refs)) {
    return false;
  }

  if (!metadata.contains("document_class") || !metadata.contains("section_path")) {
    return false;
  }

  return std::all_of(adjacent_chunk_refs.begin(), adjacent_chunk_refs.end(), [&](const auto& ref) {
    return !ref.empty() && ref != chunk_id;
  });
}

Chunker::Chunker(ChunkPolicy policy)
    : policy_(std::move(policy)) {
  if (!policy_.has_consistent_values()) {
    throw std::invalid_argument("chunk_policy_invalid");
  }
}

std::vector<ChunkRecord> Chunker::chunk(const CanonicalDocument& document) const {
  if (!has_chunkable_document_fields(document)) {
    throw std::invalid_argument("canonical_document_inconsistent");
  }
  if (document.canonical_text.empty()) {
    return {};
  }

  auto spans = split_into_spans(document);
  std::vector<ChunkRecord> records;
  records.reserve(spans.size());

  for (const auto& span : spans) {
    if (!span.has_consistent_values()) {
      throw std::runtime_error("chunk_span_invalid");
    }

    ChunkRecord record;
    record.chunk_id = build_chunk_id(document, span);
    record.document_id = document.document_id;
    record.corpus_id = document.corpus_id;
    record.source_id = document.source_id;
    record.source_uri = document.source_uri;
    record.chunk_text = document.canonical_text.substr(span.begin, span.end - span.begin);
    record.version = document.version;
    record.updated_at_ms = document.updated_at_ms;
    record.source_format = document.source_format;
    record.authority_level = document.authority_level;
    record.language = document.language;
    record.token_estimate = estimate_tokens(record.chunk_text);
    record.span_begin = span.begin;
    record.span_end = span.end;
    record.citation_ref = make_citation_ref(document, span);
    record.tags = document.tags;
    record.metadata = document.metadata;
    records.push_back(std::move(record));
  }

  for (std::size_t index = 0U; index < records.size(); ++index) {
    if (index > 0U) {
      records[index].adjacent_chunk_refs.push_back(records[index - 1U].chunk_id);
    }
    if (index + 1U < records.size()) {
      records[index].adjacent_chunk_refs.push_back(records[index + 1U].chunk_id);
    }
  }

  return records;
}

std::vector<TextSpan> Chunker::split_into_spans(const CanonicalDocument& document) const {
  if (document.canonical_text.empty()) {
    return {};
  }

  const auto& text = document.canonical_text;
  const auto breakpoints = collect_breakpoints(text);
  std::vector<TextSpan> spans;
  std::size_t start = 0U;

  while (start < text.size()) {
    if (text.size() - start <= policy_.max_chunk_chars) {
      spans.push_back({.begin = start, .end = text.size()});
      break;
    }

    const auto min_end = std::min(text.size(), start + policy_.min_chunk_chars);
    const auto target_end = std::min(text.size(), start + policy_.target_chunk_chars);
    const auto max_end = std::min(text.size(), start + policy_.max_chunk_chars);

    std::size_t end = max_end;
    if (const auto preferred_break = choose_breakpoint(breakpoints, min_end, target_end);
        preferred_break.has_value()) {
      end = *preferred_break;
    } else if (const auto relaxed_break = choose_breakpoint(breakpoints, min_end, max_end);
               relaxed_break.has_value()) {
      end = *relaxed_break;
    } else if (const auto soft_break = choose_soft_break(text, min_end, max_end);
               soft_break.has_value()) {
      end = *soft_break;
    }

    if (end <= start) {
      end = max_end;
    }

    spans.push_back({.begin = start, .end = end});
    if (end >= text.size()) {
      break;
    }

    std::size_t next_start = end;
    if (policy_.overlap_chars > 0U && end > policy_.overlap_chars) {
      auto overlap_start = adjust_overlap_start(text, end - policy_.overlap_chars, end);
      if (overlap_start > start && overlap_start < end) {
        next_start = overlap_start;
      }
    }
    if (next_start <= start) {
      next_start = end;
    }
    start = next_start;
  }

  return spans;
}

std::string Chunker::build_chunk_id(const CanonicalDocument& document,
                                    const TextSpan& span) const {
  const auto chunk_text = document.canonical_text.substr(span.begin, span.end - span.begin);
  std::string seed;
  seed.reserve(document.document_id.size() + document.version.size() + chunk_text.size() + 128U);
  seed.append(document.document_id);
  seed.push_back('\n');
  seed.append(document.version);
  seed.push_back('\n');
  seed.append(std::to_string(static_cast<int>(policy_.strategy)));
  seed.push_back('\n');
  seed.append(std::to_string(policy_.target_chunk_chars));
  seed.push_back('\n');
  seed.append(std::to_string(policy_.max_chunk_chars));
  seed.push_back('\n');
  seed.append(std::to_string(policy_.overlap_chars));
  seed.push_back('\n');
  seed.append(std::to_string(span.begin));
  seed.push_back('\n');
  seed.append(std::to_string(span.end));
  seed.push_back('\n');
  seed.append(chunk_text);
  return std::string(kChunkIdPrefix) + sha256_hex(seed);
}

}  // namespace dasall::knowledge::ingest