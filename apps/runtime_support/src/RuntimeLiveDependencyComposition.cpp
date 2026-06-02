#include "RuntimeLiveDependencyComposition.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <limits>
#include <memory>
#include <map>
#include <mutex>
#include <optional>
#include <sstream>
#include <set>
#include <string>
#include <system_error>
#include <thread>
#include <utility>

#include <vector>

#include <sqlite3.h>

#include <sys/wait.h>
#include <unistd.h>

#include "LogEvent.h"
#include "ICognitionEngine.h"
#include "IKnowledgeService.h"
#include "LLMBackedEmbeddingAdapter.h"
#include "ILLMTransport.h"
#include "ILLMManager.h"
#include "LLMBackedSummarizer.h"
#include "LLMGenerateRequest.h"
#include "LLMManagerResult.h"
#include "ObservabilityLiveComposition.h"
#include "LLMProductionFactory.h"
#include "LLMSubsystemConfig.h"
#include "IMemoryManager.h"
#include "IMultiAgentCoordinator.h"
#include "KnowledgeServiceFactory.h"
#include "KnowledgeTypes.h"
#include "IResponseBuilder.h"
#include "RuntimeDependencySet.h"
#include "ServiceLiveComposition.h"
#include "ToolManager.h"
#include "BuildProfileResolver.h"
#include "audit/AuditTypes.h"
#include "audit/IAuditLogger.h"
#include "health/IHealthMonitor.h"
#include "logging/ILogger.h"
#include "metrics/IMeter.h"
#include "metrics/MetricTypes.h"
#include "ops/ToolAuditBridge.h"
#include "ops/ToolHealthProbe.h"
#include "ops/ToolMetricsBridge.h"
#include "ops/ToolTraceBridge.h"
#include "ProfileCatalog.h"
#include "builtin/dataset/AgentDatasetTool.h"
#include "builtin/terminal/AgentTerminalTool.h"
#include "tool/ToolDescriptor.h"
#include "config/MemoryConfigProjector.h"
#include "config/InstallLayout.h"
#include "execution/BuiltinExecutorLane.h"
#include "health/RuntimeHealthProbe.h"
#include "ingest/SourceScanner.h"
#include "ITimer.h"
#include "maintenance/BackgroundMaintenanceHooks.h"
#include "bridge/ToolServiceBridge.h"
#include "registry/ToolRegistry.h"
#include "secret/SecretManagerLiveComposition.h"
#include "skills/PluginSkillBundleImporter.h"
#include "skills/SkillRegistry.h"
#include "skills/SkillRuntime.h"
#include "telemetry/RuntimeEventBus.h"
#include "telemetry/RuntimeLoggingBridge.h"
#include "telemetry/RuntimeTelemetryBridge.h"
#include "tracing/ISpan.h"
#include "tracing/ITracer.h"
#include "tracing/TraceTypes.h"
#include "provider/ProviderCatalogRepository.h"
#include "vector/DetachedVectorIndexFactory.h"

namespace dasall::apps::runtime_support {
namespace {

namespace fs = std::filesystem;

constexpr std::string_view kKnowledgeDenseSnapshotDatabaseName = "dense.sqlite";
constexpr std::string_view kKnowledgeQueryEmbeddingFeatureNote =
    "knowledge_query_embedding_default";
constexpr std::string_view kSecretRefPrefix = "secret://";
constexpr std::string_view kProfileRefPrefix = "profile://";
constexpr std::string_view kKnowledgeEmbeddingEndpointPath = "/embeddings";
constexpr std::string_view kKnowledgeQueryEncoderTransportError =
    "query_encoder_transport_failed";
constexpr std::string_view kKnowledgeQueryEncoderInvalidSecret =
    "query_encoder_invalid_secret";
constexpr std::string_view kKnowledgeQueryEncoderEmptyEmbedding =
    "query_encoder_empty_embedding";
constexpr std::string_view kKnowledgeQueryEncoderProviderMissing =
    "query_encoder_provider_missing";
constexpr std::string_view kKnowledgeQueryEncoderResponseInconsistent =
    "query_encoder_response_inconsistent";
constexpr std::string_view kKnowledgeQueryEncoderProviderRejected =
    "query_encoder_provider_rejected";
constexpr std::string_view kKnowledgeQueryEncoderSecretMissing =
    "query_encoder_secret_missing";
constexpr std::string_view kKnowledgeQueryEncoderSecretMaterializeFailed =
    "query_encoder_secret_materialize_failed";
constexpr std::string_view kKnowledgeQueryEncoderInvalidAuthRef =
    "query_encoder_invalid_auth_ref";
constexpr std::string_view kKnowledgeQueryEncoderTransportMissing =
    "query_encoder_transport_missing";

struct RuntimeKnowledgeEmbeddingProviderConfig {
  std::string provider_id;
  std::string model_id;
  std::string base_url;
  std::string auth_ref;
  std::string base_url_alias;
  std::string snapshot_version;
  std::uint32_t timeout_ms = 15000U;

