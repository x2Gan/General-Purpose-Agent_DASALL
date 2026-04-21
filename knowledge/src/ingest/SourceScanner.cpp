#include "ingest/SourceScanner.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <cstdint>
#include <fstream>
#include <map>
#include <regex>
#include <set>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace dasall::knowledge::ingest {

namespace {

constexpr std::string_view kMarkdownExtension = ".md";
constexpr std::string_view kMarkdownAltExtension = ".markdown";
constexpr std::string_view kYamlExtension = ".yaml";
constexpr std::string_view kYamlAltExtension = ".yml";
constexpr std::string_view kTextExtension = ".txt";
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

[[nodiscard]] bool has_unique_formats(const std::vector<SourceFormat>& formats) {
  std::set<SourceFormat> unique_formats(formats.begin(), formats.end());
  return unique_formats.size() == formats.size();
}

[[nodiscard]] bool is_sha256_hex(std::string_view value) {
  return value.size() == 64U &&
         std::all_of(value.begin(), value.end(), [](unsigned char character) {
           return std::isxdigit(character) != 0;
         });
}

[[nodiscard]] bool is_allowed_format(SourceFormat format,
                                     const std::vector<SourceFormat>& allowed_formats) {
  return std::find(allowed_formats.begin(), allowed_formats.end(), format) != allowed_formats.end();
}

[[nodiscard]] std::int64_t current_time_ms() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
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

[[nodiscard]] bool has_unique_ids(const std::vector<std::string>& ids) {
  std::set<std::string, std::less<>> unique_ids(ids.begin(), ids.end());
  return unique_ids.size() == ids.size();
}

[[nodiscard]] bool has_unique_source_ids(const std::vector<SourceRecord>& records) {
  std::set<std::string, std::less<>> unique_source_ids;
  for (const auto& record : records) {
    if (!unique_source_ids.insert(record.source_id).second) {
      return false;
    }
  }

  return true;
}

[[nodiscard]] std::string glob_to_regex(std::string_view pattern) {
  std::string regex_pattern;
  regex_pattern.reserve(pattern.size() * 2U + 2U);
  regex_pattern.push_back('^');

  for (const char character : pattern) {
    switch (character) {
      case '*':
        regex_pattern += ".*";
        break;
      case '?':
        regex_pattern.push_back('.');
        break;
      case '.':
      case '^':
      case '$':
      case '|':
      case '(': 
      case ')':
      case '[':
      case ']':
      case '{':
      case '}':
      case '+':
      case '\\':
        regex_pattern.push_back('\\');
        regex_pattern.push_back(character);
        break;
      default:
        regex_pattern.push_back(character);
        break;
    }
  }

  regex_pattern.push_back('$');
  return regex_pattern;
}

template <typename RecordRange>
void sort_records_by_source_id(RecordRange& records) {
  std::sort(records.begin(), records.end(), [](const SourceRecord& left, const SourceRecord& right) {
    return left.source_id < right.source_id;
  });
}

}  // namespace

bool CorpusScanPlan::has_consistent_values() const {
  return !corpus_id.empty() && !root_uri.empty() && !include_globs.empty() &&
         !allowed_formats.empty() && detail::has_unique_values(include_globs) &&
         detail::has_unique_values(exclude_globs) && has_unique_formats(allowed_formats);
}

bool SourceRecord::has_consistent_values() const {
  return !source_id.empty() && !corpus_id.empty() && !source_uri.empty() &&
         is_sha256_hex(content_hash) && version == std::string("sha256:") + content_hash &&
         updated_at_ms >= 0 && !language.empty() && detail::has_unique_values(tags);
}

bool SourceScanDelta::has_consistent_values() const {
  if (!std::all_of(added.begin(), added.end(), [](const SourceRecord& record) {
        return record.has_consistent_values();
      }) ||
      !std::all_of(updated.begin(), updated.end(), [](const SourceRecord& record) {
        return record.has_consistent_values();
      })) {
    return false;
  }

  if (!has_unique_source_ids(added) || !has_unique_source_ids(updated) ||
      !has_unique_ids(removed_source_ids) || !has_unique_ids(quarantined_source_ids)) {
    return false;
  }

  std::set<std::string, std::less<>> added_ids;
  for (const auto& record : added) {
    added_ids.insert(record.source_id);
  }

  for (const auto& record : updated) {
    if (added_ids.contains(record.source_id)) {
      return false;
    }
  }

  return true;
}

SourceScanner::SourceScanner(SourceScannerDeps deps)
    : deps_(std::move(deps)) {}

