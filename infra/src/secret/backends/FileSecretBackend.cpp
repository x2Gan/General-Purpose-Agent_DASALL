#include "FileSecretBackend.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <system_error>
#include <utility>

#include "secret/SecretErrors.h"

namespace dasall::infra::secret {
namespace {

constexpr std::string_view kFileSecretBackendSourceRef = "FileSecretBackend";

struct ParsedFieldDocument {
  bool ok = false;
  std::map<std::string, std::string> fields;
  SecretErrorCode error_code = SecretErrorCode::MaterializeFailed;
  std::string message;
};

struct ParsedMaterializedSecret {
  bool ok = false;
  std::string plaintext;
  SecretErrorCode error_code = SecretErrorCode::MaterializeFailed;
  std::string message;
};

[[nodiscard]] std::int64_t current_time_unix_ms() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}

[[nodiscard]] std::string trim_copy(std::string value) {
  const auto begin = value.find_first_not_of(" \t\r\n");
  if (begin == std::string::npos) {
    return {};
  }

  const auto end = value.find_last_not_of(" \t\r\n");
  return value.substr(begin, end - begin + 1U);
}

[[nodiscard]] std::string make_handle_id(const SecretBackendRecord& record) {
  return "file-handle://" + record.descriptor.secret_name + "/" + record.version;
}

[[nodiscard]] std::string make_lease_id(const SecretBackendRecord& record) {
  return "file-lease://" + record.descriptor.secret_name + "/" + record.version;
}

[[nodiscard]] std::string make_status_detail_ref(std::string_view backend_ref,
                                                 std::string_view suffix) {
  return "status://secret/backend/" + std::string(backend_ref) + "/" + std::string(suffix);
}

[[nodiscard]] SecretBackendFetchResult make_fetch_failure(SecretErrorCode error_code,
                                                          std::string message,
                                                          std::string stage) {
  const SecretErrorMapping mapping = map_secret_error_code(error_code);
  return SecretBackendFetchResult::failure(
      mapping.result_code,
      std::string(secret_error_code_name(error_code)) + ": " + std::move(message),
      std::move(stage),
      std::string(kFileSecretBackendSourceRef));
}

[[nodiscard]] SecretMaterializationResult make_materialize_failure(SecretErrorCode error_code,
                                                                   std::string message,
                                                                   std::string stage) {
  const SecretErrorMapping mapping = map_secret_error_code(error_code);
  return SecretMaterializationResult::failure(
      mapping.result_code,
      std::string(secret_error_code_name(error_code)) + ": " + std::move(message),
      std::move(stage),
      std::string(kFileSecretBackendSourceRef));
}

[[nodiscard]] SecretLifecycleResult make_lifecycle_failure(std::string secret_name,
                                                           SecretErrorCode error_code,
                                                           std::string message,
                                                           std::string stage) {
  const SecretErrorMapping mapping = map_secret_error_code(error_code);
  return SecretLifecycleResult::failure(
      std::move(secret_name),
      mapping.result_code,
      std::string(secret_error_code_name(error_code)) + ": " + std::move(message),
      std::move(stage),
      std::string(kFileSecretBackendSourceRef));
}

[[nodiscard]] RotationResult make_rotation_failure(std::string secret_name,
                                                   std::string previous_version,
                                                   std::string candidate_version,
                                                   contracts::ResultCode result_code,
                                                   std::string message,
                                                   bool rollback_ready = false) {
  return RotationResult::failure(std::move(secret_name),
                                 std::move(previous_version),
                                 std::move(candidate_version),
                                 "audit://secret/file/rotation/failure",
                                 result_code,
                                 std::move(message),
                                 "secret.promote_version",
                                 std::string(kFileSecretBackendSourceRef),
                                 rollback_ready);
}

[[nodiscard]] bool is_secret_name_safe(std::string_view secret_name) {
  if (secret_name.empty() || secret_name.front() == '/' || secret_name.find("..") != std::string_view::npos) {
    return false;
  }

  return std::all_of(secret_name.begin(),
                     secret_name.end(),
                     [](char value) {
                       return std::isalnum(static_cast<unsigned char>(value)) != 0 || value == '/' ||
                              value == '-' || value == '_';
                     });
}

[[nodiscard]] ParsedFieldDocument parse_field_document(const std::filesystem::path& secret_path) {
  std::ifstream stream(secret_path);
  if (!stream.is_open()) {
    return ParsedFieldDocument{
        .ok = false,
        .fields = {},
        .error_code = SecretErrorCode::BackendUnavailable,
        .message = "file backend could not open the requested secret file",
    };
  }

  ParsedFieldDocument parsed{
      .ok = true,
      .fields = {},
      .error_code = SecretErrorCode::MaterializeFailed,
      .message = {},
  };
  std::string raw_line;

  while (std::getline(stream, raw_line)) {
    const std::string trimmed = trim_copy(raw_line);
    if (trimmed.empty() || trimmed.starts_with('#')) {
      continue;
    }

    const auto separator = trimmed.find('=');
    if (separator == std::string::npos) {
      return ParsedFieldDocument{
          .ok = false,
          .fields = {},
          .error_code = SecretErrorCode::MaterializeFailed,
          .message = "secret file contains a malformed key=value line",
      };
    }

    const std::string key = trim_copy(trimmed.substr(0U, separator));
    const std::string value = trim_copy(trimmed.substr(separator + 1U));
    if (key.empty() || value.empty()) {
      return ParsedFieldDocument{
          .ok = false,
          .fields = {},
          .error_code = SecretErrorCode::MaterializeFailed,
          .message = "secret file contains an empty key or value",
      };
    }

    parsed.fields[key] = value;
  }

  return parsed;
}

[[nodiscard]] SecretClassification parse_classification(std::string_view classification) {
  if (classification == "credential") {
    return SecretClassification::Credential;
  }

  if (classification == "token") {
    return SecretClassification::Token;
  }

  if (classification == "certificate") {
    return SecretClassification::Certificate;
  }

  if (classification == "sensitive_config") {
    return SecretClassification::SensitiveConfig;
  }

  return SecretClassification::Unspecified;
}

[[nodiscard]] std::optional<SecretBackendRecord> build_record(
    const ParsedFieldDocument& parsed,
    std::string secret_name,
    const FileSecretBackendOptions& options,
    const std::filesystem::path& secret_path) {
  if (!parsed.ok) {
    return std::nullopt;
  }

  const auto classification = parsed.fields.find("classification");
  const auto rotation_policy = parsed.fields.find("rotation_policy");
  const auto owner = parsed.fields.find("owner");
  const auto version = parsed.fields.find("version");

  if (classification == parsed.fields.end() || rotation_policy == parsed.fields.end() ||
      owner == parsed.fields.end() || version == parsed.fields.end()) {
    return std::nullopt;
  }

  SecretBackendRecord record{
      .descriptor = SecretDescriptor{
          .secret_name = std::move(secret_name),
          .backend_type = SecretBackendType::File,
          .classification = parse_classification(classification->second),
          .rotation_policy_ref = rotation_policy->second,
          .owner_ref = owner->second,
      },
      .backend_ref = options.backend_ref,
      .version = version->second,
      .cipher_ref = std::string("file://") + secret_path.lexically_relative(options.root_dir).generic_string(),
      .encrypted_at_rest = options.encrypt_at_rest,
  };

  if (!record.is_valid()) {
    return std::nullopt;
  }

  return record;
}

[[nodiscard]] int hex_to_int(char value) {
  if (value >= '0' && value <= '9') {
    return value - '0';
  }

  if (value >= 'a' && value <= 'f') {
    return 10 + (value - 'a');
  }

  if (value >= 'A' && value <= 'F') {
    return 10 + (value - 'A');
  }

  return -1;
}

[[nodiscard]] ParsedMaterializedSecret decode_materialized_secret(
    const ParsedFieldDocument& parsed,
    bool encrypt_at_rest) {
  if (!parsed.ok) {
    return ParsedMaterializedSecret{
        .ok = false,
        .plaintext = {},
        .error_code = parsed.error_code,
        .message = parsed.message,
    };
  }

  if (!encrypt_at_rest) {
    const auto plaintext = parsed.fields.find("plaintext");
    if (plaintext == parsed.fields.end()) {
      return ParsedMaterializedSecret{
          .ok = false,
          .plaintext = {},
          .error_code = SecretErrorCode::MaterializeFailed,
          .message = "file backend requires plaintext when encrypt_at_rest is disabled",
      };
    }

    return ParsedMaterializedSecret{
        .ok = true,
        .plaintext = plaintext->second,
        .error_code = SecretErrorCode::MaterializeFailed,
        .message = {},
    };
  }

  const auto ciphertext_hex = parsed.fields.find("ciphertext_hex");
  if (ciphertext_hex == parsed.fields.end()) {
    return ParsedMaterializedSecret{
        .ok = false,
        .plaintext = {},
        .error_code = SecretErrorCode::MaterializeFailed,
        .message = "file backend requires ciphertext_hex when encrypt_at_rest is enabled",
    };
  }

  if ((ciphertext_hex->second.size() % 2U) != 0U) {
    return ParsedMaterializedSecret{
        .ok = false,
        .plaintext = {},
        .error_code = SecretErrorCode::MaterializeFailed,
        .message = "ciphertext_hex must contain an even number of characters",
    };
  }

  std::string plaintext;
  plaintext.reserve(ciphertext_hex->second.size() / 2U);
  for (std::size_t index = 0; index < ciphertext_hex->second.size(); index += 2U) {
    const int high = hex_to_int(ciphertext_hex->second[index]);
    const int low = hex_to_int(ciphertext_hex->second[index + 1U]);
    if (high < 0 || low < 0) {
      return ParsedMaterializedSecret{
          .ok = false,
          .plaintext = {},
          .error_code = SecretErrorCode::MaterializeFailed,
          .message = "ciphertext_hex contains a non-hexadecimal byte",
      };
    }

    plaintext.push_back(static_cast<char>((high << 4) | low));
  }

  return ParsedMaterializedSecret{
      .ok = true,
      .plaintext = std::move(plaintext),
      .error_code = SecretErrorCode::MaterializeFailed,
      .message = {},
  };
}

}  // namespace