  [[nodiscard]] bool has_consistent_values() const {
    return !provider_id.empty() && !model_id.empty() && !base_url.empty() &&
           !auth_ref.empty() && !base_url_alias.empty() &&
           !snapshot_version.empty() && timeout_ms > 0U;
  }
};

[[nodiscard]] std::string trim_copy(std::string_view value) {
  std::size_t begin = 0U;
  while (begin < value.size() &&
         std::isspace(static_cast<unsigned char>(value[begin])) != 0) {
    ++begin;
  }

  std::size_t end = value.size();
  while (end > begin &&
         std::isspace(static_cast<unsigned char>(value[end - 1U])) != 0) {
    --end;
  }

  return std::string(value.substr(begin, end - begin));
}

[[nodiscard]] std::string escape_json(std::string_view value) {
  std::string escaped;
  escaped.reserve(value.size());
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

[[nodiscard]] std::optional<std::string> secret_name_from_auth_ref(
    std::string_view auth_ref) {
  if (!auth_ref.starts_with(kSecretRefPrefix) ||
      auth_ref.size() <= kSecretRefPrefix.size()) {
    return std::nullopt;
  }

  return std::string(auth_ref.substr(kSecretRefPrefix.size()));
}

[[nodiscard]] std::optional<std::size_t> find_json_value_start(std::string_view payload,
                                                               std::string_view key,
                                                               std::size_t offset = 0U) {
  const std::string needle = "\"" + std::string(key) + "\"";
  const std::size_t key_position = payload.find(needle, offset);
  if (key_position == std::string_view::npos) {
    return std::nullopt;
  }

  const std::size_t colon_position = payload.find(':', key_position + needle.size());
  if (colon_position == std::string_view::npos) {
    return std::nullopt;
  }

  std::size_t value_position = colon_position + 1U;
  while (value_position < payload.size() &&
         std::isspace(static_cast<unsigned char>(payload[value_position])) != 0) {
    ++value_position;
  }

  if (value_position >= payload.size()) {
    return std::nullopt;
  }

  return value_position;
}

[[nodiscard]] std::optional<std::string> extract_json_array_field(
    std::string_view payload,
    std::string_view key,
    std::size_t offset = 0U) {
  const auto value_start = find_json_value_start(payload, key, offset);
  if (!value_start.has_value() || payload[*value_start] != '[') {
    return std::nullopt;
  }

  bool in_string = false;
  bool escaping = false;
  int depth = 0;
  for (std::size_t index = *value_start; index < payload.size(); ++index) {
    const char character = payload[index];
    if (escaping) {
      escaping = false;
      continue;
    }

    if (character == '\\') {
      escaping = true;
      continue;
    }

    if (character == '"') {
      in_string = !in_string;
      continue;
    }

    if (in_string) {
      continue;
    }

    if (character == '[') {
      ++depth;
    } else if (character == ']') {
      --depth;
      if (depth == 0) {
        return std::string(payload.substr(*value_start, index - *value_start + 1U));
      }
    }
  }

  return std::nullopt;
}

[[nodiscard]] std::vector<float> parse_json_float_array(std::string_view payload) {
  std::vector<float> values;
  if (payload.size() < 2U || payload.front() != '[' || payload.back() != ']') {
    return values;
  }

  std::string token;
  token.reserve(32U);
  for (std::size_t index = 1U; index + 1U < payload.size(); ++index) {
    const char character = payload[index];
    if (std::isdigit(static_cast<unsigned char>(character)) != 0 || character == '-' ||
        character == '+' || character == '.' || character == 'e' ||
        character == 'E') {
      token.push_back(character);
      continue;
    }

    if (!token.empty()) {
      try {
        values.push_back(std::stof(token));
      } catch (...) {
        return {};
      }
      token.clear();
    }
  }

  if (!token.empty()) {
    try {
      values.push_back(std::stof(token));
    } catch (...) {
      return {};
    }
  }

  return values;
}

[[nodiscard]] std::vector<float> extract_embedding_vector(std::string_view payload) {
  const auto embedding_json = extract_json_array_field(payload, "embedding");
  if (!embedding_json.has_value()) {
    return {};
  }

  return parse_json_float_array(*embedding_json);
}

[[nodiscard]] std::string normalize_base_url(std::string_view base_url) {
  std::string normalized = trim_copy(base_url);
  while (!normalized.empty() && normalized.back() == '/') {
    normalized.pop_back();
  }
  return normalized;
}

[[nodiscard]] std::string build_embedding_url(std::string_view base_url) {
  return normalize_base_url(base_url) + std::string(kKnowledgeEmbeddingEndpointPath);
}

[[nodiscard]] std::string secure_buffer_to_string(
    const infra::secret::SecureBuffer& buffer) {
  std::string value;
  value.reserve(buffer.bytes().size());
  for (const std::byte byte : buffer.bytes()) {
    value.push_back(static_cast<char>(byte));
  }
  return value;
}

[[nodiscard]] std::string shell_quote(const std::filesystem::path& path) {
  std::string quoted = "'";
  const std::string value = path.string();
  for (const char character : value) {
    if (character == '\'') {
      quoted += "'\\''";
    } else {
      quoted.push_back(character);
    }
  }
  quoted.push_back('\'');
  return quoted;
}

[[nodiscard]] std::string curl_config_escape(std::string_view value) {
  std::string escaped;
  escaped.reserve(value.size());
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

[[nodiscard]] std::optional<std::filesystem::path> make_temp_file(
    const std::filesystem::path& temp_dir,
    std::string_view prefix) {
  std::error_code error;
  std::filesystem::create_directories(temp_dir, error);
  if (error) {
    return std::nullopt;
  }

  std::filesystem::path pattern = temp_dir / (std::string(prefix) + ".XXXXXX");
  std::string raw_pattern = pattern.string();
  const int fd = ::mkstemp(raw_pattern.data());
  if (fd < 0) {
    return std::nullopt;
  }

  (void)::close(fd);
  return std::filesystem::path(raw_pattern);
}

[[nodiscard]] bool write_file(const std::filesystem::path& path,
                              std::string_view content) {
  std::ofstream stream(path, std::ios::binary | std::ios::trunc);
  if (!stream.is_open()) {
    return false;
  }

  stream.write(content.data(), static_cast<std::streamsize>(content.size()));
  stream.flush();
  return stream.good();
}

[[nodiscard]] std::string build_curl_config(const llm::LLMTransportRequest& request,
                                            const std::filesystem::path& body_path) {
  std::ostringstream config;
  config << "silent\n";
  config << "show-error\n";
  config << "request = \"";
  config << (request.method == llm::LLMTransportMethod::Post ? "POST" : "GET");
  config << "\"\n";
  config << "url = \"" << curl_config_escape(request.url) << "\"\n";
  config << "max-time = \"" << std::max<std::uint32_t>(1U, (request.timeout_ms + 999U) / 1000U)
         << "\"\n";
  config << "connect-timeout = \"10\"\n";
  config << "output = \"-\"\n";
  config << "write-out = \"\\n__DASALL_CURL_HTTP_STATUS__:%{http_code}\"\n";
  config << "user-agent = \"DASALL/0.1 runtime-knowledge-query-encoder\"\n";
  for (const auto& header : request.headers) {
    config << "header = \""
           << curl_config_escape(header.name + ": " + header.value)
           << "\"\n";
  }
  config << "data-binary = \"@" << curl_config_escape(body_path.string()) << "\"\n";
  return config.str();
}

[[nodiscard]] llm::LLMTransportResponse parse_curl_output(std::string output,
                                                          int process_status) {
  constexpr std::string_view kStatusMarker = "\n__DASALL_CURL_HTTP_STATUS__:";
  const std::size_t marker_position = output.rfind(kStatusMarker);
  if (marker_position == std::string::npos) {
    return llm::LLMTransportResponse{
        .status_code = 0U,
        .body = {},
        .error_message = "runtime query encoder transport did not return an HTTP status marker",
    };
  }

  const std::size_t status_start = marker_position + kStatusMarker.size();
  std::string status_text = output.substr(status_start);
  while (!status_text.empty() &&
         (status_text.back() == '\n' || status_text.back() == '\r')) {
    status_text.pop_back();
  }

  std::uint16_t status_code = 0U;
  try {
    status_code = static_cast<std::uint16_t>(std::stoul(status_text));
  } catch (...) {
    return llm::LLMTransportResponse{
        .status_code = 0U,
        .body = {},
        .error_message = "runtime query encoder transport returned a malformed HTTP status marker",
    };
  }

  if (process_status != 0 && status_code == 0U) {
    return llm::LLMTransportResponse{
        .status_code = 0U,
        .body = {},
        .error_message = "runtime query encoder transport failed before receiving an HTTP response",
    };
  }

  output.erase(marker_position);
  return llm::LLMTransportResponse{
      .status_code = status_code,
      .body = std::move(output),
      .error_message = {},
  };
}

[[nodiscard]] std::string run_curl_command(const std::filesystem::path& curl_path,
                                           const std::filesystem::path& config_path,
                                           int* process_status) {
  const std::string command = shell_quote(curl_path) + " --config " +
                              shell_quote(config_path);
  FILE* pipe = ::popen(command.c_str(), "r");
  if (pipe == nullptr) {
    if (process_status != nullptr) {
      *process_status = -1;
    }
    return {};
  }

  std::array<char, 4096U> buffer{};
  std::string output;
  while (true) {
    const std::size_t bytes_read = std::fread(buffer.data(), 1U, buffer.size(), pipe);
    if (bytes_read == 0U) {
      break;
    }
    output.append(buffer.data(), bytes_read);
  }

  const int close_status = ::pclose(pipe);
  if (process_status != nullptr) {
    if (WIFEXITED(close_status)) {
      *process_status = WEXITSTATUS(close_status);
    } else {
      *process_status = close_status;
    }
  }
  return output;
}

class RuntimeKnowledgeQueryEncoderCurlTransport final : public llm::ILLMTransport {
 public:
  explicit RuntimeKnowledgeQueryEncoderCurlTransport(
      std::filesystem::path temp_dir = std::filesystem::temp_directory_path())
      : temp_dir_(std::move(temp_dir)) {}

  [[nodiscard]] llm::LLMTransportResponse send(
      const llm::LLMTransportRequest& request) override {
    if (!request.has_consistent_values()) {
      return llm::LLMTransportResponse{
          .status_code = 0U,
          .body = {},
          .error_message = "runtime query encoder transport received an inconsistent request",
      };
    }

    const auto body_path = make_temp_file(temp_dir_, "dasall-runtime-query-encoder-body");
    const auto config_path = make_temp_file(temp_dir_, "dasall-runtime-query-encoder-config");
    if (!body_path.has_value() || !config_path.has_value()) {
      return llm::LLMTransportResponse{
          .status_code = 0U,
          .body = {},
          .error_message = "runtime query encoder transport could not allocate temp files",
      };
    }

    struct CleanupGuard {
      std::filesystem::path body_path;
      std::filesystem::path config_path;
      ~CleanupGuard() {
        std::error_code error;
        std::filesystem::remove(body_path, error);
        std::filesystem::remove(config_path, error);
      }
    } cleanup_guard{*body_path, *config_path};

    if (!write_file(*body_path, request.body) ||
        !write_file(*config_path, build_curl_config(request, *body_path))) {
      return llm::LLMTransportResponse{
          .status_code = 0U,
          .body = {},
          .error_message = "runtime query encoder transport could not write curl temp files",
      };
    }

    int process_status = 0;
    const auto output = run_curl_command("/usr/bin/curl", *config_path, &process_status);
    return parse_curl_output(output, process_status);
  }

 private:
  std::filesystem::path temp_dir_;
};

[[nodiscard]] std::optional<RuntimeKnowledgeEmbeddingProviderConfig>
select_runtime_knowledge_embedding_provider(const fs::path& providers_root,
                                           std::vector<std::string>* failure_reason_codes) {
  llm::provider::ProviderCatalogRepository repository;
  if (!repository.init(llm::ProviderCatalogSourceConfig{
          .baseline_root = providers_root.string(),
          .deployment_root = {},
          .snapshot_cache_root = {},
          .signature_required = false,
          .merge_mode = "overlay",
      }) ||
      !repository.reload()) {
    if (failure_reason_codes != nullptr) {
      failure_reason_codes->push_back(kKnowledgeQueryEncoderProviderMissing.data());
    }
    return std::nullopt;
  }

  const auto snapshot = repository.snapshot();
  if (snapshot == nullptr || !snapshot->has_consistent_values()) {
    if (failure_reason_codes != nullptr) {
      failure_reason_codes->push_back(kKnowledgeQueryEncoderProviderMissing.data());
    }
    return std::nullopt;
  }

  for (const auto& provider : snapshot->providers) {
    if (!provider.has_consistent_values() || !provider.runtime.activation_flag) {
      continue;
    }

    if (provider.descriptor.adapter_family != "openai_compatible") {
      continue;
    }

    for (const auto& model : snapshot->models) {
      if (!model.has_consistent_values() ||
          model.summary.provider_id != provider.descriptor.provider_id ||
          std::find(model.feature_notes.begin(),
                    model.feature_notes.end(),
                    std::string(kKnowledgeQueryEmbeddingFeatureNote)) ==
              model.feature_notes.end()) {
        continue;
      }

      RuntimeKnowledgeEmbeddingProviderConfig config;
      config.provider_id = provider.descriptor.provider_id;
      config.model_id = model.summary.model_id;
      config.base_url = provider.descriptor.base_url;
      config.auth_ref = provider.descriptor.auth_ref;
      config.base_url_alias = provider.runtime.base_url_alias;
      config.snapshot_version = provider.descriptor.source_version;
      if (config.has_consistent_values()) {
        return config;
      }
    }
  }

  if (failure_reason_codes != nullptr) {
    failure_reason_codes->push_back(kKnowledgeQueryEncoderProviderMissing.data());
  }
  return std::nullopt;
}

class RuntimeProductionQueryEncoderHealthState {
 public:
  RuntimeProductionQueryEncoderHealthState(
      std::optional<RuntimeKnowledgeEmbeddingProviderConfig> provider_config,
      std::shared_ptr<infra::secret::ISecretManager> secret_manager,
      std::shared_ptr<llm::ILLMTransport> transport,
      std::string composition_owner)
      : provider_config_(std::move(provider_config)),
        secret_manager_(std::move(secret_manager)),
        transport_(std::move(transport)),
        composition_owner_(std::move(composition_owner)) {
    if (!provider_config_.has_value()) {
      current_failure_reason_codes_ = {std::string(kKnowledgeQueryEncoderProviderMissing)};
    }
  }

  [[nodiscard]] bool has_provider_configuration() const {
    std::scoped_lock lock(mutex_);
    return provider_config_.has_value() && provider_config_->has_consistent_values();
  }

  [[nodiscard]] bool current_available() const {
    std::scoped_lock lock(mutex_);
    return local_contract_ready_locked();
  }

  [[nodiscard]] bool ensure_canary_ready(std::string_view normalized_text) {
    std::scoped_lock lock(mutex_);
    if (!local_contract_ready_locked()) {
      return false;
    }

    if (cached_query_text_ == normalized_text && !cached_embedding_.empty()) {
      return true;
    }

    return send_embedding_request_locked(normalized_text);
  }

  [[nodiscard]] std::vector<float> encode_query(std::string_view normalized_text) {
    std::scoped_lock lock(mutex_);
    if (!local_contract_ready_locked()) {
      return {};
    }

    if (cached_query_text_ == normalized_text && !cached_embedding_.empty()) {
      return cached_embedding_;
    }

    if (!send_embedding_request_locked(normalized_text)) {
      return {};
    }

    return cached_embedding_;
  }

  [[nodiscard]] std::vector<std::string> recent_reason_codes() const {
    std::scoped_lock lock(mutex_);
    return current_failure_reason_codes_;
  }

 private:
  [[nodiscard]] infra::secret::SecretAccessContext make_access_context_locked(
      std::string_view purpose) const {
    const std::string request_id = std::string("runtime-knowledge-query-encoder:") +
                                   std::string(purpose);
    return infra::secret::SecretAccessContext{
        .request_id = request_id,
        .session_id = std::nullopt,
        .task_id = request_id,
        .actor = composition_owner_,
        .consumer_module = std::string("runtime.knowledge.query_encoder"),
        .permission_domain = std::string("llm.provider.auth"),
    };
  }

  [[nodiscard]] bool local_contract_ready_locked() const {
    if (!provider_config_.has_value() || !provider_config_->has_consistent_values()) {
      current_failure_reason_codes_ = {std::string(kKnowledgeQueryEncoderProviderMissing)};
      cached_query_text_.clear();
      cached_embedding_.clear();
      remote_failure_active_ = false;
      return false;
    }

    if (transport_ == nullptr) {
      current_failure_reason_codes_ = {std::string(kKnowledgeQueryEncoderTransportMissing)};
      cached_query_text_.clear();
      cached_embedding_.clear();
      remote_failure_active_ = false;
      return false;
    }

    if (provider_config_->auth_ref.starts_with(kSecretRefPrefix)) {
      if (secret_manager_ == nullptr) {
        current_failure_reason_codes_ = {std::string(kKnowledgeQueryEncoderSecretMissing)};
        cached_query_text_.clear();
        cached_embedding_.clear();
        remote_failure_active_ = false;
        return false;
      }

      const auto secret_name = secret_name_from_auth_ref(provider_config_->auth_ref);
      if (!secret_name.has_value()) {
        current_failure_reason_codes_ = {std::string(kKnowledgeQueryEncoderInvalidAuthRef)};
        cached_query_text_.clear();
        cached_embedding_.clear();
        remote_failure_active_ = false;
        return false;
      }

      const auto inspection = secret_manager_->inspect(*secret_name);
      if (!inspection.ok || !inspection.is_valid()) {
        current_failure_reason_codes_ = {std::string(kKnowledgeQueryEncoderSecretMissing)};
        cached_query_text_.clear();
        cached_embedding_.clear();
        remote_failure_active_ = false;
        return false;
      }
    } else if (!provider_config_->auth_ref.starts_with(kProfileRefPrefix)) {
      current_failure_reason_codes_ = {std::string(kKnowledgeQueryEncoderInvalidAuthRef)};
      cached_query_text_.clear();
      cached_embedding_.clear();
      remote_failure_active_ = false;
      return false;
    }

    if (remote_failure_active_) {
      return false;
    }

    current_failure_reason_codes_.clear();
    return true;
  }

  [[nodiscard]] std::optional<std::string> materialize_bearer_token_locked() const {
    if (!provider_config_.has_value()) {
      current_failure_reason_codes_ = {std::string(kKnowledgeQueryEncoderProviderMissing)};
      return std::nullopt;
    }

    if (provider_config_->auth_ref.starts_with(kProfileRefPrefix)) {
      return std::string();
    }

    const auto secret_name = secret_name_from_auth_ref(provider_config_->auth_ref);
    if (!secret_name.has_value() || secret_manager_ == nullptr) {
      current_failure_reason_codes_ = {std::string(kKnowledgeQueryEncoderSecretMissing)};
      return std::nullopt;
    }

    const auto access_context = make_access_context_locked("materialize");
    const auto handle_result = secret_manager_->get_secret(
        infra::secret::SecretQuery{
            .secret_name = *secret_name,
            .version_hint = {},
            .purpose = std::string("knowledge_query_embedding"),
            .access_mode = infra::secret::SecretAccessMode::Materialize,
        },
        access_context);
    if (!handle_result.ok || !handle_result.is_valid()) {
      current_failure_reason_codes_ = {std::string(kKnowledgeQueryEncoderSecretMaterializeFailed)};
      return std::nullopt;
    }

    const auto materialized_result = secret_manager_->materialize(handle_result.handle,
                                                                  access_context);
    if (!materialized_result.ok || !materialized_result.is_valid() ||
        materialized_result.materialized_secret == nullptr ||
        !materialized_result.materialized_secret->is_accessible()) {
      current_failure_reason_codes_ = {std::string(kKnowledgeQueryEncoderSecretMaterializeFailed)};
      return std::nullopt;
    }

    const std::string token =
        secure_buffer_to_string(*materialized_result.materialized_secret);
    if (materialized_result.lease.is_valid()) {
      (void)secret_manager_->release(materialized_result.lease);
    }
    return token;
  }

  [[nodiscard]] bool send_embedding_request_locked(std::string_view normalized_text) const {
    if (!provider_config_.has_value() || !provider_config_->has_consistent_values()) {
      current_failure_reason_codes_ = {std::string(kKnowledgeQueryEncoderProviderMissing)};
      cached_query_text_.clear();
      cached_embedding_.clear();
      remote_failure_active_ = false;
      return false;
    }

    const auto bearer_token = materialize_bearer_token_locked();
    if (!bearer_token.has_value()) {
      cached_query_text_.clear();
      cached_embedding_.clear();
      remote_failure_active_ = false;
      return false;
    }

    llm::LLMTransportRequest request;
    request.method = llm::LLMTransportMethod::Post;
    request.url = build_embedding_url(provider_config_->base_url);
    request.auth_ref = provider_config_->auth_ref;
    request.base_url_alias = provider_config_->base_url_alias;
    request.snapshot_version = provider_config_->snapshot_version;
    request.timeout_ms = provider_config_->timeout_ms;
    request.headers = {
        llm::LLMTransportHeader{.name = "Content-Type", .value = "application/json"},
        llm::LLMTransportHeader{.name = "Accept", .value = "application/json"},
    };
    if (!bearer_token->empty()) {
      request.headers.push_back(llm::LLMTransportHeader{
          .name = "Authorization",
          .value = std::string("Bearer ") + *bearer_token,
      });
    }
    request.body = std::string("{\"model\":\"") +
                   escape_json(provider_config_->model_id) +
                   "\",\"input\":\"" +
                   escape_json(normalized_text) +
                   "\"}";

    const auto response = transport_->send(request);
    if (!response.has_consistent_values()) {
      current_failure_reason_codes_ = {std::string(kKnowledgeQueryEncoderTransportError)};
      cached_query_text_.clear();
      cached_embedding_.clear();
      remote_failure_active_ = true;
      return false;
    }

    if (!response.ok()) {
      if (response.status_code == 401U || response.status_code == 403U) {
        current_failure_reason_codes_ = {std::string(kKnowledgeQueryEncoderInvalidSecret)};
      } else if (response.status_code >= 400U && response.status_code < 500U) {
        current_failure_reason_codes_ = {std::string(kKnowledgeQueryEncoderProviderRejected)};
      } else {
        current_failure_reason_codes_ = {std::string(kKnowledgeQueryEncoderTransportError)};
      }
      cached_query_text_.clear();
      cached_embedding_.clear();
      remote_failure_active_ = true;
      return false;
    }

    const auto embedding = extract_embedding_vector(response.body);
    if (embedding.empty()) {
      current_failure_reason_codes_ = {std::string(kKnowledgeQueryEncoderEmptyEmbedding)};
      cached_query_text_.clear();
      cached_embedding_.clear();
      remote_failure_active_ = true;
      return false;
    }

    cached_query_text_ = std::string(normalized_text);
    cached_embedding_ = embedding;
    current_failure_reason_codes_.clear();
    remote_failure_active_ = false;
    return true;
  }

  std::optional<RuntimeKnowledgeEmbeddingProviderConfig> provider_config_;
  std::shared_ptr<infra::secret::ISecretManager> secret_manager_;
  std::shared_ptr<llm::ILLMTransport> transport_;
  std::string composition_owner_;
  mutable std::mutex mutex_;
  mutable std::string cached_query_text_;
  mutable std::vector<float> cached_embedding_;
  mutable std::vector<std::string> current_failure_reason_codes_;
  mutable bool remote_failure_active_ = false;
};

class RuntimeProductionEmbeddingQueryEncoder final
    : public knowledge::retrieve::IQueryEncoder {
 public:
  explicit RuntimeProductionEmbeddingQueryEncoder(
      std::shared_ptr<RuntimeProductionQueryEncoderHealthState> state)
      : state_(std::move(state)) {}

  [[nodiscard]] std::vector<float> encode(std::string_view query_text) const override {
    if (state_ == nullptr) {
      return {};
    }
    return state_->encode_query(query_text);
  }

  [[nodiscard]] bool available() const override {
    return state_ != nullptr && state_->current_available();
  }

 private:
  std::shared_ptr<RuntimeProductionQueryEncoderHealthState> state_;
};

[[nodiscard]] std::int64_t current_time_ms() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}

[[nodiscard]] bool environment_flag_enabled(const char* name) {
  const char* value = std::getenv(name);
  if (value == nullptr) {
    return false;
  }

  const std::string text(value);
  return text == "1" || text == "true" || text == "TRUE" ||
         text == "on" || text == "yes";
}

[[nodiscard]] bool runtime_cognition_first_requested() {
  return environment_flag_enabled("DASALL_RUNTIME_COGNITION_FIRST");
}

[[nodiscard]] std::string lower_ascii_copy(std::string_view value) {
  std::string lowered;
  lowered.reserve(value.size());
  for (const unsigned char character : value) {
    lowered.push_back(static_cast<char>(std::tolower(character)));
  }

  return lowered;
}

[[nodiscard]] bool contains_string(const std::vector<std::string>& values,
                                   std::string_view value) {
  return std::find(values.begin(), values.end(), value) != values.end();
}

[[nodiscard]] bool runtime_profile_allows_knowledge_hybrid_canary(
    std::string_view profile_id) {
  static const std::vector<std::string> kAllowedProfiles{
      "desktop_full",
      "cloud_full",
      "edge_balanced",
  };
  return contains_string(kAllowedProfiles, profile_id);
}

[[nodiscard]] std::vector<std::string> knowledge_hybrid_canary_allowed_corpora() {
  return {
      "architecture_reference",
      "adr_normative",
      "ssot_normative",
  };
}

[[nodiscard]] std::vector<std::string> derive_knowledge_hybrid_canary_allowlist(
    std::string_view profile_id,
    bool hybrid_runtime_configured) {
  if (!hybrid_runtime_configured ||
      !runtime_profile_allows_knowledge_hybrid_canary(profile_id)) {
    return {};
  }

  return knowledge_hybrid_canary_allowed_corpora();
}

[[nodiscard]] bool contains_all_tags(const std::vector<std::string>& candidate_tags,
                                     const std::vector<std::string>& required_tags) {
  if (required_tags.empty()) {
    return true;
  }

  std::vector<std::string> normalized_tags;
  normalized_tags.reserve(candidate_tags.size());
  for (const auto& tag : candidate_tags) {
    normalized_tags.push_back(lower_ascii_copy(tag));
  }

  return std::all_of(required_tags.begin(), required_tags.end(), [&](const auto& required_tag) {
    return contains_string(normalized_tags, lower_ascii_copy(required_tag));
  });
}

[[nodiscard]] bool matches_language(const std::optional<std::string>& candidate_language,
                                    const std::optional<std::string>& required_language) {
  if (!required_language.has_value()) {
    return true;
  }

  return candidate_language.has_value() &&
         lower_ascii_copy(*candidate_language) == lower_ascii_copy(*required_language);
}

[[nodiscard]] std::vector<std::string> deserialize_tags(const std::string& serialized_tags) {
  std::vector<std::string> tags;
  std::istringstream buffer(serialized_tags);
  std::string line;
  while (std::getline(buffer, line)) {
    if (!line.empty()) {
      tags.push_back(std::move(line));
    }
  }

  return tags;
}

[[nodiscard]] fs::path knowledge_dense_snapshot_database_path(
    const fs::path& snapshot_dir) {
  return snapshot_dir / std::string(kKnowledgeDenseSnapshotDatabaseName);
}

class RuntimeKnowledgeVectorRecallStore final : public knowledge::retrieve::IVectorRecallStore {
 public:
  RuntimeKnowledgeVectorRecallStore(memory::MemoryConfig memory_config,
                                    knowledge::DenseStoreFactoryContext context)
      : memory_config_(std::move(memory_config)),
        context_(std::move(context)) {}

  [[nodiscard]] bool available() const override {
    const auto manifest = load_active_manifest();
    if (!manifest.has_value() || !manifest->vector_enabled) {
      return false;
    }

    const auto snapshot_dir = context_.snapshots_root / manifest->snapshot_id;
    const auto dense_database_path = knowledge_dense_snapshot_database_path(snapshot_dir);
    if (!fs::exists(dense_database_path)) {
      return false;
    }

    return memory::detached_vector_index_backend_available(memory_config_,
                                                           dense_database_path);
  }

  [[nodiscard]] knowledge::retrieve::DenseQueryInputMode query_input_mode() const override {
    return knowledge::retrieve::DenseQueryInputMode::EmbeddingRequired;
  }

  [[nodiscard]] std::vector<knowledge::retrieve::RecallHit> search(
      const knowledge::retrieve::DenseQueryRequest& request) const override {
    if (!request.has_consistent_values()) {
      return {};
    }

    const auto manifest = load_active_manifest();
    if (!manifest.has_value() || !manifest->vector_enabled) {
      return {};
    }

    const auto snapshot_dir = context_.snapshots_root / manifest->snapshot_id;
    const auto dense_database_path = knowledge_dense_snapshot_database_path(snapshot_dir);
    const auto lexical_database_path = snapshot_dir / "lexical.sqlite";
    if (!fs::exists(dense_database_path) || !fs::exists(lexical_database_path)) {
      return {};
    }

    if (!memory::detached_vector_index_backend_available(memory_config_,
                                                         dense_database_path)) {
      return {};
    }

    const auto search_limit = static_cast<int>(std::max<std::size_t>(request.top_k * 8U, 32U));
    const auto vector_hits = memory::search_detached_vector_index_by_embedding(
        memory_config_,
        dense_database_path,
        request.query_embedding,
        search_limit);
    if (vector_hits.empty()) {
      return {};
    }

    sqlite3* database = nullptr;
    if (sqlite3_open_v2(lexical_database_path.string().c_str(),
                        &database,
                        SQLITE_OPEN_READONLY | SQLITE_OPEN_NOMUTEX,
                        nullptr) != SQLITE_OK) {
      if (database != nullptr) {
        sqlite3_close(database);
      }
      return {};
    }

    struct DatabaseGuard {
      sqlite3* handle = nullptr;
      ~DatabaseGuard() {
        if (handle != nullptr) {
          sqlite3_close(handle);
        }
      }
    } database_guard{database};

    sqlite3_stmt* statement = nullptr;
    constexpr const char* query_sql =
        "SELECT corpus_id, document_id, chunk_text, citation_ref, updated_at, authority_level, tags, language "
        "FROM chunks WHERE chunk_id = ?1";
    if (sqlite3_prepare_v2(database, query_sql, -1, &statement, nullptr) != SQLITE_OK) {
      return {};
    }

    struct StatementGuard {
      sqlite3_stmt* handle = nullptr;
      ~StatementGuard() {
        if (handle != nullptr) {
          sqlite3_finalize(handle);
        }
      }
    } statement_guard{statement};

    std::set<std::string, std::less<>> emitted_chunk_ids;
    std::vector<knowledge::retrieve::RecallHit> hits;
    hits.reserve(request.top_k);

    for (const auto& vector_hit : vector_hits) {
      if (vector_hit.doc_id.empty() || !emitted_chunk_ids.insert(vector_hit.doc_id).second) {
        continue;
      }

      sqlite3_reset(statement);
      sqlite3_clear_bindings(statement);
      sqlite3_bind_text(statement, 1, vector_hit.doc_id.c_str(), -1, SQLITE_TRANSIENT);
      if (sqlite3_step(statement) != SQLITE_ROW) {
        continue;
      }

      knowledge::retrieve::RecallHit hit;
      if (const auto* corpus_id = sqlite3_column_text(statement, 0); corpus_id != nullptr) {
        hit.corpus_id = reinterpret_cast<const char*>(corpus_id);
      }
      if (const auto* document_id = sqlite3_column_text(statement, 1); document_id != nullptr) {
        hit.document_id = reinterpret_cast<const char*>(document_id);
      }
      if (const auto* chunk_text = sqlite3_column_text(statement, 2); chunk_text != nullptr) {
        hit.raw_snippet = reinterpret_cast<const char*>(chunk_text);
      }
      if (const auto* citation_ref = sqlite3_column_text(statement, 3); citation_ref != nullptr) {
        hit.citation_ref = reinterpret_cast<const char*>(citation_ref);
      }
      hit.updated_at = sqlite3_column_int64(statement, 4);
      hit.authority_level = static_cast<knowledge::AuthorityLevel>(
          sqlite3_column_int(statement, 5));
      if (const auto* tags = sqlite3_column_text(statement, 6); tags != nullptr) {
        hit.tags = deserialize_tags(reinterpret_cast<const char*>(tags));
      }
      std::optional<std::string> language;
      if (sqlite3_column_type(statement, 7) != SQLITE_NULL) {
        language = std::string(reinterpret_cast<const char*>(sqlite3_column_text(statement, 7)));
      }

      if (!request.allowed_corpus_ids.empty() &&
          !contains_string(request.allowed_corpus_ids, hit.corpus_id)) {
        continue;
      }
      if (!matches_language(language, request.required_language) ||
          !contains_all_tags(hit.tags, request.required_tags)) {
        continue;
      }

      hit.chunk_id = vector_hit.doc_id;
      hit.score = 1.0F /
                  (1.0F + static_cast<float>(std::max(0.0, static_cast<double>(vector_hit.score))));
      if (!hit.has_consistent_values()) {
        continue;
      }

      hits.push_back(std::move(hit));
      if (hits.size() >= request.top_k) {
        break;
      }
    }

    return hits;
  }

 private:
  [[nodiscard]] std::optional<knowledge::IndexManifest> load_active_manifest() const {
    if (!context_.active_manifest) {
      return std::nullopt;
    }

    const auto manifest = context_.active_manifest();
    if (!manifest.has_value() || !manifest->has_consistent_values() ||
        manifest->snapshot_id.empty()) {
      return std::nullopt;
    }

    return manifest;
  }

  memory::MemoryConfig memory_config_;
  knowledge::DenseStoreFactoryContext context_;
};

class RuntimeDetachedVectorQueryEncoder final : public knowledge::retrieve::IQueryEncoder {
 public:
  explicit RuntimeDetachedVectorQueryEncoder(memory::MemoryConfig memory_config)
      : memory_config_(std::move(memory_config)) {}

  [[nodiscard]] std::vector<float> encode(std::string_view query_text) const override {
    return memory::encode_detached_vector_query_for_local_fallback(memory_config_,
                                                                   query_text);
  }

  [[nodiscard]] bool available() const override {
    return memory::detached_vector_local_query_encoder_available(memory_config_);
  }

 private:
  memory::MemoryConfig memory_config_;
};

[[nodiscard]] knowledge::index::DenseSnapshotBuildResult build_knowledge_dense_snapshot(
    const memory::MemoryConfig& memory_config,
    const knowledge::index::DenseSnapshotBuildRequest& request) {
  knowledge::index::DenseSnapshotBuildResult result;
  if (!request.has_consistent_values()) {
    result.warnings.push_back("dense_snapshot_request_inconsistent");
    return result;
  }

  const auto dense_database_path = knowledge_dense_snapshot_database_path(request.snapshot_dir);
  std::error_code remove_error;
  fs::remove(dense_database_path, remove_error);

  auto adapter = memory::create_detached_vector_index_adapter(memory_config,
                                                              dense_database_path);
  if (adapter == nullptr || !adapter->is_available()) {
    result.warnings.push_back("dense_snapshot_backend_unavailable");
    return result;
  }

  for (const auto& chunk : request.chunk_records) {
    const auto upsert = adapter->upsert(memory::VectorDocument{
        .doc_id = chunk.chunk_id,
        .doc_type = chunk.corpus_id,
        .text = chunk.chunk_text,
        .embedding = {},
    });
    if (!upsert.ok) {
      std::error_code cleanup_error;
      fs::remove(dense_database_path, cleanup_error);
      result.warnings.push_back("dense_snapshot_upsert_failed");
      return result;
    }
  }

  if (!fs::exists(dense_database_path)) {
    result.warnings.push_back("dense_snapshot_artifact_missing");
    return result;
  }

  result.ok = true;
  return result;
}

constexpr std::string_view kCognitionPlanningStage = "planning";
constexpr std::string_view kCognitionExecutionStage = "execution";

[[nodiscard]] std::string make_cognition_first_planning_payload() {
  return std::string{"{"}
         + "\"schema_version\":\"cognition.plan.v1\","
         + "\"plan_id\":\"plan-runtime-cognition-first\","
         + "\"revision\":1,"
         + "\"nodes\":[{"
         + "\"node_id\":\"plan-node:runtime-cognition-first\","
         + "\"objective\":\"collect governed evidence through the builtin dataset tool\","
         + "\"success_signal\":\"runtime_evidence_collected\","
         + "\"action_kind_hint\":\"tool_action\","
         + "\"depends_on\":[],"
         + "\"evidence_refs\":[\"runtime:cognition-first-evidence\"]}],"
         + "\"edges\":[],"
         + "\"open_questions\":[],"
         + "\"plan_rationale\":\"runtime cognition-first evidence should project a governed plan graph\","
         + "\"estimated_complexity\":1}"
      ;
}

[[nodiscard]] std::string make_cognition_first_execution_payload() {
  return std::string{"{"}
         + "\"schema_version\":\"cognition.reasoning.v1\","
         + "\"decision_kind\":\"ExecuteAction\","
         + "\"confidence\":0.82,"
         + "\"rationale\":\"runtime cognition-first evidence should preserve governed tool intent\","
         + "\"selected_node_id\":\"plan-node:runtime-cognition-first\","
         + "\"tool_intent_hint\":{"
         + "\"tool_name\":\"agent.dataset\","
         + "\"intent_summary\":\"query runtime-visible data through tool governance\","
         + "\"argument_hints\":[\"query=current_state\"],"
         + "\"evidence_refs\":[\"runtime:cognition-first-evidence\"]},"
         + "\"clarification_needed\":false,"
         + "\"clarification_question\":null,"
         + "\"response_outline\":null,"
         + "\"candidate_scores\":[{"
         + "\"candidate_name\":\"execute_action\","
         + "\"score\":0.82,"
         + "\"rationale\":\"runtime cognition-first evidence should execute the builtin tool\"}]}"
      ;
}

[[nodiscard]] std::string extract_cognition_first_prompt(
    const llm::LLMGenerateRequest& request) {
  if (request.request.messages.has_value()) {
    for (auto it = request.request.messages->rbegin();
         it != request.request.messages->rend();
         ++it) {
      if (it->rfind("user:", 0U) == 0U) {
        return it->substr(5U);
      }
      if (!it->empty()) {
        return *it;
      }
    }
  }

  return "cognition-first evidence";
}

[[nodiscard]] llm::LLMManagerResult make_cognition_first_success_result(
    const llm::LLMGenerateRequest& request,
    std::string content) {
  contracts::LLMResponse response;
  response.request_id = request.request.request_id;
  response.llm_call_id = std::string{"runtime-cognition-first-call"};
  response.response_kind = contracts::LLMResponseKind::DirectResponse;
  response.content_payload = std::move(content);
  response.completed_at = current_time_ms();
  response.model_name = std::string{"runtime.cognition-first.evidence"};
  response.prompt_id = std::string{"runtime.cognition-first"};
  response.prompt_version = std::string{"v1"};
  response.finish_reason = std::string{"stop"};
  response.input_tokens = 16U;
  response.output_tokens = 8U;
  response.total_tokens = 24U;

  llm::LLMManagerResult result;
  result.response = std::move(response);
  result.resolved_route = std::string{"runtime.cognition-first."} +
                          (request.stage.empty() ? std::string{"response"}
                                                 : request.stage);
  result.attempted_routes = {result.resolved_route};
  return result;
}

// This helper exists only to stabilize the controlled cognition-first evidence gate.
class ScriptedCognitionFirstLLMManager final : public llm::ILLMManager {
 public:
  bool init(const llm::LLMSubsystemConfig&) override {
    return true;
  }

  llm::LLMManagerResult generate(const llm::LLMGenerateRequest& request) override {
    if (request.stage == kCognitionPlanningStage) {
      return make_cognition_first_success_result(
          request,
          make_cognition_first_planning_payload());
    }

    if (request.stage == kCognitionExecutionStage) {
      return make_cognition_first_success_result(
          request,
          make_cognition_first_execution_payload());
    }

    return make_cognition_first_success_result(
        request,
        std::string{"runtime unary integration completed: "} +
            extract_cognition_first_prompt(request));
  }

  llm::LLMManagerResult stream_generate(const llm::LLMGenerateRequest& request,
                                        llm::IStreamObserver*) override {
    return generate(request);
  }

  bool abandon_call(std::string_view llm_call_id) override {
    return !llm_call_id.empty();
  }

  llm::HealthStatus health_check() const override {
    return llm::HealthStatus{
        .ready = true,
        .degraded = false,
        .message = "runtime cognition-first scripted llm manager healthy",
    };
  }
};

struct RuntimeObservabilityBundle {
  std::shared_ptr<infra::logging::ILogger> logger;
  std::shared_ptr<infra::audit::IAuditLogger> audit_logger;
  std::shared_ptr<infra::metrics::IMetricsProvider> metrics_provider;
  std::shared_ptr<infra::tracing::ITracerProvider> tracer_provider;
  std::shared_ptr<infra::IHealthMonitor> health_monitor;
  std::shared_ptr<tools::ops::ToolAuditBridge> tool_audit_bridge;
  std::shared_ptr<tools::ops::ToolMetricsBridge> tool_metrics_bridge;
  std::shared_ptr<tools::ops::ToolTraceBridge> tool_trace_bridge;
  std::shared_ptr<tools::ops::ToolHealthProbe> tool_health_probe;
  std::vector<std::shared_ptr<infra::IHealthProbe>> health_probes;
  std::string error;

  [[nodiscard]] bool ok() const {
      return logger != nullptr && audit_logger != nullptr &&
        metrics_provider != nullptr &&
           tracer_provider != nullptr && health_monitor != nullptr &&
           tool_audit_bridge != nullptr && tool_metrics_bridge != nullptr &&
           tool_trace_bridge != nullptr && tool_health_probe != nullptr &&
           error.empty();
  }
};

[[nodiscard]] std::string telemetry_value_or(std::string_view value,
                                             std::string_view fallback) {
  return value.empty() ? std::string(fallback) : std::string(value);
}

[[nodiscard]] std::string join_values(const std::vector<std::string>& values) {
  std::ostringstream stream;
  for (std::size_t index = 0; index < values.size(); ++index) {
    if (index > 0U) {
      stream << ',';
    }
    stream << values[index];
  }
  return stream.str();
}

[[nodiscard]] std::string knowledge_query_kind_name(
    const std::optional<knowledge::KnowledgeQueryKind>& query_kind) {
  if (!query_kind.has_value()) {
    return "unknown";
  }

  switch (*query_kind) {
    case knowledge::KnowledgeQueryKind::FactLookup:
      return "fact_lookup";
    case knowledge::KnowledgeQueryKind::ProcedureLookup:
      return "procedure_lookup";
    case knowledge::KnowledgeQueryKind::DiagnosticContext:
      return "diagnostic_context";
    case knowledge::KnowledgeQueryKind::PolicyEvidence:
      return "policy_evidence";
    case knowledge::KnowledgeQueryKind::MultiHop:
      return "multi_hop";
  }

  return "unknown";
}

[[nodiscard]] std::string knowledge_retrieval_mode_name(
    const std::optional<knowledge::RetrievalMode>& retrieval_mode) {
  if (!retrieval_mode.has_value()) {
    return "unknown";
  }

  switch (*retrieval_mode) {
    case knowledge::RetrievalMode::LexicalOnly:
      return "lexical_only";
    case knowledge::RetrievalMode::DenseOnly:
      return "dense_only";
    case knowledge::RetrievalMode::Hybrid:
      return "hybrid";
  }

  return "unknown";
}

[[nodiscard]] bool knowledge_event_failed(const knowledge::KnowledgeTelemetryEvent& event) {
  return event.result != "success";
}

[[nodiscard]] std::string knowledge_event_outcome(
    const knowledge::KnowledgeTelemetryEvent& event) {
  if (event.result == "success" && event.degraded) {
    return "degraded";
  }
  if (event.result == "success") {
    return "success";
  }
  return "failure";
}

[[nodiscard]] std::string knowledge_audit_action(
    const knowledge::KnowledgeTelemetryEvent& event) {
  if (event.result == "success" && event.degraded) {
    return "knowledge.retrieve.degraded";
  }
  if (event.result == "success") {
    return "knowledge.retrieve.completed";
  }
  if (event.result == "invalid_telemetry_payload") {
    return "knowledge.retrieve.invalid_payload";
  }
  return "knowledge.retrieve.failed";
}

[[nodiscard]] infra::AuditOutcome knowledge_audit_outcome(
    const knowledge::KnowledgeTelemetryEvent& event) {
  if (event.result == "success" && event.degraded) {
    return infra::AuditOutcome::Escalated;
  }
  if (event.result == "success") {
    return infra::AuditOutcome::Succeeded;
  }
  return infra::AuditOutcome::Failed;
}

[[nodiscard]] std::int64_t knowledge_latency_ms(
    const knowledge::KnowledgeTelemetryEvent& event) {
  return event.latency_ms >= 0 ? event.latency_ms : 0;
}

[[nodiscard]] infra::metrics::MetricLabels make_knowledge_metric_labels(
    const knowledge::KnowledgeTelemetryEvent& event) {
  return infra::metrics::MetricLabels{
      .module = "knowledge",
      .stage = telemetry_value_or(event.event_name, "knowledge_retrieve"),
      .profile = telemetry_value_or(event.profile_id, "unknown"),
      .outcome = knowledge_event_outcome(event),
      .error_code = knowledge_event_failed(event) ? event.error_category : std::string(),
  };
}

[[nodiscard]] infra::tracing::TraceAttributeMap make_knowledge_trace_attributes(
    const knowledge::KnowledgeTelemetryEvent& event) {
  infra::tracing::TraceAttributeMap attrs;
  attrs.emplace("knowledge.component", telemetry_value_or(event.component, "unknown"));
  attrs.emplace("knowledge.snapshot_id", telemetry_value_or(event.snapshot_id, "unknown"));
  attrs.emplace("knowledge.request_id", telemetry_value_or(event.request_id, "unknown"));
  attrs.emplace("knowledge.profile_id", telemetry_value_or(event.profile_id, "unknown"));
  attrs.emplace("knowledge.query_kind", knowledge_query_kind_name(event.query_kind));
  attrs.emplace("knowledge.retrieval_mode",
                knowledge_retrieval_mode_name(event.retrieval_mode));
  attrs.emplace("knowledge.outcome", knowledge_event_outcome(event));
  attrs.emplace("knowledge.vector_backend_ready",
                static_cast<std::int64_t>(event.vector_backend_ready ? 1 : 0));
  attrs.emplace("knowledge.warning_count", static_cast<std::int64_t>(event.warning_count));
  attrs.emplace("knowledge.sparse_hit_count",
                static_cast<std::int64_t>(event.sparse_hit_count));
  attrs.emplace("knowledge.dense_hit_count",
                static_cast<std::int64_t>(event.dense_hit_count));
  attrs.emplace("knowledge.selected_corpora", join_values(event.corpus_ids));
  attrs.emplace("knowledge.reason_codes", join_values(event.reason_codes));
  attrs.emplace("knowledge.warning_summary", join_values(event.warning_summary));
  attrs.emplace("knowledge.corpus_count", static_cast<std::int64_t>(event.corpus_count));
  attrs.emplace("knowledge.result_count", static_cast<std::int64_t>(event.result_count));
  return attrs;
}

[[nodiscard]] std::vector<std::string> make_knowledge_audit_side_effects(
    const knowledge::KnowledgeTelemetryEvent& event) {
  std::vector<std::string> side_effects;
  side_effects.reserve(15U);
  side_effects.push_back("component=" + telemetry_value_or(event.component, "unknown"));
  side_effects.push_back("snapshot_id=" + telemetry_value_or(event.snapshot_id, "unknown"));
  side_effects.push_back("profile_id=" + telemetry_value_or(event.profile_id, "unknown"));
  side_effects.push_back("query_kind=" + knowledge_query_kind_name(event.query_kind));
  side_effects.push_back("retrieval_mode=" +
                         knowledge_retrieval_mode_name(event.retrieval_mode));
  side_effects.push_back("outcome=" + knowledge_event_outcome(event));
  side_effects.push_back("vector_backend_ready=" +
                         std::string(event.vector_backend_ready ? "true" : "false"));
  side_effects.push_back("warning_count=" + std::to_string(event.warning_count));
  side_effects.push_back("warning_summary=" + join_values(event.warning_summary));
  side_effects.push_back("selected_corpora=" + join_values(event.corpus_ids));
  side_effects.push_back("sparse_hit_count=" + std::to_string(event.sparse_hit_count));
  side_effects.push_back("dense_hit_count=" + std::to_string(event.dense_hit_count));
  side_effects.push_back("corpus_count=" + std::to_string(event.corpus_count));
  side_effects.push_back("result_count=" + std::to_string(event.result_count));
  side_effects.push_back("error_category=" +
                         telemetry_value_or(event.error_category, "none"));
  if (!event.reason_codes.empty()) {
    side_effects.push_back("reason_codes=" + join_values(event.reason_codes));
  }
  return side_effects;
}

[[nodiscard]] infra::LogLevel knowledge_log_level(
    const knowledge::KnowledgeTelemetryEvent& event,
    bool fallback) {
  if (event.result == "success" && !event.degraded) {
    return fallback ? infra::LogLevel::Warn : infra::LogLevel::Info;
  }
  if (event.result == "success") {
    return infra::LogLevel::Warn;
  }
  return infra::LogLevel::Error;
}

[[nodiscard]] infra::LogEvent make_knowledge_log_event(
    const knowledge::KnowledgeTelemetryEvent& event,
    bool fallback) {
  infra::LogEvent log_event;
  log_event.level = knowledge_log_level(event, fallback);
  log_event.module = "knowledge";
  log_event.message = knowledge_audit_action(event);
  log_event.attrs = {
      {"request_id", telemetry_value_or(event.request_id, "unknown")},
      {"session_id", telemetry_value_or(event.session_id, "unknown")},
      {"component", telemetry_value_or(event.component, "unknown")},
      {"snapshot_id", telemetry_value_or(event.snapshot_id, "unknown")},
      {"profile_id", telemetry_value_or(event.profile_id, "unknown")},
      {"query_kind", knowledge_query_kind_name(event.query_kind)},
      {"retrieval_mode", knowledge_retrieval_mode_name(event.retrieval_mode)},
      {"outcome", knowledge_event_outcome(event)},
      {"vector_backend_ready", event.vector_backend_ready ? "true" : "false"},
      {"warning_count", std::to_string(event.warning_count)},
      {"warning_summary", join_values(event.warning_summary)},
      {"selected_corpora", join_values(event.corpus_ids)},
      {"sparse_hit_count", std::to_string(event.sparse_hit_count)},
      {"dense_hit_count", std::to_string(event.dense_hit_count)},
      {"reason_codes", join_values(event.reason_codes)},
      {"error_category", telemetry_value_or(event.error_category, "none")},
      {"telemetry_path", fallback ? "fallback" : "primary"},
  };
  log_event.ts = current_time_ms();
  return log_event;
}

class KnowledgeMetricsSinkState {
 public:
  explicit KnowledgeMetricsSinkState(
      std::shared_ptr<infra::metrics::IMetricsProvider> provider)
      : provider_(std::move(provider)) {}

  void emit(const knowledge::KnowledgeTelemetryEvent& event) {
    auto meter = ensure_meter();
    if (meter == nullptr) {
      return;
    }

    ensure_instruments(*meter);

    const auto timestamp = current_time_ms();
    (void)meter->record(infra::metrics::MetricSample{
        .identity_ref = event_total_identity(),
        .value = 1.0,
        .ts_unix_ms = timestamp,
        .labels = make_knowledge_metric_labels(event),
    });
    (void)meter->record(infra::metrics::MetricSample{
        .identity_ref = latency_identity(),
        .value = static_cast<double>(knowledge_latency_ms(event)),
        .ts_unix_ms = timestamp,
        .labels = make_knowledge_metric_labels(event),
    });
  }

 private:
  [[nodiscard]] static infra::metrics::MetricIdentity event_total_identity() {
    return infra::metrics::MetricIdentity{
        .name = "knowledge.retrieve.event_total",
        .type = infra::metrics::MetricType::Counter,
        .unit = "1",
        .description = "Knowledge retrieve telemetry events emitted to production sinks",
    };
  }

  [[nodiscard]] static infra::metrics::MetricIdentity latency_identity() {
    return infra::metrics::MetricIdentity{
        .name = "knowledge.retrieve.latency_ms",
        .type = infra::metrics::MetricType::Histogram,
        .unit = "ms",
        .description = "Knowledge retrieve telemetry latency in milliseconds",
    };
  }

  [[nodiscard]] std::shared_ptr<infra::metrics::IMeter> ensure_meter() {
    if (meter_ != nullptr) {
      return meter_;
    }
    if (provider_ == nullptr) {
      return nullptr;
    }

    meter_ = provider_->get_meter(infra::metrics::MeterScope{
        .name = "dasall.knowledge.telemetry",
        .version = "1.0.0",
        .schema_url = "schema://knowledge/telemetry/v1",
    });
    return meter_;
  }

  void ensure_instruments(infra::metrics::IMeter& meter) {
    if (instruments_ready_) {
      return;
    }

    (void)meter.create_counter(event_total_identity());
    (void)meter.create_histogram(latency_identity());
    instruments_ready_ = true;
  }

  std::shared_ptr<infra::metrics::IMetricsProvider> provider_;
  std::shared_ptr<infra::metrics::IMeter> meter_;
  bool instruments_ready_ = false;
};

class KnowledgeTraceSinkState {
 public:
  explicit KnowledgeTraceSinkState(
      std::shared_ptr<infra::tracing::ITracerProvider> provider)
      : provider_(std::move(provider)) {}

  void emit(const knowledge::KnowledgeTelemetryEvent& event) {
    auto tracer = ensure_tracer();
    if (tracer == nullptr) {
      return;
    }

    const auto end_ts = current_time_ms();
    const auto start_ts = end_ts - knowledge_latency_ms(event);
    auto span = tracer->start_span(infra::tracing::SpanDescriptor{
        .name = knowledge_audit_action(event),
        .kind = infra::tracing::SpanKind::Internal,
        .start_ts_unix_ms = start_ts,
        .attrs = make_knowledge_trace_attributes(event),
        .links = {},
    },
                                   nullptr);
    if (span == nullptr) {
      return;
    }

    span->add_event(telemetry_value_or(event.event_name, "knowledge_retrieve"),
                    make_knowledge_trace_attributes(event));
    const auto status_message = knowledge_event_failed(event)
                                    ? telemetry_value_or(event.error_category, "failure")
                                    : std::string("ok");
    span->set_status(knowledge_event_failed(event) ? infra::tracing::SpanStatusCode::Error
                                                   : infra::tracing::SpanStatusCode::Ok,
                     status_message);
    (void)span->end(end_ts);
  }

 private:
  [[nodiscard]] std::shared_ptr<infra::tracing::ITracer> ensure_tracer() {
    if (tracer_ != nullptr) {
      return tracer_;
    }
    if (provider_ == nullptr) {
      return nullptr;
    }

    tracer_ = provider_->get_tracer(infra::tracing::TracerScope{
        .name = "dasall.knowledge.telemetry",
        .version = "1.0.0",
        .schema_url = "schema://knowledge/telemetry/v1",
    });
    return tracer_;
  }

  std::shared_ptr<infra::tracing::ITracerProvider> provider_;
  std::shared_ptr<infra::tracing::ITracer> tracer_;
};

[[nodiscard]] knowledge::KnowledgeTelemetrySink make_knowledge_log_sink(
    std::shared_ptr<infra::logging::ILogger> logger,
    bool fallback) {
  if (logger == nullptr) {
    return {};
  }

  return [logger = std::move(logger), fallback](const knowledge::KnowledgeTelemetryEvent& event) {
    (void)logger->log(make_knowledge_log_event(event, fallback));
  };
}

[[nodiscard]] knowledge::KnowledgeTelemetrySink make_knowledge_metrics_sink(
    std::shared_ptr<infra::metrics::IMetricsProvider> metrics_provider) {
  if (metrics_provider == nullptr) {
    return {};
  }

  auto state = std::make_shared<KnowledgeMetricsSinkState>(std::move(metrics_provider));
  return [state](const knowledge::KnowledgeTelemetryEvent& event) {
    state->emit(event);
  };
}

[[nodiscard]] knowledge::KnowledgeTelemetrySink make_knowledge_trace_sink(
    std::shared_ptr<infra::tracing::ITracerProvider> tracer_provider) {
  if (tracer_provider == nullptr) {
    return {};
  }

  auto state = std::make_shared<KnowledgeTraceSinkState>(std::move(tracer_provider));
  return [state](const knowledge::KnowledgeTelemetryEvent& event) {
    state->emit(event);
  };
}

[[nodiscard]] knowledge::KnowledgeTelemetrySink make_knowledge_audit_sink(
    std::shared_ptr<infra::audit::IAuditLogger> audit_logger) {
  if (audit_logger == nullptr) {
    return {};
  }

  return [audit_logger = std::move(audit_logger)](
             const knowledge::KnowledgeTelemetryEvent& event) {
    const auto timestamp = current_time_ms();
    infra::AuditEvent audit_event;
    audit_event.event_id = telemetry_value_or(event.event_name, "knowledge_retrieve") + ":" +
                           telemetry_value_or(event.request_id, "unknown") + ":" +
                           knowledge_event_outcome(event);
    audit_event.action = knowledge_audit_action(event);
    audit_event.actor = "knowledge";
    audit_event.target = "knowledge:" + telemetry_value_or(event.snapshot_id, "unknown");
    audit_event.outcome = knowledge_audit_outcome(event);
    audit_event.evidence_ref = infra::AuditEvidenceRef{
        .kind = infra::AuditEvidenceKind::WorkerTask,
        .ref = "knowledge://retrieve/" + telemetry_value_or(event.request_id, "unknown"),
    };
    audit_event.side_effects = make_knowledge_audit_side_effects(event);
    audit_event.timestamp = timestamp;

    infra::AuditContext audit_context;
    audit_context.request_id = telemetry_value_or(event.request_id, infra::kAuditContextUnknown);
    audit_context.session_id = std::string(infra::kAuditContextUnknown);
    audit_context.trace_id = std::string(infra::kAuditContextUnknown);
    audit_context.task_id = telemetry_value_or(event.request_id, infra::kAuditContextUnknown);
    audit_context.parent_task_id = std::string(infra::kAuditContextUnknown);
    audit_context.lease_id = std::string(infra::kAuditContextUnknown);
    audit_context.worker_type = "knowledge";

    (void)audit_logger->write_audit(audit_event, audit_context);
  };
}

[[nodiscard]] knowledge::TelemetrySinks make_knowledge_production_telemetry_sinks(
    const RuntimeObservabilityBundle& observability) {
  knowledge::TelemetrySinks sinks;
  sinks.log_sink = make_knowledge_log_sink(observability.logger, false);
  sinks.metrics_sink = make_knowledge_metrics_sink(observability.metrics_provider);
  sinks.trace_sink = make_knowledge_trace_sink(observability.tracer_provider);
  sinks.audit_sink = make_knowledge_audit_sink(observability.audit_logger);
  sinks.fallback_log_sink = make_knowledge_log_sink(observability.logger, true);
  return sinks;
}

class RuntimeToolHealthSignalProvider final
    : public tools::ops::IToolHealthSignalProvider {
 public:
  RuntimeToolHealthSignalProvider(
      std::shared_ptr<tools::ops::ToolAuditBridge> audit_bridge,
      std::shared_ptr<tools::ops::ToolMetricsBridge> metrics_bridge,
      std::shared_ptr<tools::ops::ToolTraceBridge> trace_bridge,
      std::function<std::int64_t()> now_ms)
      : audit_bridge_(std::move(audit_bridge)),
        metrics_bridge_(std::move(metrics_bridge)),
        trace_bridge_(std::move(trace_bridge)),
        now_ms_(std::move(now_ms)) {}

  [[nodiscard]] tools::ops::ToolHealthSample sample(std::int64_t) override {
    tools::ops::ToolHealthSample sample;
    sample.registry.revision = 1U;
    sample.registry.descriptor_catalog_ready = true;
    sample.registry.delta_pipeline_degraded = false;
    sample.builtin_lane.available = true;
    sample.builtin_lane.concurrency_budget = 1U;
    sample.workflow_lane.available = true;
    sample.workflow_lane.concurrency_budget = 1U;
    sample.mcp.session_ready = true;
    sample.mcp.freshness = tools::CapabilityFreshness::fresh;
    sample.mcp.stale_read_allowed = true;
    sample.audit_bridge_degraded = audit_bridge_ == nullptr ||
        audit_bridge_->get_status().degraded;
    sample.metrics_bridge_degraded = metrics_bridge_ == nullptr ||
        metrics_bridge_->is_degraded();
    sample.trace_bridge_degraded = trace_bridge_ == nullptr ||
        trace_bridge_->is_degraded();
    sample.latency_ms = 0;
    sample.sampled_at_unix_ms = now_ms_ != nullptr ? now_ms_() : current_time_ms();
    sample.detail_ref = "status://runtime_support/tools/health";
    return sample;
  }

 private:
  std::shared_ptr<tools::ops::ToolAuditBridge> audit_bridge_;
  std::shared_ptr<tools::ops::ToolMetricsBridge> metrics_bridge_;
  std::shared_ptr<tools::ops::ToolTraceBridge> trace_bridge_;
  std::function<std::int64_t()> now_ms_;
};

class RuntimeControlPlaneHealthSignalProvider final
    : public runtime::IRuntimeHealthSignalProvider {
 public:
  RuntimeControlPlaneHealthSignalProvider(
      std::shared_ptr<runtime::RuntimeEventBus> event_bus,
      std::shared_ptr<runtime::RuntimeTelemetryBridge> telemetry_bridge,
      const bool maintenance_hooks_ready,
      std::function<std::int64_t()> now_ms)
      : event_bus_(std::move(event_bus)),
        telemetry_bridge_(std::move(telemetry_bridge)),
        maintenance_hooks_ready_(maintenance_hooks_ready),
        now_ms_(std::move(now_ms)) {
    if (event_bus_ != nullptr) {
      safe_mode_subscription_ = event_bus_->subscribe(
          "runtime.safe_mode",
          [this](const runtime::RuntimeEventEnvelope& event) {
            record_safe_mode_event(event);
          },
          true);
    }
  }

  ~RuntimeControlPlaneHealthSignalProvider() override {
    if (event_bus_ != nullptr && safe_mode_subscription_.is_valid()) {
      static_cast<void>(event_bus_->unsubscribe(
          safe_mode_subscription_.subscription_id));
    }
  }

  [[nodiscard]] runtime::RuntimeHealthSample sample(std::int64_t) override {
    runtime::RuntimeHealthSample sample;
    sample.dependencies_ready = event_bus_ != nullptr &&
                                telemetry_bridge_ != nullptr &&
                                maintenance_hooks_ready_;
    sample.watchdog_healthy = true;
    sample.telemetry_degraded = telemetry_bridge_ == nullptr;
    sample.event_bus_overflow = event_bus_ != nullptr && event_bus_->drop_count() > 0U;
    sample.maintenance_backlog = pending_maintenance_backlog();
    sample.safe_mode_active = safe_mode_active();
    sample.latency_ms = 0;
    sample.sampled_at_unix_ms = now_ms_ != nullptr ? now_ms_() : current_time_ms();
    sample.detail_ref = detail_ref_for(sample);
    return sample;
  }

 private:
  [[nodiscard]] static std::optional<std::string> attribute_value(
      const runtime::RuntimeEventEnvelope& event,
      const std::string& key) {
    const auto attribute_it = std::find_if(
        event.attributes.begin(),
        event.attributes.end(),
        [&key](const auto& attribute) { return attribute.key == key; });
    if (attribute_it == event.attributes.end()) {
      return std::nullopt;
    }

    return attribute_it->value;
  }

  void record_safe_mode_event(const runtime::RuntimeEventEnvelope& event) {
    const auto target_mode = attribute_value(event, "target_mode");
    if (!target_mode.has_value()) {
      return;
    }

    const std::lock_guard<std::mutex> lock(safe_mode_mutex_);
    safe_mode_active_ = *target_mode != "Normal";
  }

  [[nodiscard]] bool pending_maintenance_backlog() const {
    if (event_bus_ == nullptr) {
      return false;
    }

    const auto pending_events = event_bus_->pending_snapshot();
    return std::any_of(pending_events.begin(),
                       pending_events.end(),
                       [](const auto& event) {
                         return event.category ==
                                runtime::RuntimeEventCategory::Maintenance;
                       });
  }

  [[nodiscard]] std::optional<bool> pending_safe_mode_state() const {
    if (event_bus_ == nullptr) {
      return std::nullopt;
    }

    const auto pending_events = event_bus_->pending_snapshot();
    for (auto it = pending_events.rbegin(); it != pending_events.rend(); ++it) {
      if (it->category != runtime::RuntimeEventCategory::SafeMode) {
        continue;
      }

      if (const auto target_mode = attribute_value(*it, "target_mode");
          target_mode.has_value()) {
        return *target_mode != "Normal";
      }
    }

    return std::nullopt;
  }

  [[nodiscard]] bool safe_mode_active() const {
    if (const auto pending_state = pending_safe_mode_state();
        pending_state.has_value()) {
      return *pending_state;
    }

    const std::lock_guard<std::mutex> lock(safe_mode_mutex_);
    return safe_mode_active_;
  }

  [[nodiscard]] std::string detail_ref_for(
      const runtime::RuntimeHealthSample& sample) const {
    if (!sample.dependencies_ready) {
      return "status://runtime/health/degraded/dependencies";
    }
    if (sample.event_bus_overflow) {
      return "status://runtime/health/degraded/event_bus";
    }
    if (sample.telemetry_degraded) {
      return "status://runtime/health/degraded/telemetry";
    }
    if (sample.maintenance_backlog) {
      return "status://runtime/health/degraded/maintenance";
    }
    if (sample.safe_mode_active) {
      return "status://runtime/health/degraded/safe_mode";
    }

    return "status://runtime/health/healthy";
  }

  std::shared_ptr<runtime::RuntimeEventBus> event_bus_;
  std::shared_ptr<runtime::RuntimeTelemetryBridge> telemetry_bridge_;
  bool maintenance_hooks_ready_ = false;
  std::function<std::int64_t()> now_ms_;
  mutable std::mutex safe_mode_mutex_;
  bool safe_mode_active_ = false;
  runtime::RuntimeEventSubscription safe_mode_subscription_;
};

[[nodiscard]] std::string register_health_probe(
    const std::shared_ptr<infra::IHealthMonitor>& health_monitor,
    const std::string& probe_name,
    const std::string& probe_group,
    infra::IHealthProbe* probe) {
  if (health_monitor == nullptr || probe == nullptr) {
    return "health monitor registration requires concrete monitor and probe";
  }

  const auto result = health_monitor->register_probe(infra::HealthProbeRegistration{
      .probe_name = probe_name,
      .probe_group = probe_group,
      .probe = probe,
  });
  if (!result.ok) {
    return std::string("health probe registration failed for ") + probe_name;
  }

  return {};
}

[[nodiscard]] RuntimeObservabilityBundle compose_runtime_observability_bundle(
  const profiles::RuntimePolicySnapshot& policy_snapshot,
  const RuntimeLiveDependencyCompositionOptions& options) {
  const auto& optional_backends = policy_snapshot.ops_policy().optional_backends;
  const auto live_observability = infra::compose_live_observability(
      infra::ObservabilityLiveCompositionOptions{
          .profile_id = policy_snapshot.effective_profile_id(),
          .metrics_granularity = policy_snapshot.ops_policy().metrics_granularity,
          .trace_sample_ratio = policy_snapshot.ops_policy().trace_sample_ratio,
          .metrics_exporter_type = optional_backends.metrics_exporter_type,
          .trace_exporter_type = optional_backends.trace_exporter_type,
          .trace_exporter_otlp_endpoint = optional_backends.trace_exporter_otlp_endpoint,
      .logging_level = policy_snapshot.ops_policy().log_level,
      .logging_diag_pull_enabled =
        policy_snapshot.ops_policy().remote_diagnostics_enabled,
            .logging_config_entries = {},
      .logging_state_root_override = options.state_root_override,
      });
  if (!live_observability.ok()) {
    return RuntimeObservabilityBundle{
        .logger = nullptr,
        .audit_logger = nullptr,
        .metrics_provider = nullptr,
        .tracer_provider = nullptr,
        .health_monitor = nullptr,
        .tool_audit_bridge = nullptr,
        .tool_metrics_bridge = nullptr,
        .tool_trace_bridge = nullptr,
        .tool_health_probe = nullptr,
        .health_probes = {},
        .error = live_observability.error,
    };
  }

  auto tool_audit_bridge = std::make_shared<tools::ops::ToolAuditBridge>(
      live_observability.audit_logger.get());
  auto tool_metrics_bridge = std::make_shared<tools::ops::ToolMetricsBridge>(
      live_observability.metrics_provider,
      tools::ops::ToolMetricsBridgeOptions{
          .enabled = true,
          .profile_id = policy_snapshot.effective_profile_id(),
          .metrics_granularity = policy_snapshot.ops_policy().metrics_granularity,
          .now_ms = []() {
            return current_time_ms();
          },
      });
  auto tool_trace_bridge = std::make_shared<tools::ops::ToolTraceBridge>(
      live_observability.tracer_provider,
      tools::ops::ToolTraceBridgeOptions{
          .enabled = true,
          .profile_id = policy_snapshot.effective_profile_id(),
          .trace_sample_ratio = policy_snapshot.ops_policy().trace_sample_ratio,
      });
  auto tool_health_probe = std::make_shared<tools::ops::ToolHealthProbe>(
      std::make_shared<RuntimeToolHealthSignalProvider>(
          tool_audit_bridge,
          tool_metrics_bridge,
          tool_trace_bridge,
          []() {
            return current_time_ms();
          }),
      tools::ops::ToolHealthProbeOptions{
          .now_ms = []() {
            return current_time_ms();
          },
      });

  if (const auto register_error = register_health_probe(
          live_observability.health_monitor,
          std::string(tools::ops::kToolHealthProbeName),
          std::string(tools::ops::kToolHealthProbeGroup),
          tool_health_probe.get());
      !register_error.empty()) {
    return RuntimeObservabilityBundle{
        .logger = nullptr,
        .audit_logger = nullptr,
        .metrics_provider = nullptr,
        .tracer_provider = nullptr,
        .health_monitor = nullptr,
        .tool_audit_bridge = nullptr,
        .tool_metrics_bridge = nullptr,
        .tool_trace_bridge = nullptr,
        .tool_health_probe = nullptr,
        .health_probes = {},
        .error = register_error,
    };
  }

  return RuntimeObservabilityBundle{
      .logger = live_observability.logger,
      .audit_logger = live_observability.audit_logger,
      .metrics_provider = live_observability.metrics_provider,
      .tracer_provider = live_observability.tracer_provider,
      .health_monitor = live_observability.health_monitor,
      .tool_audit_bridge = tool_audit_bridge,
      .tool_metrics_bridge = tool_metrics_bridge,
      .tool_trace_bridge = tool_trace_bridge,
      .tool_health_probe = tool_health_probe,
      .health_probes = {tool_health_probe},
      .error = {},
  };
}

[[nodiscard]] RuntimeDependencyCompositionResult make_error(std::string error) {
  return RuntimeDependencyCompositionResult{
      .dependency_set = nullptr,
      .error = std::move(error),
  };
}

struct RuntimeKnowledgeRefreshScanTarget {
  knowledge::CorpusDescriptor descriptor;
  knowledge::ingest::CorpusScanPlan plan;
};

[[nodiscard]] std::string normalize_runtime_refresh_path(const fs::path& path) {
  return path.lexically_normal().generic_string();
}

[[nodiscard]] std::string make_runtime_refresh_source_uri(
    const fs::path& assets_root,
    std::string_view source_uri) {
  const fs::path source_path(source_uri);
  if (source_path.is_absolute()) {
    return normalize_runtime_refresh_path(source_path);
  }
  return normalize_runtime_refresh_path(assets_root / source_path);
}

void append_unique_source(std::vector<std::string>& values, std::string value) {
  if (value.empty() || contains_string(values, value)) {
    return;
  }
  values.push_back(std::move(value));
}

void sort_and_deduplicate_sources(std::vector<std::string>& values) {
  std::sort(values.begin(), values.end());
  values.erase(std::unique(values.begin(), values.end()), values.end());
}

[[nodiscard]] bool runtime_refresh_plan_has_changes(
    const RuntimeKnowledgeRefreshPlan& plan) {
  return !plan.added_sources.empty() || !plan.updated_sources.empty() ||
         !plan.removed_sources.empty();
}

[[nodiscard]] RuntimeKnowledgeRefreshPlan make_full_scan_fallback_plan() {
  RuntimeKnowledgeRefreshPlan plan;
  plan.kind = RuntimeKnowledgeRefreshPlanKind::FullScanFallback;
  return plan;
}

[[nodiscard]] bool source_scan_delta_has_corpus_quarantine(
    const knowledge::ingest::SourceScanDelta& delta,
    std::string_view corpus_id) {
  return contains_string(delta.quarantined_source_ids,
                         std::string("corpus::") + std::string(corpus_id));
}

[[nodiscard]] knowledge::CorpusDescriptor make_runtime_refresh_descriptor(
    std::string corpus_id,
    std::string display_name,
    const fs::path& source_root,
    std::vector<std::string> tags,
    knowledge::AuthorityLevel authority_level,
    knowledge::SourceKind source_kind,
    std::vector<knowledge::SourceFormat> allowed_formats,
    std::vector<std::string> include_globs,
    std::vector<std::string> exclude_globs,
    std::vector<knowledge::RetrievalMode> supported_modes,
    std::string default_language) {
  knowledge::CorpusDescriptor descriptor;
  descriptor.corpus_id = std::move(corpus_id);
  descriptor.display_name = std::move(display_name);
  descriptor.source_uri = normalize_runtime_refresh_path(source_root);
  descriptor.trust_level = knowledge::TrustLevel::Trusted;
  descriptor.authority_level = authority_level;
  descriptor.source_kind = source_kind;
  descriptor.allowed_formats = std::move(allowed_formats);
  descriptor.include_globs = std::move(include_globs);
  descriptor.exclude_globs = std::move(exclude_globs);
  descriptor.supported_modes = std::move(supported_modes);
  descriptor.tags = std::move(tags);
  descriptor.metadata = {
      {"baseline_class", "runtime_refresh_scan"},
      {"owner_module", "runtime_support"},
      {"refresh_strategy", "runtime_selective"},
      {"default_language", std::move(default_language)},
  };
  return descriptor;
}

[[nodiscard]] std::vector<RuntimeKnowledgeRefreshScanTarget>
make_runtime_knowledge_refresh_scan_targets(const fs::path& assets_root) {
  std::vector<RuntimeKnowledgeRefreshScanTarget> targets;
  const auto append_target = [&targets](knowledge::CorpusDescriptor descriptor) {
    RuntimeKnowledgeRefreshScanTarget target;
    target.plan.corpus_id = descriptor.corpus_id;
    target.plan.root_uri = descriptor.source_uri;
    target.plan.source_kind = descriptor.source_kind;
    target.plan.include_globs = descriptor.include_globs;
    target.plan.exclude_globs = descriptor.exclude_globs;
    target.plan.allowed_formats = descriptor.allowed_formats;
    target.plan.full_scan = false;
    target.descriptor = std::move(descriptor);
    targets.push_back(std::move(target));
  };

  append_target(make_runtime_refresh_descriptor(
      "architecture_reference",
      "DASALL architecture reference",
      assets_root / "docs" / "architecture",
      {"installed", "architecture", "reference"},
      knowledge::AuthorityLevel::Reference,
      knowledge::SourceKind::File,
      {knowledge::SourceFormat::Markdown},
      {"DASALL_Agent_architecture.md",
       "DASALL_Engineering_Blueprint.md",
       "DASALL_*详细设计*.md",
       "platform_linux_detailed_design.md"},
      {"*评审报告*.md",
       "*迁移影响清单*.md",
       "DASALL_boundary治理与优化说明.md"},
      {knowledge::RetrievalMode::LexicalOnly, knowledge::RetrievalMode::Hybrid},
      "zh-CN"));
  append_target(make_runtime_refresh_descriptor(
      "adr_normative",
      "DASALL ADR normative corpus",
      assets_root / "docs" / "adr",
      {"installed", "adr", "normative"},
      knowledge::AuthorityLevel::Normative,
      knowledge::SourceKind::File,
      {knowledge::SourceFormat::Markdown},
      {"ADR-*.md"},
      {},
      {knowledge::RetrievalMode::LexicalOnly, knowledge::RetrievalMode::Hybrid},
      "zh-CN"));
  append_target(make_runtime_refresh_descriptor(
      "ssot_normative",
      "DASALL SSOT normative corpus",
      assets_root / "docs" / "ssot",
      {"installed", "ssot", "normative"},
      knowledge::AuthorityLevel::Normative,
      knowledge::SourceKind::File,
      {knowledge::SourceFormat::Markdown},
      {"*.md"},
      {},
      {knowledge::RetrievalMode::LexicalOnly, knowledge::RetrievalMode::Hybrid},
      "zh-CN"));
  append_target(make_runtime_refresh_descriptor(
      "profile_policy_normative",
      "DASALL profile policy normative corpus",
      assets_root / "profiles",
      {"installed", "profile", "normative"},
      knowledge::AuthorityLevel::Normative,
      knowledge::SourceKind::ConfigSnapshot,
      {knowledge::SourceFormat::Yaml},
      {"*/runtime_policy.yaml"},
      {},
      {knowledge::RetrievalMode::LexicalOnly},
      "en"));
  append_target(make_runtime_refresh_descriptor(
      "dasall_llm_providers",
      "DASALL LLM provider manifests",
      assets_root / "llm" / "providers",
      {"installed", "llm", "provider"},
      knowledge::AuthorityLevel::Reference,
      knowledge::SourceKind::File,
      {knowledge::SourceFormat::Yaml},
      {"*.yaml", "*.yml"},
      {},
      {knowledge::RetrievalMode::LexicalOnly},
      "en"));
  return targets;
}

class InstalledAssetKnowledgeRefreshSourceProvider final
    : public IRuntimeKnowledgeRefreshSourceProvider {
 public:
  explicit InstalledAssetKnowledgeRefreshSourceProvider(fs::path assets_root)
      : assets_root_(std::move(assets_root)),
        scan_targets_(make_runtime_knowledge_refresh_scan_targets(assets_root_)) {
    baseline_ready_ = rebuild_baseline_locked();
  }

  [[nodiscard]] RuntimeKnowledgeRefreshPlan next_plan() override {
    std::scoped_lock lock(mutex_);
    if (!baseline_ready_) {
      baseline_ready_ = rebuild_baseline_locked();
      return make_full_scan_fallback_plan();
    }

    RuntimeKnowledgeRefreshPlan plan;
    plan.kind = RuntimeKnowledgeRefreshPlanKind::Selective;

    for (const auto& target : scan_targets_) {
      const auto previous_inventory = load_inventory_locked(target.descriptor.corpus_id);
      const auto delta = scan_target_locked(target);
      if (!delta.has_consistent_values() ||
          source_scan_delta_has_corpus_quarantine(delta, target.descriptor.corpus_id)) {
        baseline_ready_ = rebuild_baseline_locked();
        return make_full_scan_fallback_plan();
      }

      for (const auto& record : delta.added) {
        append_unique_source(plan.added_sources,
                             make_runtime_refresh_source_uri(assets_root_,
                                                             record.source_uri));
      }
      for (const auto& record : delta.updated) {
        append_unique_source(plan.updated_sources,
                             make_runtime_refresh_source_uri(assets_root_,
                                                             record.source_uri));
      }

      for (const auto& removed_source_id : delta.removed_source_ids) {
        const auto removed_it = std::find_if(previous_inventory.begin(), previous_inventory.end(),
                                             [&removed_source_id](const auto& record) {
                                               return record.source_id == removed_source_id;
                                             });
        if (removed_it != previous_inventory.end()) {
          append_unique_source(plan.removed_sources,
                               make_runtime_refresh_source_uri(assets_root_,
                                                               removed_it->source_uri));
        }
      }

      inventories_[target.descriptor.corpus_id] =
          apply_delta_locked(previous_inventory, delta);
    }

    sort_and_deduplicate_sources(plan.added_sources);
    sort_and_deduplicate_sources(plan.updated_sources);
    sort_and_deduplicate_sources(plan.removed_sources);
    if (!runtime_refresh_plan_has_changes(plan)) {
      return make_full_scan_fallback_plan();
    }
    return plan;
  }

 private:
  using SourceInventory = std::vector<knowledge::ingest::SourceRecord>;

  [[nodiscard]] knowledge::ingest::SourceScanDelta scan_target_locked(
      const RuntimeKnowledgeRefreshScanTarget& target) const {
    knowledge::ingest::SourceScanner scanner(knowledge::ingest::SourceScannerDeps{
        .lookup_corpus = [descriptor = target.descriptor](std::string_view corpus_id)
            -> std::optional<knowledge::CorpusDescriptor> {
          if (corpus_id == descriptor.corpus_id) {
            return descriptor;
          }
          return std::nullopt;
        },
        .load_inventory = [this](std::string_view corpus_id) {
          return load_inventory_locked(corpus_id);
        },
        .repository_root = [this]() {
          return assets_root_;
        },
        .now_ms = []() {
          return current_time_ms();
        },
    });
    return scanner.scan(target.plan);
  }

  [[nodiscard]] SourceInventory load_inventory_locked(
      std::string_view corpus_id) const {
    const auto inventory_it = inventories_.find(std::string(corpus_id));
    if (inventory_it == inventories_.end()) {
      return {};
    }
    return inventory_it->second;
  }

  [[nodiscard]] SourceInventory apply_delta_locked(
      const SourceInventory& previous_inventory,
      const knowledge::ingest::SourceScanDelta& delta) const {
    std::map<std::string, knowledge::ingest::SourceRecord, std::less<>> records;
    for (const auto& record : previous_inventory) {
      records[record.source_id] = record;
    }
    for (const auto& record : delta.added) {
      records[record.source_id] = record;
    }
    for (const auto& record : delta.updated) {
      records[record.source_id] = record;
    }
    for (const auto& removed_source_id : delta.removed_source_ids) {
      records.erase(removed_source_id);
    }

    SourceInventory merged;
    merged.reserve(records.size());
    for (const auto& [source_id, record] : records) {
      static_cast<void>(source_id);
      merged.push_back(record);
    }
    return merged;
  }

  [[nodiscard]] bool rebuild_baseline_locked() {
    if (scan_targets_.empty()) {
      return false;
    }

    inventories_.clear();
    for (const auto& target : scan_targets_) {
      const auto delta = scan_target_locked(target);
      if (!delta.has_consistent_values() ||
          source_scan_delta_has_corpus_quarantine(delta, target.descriptor.corpus_id)) {
        inventories_.clear();
        return false;
      }
      inventories_[target.descriptor.corpus_id] = apply_delta_locked({}, delta);
    }
    return true;
  }

  fs::path assets_root_;
  std::vector<RuntimeKnowledgeRefreshScanTarget> scan_targets_;
  std::map<std::string, SourceInventory, std::less<>> inventories_;
  bool baseline_ready_ = false;
  mutable std::mutex mutex_;
};

[[nodiscard]] std::shared_ptr<IRuntimeKnowledgeRefreshSourceProvider>
make_default_runtime_knowledge_refresh_source_provider(
    const fs::path& readonly_assets_root) {
  if (readonly_assets_root.empty()) {
    return nullptr;
  }
  return std::make_shared<InstalledAssetKnowledgeRefreshSourceProvider>(
      readonly_assets_root);
}

[[nodiscard]] knowledge::CorpusChangeSet make_runtime_knowledge_change_set(
    const RuntimeKnowledgeRefreshPlan& plan) {
  return knowledge::CorpusChangeSet{
      .added_sources = plan.added_sources,
      .updated_sources = plan.updated_sources,
      .removed_sources = plan.removed_sources,
  };
}

constexpr std::string_view kKnowledgeRefreshAutomationReadySuffix =
    ":knowledge-refresh-automation-ready";
constexpr std::string_view kKnowledgeRefreshAutomationFallbackPrefix =
    ":knowledge-refresh-automation-fallback:";
constexpr std::string_view kKnowledgeRefreshAutomationTimerUnavailable =
    "timer-unavailable";
constexpr std::string_view kKnowledgeRefreshAutomationInvalidInterval =
    "invalid-refresh-interval";
constexpr std::string_view kKnowledgeRefreshAutomationTimerArmFailed =
    "timer-arm-failed";

class AutoRefreshKnowledgeService final : public knowledge::IKnowledgeService {
 public:
  AutoRefreshKnowledgeService(std::shared_ptr<knowledge::IKnowledgeService> inner_service,
                              std::shared_ptr<platform::ITimer> timer,
                              platform::TimerHandle timer_handle,
                              std::shared_ptr<IRuntimeKnowledgeRefreshSourceProvider>
                                  refresh_source_provider)
      : inner_service_(std::move(inner_service)),
        timer_(std::move(timer)),
        timer_handle_(timer_handle),
        refresh_source_provider_(std::move(refresh_source_provider)) {}

  ~AutoRefreshKnowledgeService() override {
    if (timer_ != nullptr && timer_handle_.has_consistent_values()) {
      (void)timer_->cancel(timer_handle_);
    }
  }

  bool init(const knowledge::KnowledgeConfigSnapshot& config) override {
    return inner_service_->init(config);
  }

  knowledge::KnowledgeRetrieveResult retrieve(
      const knowledge::KnowledgeQuery& query) override {
    return inner_service_->retrieve(query);
  }

  knowledge::KnowledgeHealthSnapshot health_snapshot() const override {
    return inner_service_->health_snapshot();
  }

  knowledge::RefreshResult request_refresh(
      const knowledge::CorpusChangeSet& changes) override {
    return inner_service_->request_refresh(changes);
  }

 private:
  std::shared_ptr<knowledge::IKnowledgeService> inner_service_;
  std::shared_ptr<platform::ITimer> timer_;
  platform::TimerHandle timer_handle_;
  std::shared_ptr<IRuntimeKnowledgeRefreshSourceProvider> refresh_source_provider_;
};

struct KnowledgeAutoRefreshArmResult {
  std::shared_ptr<knowledge::IKnowledgeService> service;
  std::string evidence;
};

[[nodiscard]] std::string make_runtime_knowledge_automation_evidence(
    std::string_view composition_owner,
    std::string_view suffix) {
  return std::string("runtime:") + std::string(composition_owner) +
         std::string(suffix);
}

[[nodiscard]] platform::TimerSpec make_knowledge_refresh_timer_spec(
    const std::uint32_t refresh_interval_ms) {
  return platform::TimerSpec{
      .mode = platform::TimerMode::Periodic,
      .interval_ms = refresh_interval_ms,
      .initial_delay_ms = refresh_interval_ms,
      .clock_kind = platform::TimerClockKind::Monotonic,
  };
}

[[nodiscard]] KnowledgeAutoRefreshArmResult arm_runtime_knowledge_auto_refresh(
    const std::shared_ptr<knowledge::IKnowledgeService>& knowledge_service,
    const std::shared_ptr<platform::ITimer>& timer,
    const std::int64_t refresh_interval_ms,
  std::string_view composition_owner,
  const std::shared_ptr<IRuntimeKnowledgeRefreshSourceProvider>&
    refresh_source_provider) {
  if (timer == nullptr) {
    return KnowledgeAutoRefreshArmResult{
        .service = knowledge_service,
        .evidence = make_runtime_knowledge_automation_evidence(
            composition_owner,
            std::string(kKnowledgeRefreshAutomationFallbackPrefix) +
                std::string(kKnowledgeRefreshAutomationTimerUnavailable)),
    };
  }

  if (refresh_interval_ms <= 0 ||
      refresh_interval_ms >
          static_cast<std::int64_t>(std::numeric_limits<std::uint32_t>::max())) {
    return KnowledgeAutoRefreshArmResult{
        .service = knowledge_service,
        .evidence = make_runtime_knowledge_automation_evidence(
            composition_owner,
            std::string(kKnowledgeRefreshAutomationFallbackPrefix) +
                std::string(kKnowledgeRefreshAutomationInvalidInterval)),
    };
  }

  const auto started = timer->start_periodic(
      make_knowledge_refresh_timer_spec(static_cast<std::uint32_t>(refresh_interval_ms)),
      [knowledge_service, refresh_source_provider](const platform::TimerDriftStats&) {
        try {
          if (knowledge_service->health_snapshot().refresh_in_flight) {
            return;
          }

          const auto plan = refresh_source_provider != nullptr
                                ? refresh_source_provider->next_plan()
                                : make_full_scan_fallback_plan();
          if (plan.kind == RuntimeKnowledgeRefreshPlanKind::Skip) {
            return;
          }

          if (plan.kind == RuntimeKnowledgeRefreshPlanKind::Selective &&
              runtime_refresh_plan_has_changes(plan)) {
            static_cast<void>(knowledge_service->request_refresh(
                make_runtime_knowledge_change_set(plan)));
            return;
          }

          static_cast<void>(knowledge_service->request_refresh(knowledge::CorpusChangeSet{}));
        } catch (...) {
          try {
            static_cast<void>(knowledge_service->request_refresh(knowledge::CorpusChangeSet{}));
          } catch (...) {
          }
        }
      });
  if (!started.ok()) {
    return KnowledgeAutoRefreshArmResult{
        .service = knowledge_service,
        .evidence = make_runtime_knowledge_automation_evidence(
            composition_owner,
            std::string(kKnowledgeRefreshAutomationFallbackPrefix) +
                std::string(kKnowledgeRefreshAutomationTimerArmFailed)),
    };
  }

  return KnowledgeAutoRefreshArmResult{
      .service = std::make_shared<AutoRefreshKnowledgeService>(knowledge_service,
                                                               timer,
                                                               *started.value,
                                                               refresh_source_provider),
      .evidence = make_runtime_knowledge_automation_evidence(
          composition_owner,
          kKnowledgeRefreshAutomationReadySuffix),
  };
}

[[nodiscard]] fs::path selected_root(const fs::path& default_root,
                                     const fs::path& override_root) {
  return override_root.empty() ? default_root : override_root;
}

[[nodiscard]] std::string create_memory_state_dir(const fs::path& state_root,
                                                  const std::string_view& composition_owner) {
  const fs::path memory_state_root = state_root / "memory";
  std::error_code error;
  fs::create_directories(memory_state_root, error);
  if (error) {
    return std::string("memory state directory unavailable for ") +
           std::string(composition_owner) + ": " + memory_state_root.string() +
           ": " + error.message();
  }
  return {};
}

[[nodiscard]] std::optional<profiles::BuildProfileManifest> resolve_build_manifest(
    const fs::path& profiles_root,
    const profiles::RuntimePolicySnapshot& policy_snapshot,
    std::string& error) {
  const profiles::ProfileCatalog catalog(profiles_root);
  const profiles::BuildProfileResolver resolver(catalog);
    profiles::BuildProfileResolveRequest resolve_request;
    resolve_request.profile_id = policy_snapshot.effective_profile_id();
    resolve_request.expected_target_platform = std::nullopt;
    const auto manifest_result = resolver.resolve_build_manifest(resolve_request);
  if (!manifest_result.ok()) {
    error = std::string("build manifest unavailable for profile ") +
            policy_snapshot.effective_profile_id();
    return std::nullopt;
  }

  return manifest_result.manifest;
}

[[nodiscard]] std::optional<memory::MemoryConfig> make_sqlite_memory_config(
    const profiles::RuntimePolicySnapshot& policy_snapshot,
    const fs::path& profiles_root,
  const fs::path& runtime_library_root,
    const fs::path& readonly_assets_root,
    const fs::path& state_root,
  std::string& error,
  bool& vector_fail_closed) {
  const auto build_manifest =
      resolve_build_manifest(profiles_root, policy_snapshot, error);
  if (!build_manifest.has_value()) {
    return std::nullopt;
  }

  auto memory_config =
      memory::config::project_memory_config(policy_snapshot, *build_manifest);
  if (!memory_config.has_value()) {
    error = std::string("memory config projection failed for profile ") +
            policy_snapshot.effective_profile_id();
    return std::nullopt;
  }

  memory_config->storage.db_path = (state_root / "memory" / "memory.db").string();
  memory_config->storage.migrations_dir =
      (readonly_assets_root / "sql" / "memory").string();

#if defined(__APPLE__)
  constexpr const char* kSqliteExtensionSuffix = ".dylib";
#else
  constexpr const char* kSqliteExtensionSuffix = ".so";
#endif

  memory_config->vector.sqlite_vss_vector0_path =
      (runtime_library_root / "sqlite-vss" /
       (std::string("vector0") + kSqliteExtensionSuffix))
          .string();
  memory_config->vector.sqlite_vss_vss0_path =
      (runtime_library_root / "sqlite-vss" /
       (std::string("vss0") + kSqliteExtensionSuffix))
          .string();

  if (memory_config->vector.enabled &&
      (!fs::exists(memory_config->vector.sqlite_vss_vector0_path) ||
       !fs::exists(memory_config->vector.sqlite_vss_vss0_path))) {
    memory_config->vector.enabled = false;
    memory_config->vector.backend_type = memory::VectorBackend::None;
    memory_config->vector.search_top_k = 0;
    vector_fail_closed = true;
  }

  return memory_config;
}

[[nodiscard]] std::string format_knowledge_error(
    const std::optional<contracts::ErrorInfo>& error) {
  if (!error.has_value()) {
    return "none";
  }

  return !error->details.message.empty() ? error->details.message
                                         : std::string("knowledge.error");
}

[[nodiscard]] std::string join_reason_codes(
    const std::vector<std::string>& reason_codes) {
  std::ostringstream builder;
  for (std::size_t index = 0; index < reason_codes.size(); ++index) {
    if (index != 0U) {
      builder << ',';
    }
    builder << reason_codes[index];
  }
  return builder.str();
}

[[nodiscard]] knowledge::KnowledgeQuery make_installed_knowledge_probe_query() {
  knowledge::KnowledgeQuery query;
  query.request_id = "runtime-support-installed-knowledge-probe";
  query.query_text = "DeepSeek Chat";
  query.query_kind = knowledge::KnowledgeQueryKind::FactLookup;
  query.top_k = 3U;
  query.max_context_projection_items = 3U;
  return query;
}

[[nodiscard]] knowledge::KnowledgeQuery make_installed_knowledge_hybrid_canary_probe_query(
    const std::vector<std::string>& allowed_corpora) {
  knowledge::KnowledgeQuery query;
  query.request_id = "runtime-support-installed-knowledge-hybrid-canary-probe";
  query.query_text = "ContextOrchestrator PromptComposer";
  query.preferred_mode = knowledge::RetrievalMode::Hybrid;
  query.query_kind = knowledge::KnowledgeQueryKind::PolicyEvidence;
  query.allowed_corpora = allowed_corpora;
  query.top_k = 4U;
  query.max_context_projection_items = 4U;
  return query;
}

[[nodiscard]] bool installed_knowledge_hybrid_canary_ready(
    const std::shared_ptr<knowledge::IKnowledgeService>& knowledge_service,
    const std::vector<std::string>& allowed_corpora) {
  if (knowledge_service == nullptr || allowed_corpora.empty()) {
    return false;
  }

  const auto retrieve_result = knowledge_service->retrieve(
      make_installed_knowledge_hybrid_canary_probe_query(allowed_corpora));
  return retrieve_result.ok &&
         retrieve_result.mode == knowledge::RetrievalMode::Hybrid &&
       contains_string(retrieve_result.reason_codes, "runtime_canary_admitted") &&
       retrieve_result.vector_backend_ready &&
       retrieve_result.dense_hit_count > 0U;
}

[[nodiscard]] std::string validate_installed_knowledge_positive_probe(
    const std::shared_ptr<knowledge::IKnowledgeService>& knowledge_service) {
  if (knowledge_service == nullptr) {
    return "knowledge-service-null";
  }

  const auto refresh_result = knowledge_service->request_refresh(knowledge::CorpusChangeSet{});
  if (refresh_result.status != knowledge::RefreshStatus::Accepted) {
    return refresh_result.status == knowledge::RefreshStatus::Busy
        ? std::string("refresh-busy")
        : std::string("refresh-failed:") + format_knowledge_error(refresh_result.error);
  }

  const auto refresh_deadline_ms = current_time_ms() + 30000;
  knowledge::KnowledgeHealthSnapshot health_snapshot;
  while (true) {
    health_snapshot = knowledge_service->health_snapshot();
    if (!health_snapshot.refresh_in_flight &&
        health_snapshot.last_refresh_status.has_value()) {
      if (*health_snapshot.last_refresh_status == knowledge::RefreshStatus::Failed) {
        return std::string("health:refresh_failed:") +
               join_reason_codes(health_snapshot.reason_codes);
      }
      if (*health_snapshot.last_refresh_status == knowledge::RefreshStatus::Completed) {
        break;
      }
    }

    if (current_time_ms() >= refresh_deadline_ms) {
      return std::string("health:refresh_timeout:") +
             join_reason_codes(health_snapshot.reason_codes);
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }

  if (health_snapshot.freshness_state != knowledge::FreshnessState::Fresh ||
      health_snapshot.active_snapshot_id.empty()) {
    return std::string("health:") + join_reason_codes(health_snapshot.reason_codes);
  }

  const auto retrieve_result = knowledge_service->retrieve(
      make_installed_knowledge_probe_query());
  if (!retrieve_result.ok || !retrieve_result.evidence.has_value() ||
      retrieve_result.evidence->slices.empty()) {
    return std::string("retrieve:") + format_knowledge_error(retrieve_result.error);
  }

  const auto matching_slice = std::find_if(
      retrieve_result.evidence->slices.begin(),
      retrieve_result.evidence->slices.end(),
      [](const knowledge::EvidenceSlice& slice) {
        return slice.snippet.find("DeepSeek Chat") != std::string::npos ||
               slice.citation_ref.find("deepseek") != std::string::npos;
      });
  if (matching_slice == retrieve_result.evidence->slices.end()) {
    return "retrieve-missing-installed-evidence";
  }

  return {};
}

[[nodiscard]] contracts::ToolDescriptor make_runtime_dataset_descriptor() {
  auto descriptor = tools::builtin::dataset::build_descriptor();
  auto tags = descriptor.tags.value_or(std::vector<std::string>{});
  if (!contains_string(tags, "runtime")) {
    tags.push_back("runtime");
  }
  descriptor.tags = std::move(tags);
  return descriptor;
}

[[nodiscard]] contracts::ToolDescriptor make_runtime_terminal_descriptor() {
  auto descriptor = tools::builtin::terminal::build_descriptor();
  auto tags = descriptor.tags.value_or(std::vector<std::string>{});
  if (!contains_string(tags, "runtime")) {
    tags.push_back("runtime");
  }
  descriptor.tags = std::move(tags);
  return descriptor;
}

constexpr std::string_view kRuntimeSkillSourceKey = "runtime.skill.production";
constexpr std::string_view kRuntimeSkillBundleId = "runtime.skill.production";
constexpr std::string_view kRuntimeSkillAssetRoot = "skills/specs";
constexpr std::string_view kRuntimeSkillDialect = "dasall.skill.v1";
constexpr std::string_view kRuntimeSkillToolPrefix = "skill.";

struct RuntimeToolManagerComposition {
  std::shared_ptr<tools::ToolManager> tool_manager;
  std::vector<std::string> visible_tools;
  std::vector<contracts::ToolDescriptor> available_tool_descriptors;
  bool skill_runtime_active = false;
};

struct RuntimeSkillSurface {
  std::shared_ptr<tools::skills::SkillRegistry> skill_registry;
  std::shared_ptr<tools::skills::SkillRuntime> skill_runtime;
  std::map<std::string, tools::skills::SkillSpecAsset> assets_by_tool_name;
  std::vector<contracts::ToolDescriptor> descriptors;
};

[[nodiscard]] std::string tool_namespace(std::string_view tool_name) {
  const auto delimiter = tool_name.find('.');
  if (delimiter == std::string::npos) {
    return std::string(tool_name);
  }

  return std::string(tool_name.substr(0U, delimiter));
}

[[nodiscard]] bool has_skill_import_errors(
    const tools::skills::SkillImportResult& import_result) {
  return std::any_of(import_result.diagnostics.begin(),
                     import_result.diagnostics.end(),
                     [](const auto& diagnostic) {
                       return diagnostic.level ==
                              tools::skills::SkillImportDiagnosticLevel::Error;
                     });
}

[[nodiscard]] std::string make_runtime_skill_tool_name(
    const tools::skills::SkillSpecAsset& asset) {
  return std::string(kRuntimeSkillToolPrefix) + asset.name;
}

[[nodiscard]] contracts::ToolDescriptor make_runtime_skill_descriptor(
    const tools::skills::SkillSpecAsset& asset) {
  return contracts::ToolDescriptor{
      .tool_name = make_runtime_skill_tool_name(asset),
      .display_name = asset.name,
      .category = contracts::ToolCategory::Workflow,
      .capability_tier = contracts::ToolCapabilityTier::Internal,
      .is_read_only = false,
      .supports_compensation = false,
      .default_timeout_ms = 30000U,
      .input_schema_ref = std::string("schema://tools/") + asset.name + "/input/v1",
      .output_schema_ref = std::string("schema://tools/") + asset.name + "/output/v1",
      .required_scopes = std::vector<std::string>{"tools.read"},
      .tags = std::vector<std::string>{"runtime", "skill", "workflow"},
      .version = asset.version,
  };
}

[[nodiscard]] tools::ToolPolicyView make_runtime_skill_policy_view(
    const profiles::RuntimePolicySnapshot& policy_snapshot,
    const std::set<std::string>& available_tool_names) {
  std::set<std::string> capability_domains;
  for (const auto& tool_name : available_tool_names) {
    capability_domains.insert(tool_namespace(tool_name));
  }

  return tools::ToolPolicyView{
      .effective_profile_id = policy_snapshot.effective_profile_id(),
      .safe_mode_enabled = policy_snapshot.execution_policy().safe_mode_enabled,
      .high_risk_confirmation_required =
          policy_snapshot.execution_policy().requires_high_risk_confirmation,
      .audit_level = policy_snapshot.execution_policy().audit_level,
      .allowed_tool_domains = std::vector<std::string>(capability_domains.begin(),
                                                       capability_domains.end()),
      .tool_visibility_rules = policy_snapshot.prompt_policy().tool_visibility_rules,
  };
}

[[nodiscard]] bool all_skill_tools_available(
    const tools::skills::SkillSpecAsset& asset,
    const std::set<std::string>& available_tool_names) {
  return std::all_of(asset.allowed_tools.begin(),
                     asset.allowed_tools.end(),
                     [&](const auto& tool_name) {
                       return available_tool_names.contains(tool_name);
                     });
}

[[nodiscard]] RuntimeSkillSurface compose_runtime_skill_surface(
    const fs::path& readonly_assets_root,
    const profiles::RuntimePolicySnapshot& policy_snapshot,
    const std::set<std::string>& available_tool_names) {
  RuntimeSkillSurface surface{
      .skill_registry = std::make_shared<tools::skills::SkillRegistry>(),
      .skill_runtime = std::make_shared<tools::skills::SkillRuntime>(readonly_assets_root),
      .assets_by_tool_name = {},
      .descriptors = {},
  };

  tools::skills::PluginSkillBundleImporter importer(
      tools::skills::SkillImporterOptions{
          .external_skill_import_enabled = false,
          .project_root = readonly_assets_root,
      });
  const auto import_result = importer.import_bundle(
      tools::bridge::SkillAssetRef{
          .provider_ref = tools::plugin::ToolPluginProviderRef{
              .plugin_id = std::string("runtime.skill.production"),
              .export_key = std::string("skills.internal.runtime"),
              .source_revision = std::string("live"),
          },
          .source_key = std::string(kRuntimeSkillSourceKey),
          .bundle_id = std::string(kRuntimeSkillBundleId),
          .asset_root_ref = std::string(kRuntimeSkillAssetRoot),
          .dialect_ref = std::string(kRuntimeSkillDialect),
      });
  if (has_skill_import_errors(import_result)) {
    return surface;
  }

  const auto skill_policy_view = make_runtime_skill_policy_view(
      policy_snapshot,
      available_tool_names);
  for (const auto& asset : import_result.imported_assets) {
    if (!all_skill_tools_available(asset, available_tool_names)) {
      continue;
    }

    const auto tool_allowlist = surface.skill_runtime->build_tool_allowlist(
        asset,
        skill_policy_view);
    if (tool_allowlist.size() != asset.allowed_tools.size()) {
      continue;
    }

    const auto descriptor = make_runtime_skill_descriptor(asset);
    if (!descriptor.tool_name.has_value() || descriptor.tool_name->empty()) {
      continue;
    }
    if (!surface.assets_by_tool_name.emplace(*descriptor.tool_name, asset).second) {
      continue;
    }
    if (!surface.skill_registry->register_asset(asset)) {
      surface.assets_by_tool_name.erase(*descriptor.tool_name);
      continue;
    }

    surface.descriptors.push_back(descriptor);
  }

  return surface;
}

[[nodiscard]] RuntimeToolManagerComposition compose_runtime_tool_manager(
    const profiles::RuntimePolicySnapshot& policy_snapshot,
    std::shared_ptr<services::IExecutionService> execution_service,
    std::shared_ptr<services::IDataService> data_service,
    const fs::path& readonly_assets_root,
    const RuntimeObservabilityBundle& observability) {
  RuntimeToolManagerComposition composition;
  if (execution_service == nullptr || data_service == nullptr ||
      observability.tool_audit_bridge == nullptr ||
      observability.tool_metrics_bridge == nullptr ||
      observability.tool_trace_bridge == nullptr) {
    return composition;
  }

  auto registry = std::make_shared<tools::registry::ToolRegistry>();
  const auto dataset_descriptor = make_runtime_dataset_descriptor();
  const auto terminal_descriptor = make_runtime_terminal_descriptor();
  if (!registry->register_builtin(terminal_descriptor)) {
    return composition;
  }
  if (!registry->register_builtin(dataset_descriptor)) {
    return composition;
  }

  composition.visible_tools = {
      dataset_descriptor.tool_name.value_or(std::string("agent.dataset")),
      terminal_descriptor.tool_name.value_or(std::string("agent.terminal")),
  };
    composition.available_tool_descriptors = {
      dataset_descriptor,
      terminal_descriptor,
    };

  const std::set<std::string> available_tool_names = {
      dataset_descriptor.tool_name.value_or(std::string("agent.dataset")),
      terminal_descriptor.tool_name.value_or(std::string("agent.terminal")),
  };
  const auto skill_surface = compose_runtime_skill_surface(
      readonly_assets_root,
      policy_snapshot,
      available_tool_names);
  if (!skill_surface.descriptors.empty() &&
      registry->apply_plugin_extension_delta(std::string(kRuntimeSkillSourceKey),
                                             skill_surface.descriptors)) {
    composition.skill_runtime_active = true;
    for (const auto& descriptor : skill_surface.descriptors) {
      composition.available_tool_descriptors.push_back(descriptor);
      if (descriptor.tool_name.has_value()) {
        composition.visible_tools.push_back(*descriptor.tool_name);
      }
    }
  }

  auto builtin_lane = std::make_shared<tools::execution::BuiltinExecutorLane>(
      tools::execution::BuiltinExecutorLaneDependencies{
          .registry = registry,
          .service_bridge = std::make_shared<tools::bridge::ToolServiceBridge>(),
          .execution_service = std::move(execution_service),
          .data_service = std::move(data_service),
          .now_ms = {},
      });
  const auto skill_policy_view = make_runtime_skill_policy_view(
      policy_snapshot,
      available_tool_names);
  auto workflow_engine = std::make_shared<tools::execution::WorkflowEngine>(
      tools::execution::WorkflowEngineDependencies{
          .plan_loader = [skill_assets_by_tool_name = skill_surface.assets_by_tool_name,
                          skill_runtime = skill_surface.skill_runtime,
                          skill_policy_view](const auto& workflow_ir,
                                             auto& failure_reason_code,
                                             auto& failure_message)
                             -> std::optional<tools::execution::WorkflowPlan> {
            if (!workflow_ir.tool_name.has_value() || workflow_ir.tool_name->empty()) {
              failure_reason_code = "skill.runtime.tool_name_missing";
              failure_message = failure_reason_code;
              return std::nullopt;
            }

            const auto asset_it = skill_assets_by_tool_name.find(*workflow_ir.tool_name);
            if (asset_it == skill_assets_by_tool_name.end() || skill_runtime == nullptr) {
              failure_reason_code = "skill.runtime.unmapped_tool";
              failure_message = "workflow tool is not mapped to a runtime skill asset";
              return std::nullopt;
            }

            const auto instantiate_result = skill_runtime->instantiate(
                tools::skills::SkillMatchResult{
                    .matched = true,
                    .asset = asset_it->second,
                    .reason_code = "skill.match.selected",
                    .matched_terms = {asset_it->second.name},
                    .score = 1U,
                },
                skill_policy_view);
            if (!instantiate_result.instantiated ||
                !instantiate_result.instance.has_value() ||
                !instantiate_result.workflow_plan.has_value()) {
              failure_reason_code = instantiate_result.reason_code.empty()
                                        ? std::string("skill.runtime.instantiate_failed")
                                        : instantiate_result.reason_code;
              failure_message = failure_reason_code;
              return std::nullopt;
            }

            auto plan = *instantiate_result.workflow_plan;
            static_cast<void>(skill_runtime->release_instance(
                instantiate_result.instance->instance_id));
            return plan;
          },
          .builtin_executor = [builtin_lane](const contracts::ToolIR& tool_ir,
                                             const tools::ToolExecutionContext& execution_context) {
            return builtin_lane->execute(tool_ir, execution_context);
          },
          .mcp_executor = {},
      });

  tools::manager::ToolManagerDependencies dependencies;
  dependencies.registry = registry;
  dependencies.workflow_engine = std::move(workflow_engine);
  dependencies.metrics_bridge = observability.tool_metrics_bridge;
  dependencies.trace_bridge = observability.tool_trace_bridge;
  dependencies.audit_hooks =
      tools::ops::ToolAuditBridge::bind_hooks(observability.tool_audit_bridge);
  dependencies.executor = [builtin_lane](const auto& execution_request) {
    return builtin_lane->execute(
        execution_request.tool_ir,
        tools::ToolExecutionContext{
            .invocation_context = execution_request.invocation_context,
            .lane_key = execution_request.route_decision.lane_key,
        });
  };
  composition.tool_manager = std::make_shared<tools::ToolManager>(std::move(dependencies));
  return composition;
}

}  // namespace

RuntimeDependencyCompositionResult compose_minimal_live_dependency_set(
    std::shared_ptr<const profiles::RuntimePolicySnapshot> policy_snapshot,
    const std::string_view& composition_owner,
    const RuntimeLiveDependencyCompositionOptions& options) {
  if (policy_snapshot == nullptr) {
    return make_error("runtime policy snapshot is required for live dependency composition");
  }

  const auto install_layout = infra::config::resolve_install_layout();
  const fs::path readonly_assets_root = selected_root(
      install_layout.readonly_assets_root, options.readonly_assets_root_override);
    const fs::path runtime_library_root = selected_root(
      install_layout.runtime_library_root, options.runtime_library_root_override);
  const fs::path state_root = selected_root(
      install_layout.state_root, options.state_root_override);
  if (const auto state_error = create_memory_state_dir(state_root, composition_owner);
      !state_error.empty()) {
    return make_error(state_error);
  }

  auto dependency_set = std::make_shared<runtime::RuntimeDependencySet>();
  const auto secret_manager_result = infra::secret::compose_live_secret_manager(
      policy_snapshot->ops_policy().optional_backends.secret_backend_type,
      infra::secret::SecretManagerLiveCompositionOptions{
          .state_root_override = state_root,
      });
  if (!secret_manager_result.ok()) {
    return make_error(std::string("runtime secret manager composition failed for ") +
                      std::string(composition_owner) + ": " +
                      secret_manager_result.error);
  }
  dependency_set->secret_manager = secret_manager_result.secret_manager;
  dependency_set->external_evidence.push_back(
      std::string("runtime:") + std::string(composition_owner) +
      ":secret-manager-live-seam");

  const auto observability = compose_runtime_observability_bundle(*policy_snapshot,
                                                                  options);
  if (!observability.ok()) {
    return make_error(std::string("runtime observability composition failed for ") +
                      std::string(composition_owner) + ": " + observability.error);
  }
  dependency_set->logger = observability.logger;
  dependency_set->audit_logger = observability.audit_logger;
  dependency_set->metrics_provider = observability.metrics_provider;
  dependency_set->tracer_provider = observability.tracer_provider;
  dependency_set->health_monitor = observability.health_monitor;
  dependency_set->health_probes = observability.health_probes;
  dependency_set->runtime_event_bus = std::make_shared<runtime::RuntimeEventBus>(
      runtime::RuntimeEventBusOptions{
          .max_non_audit_queue_depth = 64U,
          .now_ms = []() {
            return current_time_ms();
          },
      });
  dependency_set->runtime_telemetry_bridge =
      std::make_shared<runtime::RuntimeTelemetryBridge>(
          dependency_set->runtime_event_bus,
          runtime::RuntimeTelemetryBridgeOptions{
              .runtime_instance_id = std::string(composition_owner),
              .now_ms = []() {
                return current_time_ms();
              },
          });
  if (dependency_set->runtime_event_bus != nullptr && dependency_set->logger != nullptr) {
    auto runtime_logging_bridge =
        std::make_shared<runtime::RuntimeLoggingBridge>(dependency_set->logger);
    (void)dependency_set->runtime_event_bus->subscribe(
        [runtime_logging_bridge](const runtime::RuntimeEventEnvelope& event) {
          (void)runtime_logging_bridge->handle(event);
        });
  }
  dependency_set->background_maintenance_hooks =
      std::make_shared<runtime::BackgroundMaintenanceHooks>(
          dependency_set->runtime_event_bus,
          runtime::BackgroundMaintenanceHookOptions{
              .now_ms = []() {
                return current_time_ms();
              },
              .event_name_prefix = "runtime.maintenance",
              .fallback_sink = nullptr,
          });
  dependency_set->runtime_health_probe = std::make_shared<runtime::RuntimeHealthProbe>(
      std::make_shared<RuntimeControlPlaneHealthSignalProvider>(
          dependency_set->runtime_event_bus,
          dependency_set->runtime_telemetry_bridge,
          dependency_set->background_maintenance_hooks != nullptr,
          []() {
            return current_time_ms();
          }),
      runtime::RuntimeHealthProbeOptions{
          .now_ms = []() {
            return current_time_ms();
          },
          .logger = dependency_set->logger,
          .runtime_instance_id = std::string(composition_owner),
      });
  if (const auto register_error = register_health_probe(
          dependency_set->health_monitor,
          std::string(runtime::kRuntimeHealthProbeName),
          std::string(runtime::kRuntimeHealthProbeGroup),
          dependency_set->runtime_health_probe.get());
      !register_error.empty()) {
    return make_error(std::string("runtime health probe registration failed for ") +
                      std::string(composition_owner) + ": " + register_error);
  }
  dependency_set->health_probes.push_back(dependency_set->runtime_health_probe);

  std::string memory_config_error;
  bool memory_vector_fail_closed = false;
  auto memory_config = make_sqlite_memory_config(
      *policy_snapshot,
      install_layout.profiles_root,
      runtime_library_root,
      readonly_assets_root,
      state_root,
      memory_config_error,
      memory_vector_fail_closed);
  if (!memory_config.has_value()) {
    return make_error(std::string("memory config composition failed for ") +
              std::string(composition_owner) + ": " + memory_config_error);
  }

    std::vector<std::string> runtime_embedding_provider_selection_failures;
    auto runtime_embedding_provider = select_runtime_knowledge_embedding_provider(
      readonly_assets_root / "llm" / "providers",
      &runtime_embedding_provider_selection_failures);
  auto runtime_embedding_transport =
      options.knowledge_query_encoder_transport_override;
  if (runtime_embedding_transport == nullptr) {
    runtime_embedding_transport =
        std::make_shared<RuntimeKnowledgeQueryEncoderCurlTransport>();
  }

  const bool cognition_first_requested = runtime_cognition_first_requested();
  std::shared_ptr<llm::ILLMManager> llm_manager;
  if (cognition_first_requested) {
    llm_manager = std::make_shared<ScriptedCognitionFirstLLMManager>();
  } else {
    auto llm_result = llm::create_production_llm_manager(
        *policy_snapshot,
        llm::LLMProductionFactoryOptions{
            .secret_backend = nullptr,
            .transport = nullptr,
            .provider_catalog_baseline_root = {},
            .logger = observability.logger,
            .metrics_provider = observability.metrics_provider,
            .tracer_provider = observability.tracer_provider,
            .audit_logger = observability.audit_logger,
        });
    if (!llm_result.ok()) {
      return make_error(std::string("llm manager composition failed for ") +
                        std::string(composition_owner) + ": " + llm_result.error);
    }
    llm_manager = std::move(llm_result.manager);
  }

  if (llm_manager == nullptr) {
    return make_error(std::string("llm manager composition returned null for ") +
                      std::string(composition_owner));
  }
  dependency_set->llm_manager = llm_manager;

  auto memory_manager = std::shared_ptr<memory::IMemoryManager>(
      memory::create_memory_manager(
          *memory_config,
          memory::MemoryRuntimeDependencies{
              .logger = observability.logger,
              .audit_logger = observability.audit_logger,
              .metrics_provider = observability.metrics_provider,
              .tracer_provider = observability.tracer_provider,
              .summarizer_factory =
                  [llm_manager,
                   profile_id = policy_snapshot->effective_profile_id(),
                   llm_timeout_ms = static_cast<std::uint32_t>(
                       policy_snapshot->timeout_policy().llm.timeout_ms)](
                      const memory::MemoryConfig&) {
                  LLMBackedSummarizer::Options options;
                  options.profile_id = profile_id;
                  options.timeout_ms = llm_timeout_ms;
                    return std::make_unique<LLMBackedSummarizer>(
                    llm_manager, std::move(options));
                  },
              .embedding_adapter_factory =
                  [embedding_provider = runtime_embedding_provider,
                   transport = runtime_embedding_transport,
                   secret_manager = dependency_set->secret_manager,
                   owner = std::string(composition_owner)](
                      const memory::MemoryConfig&) ->
                      std::unique_ptr<memory::IEmbeddingAdapter> {
                    if (!embedding_provider.has_value() ||
                        !embedding_provider->has_consistent_values() ||
                        transport == nullptr) {
                      return nullptr;
                    }

                    LLMBackedEmbeddingAdapter::Options options;
                    options.provider =
                        LLMBackedEmbeddingAdapter::ProviderConfig{
                        .provider_id = embedding_provider->provider_id,
                        .model_id = embedding_provider->model_id,
                        .base_url = embedding_provider->base_url,
                        .auth_ref = embedding_provider->auth_ref,
                        .base_url_alias = embedding_provider->base_url_alias,
                        .snapshot_version = embedding_provider->snapshot_version,
                        .timeout_ms = embedding_provider->timeout_ms,
                        };
                    options.composition_owner = owner + ":memory.embedding";
                    return std::make_unique<LLMBackedEmbeddingAdapter>(
                        transport,
                        secret_manager,
                        std::move(options));
                  },
              .profile_id = policy_snapshot->effective_profile_id(),
          }));
  if (memory_manager == nullptr) {
    return make_error(std::string("memory manager factory returned null for ") +
                      std::string(composition_owner));
  }

  const auto init_code = memory_manager->init(*memory_config);
  if (static_cast<int>(init_code) != 0) {
    return make_error(std::string("memory manager init failed for ") +
                      std::string(composition_owner));
  }
  dependency_set->memory_manager = std::move(memory_manager);

  auto cognition_engine = cognition::create_cognition_engine(
      *policy_snapshot,
      cognition::CognitionRuntimeDependencies{
          .llm_manager = dependency_set->llm_manager,
          .policy_snapshot = policy_snapshot,
        .logger = dependency_set->logger,
          .audit_logger = dependency_set->audit_logger,
          .metrics_provider = dependency_set->metrics_provider,
          .tracer_provider = dependency_set->tracer_provider,
      });
  if (!cognition_engine) {
    return make_error(std::string("cognition engine composition failed for ") +
                      std::string(composition_owner));
  }
  dependency_set->cognition_engine =
      std::shared_ptr<cognition::ICognitionEngine>(cognition_engine.release());

  auto response_builder = cognition::create_response_builder(
      *policy_snapshot,
      cognition::CognitionRuntimeDependencies{
          .llm_manager = dependency_set->llm_manager,
          .policy_snapshot = policy_snapshot,
          .logger = dependency_set->logger,
        .audit_logger = dependency_set->audit_logger,
        .metrics_provider = dependency_set->metrics_provider,
        .tracer_provider = dependency_set->tracer_provider,
      });
  if (!response_builder) {
    return make_error(std::string("response builder composition failed for ") +
                      std::string(composition_owner));
  }
  dependency_set->response_builder =
      std::shared_ptr<cognition::IResponseBuilder>(response_builder.release());

    std::string services_build_manifest_error;
    const auto services_build_manifest = resolve_build_manifest(
      install_layout.profiles_root,
      *policy_snapshot,
      services_build_manifest_error);
    if (!services_build_manifest.has_value()) {
    return make_error(std::string("services build manifest composition failed for ") +
              std::string(composition_owner) + ": " +
              services_build_manifest_error);
    }

    const bool local_platform_route_enabled =
      services_build_manifest->enables_module("platform_hal");
    const std::string services_toolchain_hint =
      services_build_manifest->toolchain_hint.value_or("x86_64-linux-gnu");

  const auto live_services = services::compose_live_services(
      *policy_snapshot,
      services::ServiceLiveCompositionOptions{
          .execution_capability_id = "agent.terminal",
          .data_capability_id = "agent.dataset",
          .local_service_available = true,
          .remote_service_available = false,
          .remote_timeout = false,
          .allow_route_degrade = true,
            .local_platform_route_enabled = local_platform_route_enabled,
          .observability_enabled = true,
          .observability_level = policy_snapshot->ops_policy().metrics_granularity,
            .toolchain_hint = services_toolchain_hint,
          .logger = observability.logger,
          .audit_logger = observability.audit_logger,
          .metrics_provider = observability.metrics_provider,
          .tracer_provider = observability.tracer_provider,
          .health_probe_enabled = true,
          .critical_actions = {},
          .high_risk_actions = {"agent.terminal"},
            .adapter_registry = services::ServiceLiveAdapterRegistry{
              .route_equivalence_class = "service.live",
              .local_platform = services::ServiceLiveRouteBinding{
                .adapter_id = "runtime.live.local_platform",
                .trust_class = services::ServiceLiveTrustClass::trusted_local,
                .availability_state = local_platform_route_enabled
                  ? services::ServiceLiveAvailabilityState::available
                  : services::ServiceLiveAvailabilityState::unavailable,
                .supported_capabilities = {"agent.terminal"},
                .handler = [](const services::ServiceLiveBackendRequest& request) {
                return services::ServiceLiveBackendResult{
                  .transport_outcome =
                    services::ServiceLiveTransportOutcome::acknowledged,
                  .provider_status_code = "ok",
                  .payload_json = std::string(
                            "{\"applied\":true,\"route\":\"runtime.local_platform\",\"operation\":\"") +
                          request.operation_name + "\"}",
                  .latency_ms = 2U,
                  .side_effects = {request.operation_name + ".platform_applied"},
                  .evidence_refs = {std::string("runtime://services/platform/") +
                            request.operation_name},
                };
                },
                .timeout_on_invoke = false,
              },
              .local_service = services::ServiceLiveRouteBinding{
                .adapter_id = "runtime.live.local_service",
                .trust_class = services::ServiceLiveTrustClass::caller_verified,
                .availability_state = services::ServiceLiveAvailabilityState::available,
                .supported_capabilities = {"agent.terminal", "agent.dataset"},
                .handler = [](const services::ServiceLiveBackendRequest& request) {
                if (request.request_kind == services::ServiceLiveRequestKind::action) {
                  return services::ServiceLiveBackendResult{
                    .transport_outcome =
                      services::ServiceLiveTransportOutcome::acknowledged,
                    .provider_status_code = "ok",
                    .payload_json = std::string(
                              "{\"applied\":true,\"operation\":\"") +
                            request.operation_name + "\"}",
                    .latency_ms = 5U,
                    .side_effects = {request.operation_name + ".applied"},
                    .evidence_refs = {std::string("runtime://services/local/action/") +
                            request.operation_name},
                  };
                }

                if (request.operation_name == "catalog.list") {
                  return services::ServiceLiveBackendResult{
                    .transport_outcome =
                      services::ServiceLiveTransportOutcome::acknowledged,
                    .provider_status_code = "ok",
                    .payload_json = std::string(
                              "{\"target_class\":\"") +
                            request.capability_id +
                            "\",\"routes\":[\"local_service\"]}",
                    .latency_ms = 3U,
                    .side_effects = {},
                    .evidence_refs = {std::string("runtime://services/local/catalog/") +
                            request.capability_id},
                  };
                }

                return services::ServiceLiveBackendResult{
                  .transport_outcome =
                    services::ServiceLiveTransportOutcome::acknowledged,
                  .provider_status_code = "ok",
                  .payload_json = std::string(
                            "[{\"capability_id\":\"") +
                          request.capability_id + "\",\"target_id\":\"" +
                          request.target_id + "\",\"projection\":\"" +
                          request.operation_name + "\"}]",
                  .latency_ms = 4U,
                  .side_effects = {},
                  .evidence_refs = {std::string("runtime://services/local/query/") +
                            request.operation_name},
                };
                },
                .timeout_on_invoke = false,
              },
              .remote_service = services::ServiceLiveRouteBinding{
                .adapter_id = "runtime.live.remote_service",
                .trust_class = services::ServiceLiveTrustClass::caller_verified,
                .availability_state = services::ServiceLiveAvailabilityState::unavailable,
                .supported_capabilities = {"agent.terminal", "agent.dataset"},
                .handler = [](const services::ServiceLiveBackendRequest& request) {
                if (request.request_kind == services::ServiceLiveRequestKind::action) {
                  return services::ServiceLiveBackendResult{
                    .transport_outcome =
                      services::ServiceLiveTransportOutcome::acknowledged,
                    .provider_status_code = "accepted",
                    .payload_json = std::string(
                              "{\"applied\":true,\"remote\":true,\"operation\":\"") +
                            request.operation_name + "\"}",
                    .latency_ms = 11U,
                    .side_effects = {request.operation_name + ".remote_applied"},
                    .evidence_refs = {std::string("runtime://services/remote/action/") +
                            request.operation_name},
                  };
                }

                if (request.operation_name == "catalog.list") {
                  return services::ServiceLiveBackendResult{
                    .transport_outcome =
                      services::ServiceLiveTransportOutcome::acknowledged,
                    .provider_status_code = "accepted",
                    .payload_json = std::string(
                              "{\"target_class\":\"") +
                            request.capability_id +
                            "\",\"routes\":[\"remote_service\"]}",
                    .latency_ms = 9U,
                    .side_effects = {},
                    .evidence_refs = {std::string("runtime://services/remote/catalog/") +
                            request.capability_id},
                  };
                }

                return services::ServiceLiveBackendResult{
                  .transport_outcome =
                    services::ServiceLiveTransportOutcome::acknowledged,
                  .provider_status_code = "accepted",
                  .payload_json = std::string(
                            "[{\"capability_id\":\"") +
                          request.capability_id + "\",\"target_id\":\"" +
                          request.target_id + "\",\"projection\":\"" +
                          request.operation_name + "\",\"remote\":true}]",
                  .latency_ms = 8U,
                  .side_effects = {},
                  .evidence_refs = {std::string("runtime://services/remote/query/") +
                            request.operation_name},
                };
                },
                .timeout_on_invoke = false,
              },
            },
      });
  if (!live_services.ok()) {
    return make_error(std::string("services live composition failed for ") +
                      std::string(composition_owner) + ": " + live_services.error);
  }

  if (live_services.health_probe == nullptr) {
    return make_error(std::string("services health probe composition failed for ") +
                      std::string(composition_owner));
  }
  if (const auto register_error = register_health_probe(
          dependency_set->health_monitor,
      std::string("services.capability"),
      std::string("readiness"),
          live_services.health_probe.get());
      !register_error.empty()) {
    return make_error(std::string("services health probe registration failed for ") +
                      std::string(composition_owner) + ": " + register_error);
  }
  dependency_set->health_probes.push_back(live_services.health_probe);

  const auto tool_composition = compose_runtime_tool_manager(
      *policy_snapshot,
      live_services.execution_service,
      live_services.data_service,
      readonly_assets_root,
      observability);
  dependency_set->tool_manager = tool_composition.tool_manager;
  if (dependency_set->tool_manager == nullptr) {
    return make_error(std::string("tool manager composition failed for ") +
                      std::string(composition_owner));
  }
  dependency_set->multi_agent_coordinator =
      multi_agent::create_multi_agent_coordinator(policy_snapshot->multi_agent_enabled());
  if (dependency_set->multi_agent_coordinator == nullptr) {
    return make_error(std::string("multi_agent coordinator composition failed for ") +
                      std::string(composition_owner));
  }
  dependency_set->visible_tools = tool_composition.visible_tools;
    dependency_set->available_tool_descriptors =
      tool_composition.available_tool_descriptors;
  dependency_set->external_evidence = {
      std::string("runtime:") + std::string(composition_owner) +
      (cognition_first_requested ? ":cognition-first-forced"
                                 : ":required-live-baseline"),
      std::string("runtime:") + std::string(composition_owner) +
      ":secret-manager-live-seam",
      std::string("runtime:") + std::string(composition_owner) +
      ":tool-services-production-bridge",
      std::string("runtime:") + std::string(composition_owner) +
      ":production-observability-health",
      std::string("runtime:") + std::string(composition_owner) +
      ":runtime-control-plane-observability-wired",
      std::string("runtime:") + std::string(composition_owner) +
      (policy_snapshot->multi_agent_enabled() ? ":multi-agent-enabled"
                                              : ":multi-agent-disabled"),
  };
  if (tool_composition.skill_runtime_active) {
    dependency_set->external_evidence.push_back(
        std::string("runtime:") + std::string(composition_owner) +
        ":skill-runtime-production-bridge");
  }
  if (memory_vector_fail_closed) {
    dependency_set->external_evidence.push_back(
        std::string("runtime:") + std::string(composition_owner) +
        ":memory-vector-fail-closed:sqlite-vss-assets-missing");
  }

  const bool knowledge_vector_runtime_available =
      memory_config->vector.enabled &&
      memory_config->vector.backend_type == memory::VectorBackend::SqliteVss;
  const bool knowledge_vector_runtime_overridden =
      static_cast<bool>(options.build_dense_snapshot_override) &&
      static_cast<bool>(options.create_vector_recall_store_override);
  const bool knowledge_hybrid_runtime_configured =
      knowledge_vector_runtime_available || knowledge_vector_runtime_overridden;
  const auto knowledge_runtime_canary_allowlist =
      derive_knowledge_hybrid_canary_allowlist(policy_snapshot->effective_profile_id(),
                                               knowledge_hybrid_runtime_configured);

  std::function<knowledge::index::DenseSnapshotBuildResult(
      const knowledge::index::DenseSnapshotBuildRequest& request)>
      build_dense_snapshot = options.build_dense_snapshot_override;
  std::function<std::unique_ptr<knowledge::retrieve::IVectorRecallStore>(
      const knowledge::DenseStoreFactoryContext& context)>
      create_vector_recall_store = options.create_vector_recall_store_override;
  std::function<std::unique_ptr<knowledge::retrieve::IQueryEncoder>()>
      create_query_encoder = options.create_query_encoder_override;
    std::shared_ptr<RuntimeProductionQueryEncoderHealthState>
      runtime_query_encoder_health_state;
  if (!build_dense_snapshot && knowledge_vector_runtime_available) {
    const auto vector_memory_config = *memory_config;
    build_dense_snapshot = [vector_memory_config](
                              const knowledge::index::DenseSnapshotBuildRequest& request) {
      return build_knowledge_dense_snapshot(vector_memory_config, request);
    };
  }
  if (!create_vector_recall_store && knowledge_vector_runtime_available) {
    const auto vector_memory_config = *memory_config;
    create_vector_recall_store = [vector_memory_config](
                                   const knowledge::DenseStoreFactoryContext& context) {
      return std::make_unique<RuntimeKnowledgeVectorRecallStore>(vector_memory_config,
                                                                 context);
    };
  }
  const bool local_query_fallback_forced =
      environment_flag_enabled("DASALL_DETACHED_VECTOR_LOCAL_FALLBACK");
  if (!create_query_encoder && knowledge_hybrid_runtime_configured &&
      !local_query_fallback_forced) {
    runtime_query_encoder_health_state =
        std::make_shared<RuntimeProductionQueryEncoderHealthState>(
            runtime_embedding_provider,
            dependency_set->secret_manager,
            runtime_embedding_transport,
            std::string(composition_owner));
    if (runtime_query_encoder_health_state->has_provider_configuration()) {
      create_query_encoder = [runtime_query_encoder_health_state]() {
        return std::make_unique<RuntimeProductionEmbeddingQueryEncoder>(
            runtime_query_encoder_health_state);
      };
    }
  }
  if (!create_query_encoder && knowledge_hybrid_runtime_configured) {
    const auto vector_memory_config = *memory_config;
    create_query_encoder = [vector_memory_config]() {
      return std::make_unique<RuntimeDetachedVectorQueryEncoder>(vector_memory_config);
    };
  }

  const auto knowledge_result = knowledge::create_installed_asset_knowledge_service(
      knowledge::InstalledAssetKnowledgeServiceOptions{
          .readonly_assets_root = readonly_assets_root,
          .state_root = state_root,
          .service_instance_id = std::string(composition_owner) + ":knowledge",
          .profile_id = policy_snapshot->effective_profile_id(),
          .telemetry_sinks = make_knowledge_production_telemetry_sinks(observability),
          .build_dense_snapshot = build_dense_snapshot,
          .create_vector_recall_store = create_vector_recall_store,
          .create_query_encoder = create_query_encoder,
          .runtime_canary_allowed_corpora = knowledge_runtime_canary_allowlist,
            .runtime_vector_backend_available =
              runtime_query_encoder_health_state == nullptr
                ? std::function<bool()>{}
                : [runtime_query_encoder_health_state] {
                  return runtime_query_encoder_health_state->current_available();
                },
            .runtime_canary_backend_ready =
              runtime_query_encoder_health_state == nullptr
                ? std::function<bool(const knowledge::query::NormalizedQuery&)>{}
                : [runtime_query_encoder_health_state](
                  const knowledge::query::NormalizedQuery& query) {
                  return runtime_query_encoder_health_state->ensure_canary_ready(
                    query.normalized_text);
                },
            .runtime_recent_reason_codes =
              runtime_query_encoder_health_state == nullptr
                ? std::function<std::vector<std::string>()>{}
                : [runtime_query_encoder_health_state] {
                  return runtime_query_encoder_health_state->recent_reason_codes();
                },
      });
  if (knowledge_result.ok()) {
    const auto positive_probe_error =
      validate_installed_knowledge_positive_probe(knowledge_result.service);
    if (positive_probe_error.empty()) {
      auto knowledge_refresh_source_provider =
          options.knowledge_refresh_source_provider;
      if (knowledge_refresh_source_provider == nullptr) {
        knowledge_refresh_source_provider =
            make_default_runtime_knowledge_refresh_source_provider(
                readonly_assets_root);
      }
      const auto auto_refresh = arm_runtime_knowledge_auto_refresh(
          knowledge_result.service,
          options.knowledge_refresh_timer,
          policy_snapshot->capability_cache_policy().refresh_interval_ms,
          composition_owner,
          knowledge_refresh_source_provider);
      dependency_set->knowledge_service = auto_refresh.service;
      dependency_set->external_evidence.push_back(
        std::string("runtime:") + std::string(composition_owner) +
        ":knowledge-installed-assets-ready");
      dependency_set->external_evidence.push_back(auto_refresh.evidence);
      if (!knowledge_runtime_canary_allowlist.empty() &&
          installed_knowledge_hybrid_canary_ready(knowledge_result.service,
                                                  knowledge_runtime_canary_allowlist)) {
        dependency_set->external_evidence.push_back(
          std::string("runtime:") + std::string(composition_owner) +
          ":knowledge-hybrid-canary-ready");
      }
    } else {
      dependency_set->external_evidence.push_back(
        std::string("runtime:") + std::string(composition_owner) +
        ":knowledge-degraded:" + positive_probe_error);
    }
  } else {
    dependency_set->external_evidence.push_back(
        std::string("runtime:") + std::string(composition_owner) +
        ":knowledge-unavailable:" + knowledge_result.error);
  }

  return RuntimeDependencyCompositionResult{
      .dependency_set = std::move(dependency_set),
      .error = {},
  };
}

}  // namespace dasall::apps::runtime_support