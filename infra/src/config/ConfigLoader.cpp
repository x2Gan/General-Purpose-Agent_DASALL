#include "config/ConfigLoader.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "config/ConfigErrors.h"

namespace dasall::infra::config {
namespace {

constexpr std::string_view kDefaultSourceId = "infra/config/defaults/runtime_policy.yaml";
constexpr std::string_view kDefaultVersionRef = "defaults@1";
constexpr std::string_view kRuntimeOverlayVersionRef = "runtime-overlay@1";
constexpr std::string_view kDeployVersionRef = "deploy@1";

struct ParsedYamlDocument {
  std::vector<std::pair<std::string, std::string>> scalar_values;
  std::vector<std::pair<std::string, std::vector<std::string>>> list_values;
  bool ok = false;
  std::string error;
};

[[nodiscard]] std::string trim_copy(std::string value) {
  const auto begin = value.find_first_not_of(" \t\r\n");
  if (begin == std::string::npos) {
    return {};
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
                                    const std::string_view& leaf_key) {
  std::ostringstream stream;
  bool first = true;

  for (const auto& node : path) {
    if (!first) {
      stream << '.';
    }
    stream << node.second;
    first = false;
  }

  if (!leaf_key.empty()) {
    if (!first) {
      stream << '.';
    }
    stream << leaf_key;
  }

  return stream.str();
}

[[nodiscard]] std::string strip_inline_comment(std::string value) {
  const auto comment_pos = value.find('#');
  if (comment_pos == std::string::npos) {
    return trim_copy(std::move(value));
  }

  return trim_copy(value.substr(0U, comment_pos));
}

[[nodiscard]] ParsedYamlDocument parse_yaml_stream(std::istream& stream) {
  ParsedYamlDocument parsed;
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

      const std::string list_key = join_path(path_stack, "");
      auto list_it = std::find_if(parsed.list_values.begin(),
                                  parsed.list_values.end(),
                                  [&](const auto& list_entry) {
                                    return list_entry.first == list_key;
                                  });
      if (list_it == parsed.list_values.end()) {
        parsed.list_values.emplace_back(list_key, std::vector<std::string>{});
        list_it = std::prev(parsed.list_values.end());
      }

      list_it->second.push_back(strip_inline_comment(trimmed.substr(2U)));
      continue;
    }

    const auto colon = trimmed.find(':');
    if (colon == std::string::npos) {
      parsed.error = "yaml line missing colon separator";
      return parsed;
    }

    const std::string key = trim_copy(trimmed.substr(0U, colon));
    const std::string value = strip_inline_comment(trimmed.substr(colon + 1U));

    while (!path_stack.empty() && path_stack.back().first >= indent) {
      path_stack.pop_back();
    }

    if (value.empty()) {
      path_stack.emplace_back(indent, key);
      continue;
    }

    parsed.scalar_values.emplace_back(join_path(path_stack, key), value);
  }

