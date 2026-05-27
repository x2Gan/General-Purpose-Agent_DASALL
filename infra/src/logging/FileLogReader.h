#pragma once

#include <filesystem>

#include "LogQueryService.h"

namespace dasall::infra::logging {

struct FileLogReaderOptions {
  std::filesystem::path runtime_log_path = std::filesystem::path("logs") /
                                           "runtime.log";
  bool include_rotation_family = true;

  [[nodiscard]] bool has_consistent_values() const {
    return !runtime_log_path.empty();
  }
};

class FileLogReader final : public ILogQueryRecordReader {
 public:
  explicit FileLogReader(FileLogReaderOptions options = {});

  [[nodiscard]] std::vector<LogEvent> read_window(std::int64_t start_ts_ms,
                                                  std::int64_t end_ts_ms) override;

  [[nodiscard]] const FileLogReaderOptions& options() const {
    return options_;
  }

 private:
  [[nodiscard]] std::vector<std::filesystem::path> collect_candidate_paths() const;
  [[nodiscard]] std::filesystem::path resolve_runtime_log_path() const;

  FileLogReaderOptions options_{};
};

}  // namespace dasall::infra::logging