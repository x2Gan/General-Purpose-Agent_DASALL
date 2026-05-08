#include <cstddef>
#include <chrono>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

#include "secret/SecretBootstrapWriter.h"
#include "secret/backends/FileSecretBackend.h"
#include "support/TestAssertions.h"

namespace {

class ScopedTempDir {
 public:
  ScopedTempDir()
      : path_(std::filesystem::temp_directory_path() /
              ("dasall-secret-bootstrap-writer-" +
               std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()))) {
    std::filesystem::create_directories(path_);
  }

  ~ScopedTempDir() {
    std::error_code error;
    std::filesystem::remove_all(path_, error);
  }

  [[nodiscard]] const std::filesystem::path& path() const {
    return path_;
  }

 private:
  std::filesystem::path path_;
};

[[nodiscard]] std::string buffer_to_text(const dasall::infra::secret::SecureBuffer& buffer) {
  std::string plaintext;
  plaintext.reserve(buffer.bytes().size());
  for (const std::byte value : buffer.bytes()) {
    plaintext.push_back(static_cast<char>(value));
  }

  return plaintext;
}

[[nodiscard]] std::string read_text_file(const std::filesystem::path& path) {
  std::ifstream stream(path);
  std::string content((std::istreambuf_iterator<char>(stream)),
                      std::istreambuf_iterator<char>());
  return content;
}

[[nodiscard]] dasall::infra::secret::SecretQuery make_query(std::string secret_name,
                                                            std::string version_hint = {}) {
  using dasall::infra::secret::SecretAccessMode;
  using dasall::infra::secret::SecretQuery;

  return SecretQuery{
      .secret_name = std::move(secret_name),
      .version_hint = std::move(version_hint),
      .purpose = std::string("runtime_bootstrap"),
      .access_mode = SecretAccessMode::Materialize,
  };
}

[[nodiscard]] dasall::infra::secret::SecretAccessContext make_access_context() {
  using dasall::infra::secret::SecretAccessContext;

  return SecretAccessContext{
      .request_id = std::string("req-001"),
      .session_id = std::nullopt,
      .task_id = std::string("task-001"),
      .actor = std::string("config-operator"),
      .consumer_module = std::string("cli.config"),
      .permission_domain = std::string("secret.read"),
  };
}

void test_secret_bootstrap_writer_writes_file_backend_compatible_secret_and_redacted_ref() {
  using dasall::infra::secret::FileSecretBackend;
  using dasall::infra::secret::FileSecretBackendOptions;
  using dasall::infra::secret::SecretBootstrapRequest;
  using dasall::infra::secret::SecretBootstrapWriter;
  using dasall::infra::secret::SecretBootstrapWriterOptions;
  using dasall::infra::secret::SecretProvisioningState;
  using dasall::infra::secret::SecureBuffer;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  ScopedTempDir temp_dir;
  SecretBootstrapWriter writer(SecretBootstrapWriterOptions{
      .root_dir = temp_dir.path(),
      .encrypt_at_rest = true,
      .backend_ref = std::string("file.primary"),
      .rotation_policy_ref = std::string("rotation/manual"),
      .owner_ref = std::string("llm-config"),
      .initial_version = std::string("v1"),
  });

  const auto result = writer.import_secret(SecretBootstrapRequest{
      .provider_ref = std::string("deepseek-prod"),
      .secret = SecureBuffer::from_text_copy("dsk-secret-value"),
      .auth_profile_name = std::string("primary"),
  });

  FileSecretBackend backend(FileSecretBackendOptions{
      .root_dir = temp_dir.path(),
      .encrypt_at_rest = true,
      .backend_ref = std::string("file.primary"),
      .lease_duration_ms = 60000,
      .rotation_epoch = 1,
  });

  const auto fetched = backend.fetch_record(make_query(result.secret_name, result.version));
  const auto materialized = backend.materialize_record(fetched.record, make_access_context());
  const auto file_text = read_text_file(temp_dir.path() / "llm" / "providers" / "deepseek-prod.secret");

  assert_true(result.ok && result.is_valid() &&
                  result.provisioning_state == SecretProvisioningState::Configured,
              "SecretBootstrapWriter should provision a configured result with the frozen internal bootstrap contract");
  assert_equal(std::string("secret://llm/providers/deepseek-prod"),
               result.auth_ref,
               "SecretBootstrapWriter should project the frozen redacted auth_ref for the selected provider_ref");
  assert_equal(temp_dir.path().string(),
               result.backend_root.string(),
               "SecretBootstrapWriter should report the backend root where the secret record was written");
  assert_true(fetched.ok && fetched.is_valid() && materialized.ok && materialized.is_valid(),
              "SecretBootstrapWriter output should be readable by the existing FileSecretBackend without changing the public secret ABI");
  assert_equal(std::string("dsk-secret-value"),
               buffer_to_text(*materialized.materialized_secret),
               "SecretBootstrapWriter should persist secret bytes in a format that FileSecretBackend materializes back to the original plaintext");
  assert_true(file_text.find("ciphertext_hex=") != std::string::npos &&
                  file_text.find("dsk-secret-value") == std::string::npos,
              "SecretBootstrapWriter should keep the secret record encrypted-at-rest compatible and avoid writing plaintext into the bootstrap file");
}

void test_secret_bootstrap_writer_rejects_unsafe_provider_refs_without_creating_records() {
  using dasall::infra::secret::SecretBootstrapRequest;
  using dasall::infra::secret::SecretBootstrapWriter;
  using dasall::infra::secret::SecretBootstrapWriterOptions;
  using dasall::infra::secret::SecureBuffer;
  using dasall::tests::support::assert_true;

  ScopedTempDir temp_dir;
  SecretBootstrapWriter writer(SecretBootstrapWriterOptions{
      .root_dir = temp_dir.path(),
      .encrypt_at_rest = true,
      .backend_ref = std::string("file.primary"),
      .rotation_policy_ref = std::string("rotation/manual"),
      .owner_ref = std::string("llm-config"),
      .initial_version = std::string("v1"),
  });

  const auto result = writer.import_secret(SecretBootstrapRequest{
      .provider_ref = std::string("../escape"),
      .secret = SecureBuffer::from_text_copy("ignored-secret"),
      .auth_profile_name = std::nullopt,
  });

  assert_true(!result.ok && result.is_valid() &&
                  !std::filesystem::exists(temp_dir.path() / "llm" / "providers" / ".." / "escape.secret"),
              "SecretBootstrapWriter should fail closed on unsafe provider_ref values instead of creating a path-traversing secret record");
}

}  // namespace

int main() {
  try {
    test_secret_bootstrap_writer_writes_file_backend_compatible_secret_and_redacted_ref();
    test_secret_bootstrap_writer_rejects_unsafe_provider_refs_without_creating_records();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}