#include "config/LlmSecretPage.h"

#include <filesystem>
#include <fstream>
#include <string_view>

namespace dasall::apps::cli::config {
namespace {

namespace fs = std::filesystem;

constexpr std::string_view kSecretRefPrefix = "secret://llm/providers/";

[[nodiscard]] std::optional<std::string> parse_provider_ref(
    const std::string_view ref) {
  if (!ref.starts_with(kSecretRefPrefix) ||
      ref.size() <= kSecretRefPrefix.size()) {
    return std::nullopt;
  }

  return std::string(ref.substr(kSecretRefPrefix.size()));
}

[[nodiscard]] std::string trim_trailing_newlines(std::string value) {
  while (!value.empty() &&
         (value.back() == '\n' || value.back() == '\r')) {
    value.pop_back();
  }
  return value;
}

[[nodiscard]] bool import_file_is_owner_only(const fs::path& path) {
  std::error_code error;
  const auto permissions = fs::status(path, error).permissions();
  if (error) {
    return false;
  }

  const auto shared_bits =
      permissions & (fs::perms::group_all | fs::perms::others_all);
  return shared_bits == fs::perms::none;
}

[[nodiscard]] std::optional<std::string> read_import_file(
    const fs::path& path,
    std::string* error_message) {
  if (!fs::exists(path) || !fs::is_regular_file(path)) {
    if (error_message != nullptr) {
      *error_message = "secret import file is missing: " + path.string();
    }
    return std::nullopt;
  }

  if (!import_file_is_owner_only(path)) {
    if (error_message != nullptr) {
      *error_message =
          "secret import file must be owner-only: " + path.string();
    }
    return std::nullopt;
  }

  std::ifstream stream(path, std::ios::binary);
  if (!stream.is_open()) {
    if (error_message != nullptr) {
      *error_message = "unable to open secret import file: " +
                       path.string();
    }
    return std::nullopt;
  }

  std::string content((std::istreambuf_iterator<char>(stream)),
                      std::istreambuf_iterator<char>());
  content = trim_trailing_newlines(std::move(content));
  if (content.empty()) {
    if (error_message != nullptr) {
      *error_message = "secret import file is empty: " + path.string();
    }
    return std::nullopt;
  }

  return content;
}

[[nodiscard]] bool source_is_file(const std::string_view source) {
  return source.starts_with("file:");
}

}  // namespace

bool LlmSecretPageResult::succeeded() const {
  return success && blocked_actions.empty() && error_message.empty();
}

LlmSecretPage::LlmSecretPage() = default;

LlmSecretPage::LlmSecretPage(InteractivePromptEngine prompt_engine,
                             StdinReader stdin_reader)
    : prompt_engine_(std::move(prompt_engine)),
      stdin_reader_(std::move(stdin_reader)) {}

LlmSecretPageResult LlmSecretPage::collect_and_apply(
    const DesiredSecretSettings& desired_settings,
    const dasall::infra::secret::SecretBootstrapWriter& writer) const {
  LlmSecretPageResult result;
  if (desired_settings.refs.empty()) {
    return result;
  }

  if (desired_settings.refs.size() != 1U) {
    result.success = false;
    result.blocked_actions.push_back(
        "multiple_secret_bootstrap_inputs_not_supported_in_v1");
    result.error_message =
        "LLM secret onboarding currently supports exactly one secret ref per apply";
    return result;
  }

  const auto& secret_ref = desired_settings.refs.front();
  const auto provider_ref = parse_provider_ref(secret_ref.ref);
  if (!provider_ref.has_value()) {
    result.success = false;
    result.blocked_actions.push_back("invalid_secret_ref_projection");
    result.error_message =
        "LLM secret onboarding requires auth_ref=secret://llm/providers/<provider_ref>";
    return result;
  }

  std::optional<std::string> secret_value;
  if (secret_ref.source == "prompt") {
    const auto prompt_response = prompt_engine_.prompt_secret(
        "llm.secret_value",
        "[LLMSecretPage]\nEnter LLM provider secret for " + *provider_ref);
    if (!prompt_response.accepted || prompt_response.value.empty()) {
      result.success = false;
      result.blocked_actions.push_back("llm_secret_prompt_missing_value");
      result.error_message =
          "LLM secret onboarding requires a masked prompt value before apply";
      return result;
    }
    secret_value = prompt_response.value;
  } else if (secret_ref.source == "stdin") {
    if (!stdin_reader_) {
      result.success = false;
      result.blocked_actions.push_back("llm_secret_stdin_unavailable");
      result.error_message =
          "LLM secret onboarding requires stdin data for source=stdin";
      return result;
    }
    secret_value = stdin_reader_();
    if (!secret_value.has_value() || secret_value->empty()) {
      result.success = false;
      result.blocked_actions.push_back("llm_secret_stdin_missing_value");
      result.error_message =
          "LLM secret onboarding received an empty stdin payload";
      return result;
    }
    *secret_value = trim_trailing_newlines(std::move(*secret_value));
  } else if (source_is_file(secret_ref.source)) {
    const fs::path import_path(std::string(
        secret_ref.source.substr(std::string_view("file:").size())));
    secret_value = read_import_file(import_path, &result.error_message);
    if (!secret_value.has_value()) {
      result.success = false;
      result.blocked_actions.push_back("llm_secret_import_file_invalid");
      return result;
    }
  } else {
    result.success = false;
    result.blocked_actions.push_back("llm_secret_source_unsupported");
    result.error_message =
        "LLM secret onboarding only supports source=prompt|stdin|file:<path>";
    return result;
  }

  const auto provisioning_result = writer.import_secret(
      dasall::infra::secret::SecretBootstrapRequest{
          .provider_ref = *provider_ref,
          .secret = dasall::infra::secret::SecureBuffer::from_text_copy(
              *secret_value),
          .auth_profile_name = secret_ref.auth_profile_name,
      });
  if (!provisioning_result.ok) {
    result.success = false;
    result.blocked_actions.push_back("llm_secret_bootstrap_failed");
    result.error_message = provisioning_result.error_message;
    return result;
  }

  result.written_secret_refs.push_back(provisioning_result.auth_ref);
  return result;
}

}  // namespace dasall::apps::cli::config