FileSecretBackend::FileSecretBackend(FileSecretBackendOptions options)
    : options_(std::move(options)) {}

SecretBackendFetchResult FileSecretBackend::fetch_record(const SecretQuery& query) {
  if (!root_available()) {
    last_error_code_ = map_secret_error_code(SecretErrorCode::BackendUnavailable).result_code;
    return make_fetch_failure(SecretErrorCode::BackendUnavailable,
                              "file backend root_dir is unavailable",
                              "secret.fetch_record");
  }

  if (!query.is_valid() || !is_secret_name_safe(query.secret_name)) {
    last_error_code_ = contracts::ResultCode::ValidationFieldMissing;
    return SecretBackendFetchResult::failure(contracts::ResultCode::ValidationFieldMissing,
                                             "file backend requires a safe secret_name and a valid query contract",
                                             "secret.fetch_record",
                                             std::string(kFileSecretBackendSourceRef));
  }

  const auto secret_path = resolve_secret_path(query.secret_name);
  if (!secret_path.has_value() || !std::filesystem::exists(*secret_path) ||
      !std::filesystem::is_regular_file(*secret_path)) {
    last_error_code_ = map_secret_error_code(SecretErrorCode::NotFound).result_code;
    return make_fetch_failure(SecretErrorCode::NotFound,
                              "file backend could not find the requested secret path under root_dir",
                              "secret.fetch_record");
  }

  const ParsedFieldDocument parsed = parse_field_document(*secret_path);
  const auto record = build_record(parsed, query.secret_name, options_, *secret_path);
  if (!record.has_value()) {
    last_error_code_ = map_secret_error_code(SecretErrorCode::MaterializeFailed).result_code;
    return make_fetch_failure(SecretErrorCode::MaterializeFailed,
                              parsed.message.empty()
                                  ? "file backend secret metadata is incomplete or invalid"
                                  : parsed.message,
                              "secret.fetch_record");
  }

  if (!query.version_hint.empty() && record->version != query.version_hint) {
    last_error_code_ = map_secret_error_code(SecretErrorCode::NotFound).result_code;
    return make_fetch_failure(SecretErrorCode::NotFound,
                              "file backend could not find the requested secret version",
                              "secret.fetch_record");
  }

  last_error_code_.reset();
  return SecretBackendFetchResult::success(*record);
}