SourceScanDelta SourceScanner::scan(const CorpusScanPlan& plan) const {
  SourceScanDelta delta;
  delta.full_scan = plan.full_scan;

  if (!plan.has_consistent_values()) {
    delta.quarantined_source_ids.push_back(std::string(kCorpusQuarantinePrefix) + plan.corpus_id);
    return delta;
  }

  const auto descriptor = deps_.lookup_corpus ? deps_.lookup_corpus(plan.corpus_id) : std::nullopt;
  if (!descriptor.has_value() || !descriptor->has_consistent_values() ||
      descriptor->trust_level != TrustLevel::Trusted) {
    delta.full_scan = true;
    delta.quarantined_source_ids.push_back(std::string(kCorpusQuarantinePrefix) + plan.corpus_id);
    return delta;
  }

  const auto root_path = resolve_root(plan.root_uri);
  const auto descriptor_root_path = resolve_root(descriptor->source_uri);
  const bool descriptor_allows_plan_formats =
      std::all_of(plan.allowed_formats.begin(), plan.allowed_formats.end(),
                  [&descriptor](SourceFormat format) {
                    return is_allowed_format(format, descriptor->allowed_formats);
                  });
  if (plan.source_kind != descriptor->source_kind || root_path != descriptor_root_path ||
      !descriptor_allows_plan_formats) {
    delta.full_scan = true;
    delta.quarantined_source_ids.push_back(std::string(kCorpusQuarantinePrefix) + plan.corpus_id);
    return delta;
  }

  auto previous_inventory =
      deps_.load_inventory ? deps_.load_inventory(plan.corpus_id) : std::vector<SourceRecord>{};
  if (!std::all_of(previous_inventory.begin(), previous_inventory.end(),
                   [](const SourceRecord& record) { return record.has_consistent_values(); }) ||
      !has_unique_source_ids(previous_inventory)) {
    previous_inventory.clear();
  }

  delta.full_scan = delta.full_scan || previous_inventory.empty();

  std::error_code filesystem_error;
  if (!std::filesystem::exists(root_path, filesystem_error) || filesystem_error ||
      !std::filesystem::is_directory(root_path, filesystem_error) || filesystem_error) {
    delta.quarantined_source_ids.push_back(std::string(kCorpusQuarantinePrefix) + plan.corpus_id);
    return delta;
  }

  std::vector<std::filesystem::path> candidate_paths;
  for (std::filesystem::recursive_directory_iterator iterator(root_path, filesystem_error), end;
       iterator != end && !filesystem_error;
       iterator.increment(filesystem_error)) {
    if (!iterator->is_regular_file(filesystem_error) || filesystem_error) {
      continue;
    }

    std::error_code relative_error;
    const auto relative_path = std::filesystem::relative(iterator->path(), root_path, relative_error);
    const auto relative_path_string =
        (relative_error ? iterator->path().lexically_normal() : relative_path.lexically_normal())
            .generic_string();

    const bool matches_include = std::any_of(
        plan.include_globs.begin(), plan.include_globs.end(),
        [this, &relative_path_string](const std::string& pattern) {
          return is_glob_match(relative_path_string, pattern);
        });
    if (!matches_include) {
      continue;
    }

    const bool matches_exclude = std::any_of(
        plan.exclude_globs.begin(), plan.exclude_globs.end(),
        [this, &relative_path_string](const std::string& pattern) {
          return is_glob_match(relative_path_string, pattern);
        });
    if (matches_exclude) {
      continue;
    }

    candidate_paths.push_back(iterator->path().lexically_normal());
  }

  std::sort(candidate_paths.begin(), candidate_paths.end(),
            [](const std::filesystem::path& left, const std::filesystem::path& right) {
              return left.generic_string() < right.generic_string();
            });

  std::map<std::string, SourceRecord, std::less<>> current_records;
  for (const auto& path : candidate_paths) {
    const auto source_uri = make_relative_source_uri(path);
    const auto source_id = make_source_id(plan.corpus_id, source_uri);
    const auto detected_format = detect_format(path);
    if (!detected_format.has_value() || !is_allowed_format(*detected_format, plan.allowed_formats)) {
      delta.quarantined_source_ids.push_back(source_id);
      continue;
    }

    auto record = build_source_record(path, *descriptor);
    if (!record.has_value()) {
      delta.quarantined_source_ids.push_back(source_id);
      continue;
    }

    if (!current_records.emplace(record->source_id, *record).second) {
      delta.quarantined_source_ids.push_back(record->source_id);
    }
  }

  std::map<std::string, SourceRecord, std::less<>> previous_records;
  for (const auto& record : previous_inventory) {
    previous_records.emplace(record.source_id, record);
  }

  for (const auto& [source_id, record] : current_records) {
    const auto previous_it = previous_records.find(source_id);
    if (previous_it == previous_records.end()) {
      delta.added.push_back(record);
      continue;
    }

    const auto& previous = previous_it->second;
    if (previous.content_hash != record.content_hash || previous.version != record.version ||
        previous.updated_at_ms != record.updated_at_ms) {
      delta.updated.push_back(record);
    }
  }

  for (const auto& [source_id, previous_record] : previous_records) {
    static_cast<void>(previous_record);
    if (!current_records.contains(source_id)) {
      delta.removed_source_ids.push_back(source_id);
    }
  }

  sort_records_by_source_id(delta.added);
  sort_records_by_source_id(delta.updated);
  std::sort(delta.removed_source_ids.begin(), delta.removed_source_ids.end());
  std::sort(delta.quarantined_source_ids.begin(), delta.quarantined_source_ids.end());
  delta.quarantined_source_ids.erase(
      std::unique(delta.quarantined_source_ids.begin(), delta.quarantined_source_ids.end()),
      delta.quarantined_source_ids.end());
  return delta;
}