  parsed.ok = true;
  return parsed;
}

[[nodiscard]] ConfigLoadResult make_failure(ConfigErrorCode code,
                                            std::string message,
                                            std::string stage,
                                            std::string source_ref) {
  const ConfigErrorMapping mapping = map_config_error_code(code);
  return ConfigLoadResult::failure(mapping.result_code,
                                   std::string(config_error_code_name(code)) + ": " +
                                       std::move(message),
                                   std::move(stage),
                                   std::move(source_ref));
}

[[nodiscard]] bool is_signed_integer(const std::string_view& value) {
  if (value.empty()) {
    return false;
  }

  std::size_t start = 0;
  if (value.front() == '-') {
    if (value.size() == 1U) {
      return false;
    }
    start = 1;
  }

  return std::all_of(value.begin() + static_cast<std::ptrdiff_t>(start),
                     value.end(),
                     [](unsigned char ch) { return std::isdigit(ch) != 0; });
}

[[nodiscard]] bool is_unsigned_integer(const std::string_view& value) {
  return !value.empty() &&
         std::all_of(value.begin(), value.end(), [](unsigned char ch) {
           return std::isdigit(ch) != 0;
         });
}

[[nodiscard]] ConfigValueType infer_value_type(const std::string_view& key_path,
                                               const std::string_view& serialized_value,
                                               bool is_list) {
  if (is_list) {
    return ConfigValueType::StringList;
  }

  if (key_path == "schema_version" || key_path.ends_with(".schema_version")) {
    return ConfigValueType::String;
  }

  if (serialized_value == "true" || serialized_value == "false") {
    return ConfigValueType::Boolean;
  }

  if (is_unsigned_integer(serialized_value)) {
    return ConfigValueType::UnsignedInteger;
  }

  if (is_signed_integer(serialized_value)) {
    return ConfigValueType::Integer;
  }

  return ConfigValueType::String;
}

[[nodiscard]] std::string join_string_list(const std::vector<std::string>& values) {
  std::ostringstream stream;
  stream << '[';
  for (std::size_t index = 0; index < values.size(); ++index) {
    if (index != 0U) {
      stream << ',';
    }
    stream << values[index];
  }
  stream << ']';
  return stream.str();
}

[[nodiscard]] std::vector<TypedConfig> build_typed_entries(const ParsedYamlDocument& parsed,
                                                           ConfigSourceKind source_kind,
                                                           const std::string& source_id) {
  std::vector<TypedConfig> entries;
  entries.reserve(parsed.scalar_values.size() + parsed.list_values.size());

  for (const auto& [key_path, serialized_value] : parsed.scalar_values) {
    entries.push_back(TypedConfig{
        .key_path = key_path,
        .value_type = infer_value_type(key_path, serialized_value, false),
        .serialized_value = serialized_value,
        .schema_version = std::string(kConfigSchemaVersionV1),
        .source_kind = source_kind,
        .source_id = source_id,
        .secret_backed = false,
    });
  }

  for (const auto& [key_path, values] : parsed.list_values) {
    entries.push_back(TypedConfig{
        .key_path = key_path,
        .value_type = infer_value_type(key_path, {}, true),
        .serialized_value = join_string_list(values),
        .schema_version = std::string(kConfigSchemaVersionV1),
        .source_kind = source_kind,
        .source_id = source_id,
        .secret_backed = false,
    });
  }

  return entries;
}

[[nodiscard]] ConfigLayerDocument make_layer_document(ConfigSourceKind source_kind,
                                                      ConfigDocumentFormat document_format,
                            const std::string& source_id,
                                                      std::string version_ref,
                                                      std::vector<TypedConfig> entries) {
  return ConfigLayerDocument{
      .layer_ref = ConfigLayerRef{
          .source_kind = source_kind,
          .document_format = document_format,
      .source_id = source_id,
          .version_ref = std::move(version_ref),
          .schema_version = std::string(kConfigSchemaVersionV1),
      },
      .entries = std::move(entries),
  };
}

[[nodiscard]] std::optional<std::filesystem::path> try_resolve_source_path(
    const std::filesystem::path& repository_root,
    const std::string_view& source_ref) {
  if (source_ref.empty()) {
    return std::nullopt;
  }

  std::filesystem::path path{std::string(source_ref)};
  if (path.is_relative()) {
    path = repository_root / path;
  }

  if (!std::filesystem::exists(path) || !std::filesystem::is_regular_file(path)) {
    return std::nullopt;
  }

  return std::filesystem::weakly_canonical(path);
}

[[nodiscard]] std::string to_source_id(const std::filesystem::path& repository_root,
                                       const std::filesystem::path& source_path) {
  std::error_code error;
  const std::filesystem::path relative = std::filesystem::relative(source_path, repository_root, error);
  if (!error && !relative.empty() && *relative.begin() != "..") {
    return relative.generic_string();
  }

  return source_path.generic_string();
}

[[nodiscard]] ConfigLoadResult parse_layer_file(const std::filesystem::path& source_path,
                                                const std::filesystem::path& repository_root,
                                                ConfigSourceKind source_kind,
                                                ConfigDocumentFormat document_format,
                                                std::string version_ref,
                                                std::string stage) {
  std::ifstream stream(source_path);
  if (!stream.is_open()) {
    return make_failure(ConfigErrorCode::SourceUnavailable,
                        "unable to open config layer file",
                        std::move(stage),
                        source_path.generic_string());
  }

  ParsedYamlDocument parsed = parse_yaml_stream(stream);
  if (!parsed.ok) {
    return make_failure(ConfigErrorCode::InvalidSchema,
                        parsed.error,
                        std::move(stage),
                        source_path.generic_string());
  }

  const std::string source_id = to_source_id(repository_root, source_path);
  ConfigLayerDocument document = make_layer_document(source_kind,
                                                     document_format,
                                                     source_id,
                                                     std::move(version_ref),
                                                     build_typed_entries(parsed, source_kind, source_id));
  if (!document.is_valid()) {
    return make_failure(ConfigErrorCode::InvalidSchema,
                        "parsed config layer did not satisfy the frozen typed contract",
                        std::move(stage),
                        source_id);
  }

  return ConfigLoadResult::success(std::move(document));
}

[[nodiscard]] ConfigLoadResult parse_default_layer() {
  // Repo-scoped defaults assets are not frozen yet, so v1 keeps a minimal built-in defaults layer.
  std::istringstream defaults_stream(R"(schema_version: 1
infra:
  config:
    watch:
      enabled: true
      debounce_ms: 500
    cache:
      ttl_ms: 30000
      stale_read_allowed: true
    validation:
      strict: true
    runtime_patch:
      enabled: true
      allowlist:
        - infra.config.watch.
        - ops_policy.log_level
    rollback:
      enabled: true
    source:
      external:
        enabled: false
        timeout_ms: 1000
ops_policy:
  log_level: warn
runtime_budget:
  max_turns: 12
)");

  ParsedYamlDocument parsed = parse_yaml_stream(defaults_stream);
  if (!parsed.ok) {
    return make_failure(ConfigErrorCode::InvalidSchema,
                        parsed.error,
                        "config.load_default",
                        std::string(kDefaultSourceId));
  }

  const std::string source_id(kDefaultSourceId);
  ConfigLayerDocument document = make_layer_document(
      ConfigSourceKind::Defaults,
      ConfigDocumentFormat::RuntimePolicyYamlV1,
      source_id,
      std::string(kDefaultVersionRef),
      build_typed_entries(parsed, ConfigSourceKind::Defaults, source_id));
  if (!document.is_valid()) {
    return make_failure(ConfigErrorCode::InvalidSchema,
                        "default config layer did not satisfy the frozen typed contract",
                        "config.load_default",
                        source_id);
  }

  return ConfigLoadResult::success(std::move(document));
}

}  // namespace

