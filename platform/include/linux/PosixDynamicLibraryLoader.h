#pragma once

#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>

#include "IDynamicLibraryLoader.h"

namespace dasall::platform::linux {

class PosixDynamicLibraryLoader final : public IDynamicLibraryLoader {
 public:
  PosixDynamicLibraryLoader() = default;

  PlatformResult<DynamicLibraryHandle> open_library(
      const std::string& library_path,
      const DynamicLibraryOpenOptions& options) override;
  PlatformResult<DynamicLibrarySymbol> load_symbol(
      const DynamicLibraryHandle& handle,
      const std::string& symbol_name) override;
  PlatformResult<bool> close_library(const DynamicLibraryHandle& handle) override;

 private:
  struct NativeLibraryEntry {
    void* native_handle = nullptr;
    std::string library_path;
    bool references_current_process = false;
    bool closed = false;
  };

  [[nodiscard]] PlatformError make_error(
      PlatformErrorCode code,
      PlatformErrorCategory category,
      std::string detail,
      std::string syscall_name = {},
      std::optional<int> errno_value = std::nullopt) const;
  [[nodiscard]] int make_open_flags(const DynamicLibraryOpenOptions& options) const;
  [[nodiscard]] PlatformErrorCode map_open_error_code(
      const std::string& detail) const;
  [[nodiscard]] PlatformErrorCode map_symbol_error_code(
      const std::string& detail) const;

  mutable std::mutex mutex_;
  std::unordered_map<std::string, NativeLibraryEntry> libraries_;
  std::size_t next_handle_id_ = 1U;
};

}  // namespace dasall::platform::linux