std::string SourceScanner::compute_source_hash(std::string_view content) const {
  return sha256_hex(content);
}

std::optional<SourceFormat> SourceScanner::detect_format(const std::filesystem::path& path) const {
  std::string extension = path.extension().string();
  std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char character) {
    return static_cast<char>(std::tolower(character));
  });

  if (extension == kMarkdownExtension || extension == kMarkdownAltExtension) {
    return SourceFormat::Markdown;
  }

  if (extension == kYamlExtension || extension == kYamlAltExtension) {
    return SourceFormat::Yaml;
  }

  if (extension == kTextExtension) {
    return SourceFormat::Text;
  }

  return std::nullopt;
}

std::optional<SourceRecord> SourceScanner::build_source_record(
    const std::filesystem::path& path,
    const CorpusDescriptor& descriptor) const {
  const auto format = detect_format(path);
  if (!format.has_value()) {
    return std::nullopt;
  }

  std::ifstream input(path, std::ios::binary);
  if (!input.is_open()) {
    return std::nullopt;
  }

  const std::string content((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
  if (input.bad()) {
    return std::nullopt;
  }

  const auto source_uri = make_relative_source_uri(path);
  const auto content_hash = compute_source_hash(content);
  const auto updated_at_ms = resolve_updated_at_ms(path);
  if (!is_sha256_hex(content_hash) || updated_at_ms < 0) {
    return std::nullopt;
  }

  SourceRecord record;
  record.source_id = make_source_id(descriptor.corpus_id, source_uri);
  record.corpus_id = descriptor.corpus_id;
  record.source_uri = source_uri;
  record.content_hash = content_hash;
  record.version = std::string("sha256:") + content_hash;
  record.updated_at_ms = updated_at_ms;
  record.kind = descriptor.source_kind;
  record.format = *format;
  record.authority_level = descriptor.authority_level;
  record.language = descriptor.metadata.contains("default_language") &&
                            !descriptor.metadata.at("default_language").empty()
                        ? descriptor.metadata.at("default_language")
                        : std::string("und");
  record.tags = descriptor.tags;
  return record.has_consistent_values() ? std::optional<SourceRecord>(std::move(record))
                                        : std::nullopt;
}

std::filesystem::path SourceScanner::resolve_root(const std::string& root_uri) const {
  std::filesystem::path root_path(root_uri);
  if (root_path.is_absolute()) {
    return root_path.lexically_normal();
  }

  const auto repo_root = deps_.repository_root ? deps_.repository_root() : std::filesystem::current_path();
  return (repo_root / root_path).lexically_normal();
}

std::int64_t SourceScanner::resolve_updated_at_ms(const std::filesystem::path& path) const {
  std::error_code filesystem_error;
  const auto write_time = std::filesystem::last_write_time(path, filesystem_error);
  if (!filesystem_error) {
    const auto system_time = std::chrono::time_point_cast<std::chrono::milliseconds>(
        write_time - std::filesystem::file_time_type::clock::now() + std::chrono::system_clock::now());
    return system_time.time_since_epoch().count();
  }

  return deps_.now_ms ? deps_.now_ms() : current_time_ms();
}

std::string SourceScanner::make_relative_source_uri(const std::filesystem::path& path) const {
  const auto normalized_path = path.lexically_normal();
  const auto repo_root = deps_.repository_root ? deps_.repository_root() : std::filesystem::current_path();

  std::error_code relative_error;
  const auto relative_path = std::filesystem::relative(normalized_path, repo_root, relative_error);
  if (!relative_error && !relative_path.empty() && relative_path.native().find("..") != 0U) {
    return relative_path.generic_string();
  }

  return normalized_path.generic_string();
}

std::string SourceScanner::make_source_id(std::string_view corpus_id,
                                          std::string_view source_uri) const {
  std::string source_id;
  source_id.reserve(corpus_id.size() + source_uri.size() + 2U);
  source_id.append(corpus_id);
  source_id.append("::");
  source_id.append(source_uri);
  return source_id;
}

bool SourceScanner::is_glob_match(std::string_view candidate, std::string_view pattern) const {
  try {
    return std::regex_match(candidate.begin(), candidate.end(),
                            std::regex(glob_to_regex(pattern), std::regex::ECMAScript));
  } catch (const std::regex_error&) {
    return false;
  }
}

}  // namespace dasall::knowledge::ingest