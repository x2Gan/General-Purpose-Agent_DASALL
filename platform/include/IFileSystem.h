#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "PlatformResult.h"

namespace dasall::platform {

enum class FileWriteMode {
  Overwrite,
  Append,
};

struct FileWriteOptions {
  FileWriteMode mode = FileWriteMode::Overwrite;
  bool sync_on_write = false;
  std::string tmp_suffix = ".tmp";

  [[nodiscard]] bool has_consistent_values() const {
    return !tmp_suffix.empty();
  }
};

struct FileStatResult {
  bool exists = false;
  bool is_regular_file = false;
  bool is_directory = false;
  std::uint64_t size_bytes = 0;
  std::int64_t last_modified_ms = 0;

  [[nodiscard]] bool has_consistent_values() const {
    if (!exists && (is_regular_file || is_directory)) {
      return false;
    }

    if (is_regular_file && is_directory) {
      return false;
    }

    if (!exists && size_bytes != 0U) {
      return false;
    }

    return true;
  }
};

using FileBuffer = std::vector<std::uint8_t>;

class IFileSystem {
 public:
  virtual ~IFileSystem() = default;

  virtual PlatformResult<FileBuffer> read_file(const std::string& path,
                                               std::int32_t deadline_ms) = 0;
  virtual PlatformResult<bool> write_atomic(const std::string& path,
                                            const FileBuffer& bytes,
                                            const FileWriteOptions& options) = 0;
  virtual PlatformResult<bool> ensure_directory(const std::string& path) = 0;
  virtual PlatformResult<FileStatResult> stat(const std::string& path) = 0;
};

}  // namespace dasall::platform
