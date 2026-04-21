#include "ingest/Canonicalizer.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cctype>
#include <cstdint>
#include <fstream>
#include <map>
#include <sstream>
#include <string_view>
#include <utility>
#include <vector>

namespace dasall::knowledge::ingest {

namespace {

constexpr std::string_view kDocIdPrefix = "doc:";
constexpr std::string_view kWarningTitleFallbackHeading = "title_fallback_heading";
constexpr std::string_view kWarningTitleFallbackFilename = "title_fallback_filename";
constexpr std::string_view kWarningLanguageFallbackUnd = "language_fallback_und";
constexpr std::string_view kWarningDocumentClassFallbackCorpus = "document_class_fallback_corpus";
constexpr std::string_view kWarningSectionPathFallbackHeading = "section_path_fallback_heading";
constexpr std::string_view kWarningSectionPathFallbackSourceUri = "section_path_fallback_source_uri";
constexpr std::string_view kWarningProfileNameFallbackSourcePath = "profile_name_fallback_source_path";
constexpr std::string_view kQuarantineSourceRecordInconsistent = "source_record_inconsistent";
constexpr std::string_view kQuarantineSourceReadFailed = "source_read_failed";
constexpr std::string_view kQuarantineSourceFormatUnsupported = "source_format_unsupported";
constexpr std::string_view kQuarantineYamlFlattenFailed = "yaml_flatten_failed";
constexpr std::string_view kQuarantineCanonicalTextEmpty = "canonical_text_empty";
constexpr std::string_view kQuarantineMetadataMissing = "metadata_missing";

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

struct FrontMatterParseResult {
  std::map<std::string, std::string> values;
  std::string body;
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

[[nodiscard]] std::string trim_copy(std::string_view value) {
  const auto begin = value.find_first_not_of(" \t\n\r");
  if (begin == std::string_view::npos) {
    return {};
  }
  const auto end = value.find_last_not_of(" \t\n\r");
  return std::string(value.substr(begin, end - begin + 1U));
}

[[nodiscard]] std::string trim_right_copy(std::string_view value) {
  const auto end = value.find_last_not_of(" \t\r");
  if (end == std::string_view::npos) {
    return {};
  }
  return std::string(value.substr(0, end + 1U));
}

[[nodiscard]] std::string strip_utf8_bom(std::string_view value) {
  if (value.size() >= 3U && static_cast<unsigned char>(value[0]) == 0xEFU &&
      static_cast<unsigned char>(value[1]) == 0xBBU &&
      static_cast<unsigned char>(value[2]) == 0xBFU) {
    return std::string(value.substr(3U));
  }
  return std::string(value);
}

[[nodiscard]] std::string normalize_newlines(std::string_view value) {
  std::string normalized;
  normalized.reserve(value.size());
  for (std::size_t index = 0; index < value.size(); ++index) {
    if (value[index] == '\r') {
      normalized.push_back('\n');
      if (index + 1U < value.size() && value[index + 1U] == '\n') {
        ++index;
      }
      continue;
    }
    normalized.push_back(value[index]);
  }
  return normalized;
}

[[nodiscard]] std::string normalize_text(std::string_view value) {
  return normalize_newlines(strip_utf8_bom(value));
}

[[nodiscard]] std::vector<std::string> split_lines(std::string_view value) {
  std::vector<std::string> lines;
  std::size_t cursor = 0U;
  while (cursor <= value.size()) {
    const auto next = value.find('\n', cursor);
    if (next == std::string_view::npos) {
      lines.emplace_back(value.substr(cursor));
      break;
    }
    lines.emplace_back(value.substr(cursor, next - cursor));
    cursor = next + 1U;
  }
  return lines;
}

[[nodiscard]] std::string join_lines(const std::vector<std::string>& lines) {
  std::ostringstream builder;
  for (std::size_t index = 0U; index < lines.size(); ++index) {
    if (index > 0U) {
      builder << '\n';
    }
    builder << lines[index];
  }
  return builder.str();
}

[[nodiscard]] std::string strip_wrapping_quotes(std::string value) {
  if (value.size() >= 2U &&
      ((value.front() == '"' && value.back() == '"') ||
       (value.front() == '\'' && value.back() == '\''))) {
    return value.substr(1U, value.size() - 2U);
  }
  if (value.size() >= 2U && value.front() == '[' && value.back() == ']') {
    return value.substr(1U, value.size() - 2U);
  }
  return value;
}

void append_unique(std::vector<std::string>& values, const std::string& value) {
  if (value.empty()) {
    return;
  }
  if (std::find(values.begin(), values.end(), value) == values.end()) {
    values.push_back(value);
  }
}

[[nodiscard]] std::vector<std::string> parse_tags(std::string_view raw_tags) {
  const auto flattened_tags = strip_wrapping_quotes(trim_copy(raw_tags));
  if (flattened_tags.empty()) {
    return {};
  }

  std::vector<std::string> tags;
  std::size_t cursor = 0U;
  while (cursor <= flattened_tags.size()) {
    const auto comma = flattened_tags.find(',', cursor);
    if (comma == std::string::npos) {
      append_unique(tags, trim_copy(flattened_tags.substr(cursor)));
      break;
    }
    append_unique(tags, trim_copy(flattened_tags.substr(cursor, comma - cursor)));
    cursor = comma + 1U;
  }
  std::sort(tags.begin(), tags.end());
  return tags;
}

[[nodiscard]] FrontMatterParseResult parse_front_matter(std::string_view normalized_content) {
  FrontMatterParseResult result;
  result.body = std::string(normalized_content);
  if (!normalized_content.starts_with("---\n")) {
    return result;
  }

  const auto end_marker = normalized_content.find("\n---\n", 4U);
  const auto alt_end_marker = normalized_content.find("\n...\n", 4U);
  std::size_t marker_position = std::string_view::npos;
  if (end_marker != std::string_view::npos &&
      (alt_end_marker == std::string_view::npos || end_marker < alt_end_marker)) {
    marker_position = end_marker;
  } else if (alt_end_marker != std::string_view::npos) {
    marker_position = alt_end_marker;
  }

  if (marker_position == std::string_view::npos) {
    return result;
  }

  std::string current_list_key;
  for (const auto& raw_line : split_lines(normalized_content.substr(4U, marker_position - 4U))) {
    const auto trimmed = trim_copy(trim_right_copy(raw_line));
    if (trimmed.empty()) {
      current_list_key.clear();
      continue;
    }

    if (trimmed.rfind("- ", 0) == 0 && !current_list_key.empty()) {
      auto& existing = result.values[current_list_key];
      if (!existing.empty()) {
        existing.append(", ");
      }
      existing.append(trim_copy(trimmed.substr(2U)));
      continue;
    }

    const auto colon = trimmed.find(':');
    if (colon == std::string::npos) {
      current_list_key.clear();
      continue;
    }

    const auto key = trim_copy(trimmed.substr(0U, colon));
    const auto value = trim_copy(trimmed.substr(colon + 1U));
    if (value.empty()) {
      result.values[key];
      current_list_key = key;
      continue;
    }

    result.values[key] = strip_wrapping_quotes(value);
    current_list_key.clear();
  }

  result.body = std::string(normalized_content.substr(marker_position + 5U));
  return result;
}

[[nodiscard]] bool is_setext_heading_underline(std::string_view value) {
  const auto trimmed = trim_copy(value);
  if (trimmed.empty()) {
    return false;
  }
  return std::all_of(trimmed.begin(), trimmed.end(), [first = trimmed.front()](char character) {
    return character == first && (character == '=' || character == '-');
  });
}

[[nodiscard]] std::optional<std::string> extract_first_heading(std::string_view markdown_body) {
  std::string previous_nonblank_line;
  for (const auto& raw_line : split_lines(markdown_body)) {
    const auto trimmed = trim_copy(trim_right_copy(raw_line));
    if (trimmed.empty()) {
      previous_nonblank_line.clear();
      continue;
    }

    std::size_t hashes = 0U;
    while (hashes < trimmed.size() && trimmed[hashes] == '#') {
      ++hashes;
    }
    if (hashes > 0U && hashes <= 6U &&
        (hashes == trimmed.size() || trimmed[hashes] == ' ' || trimmed[hashes] == '\t')) {
      auto heading = trim_copy(trimmed.substr(hashes));
      while (!heading.empty() && heading.back() == '#') {
        heading.pop_back();
      }
      heading = trim_copy(heading);
      if (!heading.empty()) {
        return heading;
      }
    }

    if (!previous_nonblank_line.empty() && is_setext_heading_underline(trimmed)) {
      return trim_copy(previous_nonblank_line);
    }
    previous_nonblank_line = trimmed;
  }
  return std::nullopt;
}

[[nodiscard]] std::string default_document_class_for(const SourceRecord& source) {
  if (source.corpus_id == "architecture_reference") {
    return "architecture";
  }
  if (source.corpus_id == "adr_normative") {
    return "adr";
  }
  if (source.corpus_id == "ssot_normative") {
    return "ssot";
  }
  if (source.corpus_id == "profile_policy_normative") {
    return "runtime_policy";
  }
  return source.corpus_id;
}

[[nodiscard]] std::string source_path_stem(const std::filesystem::path& source_path) {
  return trim_copy(source_path.stem().string());
}

[[nodiscard]] int indentation_width(std::string_view line) {
  int width = 0;
  for (const char character : line) {
    if (character == ' ') {
      ++width;
      continue;
    }
    if (character == '\t') {
      width += 4;
      continue;
    }
    break;
  }
  return width;
}

[[nodiscard]] bool path_less(std::string_view left, std::string_view right) {
  std::size_t left_index = 0U;
  std::size_t right_index = 0U;
  while (left_index < left.size() && right_index < right.size()) {
    if (left[left_index] == '[' && right[right_index] == '[') {
      const auto left_end = left.find(']', left_index);
      const auto right_end = right.find(']', right_index);
      if (left_end != std::string_view::npos && right_end != std::string_view::npos) {
        int left_value = 0;
        int right_value = 0;
        std::from_chars(left.data() + left_index + 1U, left.data() + left_end, left_value);
        std::from_chars(right.data() + right_index + 1U, right.data() + right_end, right_value);
        if (left_value != right_value) {
          return left_value < right_value;
        }
        left_index = left_end + 1U;
        right_index = right_end + 1U;
        continue;
      }
    }
    if (left[left_index] != right[right_index]) {
      return left[left_index] < right[right_index];
    }
    ++left_index;
    ++right_index;
  }
  return left.size() < right.size();
}

[[nodiscard]] std::optional<std::vector<std::pair<std::string, std::string>>> flatten_yaml_entries(
    std::string_view raw_content) {
  std::vector<std::pair<std::string, std::string>> entries;
  std::vector<std::pair<int, std::string>> path_stack;
  std::map<std::string, int> sequence_indexes;

  for (const auto& raw_line : split_lines(raw_content)) {
    const auto line = trim_right_copy(raw_line);
    const auto trimmed = trim_copy(line);
    if (trimmed.empty() || trimmed.rfind("#", 0) == 0) {
      continue;
    }

    const auto indent = indentation_width(line);
    while (!path_stack.empty() && indent <= path_stack.back().first) {
      path_stack.pop_back();
    }

    if (trimmed.rfind("- ", 0) == 0) {
      if (path_stack.empty()) {
        return std::nullopt;
      }

      const auto value = strip_wrapping_quotes(trim_copy(trimmed.substr(2U)));
      if (value.empty()) {
        return std::nullopt;
      }

      std::string path;
      for (std::size_t index = 0U; index < path_stack.size(); ++index) {
        if (index > 0U) {
          path.push_back('.');
        }
        path.append(path_stack[index].second);
      }

      auto& sequence_index = sequence_indexes[path];
      path.append("[");
      path.append(std::to_string(sequence_index++));
      path.append("]");
      entries.emplace_back(std::move(path), value);
      continue;
    }

    const auto colon = trimmed.find(':');
    if (colon == std::string::npos) {
      return std::nullopt;
    }

    const auto key = trim_copy(trimmed.substr(0U, colon));
    if (key.empty()) {
      return std::nullopt;
    }
    const auto value = trim_copy(trimmed.substr(colon + 1U));

    std::string path;
    for (std::size_t index = 0U; index < path_stack.size(); ++index) {
      if (index > 0U) {
        path.push_back('.');
      }
      path.append(path_stack[index].second);
    }
    if (!path.empty()) {
      path.push_back('.');
    }
    path.append(key);

    if (value.empty()) {
      path_stack.emplace_back(indent, key);
      continue;
    }

    entries.emplace_back(std::move(path), strip_wrapping_quotes(value));
  }

  std::sort(entries.begin(), entries.end(), [](const auto& left, const auto& right) {
    return path_less(left.first, right.first);
  });
  return entries;
}

[[nodiscard]] std::map<std::string, std::string> parse_flattened_lines(std::string_view canonical_text) {
  std::map<std::string, std::string> values;
  for (const auto& line : split_lines(canonical_text)) {
    const auto delimiter = line.find('=');
    if (delimiter == std::string::npos) {
      continue;
    }
    values.emplace(line.substr(0U, delimiter), line.substr(delimiter + 1U));
  }
  return values;
}

[[nodiscard]] std::string canonical_markdown_body(std::string_view raw_content) {
  const auto normalized_content = normalize_text(raw_content);
  const auto front_matter = parse_front_matter(normalized_content);

  std::vector<std::string> lines;
  for (const auto& raw_line : split_lines(front_matter.body)) {
    lines.push_back(trim_right_copy(raw_line));
  }
  while (!lines.empty() && trim_copy(lines.front()).empty()) {
    lines.erase(lines.begin());
  }
  while (!lines.empty() && trim_copy(lines.back()).empty()) {
    lines.pop_back();
  }
  return join_lines(lines);
}

[[nodiscard]] std::optional<std::int64_t> parse_optional_int64(const std::string& value) {
  if (value.empty()) {
    return std::nullopt;
  }

  std::int64_t parsed_value = 0;
  const auto result = std::from_chars(value.data(), value.data() + value.size(), parsed_value);
  if (result.ec != std::errc() || result.ptr != value.data() + value.size()) {
    return std::nullopt;
  }
  return parsed_value;
}

}  // namespace

bool CanonicalDocument::has_consistent_values() const {
  if (document_id.rfind(kDocIdPrefix, 0) != 0 || !is_sha256_hex(document_id.substr(4U)) ||
      corpus_id.empty() || source_id.empty() || source_uri.empty() || title.empty() ||
      canonical_text.empty() || !is_sha256_hex(source_hash) || version.empty() ||
      updated_at_ms < 0 || language.empty() ||
      !dasall::knowledge::detail::has_unique_values(tags)) {
    return false;
  }

  if (!metadata.contains("document_class") || !metadata.contains("section_path")) {
    return false;
  }

  if (source_format == SourceFormat::Yaml &&
      (!metadata.contains("profile_name") || !metadata.contains("policy_domain") ||
       metadata.at("policy_domain") != "runtime_policy")) {
    return false;
  }

  return true;
}

bool CanonicalizeResult::has_consistent_values() const {
  if (!dasall::knowledge::detail::has_unique_values(warnings)) {
    return false;
  }

  if (ok) {
    return document.has_value() && document->has_consistent_values() && !quarantine_reason.has_value();
  }
  return !document.has_value() && quarantine_reason.has_value() && !quarantine_reason->empty();
}

Canonicalizer::Canonicalizer(CanonicalizerPolicy policy)
    : policy_(std::move(policy)) {}

CanonicalizeResult Canonicalizer::canonicalize(const SourceRecord& source) const {
  if (!source.has_consistent_values()) {
    return {.ok = false,
            .document = std::nullopt,
            .warnings = {},
            .quarantine_reason = std::string(kQuarantineSourceRecordInconsistent)};
  }

  const auto raw_content = read_source_text(source);
  if (!raw_content.has_value()) {
    return {.ok = false,
            .document = std::nullopt,
            .warnings = {},
            .quarantine_reason = std::string(kQuarantineSourceReadFailed)};
  }

  const auto source_path = resolve_source_path(source);
  const auto normalized_content = normalize_text(*raw_content);

  std::string canonical_text;
  if (source.format == SourceFormat::Markdown) {
    canonical_text = normalize_markup(normalized_content);
  } else if (source.format == SourceFormat::Yaml) {
    const auto yaml_canonical_text = canonicalize_yaml(normalized_content);
    if (!yaml_canonical_text.has_value()) {
      return {.ok = false,
              .document = std::nullopt,
              .warnings = {},
              .quarantine_reason = std::string(kQuarantineYamlFlattenFailed)};
    }
    canonical_text = *yaml_canonical_text;
  } else {
    return {.ok = false,
            .document = std::nullopt,
            .warnings = {},
            .quarantine_reason = std::string(kQuarantineSourceFormatUnsupported)};
  }

  if (trim_copy(canonical_text).empty()) {
    return {.ok = false,
            .document = std::nullopt,
            .warnings = {},
            .quarantine_reason = std::string(kQuarantineCanonicalTextEmpty)};
  }

  std::vector<std::string> warnings;
  auto metadata = extract_metadata(normalized_content, source, source_path, warnings);
  const auto markdown_front_matter =
      source.format == SourceFormat::Markdown ? parse_front_matter(normalized_content) : FrontMatterParseResult{};
  const auto first_heading = source.format == SourceFormat::Markdown
                                 ? extract_first_heading(canonical_text)
                                 : std::optional<std::string>{};
  const auto yaml_entries =
      source.format == SourceFormat::Yaml ? parse_flattened_lines(canonical_text)
                                          : std::map<std::string, std::string>{};

  std::string title;
  if (source.format == SourceFormat::Markdown) {
    if (const auto title_it = markdown_front_matter.values.find("title");
        title_it != markdown_front_matter.values.end() && !trim_copy(title_it->second).empty()) {
      title = trim_copy(title_it->second);
    } else if (first_heading.has_value()) {
      title = *first_heading;
      append_unique(warnings, std::string(kWarningTitleFallbackHeading));
    } else {
      title = source_path_stem(source_path);
      append_unique(warnings, std::string(kWarningTitleFallbackFilename));
    }
  } else {
    if (const auto profile_name_it = metadata.find("profile_name");
        profile_name_it != metadata.end() && !trim_copy(profile_name_it->second).empty()) {
      title = profile_name_it->second + " runtime policy";
    } else {
      title = source_path_stem(source_path);
      append_unique(warnings, std::string(kWarningTitleFallbackFilename));
    }
  }

  std::string language = source.language.empty() ? std::string("und") : source.language;
  if (source.format == SourceFormat::Markdown) {
    if (const auto language_it = markdown_front_matter.values.find("language");
        language_it != markdown_front_matter.values.end() && !trim_copy(language_it->second).empty()) {
      language = trim_copy(language_it->second);
    } else if (const auto lang_it = markdown_front_matter.values.find("lang");
               lang_it != markdown_front_matter.values.end() && !trim_copy(lang_it->second).empty()) {
      language = trim_copy(lang_it->second);
    }
  }
  if (language.empty() || language == "und") {
    language = "und";
    append_unique(warnings, std::string(kWarningLanguageFallbackUnd));
  }

  auto tags = source.tags;
  if (source.format == SourceFormat::Markdown) {
    if (const auto tags_it = markdown_front_matter.values.find("tags");
        tags_it != markdown_front_matter.values.end()) {
      for (const auto& tag : parse_tags(tags_it->second)) {
        append_unique(tags, tag);
      }
    }
  }
  std::sort(tags.begin(), tags.end());

  std::string version = std::string("sha256:") + sha256_hex(canonical_text);
  if (source.format == SourceFormat::Markdown) {
    if (const auto version_it = markdown_front_matter.values.find("version");
        version_it != markdown_front_matter.values.end() && !trim_copy(version_it->second).empty()) {
      version = trim_copy(version_it->second);
    }
  } else if (const auto version_it = yaml_entries.find("version");
             version_it != yaml_entries.end() && !trim_copy(version_it->second).empty()) {
    version = trim_copy(version_it->second);
  }

  std::int64_t updated_at_ms = source.updated_at_ms;
  if (source.format == SourceFormat::Markdown) {
    if (const auto updated_at_it = markdown_front_matter.values.find("updated_at_ms");
        updated_at_it != markdown_front_matter.values.end()) {
      if (const auto parsed = parse_optional_int64(trim_copy(updated_at_it->second));
          parsed.has_value()) {
        updated_at_ms = *parsed;
      }
    }
  } else if (const auto updated_at_it = yaml_entries.find("updated_at_ms");
             updated_at_it != yaml_entries.end()) {
    if (const auto parsed = parse_optional_int64(trim_copy(updated_at_it->second));
        parsed.has_value()) {
      updated_at_ms = *parsed;
    }
  }

  if (!metadata.contains("document_class") || !metadata.contains("section_path") ||
      (source.format == SourceFormat::Yaml &&
       (!metadata.contains("profile_name") || !metadata.contains("policy_domain") ||
        metadata.at("policy_domain") != "runtime_policy"))) {
    std::sort(warnings.begin(), warnings.end());
    warnings.erase(std::unique(warnings.begin(), warnings.end()), warnings.end());
    return {.ok = false,
            .document = std::nullopt,
            .warnings = warnings,
            .quarantine_reason = std::string(kQuarantineMetadataMissing)};
  }

  std::sort(warnings.begin(), warnings.end());
  warnings.erase(std::unique(warnings.begin(), warnings.end()), warnings.end());

  CanonicalDocument document;
  document.document_id = build_document_id(source, version, canonical_text);
  document.corpus_id = source.corpus_id;
  document.source_id = source.source_id;
  document.source_uri = source.source_uri;
  document.title = title;
  document.canonical_text = canonical_text;
  document.source_hash = source.content_hash;
  document.version = version;
  document.updated_at_ms = updated_at_ms;
  document.source_format = source.format;
  document.authority_level = source.authority_level;
  document.language = language;
  document.tags = tags;
  document.metadata = std::move(metadata);

  if (!document.has_consistent_values()) {
    return {.ok = false,
            .document = std::nullopt,
            .warnings = warnings,
            .quarantine_reason = std::string(kQuarantineMetadataMissing)};
  }

  return {.ok = true,
          .document = std::move(document),
          .warnings = warnings,
          .quarantine_reason = std::nullopt};
}

std::string Canonicalizer::normalize_markup(std::string_view raw_content) const {
  return canonical_markdown_body(raw_content);
}

std::map<std::string, std::string> Canonicalizer::extract_metadata(
    std::string_view normalized_content,
    const SourceRecord& source,
    const std::filesystem::path& source_path,
    std::vector<std::string>& warnings) const {
  std::map<std::string, std::string> metadata;

  if (source.format == SourceFormat::Markdown) {
    const auto front_matter = parse_front_matter(normalized_content);
    for (const auto& [key, value] : front_matter.values) {
      if (key == "title" || key == "language" || key == "lang" || key == "tags" ||
          key == "version" || key == "updated_at_ms") {
        continue;
      }
      if (!trim_copy(value).empty()) {
        metadata.emplace(key, trim_copy(value));
      }
    }

    if (!metadata.contains("document_class")) {
      metadata["document_class"] = default_document_class_for(source);
      append_unique(warnings, std::string(kWarningDocumentClassFallbackCorpus));
    }
    if (!metadata.contains("section_path")) {
      if (const auto heading = extract_first_heading(canonical_markdown_body(normalized_content));
          heading.has_value()) {
        metadata["section_path"] = *heading;
        append_unique(warnings, std::string(kWarningSectionPathFallbackHeading));
      } else {
        metadata["section_path"] = source.source_uri;
        append_unique(warnings, std::string(kWarningSectionPathFallbackSourceUri));
      }
    }
    return metadata;
  }

  const auto canonical_yaml = canonicalize_yaml(normalized_content);
  const auto yaml_entries = parse_flattened_lines(canonical_yaml.value_or(""));
  metadata["document_class"] = "runtime_policy";
  if (const auto profile_name_it = yaml_entries.find("profile_meta.profile_id");
      profile_name_it != yaml_entries.end() && !trim_copy(profile_name_it->second).empty()) {
    metadata["profile_name"] = trim_copy(profile_name_it->second);
  } else {
    const auto profile_name = source_path.parent_path().filename().string();
    if (!trim_copy(profile_name).empty()) {
      metadata["profile_name"] = profile_name;
      append_unique(warnings, std::string(kWarningProfileNameFallbackSourcePath));
    }
  }
  metadata["policy_domain"] = "runtime_policy";
  if (metadata.contains("profile_name")) {
    metadata["section_path"] = metadata["profile_name"] + "/runtime_policy";
  } else {
    metadata["section_path"] = source.source_uri;
    append_unique(warnings, std::string(kWarningSectionPathFallbackSourceUri));
  }
  if (const auto schema_version_it = yaml_entries.find("schema_version");
      schema_version_it != yaml_entries.end()) {
    metadata["schema_version"] = schema_version_it->second;
  }
  return metadata;
}

std::optional<std::string> Canonicalizer::canonicalize_yaml(std::string_view raw_content) const {
  const auto entries = flatten_yaml_entries(normalize_text(raw_content));
  if (!entries.has_value() || entries->empty()) {
    return std::nullopt;
  }

  std::ostringstream builder;
  for (std::size_t index = 0U; index < entries->size(); ++index) {
    if (index > 0U) {
      builder << '\n';
    }
    builder << (*entries)[index].first << '=' << (*entries)[index].second;
  }
  return builder.str();
}

std::optional<std::string> Canonicalizer::read_source_text(const SourceRecord& source) const {
  std::ifstream input(resolve_source_path(source), std::ios::binary);
  if (!input.is_open()) {
    return std::nullopt;
  }

  const std::string content((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
  if (input.bad()) {
    return std::nullopt;
  }
  return content;
}

std::filesystem::path Canonicalizer::resolve_source_path(const SourceRecord& source) const {
  std::filesystem::path source_path(source.source_uri);
  if (source_path.is_absolute()) {
    return source_path.lexically_normal();
  }

  const auto repository_root =
      policy_.repository_root.empty() ? std::filesystem::current_path() : policy_.repository_root;
  return (repository_root / source_path).lexically_normal();
}

std::string Canonicalizer::build_document_id(const SourceRecord& source,
                                             std::string_view version,
                                             std::string_view canonical_text) const {
  std::string document_seed;
  document_seed.reserve(source.corpus_id.size() + source.source_uri.size() + version.size() +
                        canonical_text.size() + 3U);
  document_seed.append(source.corpus_id);
  document_seed.push_back('\n');
  document_seed.append(source.source_uri);
  document_seed.push_back('\n');
  document_seed.append(version);
  document_seed.push_back('\n');
  document_seed.append(canonical_text);
  return std::string(kDocIdPrefix) + sha256_hex(document_seed);
}

}  // namespace dasall::knowledge::ingest