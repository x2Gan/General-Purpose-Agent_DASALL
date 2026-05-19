#include "index/CorpusCatalog.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iomanip>
#include <map>
#include <set>
#include <sstream>
#include <utility>

namespace dasall::knowledge::index {

namespace {

constexpr int kCatalogFormatVersion = 1;

using ScalarFields = std::map<std::string, std::string, std::less<>>;

[[nodiscard]] bool descriptor_list_is_consistent(
    const std::vector<CorpusDescriptor>& descriptors) {
  std::set<std::string> corpus_ids;
  std::set<std::string> source_uris;

  for (const auto& descriptor : descriptors) {
    if (!descriptor.has_consistent_values()) {
      return false;
    }

    if (!corpus_ids.insert(descriptor.corpus_id).second) {
      return false;
    }

    if (!source_uris.insert(descriptor.source_uri).second) {
      return false;
    }
  }

  return true;
}

[[nodiscard]] bool descriptor_matches_all_tags(const CorpusDescriptor& descriptor,
                                               const std::vector<std::string>& tags) {
  return std::all_of(tags.begin(), tags.end(), [&descriptor](const std::string& tag) {
    return std::find(descriptor.tags.begin(), descriptor.tags.end(), tag) != descriptor.tags.end();
  });
}

[[nodiscard]] bool descriptor_supports_mode(const CorpusDescriptor& descriptor,
                                            RetrievalMode mode) {
  return std::find(descriptor.supported_modes.begin(), descriptor.supported_modes.end(), mode) !=
         descriptor.supported_modes.end();
}

[[nodiscard]] std::string fnv1a_hex(std::string_view content) {
  constexpr std::uint64_t kOffsetBasis = 14695981039346656037ULL;
  constexpr std::uint64_t kPrime = 1099511628211ULL;

  std::uint64_t hash = kOffsetBasis;
  for (const char character : content) {
    hash ^= static_cast<unsigned char>(character);
    hash *= kPrime;
  }

  std::ostringstream output;
  output << std::hex << std::setw(16) << std::setfill('0') << hash;
  return output.str();
}

[[nodiscard]] std::string escape_json_string(std::string_view value) {
  std::string escaped;
  escaped.reserve(value.size() + 8U);
  for (const char character : value) {
    switch (character) {
      case '\\':
        escaped += "\\\\";
        break;
      case '"':
        escaped += "\\\"";
        break;
      case '\n':
        escaped += "\\n";
        break;
      case '\r':
        escaped += "\\r";
        break;
      case '\t':
        escaped += "\\t";
        break;
      default:
        escaped.push_back(character);
        break;
    }
  }
  return escaped;
}

void skip_whitespace(std::string_view text, std::size_t& offset) {
  while (offset < text.size() &&
         std::isspace(static_cast<unsigned char>(text[offset])) != 0) {
    ++offset;
  }
}

bool parse_quoted_string(std::string_view text,
                         std::size_t& offset,
                         std::string& value) {
  if (offset >= text.size() || text[offset] != '"') {
    return false;
  }

  ++offset;
  value.clear();
  while (offset < text.size()) {
    const char character = text[offset++];
    if (character == '"') {
      return true;
    }

    if (character != '\\') {
      value.push_back(character);
      continue;
    }

    if (offset >= text.size()) {
      return false;
    }

    switch (text[offset++]) {
      case '\\':
        value.push_back('\\');
        break;
      case '"':
        value.push_back('"');
        break;
      case 'n':
        value.push_back('\n');
        break;
      case 'r':
        value.push_back('\r');
        break;
      case 't':
        value.push_back('\t');
        break;
      default:
        return false;
    }
  }

  return false;
}

[[nodiscard]] std::optional<ScalarFields> parse_scalar_json_object(std::string_view text) {
  std::size_t offset = 0U;
  skip_whitespace(text, offset);
  if (offset >= text.size() || text[offset] != '{') {
    return std::nullopt;
  }

  ++offset;
  ScalarFields fields;
  skip_whitespace(text, offset);
  if (offset < text.size() && text[offset] == '}') {
    ++offset;
    skip_whitespace(text, offset);
    return offset == text.size() ? std::optional<ScalarFields>(std::move(fields))
                                 : std::nullopt;
  }

  while (offset < text.size()) {
    skip_whitespace(text, offset);

    std::string key;
    if (!parse_quoted_string(text, offset, key)) {
      return std::nullopt;
    }

    skip_whitespace(text, offset);
    if (offset >= text.size() || text[offset] != ':') {
      return std::nullopt;
    }
    ++offset;
    skip_whitespace(text, offset);

    std::string value;
    if (offset < text.size() && text[offset] == '"') {
      if (!parse_quoted_string(text, offset, value)) {
        return std::nullopt;
      }
    } else {
      const auto value_begin = offset;
      while (offset < text.size() && text[offset] != ',' && text[offset] != '}') {
        ++offset;
      }
      const auto value_end = offset;
      while (offset > value_begin &&
             std::isspace(static_cast<unsigned char>(text[offset - 1U])) != 0) {
        --offset;
      }
      value = std::string(text.substr(value_begin, offset - value_begin));
      offset = value_end;
    }

    fields.emplace(std::move(key), std::move(value));
    skip_whitespace(text, offset);
    if (offset >= text.size()) {
      return std::nullopt;
    }

    if (text[offset] == '}') {
      ++offset;
      skip_whitespace(text, offset);
      return offset == text.size() ? std::optional<ScalarFields>(std::move(fields))
                                   : std::nullopt;
    }

    if (text[offset] != ',') {
      return std::nullopt;
    }
    ++offset;
  }

  return std::nullopt;
}

[[nodiscard]] std::optional<std::int64_t> parse_int64_field(const ScalarFields& fields,
                                                            std::string_view key) {
  const auto iterator = fields.find(std::string(key));
  if (iterator == fields.end() || iterator->second.empty()) {
    return std::nullopt;
  }

  try {
    return std::stoll(iterator->second);
  } catch (const std::exception&) {
    return std::nullopt;
  }
}

[[nodiscard]] std::string join_string_list(const std::vector<std::string>& values) {
  std::ostringstream output;
  for (std::size_t index = 0U; index < values.size(); ++index) {
    if (index != 0U) {
      output << '\n';
    }
    output << values[index];
  }
  return output.str();
}

[[nodiscard]] std::vector<std::string> split_string_list(std::string_view raw_values) {
  std::vector<std::string> values;
  if (raw_values.empty()) {
    return values;
  }

  std::size_t cursor = 0U;
  while (cursor <= raw_values.size()) {
    const auto separator = raw_values.find('\n', cursor);
    if (separator == std::string_view::npos) {
      values.emplace_back(raw_values.substr(cursor));
      break;
    }

    values.emplace_back(raw_values.substr(cursor, separator - cursor));
    cursor = separator + 1U;
  }
  return values;
}

[[nodiscard]] std::string trust_level_name(TrustLevel trust_level) {
  switch (trust_level) {
    case TrustLevel::Trusted:
      return "trusted";
    case TrustLevel::Quarantined:
      return "quarantined";
    case TrustLevel::Unregistered:
      return "unregistered";
  }

  return "trusted";
}

[[nodiscard]] std::optional<TrustLevel> parse_trust_level(std::string_view raw_value) {
  if (raw_value == "trusted") {
    return TrustLevel::Trusted;
  }
  if (raw_value == "quarantined") {
    return TrustLevel::Quarantined;
  }
  if (raw_value == "unregistered") {
    return TrustLevel::Unregistered;
  }
  return std::nullopt;
}

[[nodiscard]] std::string authority_level_name(AuthorityLevel authority_level) {
  switch (authority_level) {
    case AuthorityLevel::Normative:
      return "normative";
    case AuthorityLevel::Reference:
      return "reference";
    case AuthorityLevel::Advisory:
      return "advisory";
  }

  return "reference";
}

[[nodiscard]] std::optional<AuthorityLevel> parse_authority_level(std::string_view raw_value) {
  if (raw_value == "normative") {
    return AuthorityLevel::Normative;
  }
  if (raw_value == "reference") {
    return AuthorityLevel::Reference;
  }
  if (raw_value == "advisory") {
    return AuthorityLevel::Advisory;
  }
  return std::nullopt;
}

[[nodiscard]] std::string source_kind_name(SourceKind source_kind) {
  switch (source_kind) {
    case SourceKind::File:
      return "file";
    case SourceKind::ConfigSnapshot:
      return "config_snapshot";
    case SourceKind::CuratedBundle:
      return "curated_bundle";
  }

  return "file";
}

[[nodiscard]] std::optional<SourceKind> parse_source_kind(std::string_view raw_value) {
  if (raw_value == "file") {
    return SourceKind::File;
  }
  if (raw_value == "config_snapshot") {
    return SourceKind::ConfigSnapshot;
  }
  if (raw_value == "curated_bundle") {
    return SourceKind::CuratedBundle;
  }
  return std::nullopt;
}

[[nodiscard]] std::string source_format_name(SourceFormat source_format) {
  switch (source_format) {
    case SourceFormat::Markdown:
      return "markdown";
    case SourceFormat::Yaml:
      return "yaml";
    case SourceFormat::Text:
      return "text";
  }

  return "markdown";
}

[[nodiscard]] std::optional<SourceFormat> parse_source_format(std::string_view raw_value) {
  if (raw_value == "markdown") {
    return SourceFormat::Markdown;
  }
  if (raw_value == "yaml") {
    return SourceFormat::Yaml;
  }
  if (raw_value == "text") {
    return SourceFormat::Text;
  }
  return std::nullopt;
}

[[nodiscard]] std::string retrieval_mode_name(RetrievalMode retrieval_mode) {
  switch (retrieval_mode) {
    case RetrievalMode::LexicalOnly:
      return "lexical_only";
    case RetrievalMode::DenseOnly:
      return "dense_only";
    case RetrievalMode::Hybrid:
      return "hybrid";
  }

  return "lexical_only";
}

[[nodiscard]] std::optional<RetrievalMode> parse_retrieval_mode(std::string_view raw_value) {
  if (raw_value == "lexical_only") {
    return RetrievalMode::LexicalOnly;
  }
  if (raw_value == "dense_only") {
    return RetrievalMode::DenseOnly;
  }
  if (raw_value == "hybrid") {
    return RetrievalMode::Hybrid;
  }
  return std::nullopt;
}

[[nodiscard]] std::string serialize_source_formats(
    const std::vector<SourceFormat>& source_formats) {
  std::vector<std::string> values;
  values.reserve(source_formats.size());
  for (const auto source_format : source_formats) {
    values.push_back(source_format_name(source_format));
  }
  return join_string_list(values);
}

[[nodiscard]] std::optional<std::vector<SourceFormat>> deserialize_source_formats(
    std::string_view raw_value) {
  std::vector<SourceFormat> values;
  for (const auto& token : split_string_list(raw_value)) {
    const auto parsed_value = parse_source_format(token);
    if (!parsed_value.has_value()) {
      return std::nullopt;
    }
    values.push_back(*parsed_value);
  }
  return values;
}

[[nodiscard]] std::string serialize_retrieval_modes(
    const std::vector<RetrievalMode>& retrieval_modes) {
  std::vector<std::string> values;
  values.reserve(retrieval_modes.size());
  for (const auto retrieval_mode : retrieval_modes) {
    values.push_back(retrieval_mode_name(retrieval_mode));
  }
  return join_string_list(values);
}

[[nodiscard]] std::optional<std::vector<RetrievalMode>> deserialize_retrieval_modes(
    std::string_view raw_value) {
  std::vector<RetrievalMode> values;
  for (const auto& token : split_string_list(raw_value)) {
    const auto parsed_value = parse_retrieval_mode(token);
    if (!parsed_value.has_value()) {
      return std::nullopt;
    }
    values.push_back(*parsed_value);
  }
  return values;
}

[[nodiscard]] std::string serialize_metadata(const std::map<std::string, std::string>& metadata) {
  std::ostringstream output;
  bool first_entry = true;
  for (const auto& [key, value] : metadata) {
    if (!first_entry) {
      output << '\n';
    }
    first_entry = false;
    output << key << '\t' << value;
  }
  return output.str();
}

[[nodiscard]] std::optional<std::map<std::string, std::string>> deserialize_metadata(
    std::string_view raw_value) {
  std::map<std::string, std::string> metadata;
  for (const auto& line : split_string_list(raw_value)) {
    if (line.empty()) {
      continue;
    }

    const auto separator = line.find('\t');
    if (separator == std::string::npos) {
      return std::nullopt;
    }

    metadata.emplace(line.substr(0, separator), line.substr(separator + 1U));
  }
  return metadata;
}

[[nodiscard]] std::string serialize_descriptor_line(const CorpusDescriptor& descriptor) {
  std::ostringstream output;
  output << '{'
         << "\"corpus_id\":\"" << escape_json_string(descriptor.corpus_id) << "\","
         << "\"display_name\":\"" << escape_json_string(descriptor.display_name) << "\","
         << "\"source_uri\":\"" << escape_json_string(descriptor.source_uri) << "\","
         << "\"trust_level\":\"" << trust_level_name(descriptor.trust_level) << "\","
         << "\"authority_level\":\"" << authority_level_name(descriptor.authority_level) << "\","
         << "\"source_kind\":\"" << source_kind_name(descriptor.source_kind) << "\","
         << "\"allowed_formats\":\"" << escape_json_string(serialize_source_formats(descriptor.allowed_formats)) << "\","
         << "\"include_globs\":\"" << escape_json_string(join_string_list(descriptor.include_globs)) << "\","
         << "\"exclude_globs\":\"" << escape_json_string(join_string_list(descriptor.exclude_globs)) << "\","
         << "\"supported_modes\":\"" << escape_json_string(serialize_retrieval_modes(descriptor.supported_modes)) << "\","
         << "\"active_snapshot_id\":\"" << escape_json_string(descriptor.active_snapshot_id) << "\","
         << "\"last_updated_ms\":" << descriptor.last_updated_ms << ','
         << "\"tags\":\"" << escape_json_string(join_string_list(descriptor.tags)) << "\","
         << "\"metadata\":\"" << escape_json_string(serialize_metadata(descriptor.metadata)) << "\""
         << '}';
  return output.str();
}

[[nodiscard]] std::optional<CorpusDescriptor> deserialize_descriptor_line(std::string_view line) {
  const auto fields = parse_scalar_json_object(line);
  if (!fields.has_value()) {
    return std::nullopt;
  }

  const auto corpus_iterator = fields->find("corpus_id");
  const auto display_name_iterator = fields->find("display_name");
  const auto source_uri_iterator = fields->find("source_uri");
  const auto trust_level_iterator = fields->find("trust_level");
  const auto authority_level_iterator = fields->find("authority_level");
  const auto source_kind_iterator = fields->find("source_kind");
  const auto allowed_formats_iterator = fields->find("allowed_formats");
  const auto include_globs_iterator = fields->find("include_globs");
  const auto exclude_globs_iterator = fields->find("exclude_globs");
  const auto supported_modes_iterator = fields->find("supported_modes");
  const auto active_snapshot_iterator = fields->find("active_snapshot_id");
  const auto tags_iterator = fields->find("tags");
  const auto metadata_iterator = fields->find("metadata");

  if (corpus_iterator == fields->end() || display_name_iterator == fields->end() ||
      source_uri_iterator == fields->end() || trust_level_iterator == fields->end() ||
      authority_level_iterator == fields->end() || source_kind_iterator == fields->end() ||
      allowed_formats_iterator == fields->end() || include_globs_iterator == fields->end() ||
      exclude_globs_iterator == fields->end() || supported_modes_iterator == fields->end() ||
      active_snapshot_iterator == fields->end() || tags_iterator == fields->end() ||
      metadata_iterator == fields->end()) {
    return std::nullopt;
  }

  CorpusDescriptor descriptor;
  descriptor.corpus_id = corpus_iterator->second;
  descriptor.display_name = display_name_iterator->second;
  descriptor.source_uri = source_uri_iterator->second;
  descriptor.active_snapshot_id = active_snapshot_iterator->second;

  const auto trust_level = parse_trust_level(trust_level_iterator->second);
  const auto authority_level = parse_authority_level(authority_level_iterator->second);
  const auto source_kind = parse_source_kind(source_kind_iterator->second);
  const auto allowed_formats = deserialize_source_formats(allowed_formats_iterator->second);
  const auto supported_modes = deserialize_retrieval_modes(supported_modes_iterator->second);
  const auto last_updated_ms = parse_int64_field(*fields, "last_updated_ms");
  const auto metadata = deserialize_metadata(metadata_iterator->second);
  if (!trust_level.has_value() || !authority_level.has_value() || !source_kind.has_value() ||
      !allowed_formats.has_value() || !supported_modes.has_value() ||
      !last_updated_ms.has_value() || !metadata.has_value()) {
    return std::nullopt;
  }

  descriptor.trust_level = *trust_level;
  descriptor.authority_level = *authority_level;
  descriptor.source_kind = *source_kind;
  descriptor.allowed_formats = *allowed_formats;
  descriptor.include_globs = split_string_list(include_globs_iterator->second);
  descriptor.exclude_globs = split_string_list(exclude_globs_iterator->second);
  descriptor.supported_modes = *supported_modes;
  descriptor.last_updated_ms = *last_updated_ms;
  descriptor.tags = split_string_list(tags_iterator->second);
  descriptor.metadata = *metadata;
  return descriptor.has_consistent_values() ? std::optional<CorpusDescriptor>(std::move(descriptor))
                                            : std::nullopt;
}

[[nodiscard]] CorpusCatalogSnapshot load_snapshot_from_disk(
    const std::filesystem::path& catalog_path) {
  if (catalog_path.empty() || !std::filesystem::exists(catalog_path)) {
    return CorpusCatalogSnapshot();
  }

  std::ifstream input(catalog_path, std::ios::binary);
  if (!input.is_open()) {
    return CorpusCatalogSnapshot();
  }

  std::ostringstream buffer;
  buffer << input.rdbuf();
  const auto file_payload = buffer.str();
  const auto fields = parse_scalar_json_object(file_payload);
  if (!fields.has_value()) {
    return CorpusCatalogSnapshot();
  }

  const auto format_version = parse_int64_field(*fields, "catalog_format_version");
  const auto descriptors_iterator = fields->find("descriptors_jsonl");
  const auto checksum_iterator = fields->find("catalog_checksum");
  if (!format_version.has_value() || *format_version != kCatalogFormatVersion ||
      descriptors_iterator == fields->end() || checksum_iterator == fields->end()) {
    return CorpusCatalogSnapshot();
  }

  const auto descriptors_jsonl = descriptors_iterator->second;
  const auto expected_checksum = fnv1a_hex(std::to_string(kCatalogFormatVersion) + "\n" +
                                           descriptors_jsonl);
  if (checksum_iterator->second != expected_checksum) {
    return CorpusCatalogSnapshot();
  }

  std::vector<CorpusDescriptor> descriptors;
  for (const auto& line : split_string_list(descriptors_jsonl)) {
    if (line.empty()) {
      continue;
    }

    const auto descriptor = deserialize_descriptor_line(line);
    if (!descriptor.has_value()) {
      return CorpusCatalogSnapshot();
    }
    descriptors.push_back(*descriptor);
  }

  CorpusCatalog catalog;
  if (!catalog.replace_all(std::move(descriptors))) {
    return CorpusCatalogSnapshot();
  }

  return catalog.snapshot();
}

}  // namespace

struct CorpusCatalogSnapshot::State {
  std::vector<CorpusDescriptor> descriptors;
  std::map<std::string, std::size_t, std::less<>> descriptors_by_id;
  std::map<std::string, std::size_t, std::less<>> descriptors_by_source_uri;
};

bool CorpusCatalogDelta::has_consistent_values() const {
  std::set<std::string> upserted_ids;
  std::set<std::string> removed_ids;

  for (const auto& descriptor : upserted_descriptors) {
    if (!descriptor.has_consistent_values()) {
      return false;
    }

    if (!upserted_ids.insert(descriptor.corpus_id).second) {
      return false;
    }
  }

  for (const auto& removed_corpus_id : removed_corpus_ids) {
    if (removed_corpus_id.empty()) {
      return false;
    }

    if (!removed_ids.insert(removed_corpus_id).second) {
      return false;
    }

    if (upserted_ids.contains(removed_corpus_id)) {
      return false;
    }
  }

  return true;
}

CorpusCatalogSnapshot::CorpusCatalogSnapshot()
    : state_(std::make_shared<const State>()) {}

CorpusCatalogSnapshot::CorpusCatalogSnapshot(std::shared_ptr<const State> state)
    : state_(state ? std::move(state) : std::make_shared<const State>()) {}

bool CorpusCatalogSnapshot::empty() const {
  return size() == 0U;
}

std::size_t CorpusCatalogSnapshot::size() const {
  return state_->descriptors.size();
}

bool CorpusCatalogSnapshot::has_consistent_values() const {
  return state_ != nullptr && descriptor_list_is_consistent(state_->descriptors) &&
         state_->descriptors.size() == state_->descriptors_by_id.size() &&
         state_->descriptors.size() == state_->descriptors_by_source_uri.size();
}

std::vector<CorpusDescriptor> CorpusCatalogSnapshot::list_all() const {
  return state_->descriptors;
}

std::optional<CorpusDescriptor> CorpusCatalogSnapshot::find_by_id(std::string_view corpus_id) const {
  const auto descriptor_it = state_->descriptors_by_id.find(corpus_id);
  if (descriptor_it == state_->descriptors_by_id.end()) {
    return std::nullopt;
  }

  return state_->descriptors.at(descriptor_it->second);
}

std::vector<CorpusDescriptor> CorpusCatalogSnapshot::filter_by_tags(
    const std::vector<std::string>& tags) const {
  if (tags.empty()) {
    return list_all();
  }

  std::vector<CorpusDescriptor> matches;
  matches.reserve(state_->descriptors.size());
  for (const auto& descriptor : state_->descriptors) {
    if (descriptor_matches_all_tags(descriptor, tags)) {
      matches.push_back(descriptor);
    }
  }

  return matches;
}

std::vector<CorpusDescriptor> CorpusCatalogSnapshot::filter_by_mode(RetrievalMode mode) const {
  std::vector<CorpusDescriptor> matches;
  matches.reserve(state_->descriptors.size());
  for (const auto& descriptor : state_->descriptors) {
    if (descriptor_supports_mode(descriptor, mode)) {
      matches.push_back(descriptor);
    }
  }

  return matches;
}

CorpusCatalog::CorpusCatalog() = default;

CorpusCatalog::CorpusCatalog(CorpusCatalogDeps deps)
    : deps_(std::move(deps)) {
  active_snapshot_ = load_snapshot_from_disk(deps_.catalog_path);
}

CorpusCatalog::CorpusCatalog(CorpusCatalogSnapshot initial_snapshot)
    : active_snapshot_(std::move(initial_snapshot)) {}

CorpusCatalogSnapshot CorpusCatalog::snapshot() const {
  return active_snapshot_;
}

bool CorpusCatalog::replace_all(std::vector<CorpusDescriptor> descriptors) {
  if (!descriptor_list_is_consistent(descriptors)) {
    return false;
  }

  auto state = std::make_shared<CorpusCatalogSnapshot::State>();
  state->descriptors = std::move(descriptors);
  for (std::size_t descriptor_index = 0; descriptor_index < state->descriptors.size();
       ++descriptor_index) {
    const auto& descriptor = state->descriptors[descriptor_index];
    state->descriptors_by_id.emplace(descriptor.corpus_id, descriptor_index);
    state->descriptors_by_source_uri.emplace(descriptor.source_uri, descriptor_index);
  }

  CorpusCatalogSnapshot next_snapshot(
      std::shared_ptr<const CorpusCatalogSnapshot::State>(std::move(state)));
  if (!persist_snapshot(next_snapshot)) {
    return false;
  }

  active_snapshot_ = std::move(next_snapshot);
  return true;
}

bool CorpusCatalog::apply_delta(const CorpusCatalogDelta& delta) {
  if (!delta.has_consistent_values()) {
    return false;
  }

  auto next_descriptors = active_snapshot_.list_all();
  for (const auto& removed_corpus_id : delta.removed_corpus_ids) {
    std::erase_if(next_descriptors, [&removed_corpus_id](const CorpusDescriptor& descriptor) {
      return descriptor.corpus_id == removed_corpus_id;
    });
  }

  for (const auto& upserted_descriptor : delta.upserted_descriptors) {
    const auto existing_descriptor_it = std::find_if(
        next_descriptors.begin(), next_descriptors.end(), [&upserted_descriptor](const CorpusDescriptor& descriptor) {
          return descriptor.corpus_id == upserted_descriptor.corpus_id;
        });

    if (existing_descriptor_it == next_descriptors.end()) {
      next_descriptors.push_back(upserted_descriptor);
      continue;
    }

    *existing_descriptor_it = upserted_descriptor;
  }

  return replace_all(std::move(next_descriptors));
}

bool CorpusCatalog::persist_snapshot(const CorpusCatalogSnapshot& snapshot) const {
  if (deps_.catalog_path.empty()) {
    return true;
  }

  try {
    const auto parent_path = deps_.catalog_path.parent_path();
    if (!parent_path.empty()) {
      std::filesystem::create_directories(parent_path);
    }

    std::string descriptors_jsonl;
    for (const auto& descriptor : snapshot.list_all()) {
      descriptors_jsonl += serialize_descriptor_line(descriptor);
      descriptors_jsonl.push_back('\n');
    }

    const auto checksum =
        fnv1a_hex(std::to_string(kCatalogFormatVersion) + "\n" + descriptors_jsonl);
    const auto content = std::string{"{\"catalog_format_version\":"} +
                         std::to_string(kCatalogFormatVersion) +
                         ",\"descriptors_jsonl\":\"" +
                         escape_json_string(descriptors_jsonl) +
                         "\",\"catalog_checksum\":\"" +
                         escape_json_string(checksum) + "\"}\n";

    auto temporary_path = deps_.catalog_path;
    temporary_path += ".tmp";
    std::ofstream output(temporary_path, std::ios::binary | std::ios::trunc);
    if (!output.is_open()) {
      return false;
    }

    output << content;
    output.close();
    if (!output) {
      std::error_code cleanup_error;
      std::filesystem::remove(temporary_path, cleanup_error);
      return false;
    }

    std::filesystem::rename(temporary_path, deps_.catalog_path);
    return true;
  } catch (const std::exception&) {
    return false;
  }
}

}  // namespace dasall::knowledge::index