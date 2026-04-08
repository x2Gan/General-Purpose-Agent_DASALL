#include <exception>
#include <iostream>

#include "dasall/tests/support/TestAssertions.h"
#include "linux/PosixDynamicLibraryLoader.h"

extern "C" int dasall_test_dynamic_loader_anchor() {
  return 42;
}

namespace {

void test_posix_dynamic_library_loader_opens_current_process_loads_symbol_and_closes_idempotently() {
  using dasall::platform::DynamicLibraryOpenOptions;
  using dasall::platform::linux::PosixDynamicLibraryLoader;
  using dasall::tests::support::assert_true;

  PosixDynamicLibraryLoader loader;
  DynamicLibraryOpenOptions options;
  options.allow_current_process = true;
  options.export_symbols_globally = true;

  const auto opened = loader.open_library(std::string(), options);
  assert_true(opened.ok(),
              "open_library should succeed for the current process when explicitly allowed");
  assert_true(opened.value->references_current_process,
              "current-process open should record a self handle");

  const auto symbol =
      loader.load_symbol(*opened.value, "dasall_test_dynamic_loader_anchor");
  assert_true(symbol.ok(),
              "load_symbol should resolve an exported symbol from the current process");

  using AnchorFunction = int (*)();
  const auto anchor = reinterpret_cast<AnchorFunction>(symbol.value->address);
  assert_true(anchor() == 42,
              "resolved symbol should remain callable through the returned address");

  const auto first_close = loader.close_library(*opened.value);
  assert_true(first_close.ok(), "close_library should succeed for an active handle");

  const auto second_close = loader.close_library(*opened.value);
  assert_true(second_close.ok(),
              "close_library should be idempotent for a previously closed handle");
}

void test_posix_dynamic_library_loader_reports_missing_library_and_symbol() {
  using dasall::platform::DynamicLibraryOpenOptions;
  using dasall::platform::PlatformErrorCode;
  using dasall::platform::linux::PosixDynamicLibraryLoader;
  using dasall::tests::support::assert_true;

  PosixDynamicLibraryLoader loader;

  const auto missing_library = loader.open_library("/tmp/does-not-exist-dasall.so", {});
  assert_true(!missing_library.ok(),
              "open_library should fail for a library path that does not exist");
  assert_true(missing_library.error->code == PlatformErrorCode::NotFound,
              "missing library should map to NotFound");

  DynamicLibraryOpenOptions self_options;
  self_options.allow_current_process = true;
  const auto opened = loader.open_library(std::string(), self_options);
  assert_true(opened.ok(), "current-process open should succeed for symbol negative test");

  const auto missing_symbol =
      loader.load_symbol(*opened.value, "dasall_test_missing_dynamic_loader_symbol");
  assert_true(!missing_symbol.ok(), "load_symbol should fail for an unknown exported name");
  assert_true(missing_symbol.error->code == PlatformErrorCode::NotFound,
              "missing symbol should map to NotFound");
}

}  // namespace

int main() {
  try {
    test_posix_dynamic_library_loader_opens_current_process_loads_symbol_and_closes_idempotently();
    test_posix_dynamic_library_loader_reports_missing_library_and_symbol();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}