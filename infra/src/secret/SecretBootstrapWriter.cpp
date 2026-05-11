#include "secret/SecretBootstrapWriter.h"

#include <cctype>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>

#include <cerrno>
#include <grp.h>
#include <sys/types.h>
#include <unistd.h>

#include "secret/ISecretBackend.h"
#include "secret/backends/FileSecretBackend.h"

namespace dasall::infra::secret {
namespace {

namespace fs = std::filesystem;

[[nodiscard]] bool is_provider_ref_safe(const std::string_view provider_ref) {
  if (provider_ref.empty()) {
    return false;
  }

  return std::all_of(provider_ref.begin(),
                     provider_ref.end(),
                     [](const char value) {
                       return std::isalnum(static_cast<unsigned char>(value)) != 0 ||
                              value == '-' || value == '_';
                     });
}

[[nodiscard]] std::string make_secret_name(const std::string_view provider_ref) {
  return std::string("llm/providers/") + std::string(provider_ref);
}

[[nodiscard]] std::string make_auth_ref(const std::string_view provider_ref) {
  return std::string("secret://llm/providers/") + std::string(provider_ref);
}

[[nodiscard]] std::string hex_encode(const std::span<const std::byte> bytes) {
  constexpr char kHexDigits[] = "0123456789abcdef";

  std::string encoded;
  encoded.reserve(bytes.size() * 2U);
  for (const std::byte value : bytes) {
    const auto byte_value = static_cast<unsigned char>(value);
    encoded.push_back(kHexDigits[(byte_value >> 4U) & 0x0FU]);
    encoded.push_back(kHexDigits[byte_value & 0x0FU]);
  }
  return encoded;
}

[[nodiscard]] std::string next_version(const std::string_view current_version,
                                       const std::string_view fallback_version) {
  if (current_version.empty()) {
    return std::string(fallback_version);
  }

  if (current_version.size() >= 2U && current_version.front() == 'v') {
    const std::string numeric_suffix(current_version.substr(1U));
    bool all_digits = !numeric_suffix.empty();
    for (const char value : numeric_suffix) {
      if (std::isdigit(static_cast<unsigned char>(value)) == 0) {
        all_digits = false;
        break;
      }
    }

    if (all_digits) {
      const auto numeric_value = std::stoull(numeric_suffix);
      return "v" + std::to_string(numeric_value + 1U);
    }
  }

  return std::string(current_version) + ".next";
}

[[nodiscard]] std::optional<std::string> current_version_for_existing_secret(
    const SecretBootstrapWriterOptions& options,
    const std::string_view secret_name) {
  FileSecretBackend backend(FileSecretBackendOptions{
      .root_dir = options.root_dir,
      .encrypt_at_rest = options.encrypt_at_rest,
      .backend_ref = options.backend_ref,
      .lease_duration_ms = 60000,
      .rotation_epoch = 1,
  });

  const auto fetched = backend.fetch_record(SecretQuery{
      .secret_name = std::string(secret_name),
      .version_hint = {},
      .purpose = std::string("bootstrap_import"),
      .access_mode = SecretAccessMode::Rotate,
  });
  if (!fetched.ok) {
    return std::nullopt;
  }

  return fetched.record.version;
}

[[nodiscard]] bool ensure_directory(const fs::path& path,
                                    fs::perms permissions,
                                    std::string* error_message) {
  std::error_code error;
  fs::create_directories(path, error);
  if (error) {
    if (error_message != nullptr) {
      *error_message = "unable to create secret directory: " + path.string();
    }
    return false;
  }

  fs::permissions(path, permissions, fs::perm_options::replace, error);
  if (error) {
    if (error_message != nullptr) {
      *error_message = "unable to set secret directory permissions: " + path.string();
    }
    return false;
  }

  return true;
}

[[nodiscard]] std::optional<gid_t> runtime_group_gid(
    const std::string& group_name) {
  if (group_name.empty()) {
    return std::nullopt;
  }

  const struct group* group_entry = ::getgrnam(group_name.c_str());
  if (group_entry == nullptr) {
    return std::nullopt;
  }

  return group_entry->gr_gid;
}

[[nodiscard]] bool apply_runtime_reader_access(const fs::path& path,
                                               const std::string& group_name,
                                               const bool directory,
                                               std::string* error_message) {
  const auto group_id = runtime_group_gid(group_name);
  if (group_id.has_value()) {
    if (::chown(path.c_str(), static_cast<uid_t>(-1), *group_id) != 0 && errno != EPERM) {
      if (error_message != nullptr) {
        *error_message = "unable to assign secret runtime reader group: " + path.string();
      }
      return false;
    }
  }

  std::error_code error;
  const auto permissions = directory
                               ? (fs::perms::owner_all | fs::perms::group_read |
                                  fs::perms::group_exec)
                               : (fs::perms::owner_read | fs::perms::owner_write |
                                  fs::perms::group_read);
  fs::permissions(path, permissions, fs::perm_options::replace, error);
  if (error) {
    if (error_message != nullptr) {
      *error_message = "unable to set secret runtime reader permissions: " + path.string();
    }
    return false;
  }

  return true;
}

[[nodiscard]] bool write_secret_document(const fs::path& path,
                                         const std::string& document,
                                         std::string* error_message) {
  std::ofstream stream(path, std::ios::binary | std::ios::trunc);
  if (!stream.is_open()) {
    if (error_message != nullptr) {
      *error_message = "unable to open temp secret file: " + path.string();
    }
    return false;
  }

  stream.write(document.data(), static_cast<std::streamsize>(document.size()));
  stream.flush();
  if (!stream.good()) {
    if (error_message != nullptr) {
      *error_message = "unable to flush temp secret file: " + path.string();
    }
    return false;
  }

  std::error_code error;
  fs::permissions(path,
                  fs::perms::owner_read | fs::perms::owner_write,
                  fs::perm_options::replace,
                  error);
  if (error) {
    if (error_message != nullptr) {
      *error_message = "unable to set secret file permissions: " + path.string();
    }
    return false;
  }

  return true;
}

}  // namespace

std::string_view to_string(const SecretProvisioningState state) {
  switch (state) {
    case SecretProvisioningState::Missing:
      return "missing";
    case SecretProvisioningState::Configured:
      return "configured";
    case SecretProvisioningState::RuntimeVerified:
      return "runtime_verified";
  }

  return "missing";
}

bool SecretBootstrapRequest::is_valid() const {
  return is_provider_ref_safe(provider_ref) && secret.is_accessible() && secret.size() > 0U;
}

SecretProvisioningResult SecretProvisioningResult::success(
    std::string auth_ref,
    std::filesystem::path backend_root,
    std::string secret_name,
    std::string version) {
  return SecretProvisioningResult{
      .ok = true,
      .auth_ref = std::move(auth_ref),
      .backend_root = std::move(backend_root),
      .provisioning_state = SecretProvisioningState::Configured,
      .secret_name = std::move(secret_name),
      .version = std::move(version),
      .error_message = {},
  };
}

SecretProvisioningResult SecretProvisioningResult::failure(
    std::filesystem::path backend_root,
    std::string error_message) {
  return SecretProvisioningResult{
      .ok = false,
      .auth_ref = {},
      .backend_root = std::move(backend_root),
      .provisioning_state = SecretProvisioningState::Missing,
      .secret_name = {},
      .version = {},
      .error_message = std::move(error_message),
  };
}

bool SecretProvisioningResult::is_valid() const {
  if (ok) {
    return !auth_ref.empty() && !backend_root.empty() &&
           provisioning_state == SecretProvisioningState::Configured &&
           !secret_name.empty() && !version.empty() && error_message.empty();
  }

  return provisioning_state == SecretProvisioningState::Missing &&
         !backend_root.empty() && !error_message.empty();
}

SecretBootstrapWriter::SecretBootstrapWriter(SecretBootstrapWriterOptions options)
    : options_(std::move(options)) {}

SecretProvisioningResult SecretBootstrapWriter::import_secret(
    SecretBootstrapRequest request) const {
  if (!request.is_valid()) {
    return SecretProvisioningResult::failure(
        options_.root_dir,
        "secret bootstrap request requires a safe provider_ref and non-empty secret bytes");
  }

  std::string error_message;
  if (!ensure_directory(options_.root_dir,
                        fs::perms::owner_all,
                        &error_message)) {
    return SecretProvisioningResult::failure(options_.root_dir,
                                             std::move(error_message));
  }
  if (!apply_runtime_reader_access(options_.root_dir,
                                   options_.runtime_reader_group,
                                   true,
                                   &error_message)) {
    return SecretProvisioningResult::failure(options_.root_dir,
                                             std::move(error_message));
  }

  const fs::path provider_directory = options_.root_dir / "llm" / "providers";
  if (!ensure_directory(provider_directory,
                        fs::perms::owner_all,
                        &error_message)) {
    return SecretProvisioningResult::failure(options_.root_dir,
                                             std::move(error_message));
  }
  if (!apply_runtime_reader_access(options_.root_dir / "llm",
                                   options_.runtime_reader_group,
                                   true,
                                   &error_message) ||
      !apply_runtime_reader_access(provider_directory,
                                   options_.runtime_reader_group,
                                   true,
                                   &error_message)) {
    return SecretProvisioningResult::failure(options_.root_dir,
                                             std::move(error_message));
  }

  const std::string secret_name = make_secret_name(request.provider_ref);
  const auto existing_version = current_version_for_existing_secret(options_, secret_name);
  const std::string current_version =
      existing_version.has_value() ? *existing_version : std::string();
  const std::string version = next_version(current_version,
                                           options_.initial_version);

  std::ostringstream document;
  document << "secret_name=" << secret_name << '\n';
  document << "classification=token\n";
  document << "rotation_policy=" << options_.rotation_policy_ref << '\n';
  document << "owner=" << options_.owner_ref << '\n';
  document << "version=" << version << '\n';
  if (request.auth_profile_name.has_value() && !request.auth_profile_name->empty()) {
    document << "auth_profile=" << *request.auth_profile_name << '\n';
  }

  if (options_.encrypt_at_rest) {
    document << "ciphertext_hex=" << hex_encode(request.secret.bytes()) << '\n';
  } else {
    document << "plaintext=";
    for (const std::byte value : request.secret.bytes()) {
      document << static_cast<char>(value);
    }
    document << '\n';
  }

  const fs::path target_path = provider_directory / (request.provider_ref + ".secret");
  const fs::path temp_path = provider_directory / (request.provider_ref + ".secret.tmp");
  std::error_code error;
  fs::remove(temp_path, error);

  if (!write_secret_document(temp_path, document.str(), &error_message)) {
    fs::remove(temp_path, error);
    return SecretProvisioningResult::failure(options_.root_dir,
                                             std::move(error_message));
  }
  if (!apply_runtime_reader_access(temp_path,
                                   options_.runtime_reader_group,
                                   false,
                                   &error_message)) {
    fs::remove(temp_path, error);
    return SecretProvisioningResult::failure(options_.root_dir,
                                             std::move(error_message));
  }

  fs::rename(temp_path, target_path, error);
  if (error) {
    fs::remove(temp_path, error);
    return SecretProvisioningResult::failure(
        options_.root_dir,
        "unable to promote temp secret file into canonical location: " + target_path.string());
  }
        if (!apply_runtime_reader_access(target_path,
                     options_.runtime_reader_group,
                     false,
                     &error_message)) {
          return SecretProvisioningResult::failure(options_.root_dir,
                       std::move(error_message));
        }

  return SecretProvisioningResult::success(make_auth_ref(request.provider_ref),
                                           options_.root_dir,
                                           secret_name,
                                           version);
}

}  // namespace dasall::infra::secret