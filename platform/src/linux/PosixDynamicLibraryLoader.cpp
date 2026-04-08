#include "linux/PosixDynamicLibraryLoader.h"

#include <dlfcn.h>

#include <cerrno>
#include <cstring>
#include <string_view>
#include <utility>

namespace dasall::platform::linux {
namespace {

[[nodiscard]] bool contains_token(std::string_view text, std::string_view token) {
  return text.find(token) != std::string_view::npos;
}

[[nodiscard]] std::string normalized_library_path(const std::string& library_path,
                                                  bool references_current_process) {
  if (references_current_process) {
    return std::string("@self");
  }

  return library_path;
}

}  // namespace

PlatformResult<DynamicLibraryHandle> PosixDynamicLibraryLoader::open_library(
    const std::string& library_path,
    const DynamicLibraryOpenOptions& options) {
  if (!options.has_consistent_values() ||
      (library_path.empty() && !options.allow_current_process)) {
    return PlatformResult<DynamicLibraryHandle>::failure(
        make_error(PlatformErrorCode::InvalidArgument,
                   PlatformErrorCategory::Validation,
                   "dynamic library path is empty and current-process loading is disabled"));
  }

  dlerror();
  void* native_handle = nullptr;
  const bool references_current_process =
      library_path.empty() && options.allow_current_process;
  if (references_current_process) {
    native_handle = dlopen(nullptr, make_open_flags(options));
  } else {
    native_handle = dlopen(library_path.c_str(), make_open_flags(options));
  }

  if (native_handle == nullptr) {
    const char* error_text = dlerror();
    const std::string detail =
        (error_text == nullptr) ? std::string("dlopen failed") : std::string(error_text);
    return PlatformResult<DynamicLibraryHandle>::failure(
        make_error(map_open_error_code(detail),
                   PlatformErrorCategory::IO,
                   detail,
                   std::string("dlopen"),
                   errno));
  }

  std::lock_guard<std::mutex> lock(mutex_);
  const std::string handle_ref = std::string("dl://") +
                                 std::to_string(next_handle_id_++);
  libraries_[handle_ref] = NativeLibraryEntry{
      .native_handle = native_handle,
      .library_path = normalized_library_path(library_path,
                                              references_current_process),
      .references_current_process = references_current_process,
      .closed = false,
  };

  return PlatformResult<DynamicLibraryHandle>::success(DynamicLibraryHandle{
      .handle_ref = handle_ref,
      .library_path = libraries_[handle_ref].library_path,
      .references_current_process = references_current_process,
  });
}

PlatformResult<DynamicLibrarySymbol> PosixDynamicLibraryLoader::load_symbol(
    const DynamicLibraryHandle& handle,
    const std::string& symbol_name) {
  if (!handle.has_consistent_values() || symbol_name.empty()) {
    return PlatformResult<DynamicLibrarySymbol>::failure(
        make_error(PlatformErrorCode::InvalidArgument,
                   PlatformErrorCategory::Validation,
                   "dynamic library handle or symbol name is invalid"));
  }

  std::lock_guard<std::mutex> lock(mutex_);
  const auto library_it = libraries_.find(handle.handle_ref);
  if (library_it == libraries_.end() || library_it->second.closed ||
      library_it->second.native_handle == nullptr) {
    return PlatformResult<DynamicLibrarySymbol>::failure(
        make_error(PlatformErrorCode::NotFound,
                   PlatformErrorCategory::IO,
                   "dynamic library handle is not active"));
  }

  dlerror();
  void* symbol_address =
      dlsym(library_it->second.native_handle, symbol_name.c_str());
  const char* error_text = dlerror();
  if (error_text != nullptr || symbol_address == nullptr) {
    const std::string detail = (error_text == nullptr)
                                   ? std::string("dlsym failed")
                                   : std::string(error_text);
    return PlatformResult<DynamicLibrarySymbol>::failure(
        make_error(map_symbol_error_code(detail),
                   PlatformErrorCategory::IO,
                   detail,
                   std::string("dlsym"),
                   errno));
  }

  return PlatformResult<DynamicLibrarySymbol>::success(DynamicLibrarySymbol{
      .symbol_name = symbol_name,
      .address = reinterpret_cast<std::uintptr_t>(symbol_address),
  });
}

PlatformResult<bool> PosixDynamicLibraryLoader::close_library(
    const DynamicLibraryHandle& handle) {
  if (!handle.has_consistent_values()) {
    return PlatformResult<bool>::failure(
        make_error(PlatformErrorCode::InvalidArgument,
                   PlatformErrorCategory::Validation,
                   "dynamic library handle is invalid"));
  }

  std::lock_guard<std::mutex> lock(mutex_);
  const auto library_it = libraries_.find(handle.handle_ref);
  if (library_it == libraries_.end()) {
    return PlatformResult<bool>::failure(
        make_error(PlatformErrorCode::NotFound,
                   PlatformErrorCategory::IO,
                   "dynamic library handle was not opened by this loader"));
  }

  if (library_it->second.closed) {
    return PlatformResult<bool>::success(true);
  }

  if (library_it->second.native_handle != nullptr &&
      dlclose(library_it->second.native_handle) != 0) {
    const char* error_text = dlerror();
    const std::string detail = (error_text == nullptr)
                                   ? std::string("dlclose failed")
                                   : std::string(error_text);
    return PlatformResult<bool>::failure(
        make_error(PlatformErrorCode::InternalFailure,
                   PlatformErrorCategory::Internal,
                   detail,
                   std::string("dlclose"),
                   errno));
  }

  library_it->second.native_handle = nullptr;
  library_it->second.closed = true;
  return PlatformResult<bool>::success(true);
}

PlatformError PosixDynamicLibraryLoader::make_error(
    PlatformErrorCode code,
    PlatformErrorCategory category,
    std::string detail,
    std::string syscall_name,
    std::optional<int> errno_value) const {
  return PlatformError{
      .code = code,
      .category = category,
      .retryable_hint = false,
      .syscall_name = std::move(syscall_name),
      .errno_value = errno_value,
      .detail = std::move(detail),
  };
}

int PosixDynamicLibraryLoader::make_open_flags(
    const DynamicLibraryOpenOptions& options) const {
  int flags = options.resolve_symbols_now ? RTLD_NOW : RTLD_LAZY;
  flags |= options.export_symbols_globally ? RTLD_GLOBAL : RTLD_LOCAL;
  return flags;
}

PlatformErrorCode PosixDynamicLibraryLoader::map_open_error_code(
    const std::string& detail) const {
  if (contains_token(detail, "No such file") ||
      contains_token(detail, "cannot open shared object file")) {
    return PlatformErrorCode::NotFound;
  }

  if (contains_token(detail, "Permission denied")) {
    return PlatformErrorCode::PermissionDenied;
  }

  return PlatformErrorCode::InternalFailure;
}

PlatformErrorCode PosixDynamicLibraryLoader::map_symbol_error_code(
    const std::string& detail) const {
  if (contains_token(detail, "undefined symbol")) {
    return PlatformErrorCode::NotFound;
  }

  return PlatformErrorCode::InternalFailure;
}

}  // namespace dasall::platform::linux