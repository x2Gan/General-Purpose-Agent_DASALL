#include <cstddef>
#include <chrono>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>

#include "error/ResultCode.h"
#include "secret/SecureBuffer.h"
#include "secret/SecretManagerLiveComposition.h"
#include "secret/SecretTypes.h"
#include "support/TestAssertions.h"

namespace {

class ScopedTempDir {
 public:
  explicit ScopedTempDir(const std::string& stem)
      : path_(std::filesystem::temp_directory_path() /
              (stem + "-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()))) {
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

[[nodiscard]] std::string hex_encode(const std::string& plaintext) {
  constexpr char kHexDigits[] = "0123456789abcdef";

  std::string encoded;
  encoded.reserve(plaintext.size() * 2U);
  for (const unsigned char value : plaintext) {
    encoded.push_back(kHexDigits[(value >> 4U) & 0x0FU]);
    encoded.push_back(kHexDigits[value & 0x0FU]);
  }

  return encoded;
}

void write_secret_fixture(const std::filesystem::path& root_dir,
                         const std::string& secret_name,
                         const std::string& plaintext) {
  std::filesystem::path secret_path = root_dir / (secret_name + ".secret");
  std::filesystem::create_directories(secret_path.parent_path());

  std::ofstream stream(secret_path);
  stream << "secret_name=" << secret_name << '\n';
  stream << "classification=credential\n";
  stream << "rotation_policy=rotation/default\n";
  stream << "owner=ops\n";
  stream << "version=v1\n";
  stream << "ciphertext_hex=" << hex_encode(plaintext) << '\n';
}

[[nodiscard]] dasall::infra::secret::SecretQuery make_query(const std::string& secret_name) {
  using dasall::infra::secret::SecretAccessMode;
  using dasall::infra::secret::SecretQuery;

  return SecretQuery{
      .secret_name = secret_name,
      .version_hint = std::string("v1"),
      .purpose = std::string("runtime_bootstrap"),
      .access_mode = SecretAccessMode::Materialize,
  };
}

[[nodiscard]] dasall::infra::secret::SecretAccessContext make_access_context() {
  using dasall::infra::secret::SecretAccessContext;

  return SecretAccessContext{
      .request_id = std::string("req-secret-live-composition"),
      .session_id = std::nullopt,
      .task_id = std::string("task-secret-live-composition"),
      .actor = std::string("runtime"),
      .consumer_module = std::string("runtime"),
      .permission_domain = std::string("secret.read"),
  };
}

void test_live_secret_manager_composition_materializes_file_backed_secret() {
  using dasall::infra::secret::compose_live_secret_manager;
  using dasall::infra::secret::SecretManagerLiveCompositionOptions;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  ScopedTempDir state_root("dasall-secret-live-composition-state");
  const std::string secret_name = "llm/providers/deepseek-prod";
  write_secret_fixture(state_root.path() / "secrets",
                       secret_name,
                       "deepseek-live-secret");

  const auto composition = compose_live_secret_manager(
      "file",
      SecretManagerLiveCompositionOptions{
          .state_root_override = state_root.path(),
      });

  assert_true(composition.ok() && composition.secret_manager != nullptr,
              "SecretManagerLiveCompositionTest should materialize a live ISecretManager seam for the file backend");

  const auto handle_result = composition.secret_manager->get_secret(
      make_query(secret_name), make_access_context());
  const auto materialized_result = composition.secret_manager->materialize(
      handle_result.handle, make_access_context());

  assert_true(handle_result.ok && handle_result.is_valid(),
              "SecretManagerLiveCompositionTest should turn a file-backed live composition into a valid secret handle");
  assert_true(materialized_result.ok && materialized_result.is_valid() &&
                  materialized_result.materialized_secret != nullptr,
              "SecretManagerLiveCompositionTest should materialize a file-backed live secret manager through the standard ISecretManager ABI");
  assert_equal(std::string("deepseek-live-secret"),
               buffer_to_text(*materialized_result.materialized_secret),
               "SecretManagerLiveCompositionTest should preserve the file-backed secret plaintext through SecureBuffer only");

  const auto released_result = composition.secret_manager->release(materialized_result.lease);
  assert_true(released_result.ok && released_result.is_valid(),
              "SecretManagerLiveCompositionTest should release the lease produced by the live file-backed secret manager");
}

void test_live_secret_manager_composition_surfaces_backend_unavailable_when_secret_root_is_missing() {
  using dasall::contracts::ResultCode;
  using dasall::infra::secret::compose_live_secret_manager;
  using dasall::infra::secret::SecretManagerLiveCompositionOptions;
  using dasall::tests::support::assert_true;

  const auto missing_state_root = std::filesystem::temp_directory_path() /
                                  "dasall-secret-live-composition-missing-root";
  std::error_code error;
  std::filesystem::remove_all(missing_state_root, error);

  const auto composition = compose_live_secret_manager(
      "file",
      SecretManagerLiveCompositionOptions{
          .state_root_override = missing_state_root,
      });

  assert_true(composition.ok() && composition.secret_manager != nullptr,
              "SecretManagerLiveCompositionTest should still return the live secret manager seam object even when the secret root is missing");

  const auto handle_result = composition.secret_manager->get_secret(
      make_query("llm/providers/deepseek-prod"), make_access_context());
  assert_true(!handle_result.ok && handle_result.is_valid() &&
                  handle_result.result_code == ResultCode::ProviderTimeout,
              "SecretManagerLiveCompositionTest should surface backend-unavailable get_secret failures through the standard provider-timeout mapping when the live secret root is missing");
}

}  // namespace

int main() {
  try {
    test_live_secret_manager_composition_materializes_file_backed_secret();
    test_live_secret_manager_composition_surfaces_backend_unavailable_when_secret_root_is_missing();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}