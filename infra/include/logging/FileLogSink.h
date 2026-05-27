#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string_view>

#include "logging/ILogSink.h"

namespace dasall::infra::logging {

enum class FileLogPathPolicy {
  BuildTreeDefault = 0,
  InstalledAuthoritative = 1,
};

[[nodiscard]] inline constexpr std::string_view file_log_path_policy_name(
    FileLogPathPolicy policy) {
  switch (policy) {
    case FileLogPathPolicy::BuildTreeDefault:
      return "build_tree_default";
    case FileLogPathPolicy::InstalledAuthoritative:
      return "installed_authoritative";
  }

  return "unknown";
}

struct FileLogSinkOptions {
  std::filesystem::path file_path;
  std::filesystem::path state_root_override;
  std::size_t rotate_max_size_bytes = 50U * 1024U * 1024U;
  std::size_t rotate_max_files = 10U;
  FileLogPathPolicy path_policy = FileLogPathPolicy::BuildTreeDefault;

  [[nodiscard]] bool has_consistent_values() const {
    return rotate_max_size_bytes > 0U && rotate_max_files > 0U &&
           (state_root_override.empty() || state_root_override.is_absolute());
  }
};

class FileLogSink final : public ILogSink {
 public:
  explicit FileLogSink(FileLogSinkOptions options = {});

  LogWriteResult write(const LogEvent& event) override;
  LogWriteResult flush(const LogFlushDeadline& deadline) override;

  [[nodiscard]] const FileLogSinkOptions& options() const {
    return options_;
  }

  [[nodiscard]] const std::filesystem::path& resolved_file_path() const {
    return resolved_file_path_;
  }

  [[nodiscard]] std::uint64_t rotation_total() const {
    return rotation_total_;
  }

 private:
  [[nodiscard]] std::filesystem::path resolve_file_path() const;
  [[nodiscard]] LogWriteResult ensure_parent_directory(
      const std::filesystem::path& target_path) const;
  [[nodiscard]] LogWriteResult rotate_if_needed(std::size_t incoming_bytes);
  [[nodiscard]] bool may_auto_create_parent() const;

  FileLogSinkOptions options_;
  std::filesystem::path resolved_file_path_;
  std::uint64_t rotation_total_ = 0;
};

}  // namespace dasall::infra::logging