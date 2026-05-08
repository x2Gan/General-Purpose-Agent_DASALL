#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "config/ConfigCommandTypes.h"

namespace dasall::apps::cli::config {

inline constexpr char kDaemonProfileSelectionKey[] =
    "DASALL_DAEMON_PROFILE_ID";

struct DaemonConfigFileStorePaths {
  std::filesystem::path defaults_file = "/etc/default/dasall-daemon";
  std::filesystem::path daemon_config_file = "/etc/dasall/daemon.json";
};

struct DaemonConfigStoreSnapshot {
  bool defaults_file_exists = false;
  bool daemon_config_file_exists = false;
  std::optional<std::string> profile_id;
  std::string defaults_file_text;
  std::string daemon_config_json;
};

struct ConfigFileWriteOperation {
  std::filesystem::path target_path;
  std::filesystem::path temp_path;
  std::filesystem::path backup_path;
  bool existed_before = false;
  std::filesystem::perms target_permissions =
      std::filesystem::perms::owner_read |
      std::filesystem::perms::owner_write |
      std::filesystem::perms::group_read |
      std::filesystem::perms::others_read;
};

struct ConfigFileWriteTransaction {
  std::vector<ConfigFileWriteOperation> operations;
  bool apply_failed_rolled_back = false;
};

struct ConfigFileWriteResult {
  bool success = false;
  bool rolled_back = false;
  std::string error_message;
  ConfigFileWriteTransaction transaction;
};

class DaemonConfigFileStore {
 public:
  explicit DaemonConfigFileStore(
      DaemonConfigFileStorePaths paths = DaemonConfigFileStorePaths{});

  [[nodiscard]] const DaemonConfigFileStorePaths& paths() const noexcept;

  [[nodiscard]] std::optional<DaemonConfigStoreSnapshot> load_current(
      std::string* error_message = nullptr) const;

  [[nodiscard]] ConfigFileWriteResult write_desired(
      const DesiredConfigSnapshot& desired) const;

  [[nodiscard]] bool rollback_last_write(
      const ConfigFileWriteTransaction& transaction,
      std::string* error_message = nullptr) const;

 private:
  DaemonConfigFileStorePaths paths_;
};

}  // namespace dasall::apps::cli::config