SecretMaterializationResult FileSecretBackend::materialize_record(
    const SecretBackendRecord& record,
    const SecretAccessContext& access_context) {
  if (!root_available()) {
    last_error_code_ = map_secret_error_code(SecretErrorCode::BackendUnavailable).result_code;
    return make_materialize_failure(SecretErrorCode::BackendUnavailable,
                                    "file backend root_dir is unavailable",
                                    "secret.materialize_record");
  }

  if (!access_context.is_valid()) {
    last_error_code_ = contracts::ResultCode::ValidationFieldMissing;
    return SecretMaterializationResult::failure(
        contracts::ResultCode::ValidationFieldMissing,
        "file backend requires an auditable access context before materialization",
        "secret.materialize_record",
        std::string(kFileSecretBackendSourceRef));
  }

  const auto secret_path = resolve_secret_path(record.descriptor.secret_name);
  if (!secret_path.has_value() || !std::filesystem::exists(*secret_path) ||
      !std::filesystem::is_regular_file(*secret_path)) {
    last_error_code_ = map_secret_error_code(SecretErrorCode::NotFound).result_code;
    return make_materialize_failure(SecretErrorCode::NotFound,
                                    "file backend cannot materialize a missing secret path",
                                    "secret.materialize_record");
  }

  const ParsedFieldDocument parsed = parse_field_document(*secret_path);
  const auto current_record = build_record(parsed,
                                           record.descriptor.secret_name,
                                           options_,
                                           *secret_path);
  if (!current_record.has_value()) {
    last_error_code_ = map_secret_error_code(SecretErrorCode::MaterializeFailed).result_code;
    return make_materialize_failure(SecretErrorCode::MaterializeFailed,
                                    parsed.message.empty()
                                        ? "file backend secret metadata is incomplete or invalid"
                                        : parsed.message,
                                    "secret.materialize_record");
  }

  if (current_record->version != record.version) {
    last_error_code_ = map_secret_error_code(SecretErrorCode::VersionStale).result_code;
    return make_materialize_failure(SecretErrorCode::VersionStale,
                                    "file backend detected a stale record version during materialization",
                                    "secret.materialize_record");
  }

  const ParsedMaterializedSecret materialized = decode_materialized_secret(parsed,
                                                                           options_.encrypt_at_rest);
  if (!materialized.ok) {
    last_error_code_ = map_secret_error_code(materialized.error_code).result_code;
    return make_materialize_failure(materialized.error_code,
                                    materialized.message,
                                    "secret.materialize_record");
  }

  const std::int64_t issued_at_ms = current_time_unix_ms();
  const std::int64_t expires_at_ms = issued_at_ms + std::max<std::int64_t>(1, options_.lease_duration_ms);
  last_error_code_.reset();
  return SecretMaterializationResult::success(
      std::make_shared<SecureBuffer>(SecureBuffer::from_text_copy(materialized.plaintext)),
      SecretLease{
          .lease_id = make_lease_id(*current_record),
          .handle_id = make_handle_id(*current_record),
          .consumer_ref = access_context.consumer_module,
          .expires_at_ms = expires_at_ms,
          .rotation_epoch = std::max<std::uint64_t>(1, options_.rotation_epoch),
          .state = SecretLeaseState::Active,
      });
}

