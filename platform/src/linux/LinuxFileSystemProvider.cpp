#include "linux/LinuxFileSystemProvider.h"

#include <optional>
#include <utility>

namespace dasall::platform::linux {

PlatformResult<FileBuffer> LinuxFileSystemProvider::read_file(const std::string& path,
                                                              std::int32_t deadline_ms) {
  if (path.empty() || deadline_ms < 0) {
    return PlatformResult<FileBuffer>::failure(
        make_error(PlatformErrorCode::InvalidArgument,
                   PlatformErrorCategory::Validation,
                   "file path or deadline is invalid"));
  }

  if (is_permission_denied_path(path)) {
    return PlatformResult<FileBuffer>::failure(
        make_error(PlatformErrorCode::PermissionDenied,
                   PlatformErrorCategory::IO,
                   "path is blocked by permission policy"));
  }

  std::lock_guard<std::mutex> lock(mutex_);
  const auto it = files_.find(path);
  if (it == files_.end()) {
    return PlatformResult<FileBuffer>::failure(
        make_error(PlatformErrorCode::NotFound,
                   PlatformErrorCategory::IO,
                   "file does not exist"));
  }

  return PlatformResult<FileBuffer>::success(it->second.bytes);
}

PlatformResult<bool> LinuxFileSystemProvider::write_atomic(const std::string& path,
                                                           const FileBuffer& bytes,
                                                           const FileWriteOptions& options) {
  if (path.empty() || !options.has_consistent_values()) {
    return PlatformResult<bool>::failure(
        make_error(PlatformErrorCode::InvalidArgument,
                   PlatformErrorCategory::Validation,
                   "write path or options are invalid"));
  }

  if (is_permission_denied_path(path)) {
    return PlatformResult<bool>::failure(
        make_error(PlatformErrorCode::PermissionDenied,
                   PlatformErrorCategory::IO,
                   "path is blocked by permission policy"));
  }

  if (bytes.size() > kMaxWritableBytes) {
    return PlatformResult<bool>::failure(
        make_error(PlatformErrorCode::NoSpace,
                   PlatformErrorCategory::IO,
                   "write exceeds skeleton disk budget"));
  }

  std::lock_guard<std::mutex> lock(mutex_);
  auto& file_entry = files_[path];
  if (options.mode == FileWriteMode::Append) {
    file_entry.bytes.insert(file_entry.bytes.end(), bytes.begin(), bytes.end());
  } else {
    file_entry.bytes = bytes;
  }

  file_entry.last_modified_ms = next_timestamp_ms();
  return PlatformResult<bool>::success(true);
}

PlatformResult<bool> LinuxFileSystemProvider::ensure_directory(const std::string& path) {
  if (path.empty()) {
    return PlatformResult<bool>::failure(
        make_error(PlatformErrorCode::InvalidArgument,
                   PlatformErrorCategory::Validation,
                   "directory path is invalid"));
  }

  if (is_permission_denied_path(path)) {
    return PlatformResult<bool>::failure(
        make_error(PlatformErrorCode::PermissionDenied,
                   PlatformErrorCategory::IO,
                   "directory path is blocked by permission policy"));
  }

  std::lock_guard<std::mutex> lock(mutex_);
  directories_.insert(path);
  return PlatformResult<bool>::success(true);
}

PlatformResult<FileStatResult> LinuxFileSystemProvider::stat(const std::string& path) {
  if (path.empty()) {
    return PlatformResult<FileStatResult>::failure(
        make_error(PlatformErrorCode::InvalidArgument,
                   PlatformErrorCategory::Validation,
                   "stat path is invalid"));
  }

  if (is_permission_denied_path(path)) {
    return PlatformResult<FileStatResult>::failure(
        make_error(PlatformErrorCode::PermissionDenied,
                   PlatformErrorCategory::IO,
                   "stat path is blocked by permission policy"));
  }

  std::lock_guard<std::mutex> lock(mutex_);
  const auto file_it = files_.find(path);
  if (file_it != files_.end()) {
    return PlatformResult<FileStatResult>::success(FileStatResult{
        .exists = true,
        .is_regular_file = true,
        .is_directory = false,
        .size_bytes = static_cast<std::uint64_t>(file_it->second.bytes.size()),
        .last_modified_ms = file_it->second.last_modified_ms,
    });
  }

  const bool is_directory = (directories_.find(path) != directories_.end());
  return PlatformResult<FileStatResult>::success(FileStatResult{
      .exists = is_directory,
      .is_regular_file = false,
      .is_directory = is_directory,
      .size_bytes = 0,
      .last_modified_ms = 0,
  });
}

PlatformError LinuxFileSystemProvider::make_error(PlatformErrorCode code,
                                                  PlatformErrorCategory category,
                                                  std::string detail) const {
  return PlatformError{
      .code = code,
      .category = category,
      .retryable_hint = (code == PlatformErrorCode::Timeout),
      .syscall_name = {},
      .errno_value = std::nullopt,
      .detail = std::move(detail),
  };
}

bool LinuxFileSystemProvider::is_permission_denied_path(const std::string& path) const {
  return path.find("/forbidden") != std::string::npos;
}

std::int64_t LinuxFileSystemProvider::next_timestamp_ms() {
  return ++timestamp_ms_;
}

}  // namespace dasall::platform::linux