#include "transport/CurlCommandLLMTransport.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>

#include <sys/wait.h>
#include <unistd.h>

#include "secret/ISecretBackend.h"
#include "secret/SecretTypes.h"

namespace dasall::llm::transport {
namespace {

constexpr std::string_view kSecretRefPrefix = "secret://";
constexpr std::string_view kProfileNoAuthPrefix = "profile://";
constexpr std::string_view kStatusMarker = "\n__DASALL_CURL_HTTP_STATUS__:";

[[nodiscard]] LLMTransportResponse make_error(std::string message) {
  return LLMTransportResponse{
      .status_code = 0U,
      .body = {},
      .error_message = std::move(message),
  };
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

[[nodiscard]] std::string secure_buffer_to_string(
    const infra::secret::SecureBuffer& buffer) {
  std::string value;
  value.reserve(buffer.size());
  for (const std::byte byte : buffer.bytes()) {
    value.push_back(static_cast<char>(byte));
  }
  return value;
}

[[nodiscard]] std::optional<std::string> materialize_secret_ref(
    infra::secret::ISecretBackend& backend,
    std::string_view auth_ref,
    std::string_view actor,
    std::string_view base_url_alias,
    std::string* error_message) {
  if (!auth_ref.starts_with(kSecretRefPrefix) || auth_ref.size() <= kSecretRefPrefix.size()) {
    if (error_message != nullptr) {
      *error_message = "transport auth_ref must be a non-empty secret:// reference";
    }
    return std::nullopt;
  }

  const std::string secret_name(auth_ref.substr(kSecretRefPrefix.size()));
  const auto fetched = backend.fetch_record(infra::secret::SecretQuery{
      .secret_name = secret_name,
      .version_hint = {},
      .purpose = "llm_transport_authorization",
      .access_mode = infra::secret::SecretAccessMode::Materialize,
  });
  if (!fetched.ok) {
    if (error_message != nullptr) {
      *error_message = "secret backend could not fetch auth_ref";
    }
    return std::nullopt;
  }

  const auto materialized = backend.materialize_record(
      fetched.record,
      infra::secret::SecretAccessContext{
          .request_id = std::nullopt,
          .session_id = std::nullopt,
          .task_id = std::string("llm-transport:") + std::string(base_url_alias),
          .actor = std::string(actor),
          .consumer_module = "llm.transport.curl",
          .permission_domain = "llm.provider.auth",
      });
  if (!materialized.ok || materialized.materialized_secret == nullptr ||
      !materialized.materialized_secret->is_accessible()) {
    if (error_message != nullptr) {
      *error_message = "secret backend could not materialize auth_ref";
    }
    return std::nullopt;
  }

  return secure_buffer_to_string(*materialized.materialized_secret);
}

[[nodiscard]] std::optional<std::string> make_authorization_header(
    const LLMTransportRequest& request,
    const CurlCommandLLMTransportOptions& options,
    std::string* error_message) {
  if (request.auth_ref.starts_with(kProfileNoAuthPrefix)) {
    return std::string();
  }

  if (options.secret_backend == nullptr) {
    if (error_message != nullptr) {
      *error_message = "curl transport requires a secret backend for provider auth";
    }
    return std::nullopt;
  }

  const auto secret = materialize_secret_ref(*options.secret_backend,
                                            request.auth_ref,
                                            options.actor,
                                            request.base_url_alias,
                                            error_message);
  if (!secret.has_value() || secret->empty()) {
    return std::nullopt;
  }

  return "Authorization: Bearer " + *secret;
}

[[nodiscard]] std::string method_to_string(LLMTransportMethod method) {
  return method == LLMTransportMethod::Post ? "POST" : "GET";
}

[[nodiscard]] std::uint32_t timeout_seconds(std::uint32_t timeout_ms) {
  return std::max<std::uint32_t>(1U, (timeout_ms + 999U) / 1000U);
}

[[nodiscard]] std::string build_curl_config(
    const LLMTransportRequest& request,
    const std::optional<std::filesystem::path>& body_path,
    const std::optional<std::string>& authorization_header) {
  std::ostringstream config;
  config << "silent\n";
  config << "show-error\n";
  config << "request = \"" << method_to_string(request.method) << "\"\n";
  config << "url = \"" << curl_config_escape(request.url) << "\"\n";
  config << "max-time = \"" << timeout_seconds(request.timeout_ms) << "\"\n";
  config << "connect-timeout = \"10\"\n";
  config << "output = \"-\"\n";
  config << "write-out = \"\\n__DASALL_CURL_HTTP_STATUS__:%{http_code}\"\n";
  config << "user-agent = \"DASALL/0.1 llm-transport\"\n";

  for (const auto& header : request.headers) {
    config << "header = \"" << curl_config_escape(header.name + ": " + header.value)
           << "\"\n";
  }

  if (authorization_header.has_value() && !authorization_header->empty()) {
    config << "header = \"" << curl_config_escape(*authorization_header) << "\"\n";
  }

  if (body_path.has_value()) {
    config << "data-binary = \"@" << curl_config_escape(body_path->string()) << "\"\n";
  }

  return config.str();
}

[[nodiscard]] LLMTransportResponse parse_curl_output(std::string output,
                                                     int process_status) {
  const std::size_t marker_position = output.rfind(kStatusMarker);
  if (marker_position == std::string::npos) {
    return make_error("curl transport did not return an HTTP status marker");
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
    return make_error("curl transport returned a malformed HTTP status marker");
  }

  if (process_status != 0 && status_code == 0U) {
    return make_error("curl transport process failed before receiving an HTTP response");
  }

  output.erase(marker_position);
  return LLMTransportResponse{
      .status_code = status_code,
      .body = std::move(output),
      .error_message = {},
  };
}

[[nodiscard]] std::string run_curl_command(const std::filesystem::path& curl_path,
                                           const std::filesystem::path& config_path,
                                           int* process_status) {
  const std::string command = shell_quote(curl_path) + " --config " + shell_quote(config_path);
  FILE* pipe = ::popen(command.c_str(), "r");
  if (pipe == nullptr) {
    if (process_status != nullptr) {
      *process_status = -1;
    }
    return {};
  }

  std::string output;
  std::array<char, 4096> buffer{};
  while (std::fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
    output += buffer.data();
  }

  const int status = ::pclose(pipe);
  if (process_status != nullptr) {
    if (status < 0) {
      *process_status = -1;
    } else if (WIFEXITED(status)) {
      *process_status = WEXITSTATUS(status);
    } else {
      *process_status = status;
    }
  }
  return output;
}

}  // namespace

CurlCommandLLMTransport::CurlCommandLLMTransport(CurlCommandLLMTransportOptions options)
    : options_(std::move(options)) {}

LLMTransportResponse CurlCommandLLMTransport::send(const LLMTransportRequest& request) {
  if (!request.has_consistent_values()) {
    return make_error("curl transport received an inconsistent request");
  }

  if (!request.header_refs.empty()) {
    return make_error("curl transport does not support dynamic header_refs in v1");
  }

  std::error_code error;
  if (!std::filesystem::exists(options_.curl_path, error)) {
    return make_error("curl transport could not find curl executable");
  }

  std::string auth_error;
  const auto authorization_header = make_authorization_header(request, options_, &auth_error);
  if (!authorization_header.has_value()) {
    return make_error(auth_error.empty() ? "curl transport could not resolve provider auth" : auth_error);
  }

  std::optional<std::filesystem::path> body_path;
  if (request.method == LLMTransportMethod::Post) {
    body_path = make_temp_file(options_.temp_dir, "dasall-llm-body");
    if (!body_path.has_value() || !write_file(*body_path, request.body)) {
      if (body_path.has_value()) {
        std::filesystem::remove(*body_path, error);
      }
      return make_error("curl transport could not write request body temp file");
    }
  }

  const auto config_path = make_temp_file(options_.temp_dir, "dasall-llm-curl");
  if (!config_path.has_value()) {
    if (body_path.has_value()) {
      std::filesystem::remove(*body_path, error);
    }
    return make_error("curl transport could not create curl config temp file");
  }

  const std::string curl_config = build_curl_config(request, body_path, authorization_header);
  if (!write_file(*config_path, curl_config)) {
    std::filesystem::remove(*config_path, error);
    if (body_path.has_value()) {
      std::filesystem::remove(*body_path, error);
    }
    return make_error("curl transport could not write curl config temp file");
  }

  int process_status = 0;
  std::string output = run_curl_command(options_.curl_path, *config_path, &process_status);
  std::filesystem::remove(*config_path, error);
  if (body_path.has_value()) {
    std::filesystem::remove(*body_path, error);
  }

  return parse_curl_output(std::move(output), process_status);
}

}  // namespace dasall::llm::transport