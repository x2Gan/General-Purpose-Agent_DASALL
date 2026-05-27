#include "logging/FileLogSink.h"

#include <fstream>
#include <string>
#include <system_error>

#include "config/InstallLayout.h"
#include "logging/LoggingErrors.h"

namespace dasall::infra::logging {

namespace {

namespace fs = std::filesystem;

constexpr std::string_view kFileLogSinkSourceRef = "FileLogSink";

[[nodiscard]] fs::path default_build_tree_log_path() {
  std::error_code error;
  const fs::path cwd = fs::current_path(error);
  if (error) {
    return fs::path("logs") / "runtime.log";
  }

  return cwd / "logs" / "runtime.log";
}

[[nodiscard]] bool is_default_relative_runtime_log_path(const fs::path& path) {
  return path.lexically_normal() == (fs::path("logs") / "runtime.log");
}

[[nodiscard]] LogWriteResult make_logging_failure(LoggingErrorCode code,
                                                  std::string message,
                                                  std::string stage) {
  const auto mapping = map_logging_error_code(code);
  return LogWriteResult::failure(mapping.result_code,
                                 std::move(message),
                                 std::move(stage),
                                 std::string(kFileLogSinkSourceRef));
}

[[nodiscard]] fs::path resolve_state_root(
    const FileLogSinkOptions& options) {
  if (!options.state_root_override.empty()) {
    return options.state_root_override;
  }

  return config::resolve_install_layout().state_root;
}

}  // namespace

FileLogSink::FileLogSink(FileLogSinkOptions options)
    : options_(std::move(options)), resolved_file_path_(resolve_file_path()) {}

LogWriteResult FileLogSink::write(const LogEvent& event) {
  if (!options_.has_consistent_values()) {
    return make_logging_failure(
        LoggingErrorCode::ConfigInvalid,
        "file log sink requires a positive rotate size, a positive rotate file count, and an absolute optional state_root override",
        "logging.sink.config");
  }

  if (!event.attrs_are_serializable()) {
    return LogWriteResult::failure(
        contracts::ResultCode::ValidationFieldMissing,
        "file log sink requires serializable log attrs",
        "logging.sink.validation",
        std::string(kFileLogSinkSourceRef));
  }

  resolved_file_path_ = resolve_file_path();

  const auto ensure_result = ensure_parent_directory(resolved_file_path_);
  if (!ensure_result.ok) {
    return ensure_result;
  }

  const std::string payload = event.message + '\n';
  const auto rotation_result = rotate_if_needed(payload.size());
  if (!rotation_result.ok) {
    return rotation_result;
  }

  std::ofstream stream(resolved_file_path_, std::ios::binary | std::ios::app);
  if (!stream.is_open()) {
    return make_logging_failure(
        LoggingErrorCode::SinkIo,
        "file log sink could not open target path " +
            resolved_file_path_.string(),
        "logging.sink.io");
  }

  stream.write(payload.data(), static_cast<std::streamsize>(payload.size()));
  stream.flush();
  if (!stream.good()) {
    return make_logging_failure(
        LoggingErrorCode::SinkIo,
        "file log sink failed while writing target path " +
            resolved_file_path_.string(),
        "logging.sink.io");
  }

  return LogWriteResult::success();
}

LogWriteResult FileLogSink::flush(const LogFlushDeadline& deadline) {
  if (!deadline.is_valid()) {
    return LogWriteResult::failure(
        contracts::ResultCode::ValidationFieldMissing,
        "file log sink flush deadline must be greater than zero",
        "logging.sink.flush",
        std::string(kFileLogSinkSourceRef));
  }

  return LogWriteResult::success();
}

std::filesystem::path FileLogSink::resolve_file_path() const {
  if (options_.file_path.is_absolute()) {
    return options_.file_path;
  }

  if (options_.path_policy == FileLogPathPolicy::InstalledAuthoritative) {
    const auto state_root = resolve_state_root(options_);
    if (options_.file_path.empty() ||
        is_default_relative_runtime_log_path(options_.file_path)) {
      return state_root / "logging" / "runtime.log";
    }

    return state_root / options_.file_path;
  }

  if (options_.file_path.empty()) {
    return default_build_tree_log_path();
  }

  std::error_code error;
  const auto cwd = std::filesystem::current_path(error);
  if (error) {
    return options_.file_path;
  }

  return cwd / options_.file_path;
}

LogWriteResult FileLogSink::ensure_parent_directory(
    const std::filesystem::path& target_path) const {
  const auto parent_path = target_path.parent_path();
  if (parent_path.empty()) {
    return LogWriteResult::success();
  }

  std::error_code error;
  if (fs::exists(parent_path, error)) {
    if (error) {
      return make_logging_failure(
          LoggingErrorCode::SinkIo,
          "file log sink failed while checking parent path " +
              parent_path.string(),
          "logging.sink.parent");
    }

    if (!fs::is_directory(parent_path, error) || error) {
      return make_logging_failure(
          LoggingErrorCode::SinkIo,
          "file log sink parent path is not a directory: " +
              parent_path.string(),
          "logging.sink.parent");
    }

    return LogWriteResult::success();
  }

  if (!may_auto_create_parent()) {
    return make_logging_failure(
        LoggingErrorCode::SinkIo,
        "file log sink parent path is missing for explicit target " +
            parent_path.string(),
        "logging.sink.parent");
  }

  fs::create_directories(parent_path, error);
  if (error) {
    return make_logging_failure(
        LoggingErrorCode::SinkIo,
        "file log sink could not create parent path " +
            parent_path.string(),
        "logging.sink.parent");
  }

  return LogWriteResult::success();
}

LogWriteResult FileLogSink::rotate_if_needed(std::size_t incoming_bytes) {
  std::error_code error;
  if (!fs::exists(resolved_file_path_, error)) {
    if (error) {
      return make_logging_failure(
          LoggingErrorCode::SinkIo,
          "file log sink could not stat target path " +
              resolved_file_path_.string(),
          "logging.sink.rotate");
    }

    return LogWriteResult::success();
  }

  const auto current_size = fs::file_size(resolved_file_path_, error);
  if (error) {
    return make_logging_failure(
        LoggingErrorCode::SinkIo,
        "file log sink could not read file size for " +
            resolved_file_path_.string(),
        "logging.sink.rotate");
  }

  if (current_size + incoming_bytes <= options_.rotate_max_size_bytes) {
    return LogWriteResult::success();
  }

  const auto make_rotation_path = [this](std::size_t index) {
    return fs::path(resolved_file_path_.string() + "." + std::to_string(index));
  };

  const auto oldest_rotation_path = make_rotation_path(options_.rotate_max_files);
  if (fs::exists(oldest_rotation_path, error)) {
    fs::remove(oldest_rotation_path, error);
    if (error) {
      return make_logging_failure(
          LoggingErrorCode::SinkIo,
          "file log sink could not remove oldest rotation path " +
              oldest_rotation_path.string(),
          "logging.sink.rotate");
    }
  }

  for (std::size_t index = options_.rotate_max_files; index > 1U; --index) {
    const auto from_path = make_rotation_path(index - 1U);
    const auto to_path = make_rotation_path(index);

    error.clear();
    if (!fs::exists(from_path, error)) {
      if (error) {
        return make_logging_failure(
            LoggingErrorCode::SinkIo,
            "file log sink could not stat rotation path " +
                from_path.string(),
            "logging.sink.rotate");
      }

      continue;
    }

    fs::rename(from_path, to_path, error);
    if (error) {
      return make_logging_failure(
          LoggingErrorCode::SinkIo,
          "file log sink could not rename rotation path " +
              from_path.string() + " -> " + to_path.string(),
          "logging.sink.rotate");
    }
  }

  const auto first_rotation_path = make_rotation_path(1U);
  fs::rename(resolved_file_path_, first_rotation_path, error);
  if (error) {
    return make_logging_failure(
        LoggingErrorCode::SinkIo,
        "file log sink could not rotate active log path " +
            resolved_file_path_.string(),
        "logging.sink.rotate");
  }

  ++rotation_total_;
  return LogWriteResult::success();
}

bool FileLogSink::may_auto_create_parent() const {
  if (options_.file_path.is_absolute()) {
    return false;
  }

  return options_.file_path.empty() ||
         is_default_relative_runtime_log_path(options_.file_path);
}

}  // namespace dasall::infra::logging