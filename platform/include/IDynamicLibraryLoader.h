#pragma once

#include <cstdint>
#include <string>

#include "PlatformResult.h"

namespace dasall::platform {

struct DynamicLibraryOpenOptions {
  bool resolve_symbols_now = true;
  bool export_symbols_globally = false;
  bool allow_current_process = false;

  [[nodiscard]] bool has_consistent_values() const {
    return true;
  }
};

struct DynamicLibraryHandle {
  std::string handle_ref;
  std::string library_path;
  bool references_current_process = false;

  [[nodiscard]] bool has_consistent_values() const {
    return !handle_ref.empty();
  }
};

struct DynamicLibrarySymbol {
  std::string symbol_name;
  std::uintptr_t address = 0U;

  [[nodiscard]] bool has_consistent_values() const {
    return !symbol_name.empty() && address != 0U;
  }
};

class IDynamicLibraryLoader {
 public:
  virtual ~IDynamicLibraryLoader() = default;

  virtual PlatformResult<DynamicLibraryHandle> open_library(
      const std::string& library_path,
      const DynamicLibraryOpenOptions& options) = 0;
  virtual PlatformResult<DynamicLibrarySymbol> load_symbol(
      const DynamicLibraryHandle& handle,
      const std::string& symbol_name) = 0;
  virtual PlatformResult<bool> close_library(
      const DynamicLibraryHandle& handle) = 0;
};

}  // namespace dasall::platform