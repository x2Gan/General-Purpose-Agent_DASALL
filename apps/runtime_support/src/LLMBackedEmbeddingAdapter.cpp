#include "LLMBackedEmbeddingAdapter.h"

#include <algorithm>
#include <cctype>
#include <optional>
#include <string_view>
#include <utility>

#include "ILLMTransport.h"
#include "secret/ISecretManager.h"
#include "secret/SecretTypes.h"

namespace dasall::apps::runtime_support {
namespace {

constexpr std::string_view kSecretRefPrefix = "secret://";
constexpr std::string_view kProfileRefPrefix = "profile://";
constexpr std::string_view kEmbeddingEndpointPath = "/embeddings";

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

[[nodiscard]] std::optional<std::size_t> find_json_value_start(
    std::string_view payload,
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
  const auto data_array = extract_json_array_field(payload, "data");
  if (!data_array.has_value()) {
    return {};
  }

  const auto embedding_json = extract_json_array_field(*data_array, "embedding");
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
  return normalize_base_url(base_url) + std::string(kEmbeddingEndpointPath);
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

[[nodiscard]] infra::secret::SecretAccessContext make_access_context(
    const LLMBackedEmbeddingAdapter::Options& options,
    std::string_view purpose) {
  const std::string request_id = options.composition_owner + ":" +
                                 std::string(purpose);
  return infra::secret::SecretAccessContext{
      .request_id = request_id,
      .session_id = std::nullopt,
      .task_id = request_id,
      .actor = options.composition_owner,
      .consumer_module = "runtime.memory.embedding",
      .permission_domain = "llm.provider.auth",
  };
}

[[nodiscard]] std::optional<std::string> materialize_bearer_token(
    const std::shared_ptr<infra::secret::ISecretManager>& secret_manager,
    const LLMBackedEmbeddingAdapter::Options& options) {
  if (options.provider.auth_ref.starts_with(kProfileRefPrefix)) {
    return std::string();
  }

  const auto secret_name = secret_name_from_auth_ref(options.provider.auth_ref);
  if (!secret_name.has_value() || secret_manager == nullptr) {
    return std::nullopt;
  }

  const auto access_context = make_access_context(options, "materialize");
  const auto handle_result = secret_manager->get_secret(
      infra::secret::SecretQuery{
          .secret_name = *secret_name,
          .version_hint = {},
          .purpose = "memory_embedding",
          .access_mode = infra::secret::SecretAccessMode::Materialize,
      },
      access_context);
  if (!handle_result.ok || !handle_result.handle.is_valid()) {
    return std::nullopt;
  }

  const auto materialized_result =
      secret_manager->materialize(handle_result.handle, access_context);
  if (!materialized_result.ok || !materialized_result.is_valid() ||
      materialized_result.materialized_secret == nullptr ||
      !materialized_result.materialized_secret->is_accessible()) {
    return std::nullopt;
  }

  const std::string token =
      secure_buffer_to_string(*materialized_result.materialized_secret);
  if (materialized_result.lease.is_valid()) {
    (void)secret_manager->release(materialized_result.lease);
  }
  return token;
}

}  // namespace

LLMBackedEmbeddingAdapter::LLMBackedEmbeddingAdapter(
    std::shared_ptr<llm::ILLMTransport> transport,
    std::shared_ptr<infra::secret::ISecretManager> secret_manager,
    Options options)
    : transport_(std::move(transport)),
      secret_manager_(std::move(secret_manager)),
      options_(std::move(options)),
      dimension_(std::max(0, options_.expected_dimension)) {}

std::vector<float> LLMBackedEmbeddingAdapter::embed(const std::string& text) const {
  if (text.empty() || transport_ == nullptr ||
      !options_.provider.has_consistent_values()) {
    return {};
  }

  const auto bearer_token = materialize_bearer_token(secret_manager_, options_);
  if (!bearer_token.has_value()) {
    return {};
  }

  llm::LLMTransportRequest request;
  request.method = llm::LLMTransportMethod::Post;
  request.url = build_embedding_url(options_.provider.base_url);
  request.auth_ref = options_.provider.auth_ref;
  request.base_url_alias = options_.provider.base_url_alias;
  request.snapshot_version = options_.provider.snapshot_version;
  request.timeout_ms = options_.provider.timeout_ms;
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
                 escape_json(options_.provider.model_id) +
                 "\",\"input\":\"" +
                 escape_json(text) +
                 "\"}";

  const auto response = transport_->send(request);
  if (!response.ok()) {
    return {};
  }

  const auto embedding = extract_embedding_vector(response.body);
  if (embedding.empty()) {
    return {};
  }

  std::scoped_lock lock(mutex_);
  if (dimension_ == 0) {
    dimension_ = static_cast<int>(embedding.size());
  } else if (dimension_ != static_cast<int>(embedding.size())) {
    return {};
  }

  return embedding;
}

int LLMBackedEmbeddingAdapter::dimension() const {
  std::scoped_lock lock(mutex_);
  return dimension_;
}

}  // namespace dasall::apps::runtime_support