RotationResult FileSecretBackend::promote_version(
    const SecretVersionPromotionRequest& request) {
  if (!root_available()) {
    last_error_code_ = map_secret_error_code(SecretErrorCode::BackendUnavailable).result_code;
    return make_rotation_failure(request.secret_name,
                                 request.previous_version,
                                 request.candidate_version,
                                 map_secret_error_code(SecretErrorCode::BackendUnavailable).result_code,
                                 "file backend root_dir is unavailable",
                                 true);
  }

  if (!request.is_valid()) {
    last_error_code_ = contracts::ResultCode::ValidationFieldMissing;
    return make_rotation_failure(request.secret_name,
                                 request.previous_version,
                                 request.candidate_version,
                                 contracts::ResultCode::ValidationFieldMissing,
                                 "file backend promotion request must satisfy the frozen promotion contract",
                                 false);
  }

  return make_rotation_failure(request.secret_name,
                               request.previous_version,
                               request.candidate_version,
                               contracts::ResultCode::ToolExecutionFailed,
                               "file backend rotation is intentionally deferred in the v1 skeleton",
                               false);
}

SecretLifecycleResult FileSecretBackend::revoke_version(std::string_view secret_name,
                                                        std::string_view version) {
  if (!root_available()) {
    last_error_code_ = map_secret_error_code(SecretErrorCode::BackendUnavailable).result_code;
    return make_lifecycle_failure(std::string(secret_name),
                                  SecretErrorCode::BackendUnavailable,
                                  "file backend root_dir is unavailable",
                                  "secret.revoke_version");
  }

  if (secret_name.empty() || version.empty()) {
    last_error_code_ = contracts::ResultCode::ValidationFieldMissing;
    return SecretLifecycleResult::failure(std::string(secret_name),
                                          contracts::ResultCode::ValidationFieldMissing,
                                          "file backend revoke_version requires a non-empty secret_name and version",
                                          "secret.revoke_version",
                                          std::string(kFileSecretBackendSourceRef));
  }

  return make_lifecycle_failure(std::string(secret_name),
                                SecretErrorCode::MaterializeFailed,
                                "file backend revoke is intentionally deferred in the v1 skeleton",
                                "secret.revoke_version");
}

SecretBackendStatus FileSecretBackend::get_backend_status() const {
  const bool available = root_available();
  return SecretBackendStatus{
      .state = available ? SecretBackendState::Available : SecretBackendState::Unavailable,
      .backend_ref = options_.backend_ref,
      .rate_limited = false,
      .read_only_fallback_ready = false,
      .last_error_code = last_error_code_,
      .detail_ref = make_status_detail_ref(options_.backend_ref,
                                           available ? "available" : "unavailable"),
  };
}

std::optional<std::filesystem::path> FileSecretBackend::resolve_secret_path(
    std::string_view secret_name) const {
  if (!is_secret_name_safe(secret_name)) {
    return std::nullopt;
  }

  std::filesystem::path relative_path;
  std::stringstream stream{std::string(secret_name)};
  std::string segment;
  while (std::getline(stream, segment, '/')) {
    if (segment.empty() || segment == "." || segment == "..") {
      return std::nullopt;
    }
    relative_path /= segment;
  }

  relative_path.replace_extension(".secret");
  return options_.root_dir / relative_path;
}

bool FileSecretBackend::root_available() const {
  return std::filesystem::exists(options_.root_dir) &&
         std::filesystem::is_directory(options_.root_dir);
}

}  // namespace dasall::infra::secret