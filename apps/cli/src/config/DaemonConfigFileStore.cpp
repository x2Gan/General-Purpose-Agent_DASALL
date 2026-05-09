#include "config/DaemonConfigFileStore.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cctype>
#include <fstream>
#include <sstream>
#include <string_view>

namespace dasall::apps::cli::config {

namespace {

constexpr std::filesystem::perms kDefaultConfigFilePerms =
    std::filesystem::perms::owner_read |
    std::filesystem::perms::owner_write |
    std::filesystem::perms::group_read |
    std::filesystem::perms::others_read;

[[nodiscard]] std::string trim_copy(std::string_view text) {
  std::size_t begin = 0;
  while (begin < text.size() &&
         std::isspace(static_cast<unsigned char>(text[begin])) != 0) {
    ++begin;
  }

  std::size_t end = text.size();
  while (end > begin &&
         std::isspace(static_cast<unsigned char>(text[end - 1U])) != 0) {
    --end;
  }

  return std::string(text.substr(begin, end - begin));
}

[[nodiscard]] bool read_text_file(const std::filesystem::path& path,
                                  std::string* content,
                                  std::string* error_message) {
  std::ifstream stream(path);
  if (!stream.is_open()) {
    if (error_message != nullptr) {
      *error_message = "unable to open file: " + path.string();
    }
    return false;
  }

  std::ostringstream buffer;
  buffer << stream.rdbuf();
  *content = buffer.str();
  return true;
}

[[nodiscard]] bool write_text_file(const std::filesystem::path& path,
                                   std::string_view content,
                                   std::filesystem::perms permissions,
                                   std::string* error_message) {
  std::ofstream stream(path, std::ios::binary | std::ios::trunc);
  if (!stream.is_open()) {
    if (error_message != nullptr) {
      *error_message = "unable to write file: " + path.string();
    }
    return false;
  }

  stream.write(content.data(), static_cast<std::streamsize>(content.size()));
  stream.flush();
  if (!stream.good()) {
    if (error_message != nullptr) {
      *error_message = "unable to flush file: " + path.string();
    }
    return false;
  }

  std::error_code permissions_error;
  std::filesystem::permissions(path, permissions,
                               std::filesystem::perm_options::replace,
                               permissions_error);
  if (permissions_error) {
    if (error_message != nullptr) {
      *error_message = "unable to set permissions on file: " + path.string();
    }
    return false;
  }

  return true;
}

[[nodiscard]] bool fsync_path(const std::filesystem::path& path,
                              int open_flags,
                              std::string* error_message) {
  const int handle = ::open(path.c_str(), open_flags);
  if (handle < 0) {
    if (error_message != nullptr) {
      *error_message = "open failed during fsync: " + path.string();
    }
    return false;
  }

  const bool ok = ::fsync(handle) == 0;
  const int close_result = ::close(handle);
  (void)close_result;
  if (!ok && error_message != nullptr) {
    *error_message = "fsync failed for path: " + path.string();
  }
  return ok;
}

[[nodiscard]] bool fsync_file(const std::filesystem::path& path,
                              std::string* error_message) {
  return fsync_path(path, O_RDONLY, error_message);
}

[[nodiscard]] bool fsync_directory(const std::filesystem::path& path,
                                   std::string* error_message) {
  return fsync_path(path, O_RDONLY | O_DIRECTORY, error_message);
}

[[nodiscard]] bool validate_json_string(std::string_view input) {
  struct JsonParser {
    std::string_view text;
    std::size_t index = 0;

    void skip_ws() {
      while (index < text.size() &&
             std::isspace(static_cast<unsigned char>(text[index])) != 0) {
        ++index;
      }
    }

    bool parse_literal(std::string_view literal) {
      skip_ws();
      if (text.substr(index, literal.size()) != literal) {
        return false;
      }
      index += literal.size();
      return true;
    }

    bool parse_string() {
      skip_ws();
      if (index >= text.size() || text[index] != '"') {
        return false;
      }
      ++index;
      while (index < text.size()) {
        const char current = text[index++];
        if (current == '\\') {
          if (index >= text.size()) {
            return false;
          }
          ++index;
          continue;
        }
        if (current == '"') {
          return true;
        }
      }
      return false;
    }

    bool parse_number() {
      skip_ws();
      const std::size_t begin = index;
      if (index < text.size() && (text[index] == '-' || text[index] == '+')) {
        ++index;
      }
      bool digits_seen = false;
      while (index < text.size() &&
             std::isdigit(static_cast<unsigned char>(text[index])) != 0) {
        digits_seen = true;
        ++index;
      }
      if (index < text.size() && text[index] == '.') {
        ++index;
        while (index < text.size() &&
               std::isdigit(static_cast<unsigned char>(text[index])) != 0) {
          digits_seen = true;
          ++index;
        }
      }
      return digits_seen && index > begin;
    }

    bool parse_array() {
      skip_ws();
      if (index >= text.size() || text[index] != '[') {
        return false;
      }
      ++index;
      skip_ws();
      if (index < text.size() && text[index] == ']') {
        ++index;
        return true;
      }
      while (parse_value()) {
        skip_ws();
        if (index < text.size() && text[index] == ',') {
          ++index;
          continue;
        }
        if (index < text.size() && text[index] == ']') {
          ++index;
          return true;
        }
        return false;
      }
      return false;
    }

    bool parse_object() {
      skip_ws();
      if (index >= text.size() || text[index] != '{') {
        return false;
      }
      ++index;
      skip_ws();
      if (index < text.size() && text[index] == '}') {
        ++index;
        return true;
      }
      while (parse_string()) {
        skip_ws();
        if (index >= text.size() || text[index] != ':') {
          return false;
        }
        ++index;
        if (!parse_value()) {
          return false;
        }
        skip_ws();
        if (index < text.size() && text[index] == ',') {
          ++index;
          continue;
        }
        if (index < text.size() && text[index] == '}') {
          ++index;
          return true;
        }
        return false;
      }
      return false;
    }

    bool parse_value() {
      skip_ws();
      if (index >= text.size()) {
        return false;
      }
      const char current = text[index];
      if (current == '{') {
        return parse_object();
      }
      if (current == '[') {
        return parse_array();
      }
      if (current == '"') {
        return parse_string();
      }
      if (current == 't') {
        return parse_literal("true");
      }
      if (current == 'f') {
        return parse_literal("false");
      }
      if (current == 'n') {
        return parse_literal("null");
      }
      return parse_number();
    }
  };

  JsonParser parser{input};
  if (!parser.parse_value()) {
    return false;
  }
  parser.skip_ws();
  return parser.index == input.size();
}

[[nodiscard]] std::optional<std::string> parse_profile_id_from_defaults(
    const std::string& defaults_file_text) {
  std::istringstream stream(defaults_file_text);
  std::string line;
  while (std::getline(stream, line)) {
    const std::string trimmed = trim_copy(line);
    if (trimmed.empty() || trimmed.starts_with('#')) {
      continue;
    }

    const auto delimiter = trimmed.find('=');
    if (delimiter == std::string::npos) {
      continue;
    }

    const std::string key = trim_copy(trimmed.substr(0, delimiter));
    const std::string value = trim_copy(trimmed.substr(delimiter + 1U));
    if (key == kDaemonProfileSelectionKey && !value.empty()) {
      return value;
    }
  }

  return std::nullopt;
}

[[nodiscard]] std::string render_defaults_file(
    const DesiredConfigSnapshot& desired) {
  return std::string(
             "# v1 ownership: profile selection only; daemon.* stays in /etc/dasall/daemon.json\n"
             "# P0 operator path stays root/sudo-only; use sudo dasall config or edit the canonical files explicitly.\n") +
         std::string(kDaemonProfileSelectionKey) + "=" + desired.profile_id +
         "\n";
}

[[nodiscard]] std::string bool_to_json(const bool value) {
  return value ? "true" : "false";
}

[[nodiscard]] std::string render_daemon_json(
    const DesiredConfigSnapshot& desired) {
  return std::string("{\n") +
         "  \"daemon\": {\n" +
         "    \"socket_path\": \"" + desired.daemon.socket_path + "\",\n" +
         "    \"startup_mode\": \"direct_bind\",\n" +
         "    \"diag_enabled\": " + bool_to_json(desired.daemon.diag_enabled) + ",\n" +
         "    \"override_enabled\": " +
         bool_to_json(desired.daemon.override_enabled) + ",\n" +
         "    \"watchdog_enabled\": " +
         bool_to_json(desired.daemon.watchdog_enabled) + ",\n" +
         "    \"log_format\": \"" + desired.daemon.log_format + "\"\n" +
         "  }\n" +
         "}\n";
}

[[nodiscard]] std::filesystem::path backup_path_for(
    const std::filesystem::path& target_path) {
  return target_path.parent_path() /
         (target_path.filename().string() + ".last-known-good");
}

[[nodiscard]] std::filesystem::path temp_path_for(
    const std::filesystem::path& target_path) {
  return target_path.parent_path() /
         (target_path.filename().string() + ".tmp");
}

[[nodiscard]] std::filesystem::perms permissions_for_path(
    const std::filesystem::path& path) {
  if (std::filesystem::exists(path)) {
    std::error_code error;
    const auto status = std::filesystem::status(path, error);
    if (!error) {
      return status.permissions();
    }
  }

  return kDefaultConfigFilePerms;
}

[[nodiscard]] bool write_target_atomically(
    const std::filesystem::path& target_path,
    std::string_view content,
    std::filesystem::perms permissions,
    std::string* error_message) {
  std::error_code create_error;
  std::filesystem::create_directories(target_path.parent_path(), create_error);
  if (create_error) {
    if (error_message != nullptr) {
      *error_message = "unable to create parent directory: " +
                       target_path.parent_path().string();
    }
    return false;
  }

  const auto temp_path = temp_path_for(target_path);
  if (!write_text_file(temp_path, content, permissions, error_message)) {
    return false;
  }

  if (!fsync_file(temp_path, error_message)) {
    return false;
  }

  if (!fsync_directory(target_path.parent_path(), error_message)) {
    return false;
  }

  std::error_code rename_error;
  std::filesystem::rename(temp_path, target_path, rename_error);
  if (rename_error) {
    if (error_message != nullptr) {
      *error_message = "rename failed for file: " + target_path.string();
    }
    return false;
  }

  return fsync_directory(target_path.parent_path(), error_message);
}

void assign_error(std::string* error_message, std::string message) {
  if (error_message != nullptr) {
    *error_message = std::move(message);
  }
}

}  // namespace

DaemonConfigFileStore::DaemonConfigFileStore(DaemonConfigFileStorePaths paths)
    : paths_(std::move(paths)) {}

const DaemonConfigFileStorePaths& DaemonConfigFileStore::paths() const noexcept {
  return paths_;
}

std::optional<DaemonConfigStoreSnapshot> DaemonConfigFileStore::load_current(
    std::string* error_message) const {
  DaemonConfigStoreSnapshot snapshot;
  snapshot.defaults_file_exists = std::filesystem::exists(paths_.defaults_file);
  snapshot.daemon_config_file_exists =
      std::filesystem::exists(paths_.daemon_config_file);
  snapshot.daemon_config_valid = !snapshot.daemon_config_file_exists;

  if (snapshot.defaults_file_exists &&
      !read_text_file(paths_.defaults_file, &snapshot.defaults_file_text,
                      error_message)) {
    return std::nullopt;
  }
  snapshot.profile_id = parse_profile_id_from_defaults(snapshot.defaults_file_text);

  if (snapshot.daemon_config_file_exists) {
    if (!read_text_file(paths_.daemon_config_file, &snapshot.daemon_config_json,
                        error_message)) {
      return std::nullopt;
    }
    snapshot.daemon_config_valid =
        validate_json_string(snapshot.daemon_config_json);
  }

  return snapshot;
}

ConfigFileWriteResult DaemonConfigFileStore::write_desired(
    const DesiredConfigSnapshot& desired) const {
  ConfigFileWriteResult result;
  if (!desired.is_well_formed()) {
    result.error_message = "desired config snapshot is not well formed";
    return result;
  }

  std::string load_error;
  const auto current_snapshot = load_current(&load_error);
  if (!current_snapshot.has_value()) {
    result.error_message = std::move(load_error);
    return result;
  }

  const std::vector<std::pair<std::filesystem::path, std::string>> writes = {
      {paths_.defaults_file, render_defaults_file(desired)},
      {paths_.daemon_config_file, render_daemon_json(desired)},
  };

  for (const auto& [target_path, content] : writes) {
    ConfigFileWriteOperation operation;
    operation.target_path = target_path;
    operation.temp_path = temp_path_for(target_path);
    operation.backup_path = backup_path_for(target_path);
    operation.existed_before = std::filesystem::exists(target_path);
    operation.target_permissions = permissions_for_path(target_path);

    if (operation.existed_before) {
      std::error_code copy_error;
      std::filesystem::copy_file(target_path, operation.backup_path,
                                 std::filesystem::copy_options::overwrite_existing,
                                 copy_error);
      if (copy_error) {
        result.error_message = "unable to create backup for file: " +
                               target_path.string();
        return result;
      }
      std::error_code backup_permissions_error;
      std::filesystem::permissions(operation.backup_path,
                                   operation.target_permissions,
                                   std::filesystem::perm_options::replace,
                                   backup_permissions_error);
      if (backup_permissions_error) {
        result.error_message = "unable to preserve backup permissions for file: " +
                               target_path.string();
        return result;
      }
    }

    std::string write_error;
    if (!write_target_atomically(target_path, content, operation.target_permissions,
                                 &write_error)) {
      result.error_message = std::move(write_error);
      if (rollback_last_write(result.transaction, &write_error)) {
        result.rolled_back = true;
        result.transaction.apply_failed_rolled_back = true;
      } else if (!write_error.empty()) {
        result.error_message += "; rollback failed: " + write_error;
      }
      return result;
    }

    result.transaction.operations.push_back(std::move(operation));
  }

  result.success = true;
  return result;
}

bool DaemonConfigFileStore::rollback_last_write(
    const ConfigFileWriteTransaction& transaction,
    std::string* error_message) const {
  for (auto it = transaction.operations.rbegin();
       it != transaction.operations.rend(); ++it) {
    const auto& operation = *it;
    if (operation.existed_before) {
      std::string backup_content;
      if (!read_text_file(operation.backup_path, &backup_content, error_message)) {
        return false;
      }
      if (!write_target_atomically(operation.target_path, backup_content,
                                   operation.target_permissions,
                                   error_message)) {
        return false;
      }
    } else {
      std::error_code remove_error;
      std::filesystem::remove(operation.target_path, remove_error);
      if (remove_error) {
        assign_error(error_message,
                     "unable to remove newly created file during rollback: " +
                         operation.target_path.string());
        return false;
      }
      if (!fsync_directory(operation.target_path.parent_path(), error_message)) {
        return false;
      }
    }
  }

  return true;
}

}  // namespace dasall::apps::cli::config