#include <cstddef>
#include <chrono>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

#include "secret/backends/FileSecretBackend.h"
#include "support/TestAssertions.h"

namespace {

class ScopedTempDir {
 public:
  ScopedTempDir()
      : path_(std::filesystem::temp_directory_path() /
              ("dasall-file-secret-backend-" +
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

void write_encrypted_fixture(const std::filesystem::path& root_dir,
                            const std::string& secret_name) {
  std::filesystem::path secret_path = root_dir;
  secret_path /= "db";
  std::filesystem::create_directories(secret_path);
  secret_path /= "root.secret";

  std::ofstream stream(secret_path);
  stream << "secret_name=" << secret_name << '\n';
  stream << "classification=credential\n";
  stream << "rotation_policy=rotation/default\n";
  stream << "owner=ops\n";
  stream << "version=v3\n";
  stream << "ciphertext_hex=726f6f742d70617373776f7264\n";
}

[[nodiscard]] std::size_t count_regular_files(const std::filesystem::path& root_dir) {
  std::size_t count = 0U;
  for (const auto& entry : std::filesystem::recursive_directory_iterator(root_dir)) {
    if (entry.is_regular_file()) {
      ++count;
    }
  }

  return count;
}

[[nodiscard]] dasall::infra::secret::SecretQuery make_query(std::string secret_name) {
  using dasall::infra::secret::SecretAccessMode;
  using dasall::infra::secret::SecretQuery;

  return SecretQuery{
      .secret_name = std::move(secret_name),
      .version_hint = std::string("v3"),
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
      .actor = std::string("runtime"),
      .consumer_module = std::string("runtime"),
      .permission_domain = std::string("secret.read"),
  };
}

void test_file_secret_backend_reads_metadata_and_materializes_without_creating_temp_plaintext_files() {
  using dasall::infra::secret::FileSecretBackend;
  using dasall::infra::secret::FileSecretBackendOptions;
  using dasall::infra::secret::SecretBackendState;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  ScopedTempDir temp_dir;
  write_encrypted_fixture(temp_dir.path(), "db/root");

  FileSecretBackend backend(FileSecretBackendOptions{
      .root_dir = temp_dir.path(),
      .encrypt_at_rest = true,
      .backend_ref = std::string("file.primary"),
      .lease_duration_ms = 60000,
      .rotation_epoch = 1,
  });

  const auto fetched = backend.fetch_record(make_query("db/root"));
  const auto materialized = backend.materialize_record(fetched.record, make_access_context());
  const auto status = backend.get_backend_status();

  assert_true(fetched.ok && fetched.is_valid() && fetched.record.encrypted_at_rest,
              "FileSecretBackend should read a valid encrypted-at-rest metadata record from the configured root_dir");
  assert_true(materialized.ok && materialized.is_valid() && materialized.materialized_secret != nullptr,
              "FileSecretBackend should materialize the encoded fixture into a secure buffer and valid lease");
  assert_equal(std::string("root-password"),
               buffer_to_text(*materialized.materialized_secret),
               "FileSecretBackend should decode the encrypted-at-rest fixture into the expected secret bytes");
  assert_true(status.is_valid() && status.state == SecretBackendState::Available,
              "FileSecretBackend should report an available backend state when root_dir exists and is readable");
  assert_true(count_regular_files(temp_dir.path()) == 1U,
              "FileSecretBackend should not create extra plaintext temporary files while reading and materializing the secret");
}

void test_file_secret_backend_returns_not_found_for_missing_secret_paths() {
  using dasall::contracts::ResultCode;
  using dasall::infra::secret::FileSecretBackend;
  using dasall::infra::secret::FileSecretBackendOptions;
  using dasall::tests::support::assert_true;

  ScopedTempDir temp_dir;
  FileSecretBackend backend(FileSecretBackendOptions{
      .root_dir = temp_dir.path(),
      .encrypt_at_rest = true,
      .backend_ref = std::string("file.primary"),
      .lease_duration_ms = 60000,
      .rotation_epoch = 1,
  });

  const auto fetched = backend.fetch_record(make_query("db/missing"));
  assert_true(!fetched.ok && fetched.is_valid() &&
                  fetched.result_code == ResultCode::ValidationFieldMissing,
              "FileSecretBackend should return the frozen not-found mapping when the secret path is missing under root_dir");
}

void test_file_secret_backend_reports_backend_unavailable_when_root_dir_is_missing() {
  using dasall::contracts::ResultCode;
  using dasall::infra::secret::FileSecretBackend;
  using dasall::infra::secret::FileSecretBackendOptions;
  using dasall::infra::secret::SecretBackendState;
  using dasall::tests::support::assert_true;

  const std::filesystem::path missing_root =
      std::filesystem::temp_directory_path() / "dasall-file-secret-backend-missing-root";
  std::error_code error;
  std::filesystem::remove_all(missing_root, error);

  FileSecretBackend backend(FileSecretBackendOptions{
      .root_dir = missing_root,
      .encrypt_at_rest = true,
      .backend_ref = std::string("file.primary"),
      .lease_duration_ms = 60000,
      .rotation_epoch = 1,
  });

  const auto fetched = backend.fetch_record(make_query("db/root"));
  const auto status = backend.get_backend_status();

  assert_true(!fetched.ok && fetched.is_valid() &&
                  fetched.result_code == ResultCode::ProviderTimeout,
              "FileSecretBackend should surface backend-unavailable fetch failures when root_dir is missing");
  assert_true(status.is_valid() && status.state == SecretBackendState::Unavailable &&
                  status.last_error_code == ResultCode::ProviderTimeout,
              "FileSecretBackend should report an unavailable backend status with the last backend-unavailable result code");
}

}  // namespace

int main() {
  try {
    test_file_secret_backend_reads_metadata_and_materializes_without_creating_temp_plaintext_files();
    test_file_secret_backend_returns_not_found_for_missing_secret_paths();
    test_file_secret_backend_reports_backend_unavailable_when_root_dir_is_missing();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}