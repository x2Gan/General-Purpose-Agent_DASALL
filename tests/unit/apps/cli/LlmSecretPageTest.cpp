#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>

#include <unistd.h>

#include "config/LlmSecretPage.h"
#include "support/TestAssertions.h"

namespace {

namespace fs = std::filesystem;

[[nodiscard]] fs::path make_temp_directory(std::string_view stem) {
  const auto unique_id = std::to_string(::getpid()) + "-" +
                         std::to_string(
                             fs::file_time_type::clock::now().time_since_epoch().count());
  const fs::path temp_root = fs::temp_directory_path() /
                             (std::string(stem) + "-" + unique_id);
  fs::create_directories(temp_root);
  return temp_root;
}

void cleanup_path(const fs::path& path) {
  std::error_code error;
  fs::remove_all(path, error);
}

void write_text_file(const fs::path& path, std::string_view content) {
  fs::create_directories(path.parent_path());
  std::ofstream stream(path, std::ios::binary | std::ios::trunc);
  stream << content;
}

[[nodiscard]] std::string read_text_file(const fs::path& path) {
  std::ifstream stream(path, std::ios::binary);
  return std::string(std::istreambuf_iterator<char>(stream),
                     std::istreambuf_iterator<char>());
}

void test_collect_and_apply_uses_masked_prompt_for_prompt_source() {
  using dasall::apps::cli::config::DesiredSecretRefInput;
  using dasall::apps::cli::config::DesiredSecretSettings;
  using dasall::apps::cli::config::InteractivePromptEngine;
  using dasall::apps::cli::config::LlmSecretPage;
  using dasall::infra::secret::SecretBootstrapWriter;
  using dasall::infra::secret::SecretBootstrapWriterOptions;
  using dasall::tests::support::assert_true;

  const fs::path workspace = make_temp_directory("llm-secret-page-prompt");
  bool saw_masked_prompt = false;
  LlmSecretPage page(InteractivePromptEngine(
      [&saw_masked_prompt](const dasall::apps::cli::config::PromptRequest& request)
          -> std::optional<std::string> {
        saw_masked_prompt = request.masked;
        return std::string("prompt-secret-value");
      },
      {}));
  SecretBootstrapWriter writer(SecretBootstrapWriterOptions{
      .root_dir = workspace / "var/lib/dasall/secrets",
  });

  DesiredSecretSettings desired_settings;
  desired_settings.refs.push_back(DesiredSecretRefInput{
      .ref = "secret://llm/providers/deepseek-prod",
      .source = "prompt",
      .auth_profile_name = std::string("primary"),
  });

  const auto result = page.collect_and_apply(desired_settings, writer);
  const auto stored_text = read_text_file(workspace / "var/lib/dasall/secrets/llm/providers/deepseek-prod.secret");

  assert_true(result.succeeded() && saw_masked_prompt,
              "LlmSecretPage should collect prompt-backed secrets through a masked prompt before calling the bootstrap writer");
  assert_true(result.written_secret_refs.size() == 1U &&
                  result.written_secret_refs.front() == "secret://llm/providers/deepseek-prod",
              "LlmSecretPage should return only the redacted auth_ref after prompt-backed onboarding succeeds");
  assert_true(stored_text.find("prompt-secret-value") == std::string::npos,
              "LlmSecretPage prompt-backed onboarding should avoid persisting plaintext into the secret backend file");

  cleanup_path(workspace);
}

void test_collect_and_apply_reads_stdin_for_headless_source() {
  using dasall::apps::cli::config::DesiredSecretRefInput;
  using dasall::apps::cli::config::DesiredSecretSettings;
  using dasall::apps::cli::config::LlmSecretPage;
  using dasall::infra::secret::SecretBootstrapWriter;
  using dasall::infra::secret::SecretBootstrapWriterOptions;
  using dasall::tests::support::assert_true;

  const fs::path workspace = make_temp_directory("llm-secret-page-stdin");
  LlmSecretPage page(dasall::apps::cli::config::InteractivePromptEngine{},
                     []() -> std::optional<std::string> {
    return std::string("stdin-secret-value\n");
  });
  SecretBootstrapWriter writer(SecretBootstrapWriterOptions{
      .root_dir = workspace / "var/lib/dasall/secrets",
  });

  DesiredSecretSettings desired_settings;
  desired_settings.refs.push_back(DesiredSecretRefInput{
      .ref = "secret://llm/providers/deepseek-prod",
      .source = "stdin",
      .auth_profile_name = std::nullopt,
  });

  const auto result = page.collect_and_apply(desired_settings, writer);

  assert_true(result.succeeded() && result.written_secret_refs.size() == 1U,
              "LlmSecretPage should support source=stdin for headless secret onboarding");

  cleanup_path(workspace);
}

void test_collect_and_apply_rejects_group_readable_import_file() {
  using dasall::apps::cli::config::DesiredSecretRefInput;
  using dasall::apps::cli::config::DesiredSecretSettings;
  using dasall::apps::cli::config::LlmSecretPage;
  using dasall::infra::secret::SecretBootstrapWriter;
  using dasall::infra::secret::SecretBootstrapWriterOptions;
  using dasall::tests::support::assert_true;

  const fs::path workspace = make_temp_directory("llm-secret-page-import-file");
  const fs::path import_file = workspace / "secrets/provider.key";
  write_text_file(import_file, "file-secret-value\n");
  std::error_code permissions_error;
  fs::permissions(import_file,
                  fs::perms::owner_read | fs::perms::owner_write |
                      fs::perms::group_read,
                  fs::perm_options::replace,
                  permissions_error);

  LlmSecretPage page;
  SecretBootstrapWriter writer(SecretBootstrapWriterOptions{
      .root_dir = workspace / "var/lib/dasall/secrets",
  });

  DesiredSecretSettings desired_settings;
  desired_settings.refs.push_back(DesiredSecretRefInput{
      .ref = "secret://llm/providers/deepseek-prod",
      .source = "file:" + import_file.string(),
      .auth_profile_name = std::nullopt,
  });

  const auto result = page.collect_and_apply(desired_settings, writer);

  assert_true(!result.succeeded() &&
                  result.error_message.find("owner-only") != std::string::npos,
              "LlmSecretPage should fail closed when an import file is readable by group or others");

  cleanup_path(workspace);
}

}  // namespace

int main() {
  try {
    test_collect_and_apply_uses_masked_prompt_for_prompt_source();
    test_collect_and_apply_reads_stdin_for_headless_source();
    test_collect_and_apply_rejects_group_readable_import_file();
  } catch (const std::exception& ex) {
    std::cerr << "LlmSecretPageTest failed: " << ex.what() << '\n';
    return 1;
  }

  std::cout << "LlmSecretPageTest passed\n";
  return 0;
}