ConfigLoader::ConfigLoader(ConfigLoaderOptions options) : options_(std::move(options)) {}

std::filesystem::path ConfigLoader::resolve_repository_root() const {
  if (!options_.repository_root.empty()) {
    return std::filesystem::weakly_canonical(options_.repository_root);
  }

  std::filesystem::path cursor = std::filesystem::current_path();
  while (!cursor.empty()) {
    if (std::filesystem::exists(cursor / "profiles") &&
        std::filesystem::exists(cursor / "infra") &&
        std::filesystem::exists(cursor / "tests")) {
      return cursor;
    }

    const std::filesystem::path parent = cursor.parent_path();
    if (parent == cursor) {
      break;
    }

    cursor = parent;
  }

  return std::filesystem::current_path();
}

ConfigLoadResult ConfigLoader::load_default() {
  return parse_default_layer();
}

ConfigLoadResult ConfigLoader::load_profile(std::string_view profile_id) {
  if (!is_supported_profile_id(profile_id)) {
    return make_failure(ConfigErrorCode::InvalidSchema,
                        "profile loader only accepts the five frozen profile identifiers",
                        "config.load_profile",
                        std::string(profile_id));
  }

  const std::filesystem::path repository_root = resolve_repository_root();
  const std::filesystem::path source_path = repository_root / "profiles" / std::string(profile_id) /
                                            "runtime_policy.yaml";
  if (!std::filesystem::exists(source_path)) {
    return make_failure(ConfigErrorCode::SourceUnavailable,
                        "profile runtime_policy.yaml is unavailable for the requested profile",
                        "config.load_profile",
                        source_path.generic_string());
  }

  return parse_layer_file(source_path,
                          repository_root,
                          ConfigSourceKind::Profile,
                          ConfigDocumentFormat::RuntimePolicyYamlV1,
                          std::string(profile_id) + "@1",
                          "config.load_profile");
}

ConfigLoadResult ConfigLoader::load_deploy(std::string_view source_ref) {
  const std::filesystem::path repository_root = resolve_repository_root();
  const std::optional<std::filesystem::path> source_path =
      try_resolve_source_path(repository_root, source_ref);
  if (!source_path.has_value()) {
    return make_failure(ConfigErrorCode::SourceUnavailable,
                        "deployment loader requires a reachable local overlay file",
                        "config.load_deploy",
                        std::string(source_ref));
  }

  return parse_layer_file(*source_path,
                          repository_root,
                          ConfigSourceKind::DeploymentOverride,
                          ConfigDocumentFormat::DeploymentOverlayYamlV1,
                          std::string(kDeployVersionRef),
                          "config.load_deploy");
}

ConfigLoadResult ConfigLoader::load_runtime_overlay() {
  const std::filesystem::path repository_root = resolve_repository_root();
  const std::optional<std::filesystem::path> source_path =
      try_resolve_source_path(repository_root, options_.runtime_overlay_source_ref);
  if (!source_path.has_value()) {
    return make_failure(ConfigErrorCode::SourceUnavailable,
                        "runtime overlay loader requires a reachable local patch file",
                        "config.load_runtime_overlay",
                        options_.runtime_overlay_source_ref);
  }

  return parse_layer_file(*source_path,
                          repository_root,
                          ConfigSourceKind::RuntimeOverride,
                          ConfigDocumentFormat::RuntimeOverridePatchV1,
                          std::string(kRuntimeOverlayVersionRef),
                          "config.load_runtime_overlay");
}

}  // namespace dasall::infra::config