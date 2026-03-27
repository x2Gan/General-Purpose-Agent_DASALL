#pragma once

#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include "IFileSystem.h"

namespace dasall::platform::linux {

class LinuxFileSystemProvider final : public IFileSystem {
 public:
  LinuxFileSystemProvider() = default;

  PlatformResult<FileBuffer> read_file(const std::string& path,
                                       std::int32_t deadline_ms) override;
  PlatformResult<bool> write_atomic(const std::string& path,
                                    const FileBuffer& bytes,
                                    const FileWriteOptions& options) override;
  PlatformResult<bool> ensure_directory(const std::string& path) override;
  PlatformResult<FileStatResult> stat(const std::string& path) override;

 private:
  struct FileEntry {
    FileBuffer bytes;
    std::int64_t last_modified_ms = 0;
  };

  [[nodiscard]] PlatformError make_error(PlatformErrorCode code,
                                         PlatformErrorCategory category,
                                         std::string detail) const;
  [[nodiscard]] bool is_permission_denied_path(const std::string& path) const;
  [[nodiscard]] std::int64_t next_timestamp_ms();

  static constexpr std::size_t kMaxWritableBytes = 65536U;

  mutable std::mutex mutex_;
  std::unordered_map<std::string, FileEntry> files_;
  std::unordered_set<std::string> directories_;
  std::int64_t timestamp_ms_ = 1000;
};

}  // namespace dasall::platform::linux