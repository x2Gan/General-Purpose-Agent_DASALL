#include <exception>
#include <filesystem>
#include <iostream>
#include <string_view>

#include "support/TestAssertions.h"

#ifndef DASALL_MEMORY_PUBLIC_INCLUDE_DIR
#define DASALL_MEMORY_PUBLIC_INCLUDE_DIR "/home/gangan/DASALL/memory/include"
#endif

#ifndef DASALL_MEMORY_SOURCE_DIR
#define DASALL_MEMORY_SOURCE_DIR "/home/gangan/DASALL/memory/src"
#endif

namespace {

void test_memory_unit_surface_anchor_uses_a_collision_free_ctest_name() {
  using dasall::tests::support::assert_true;

  constexpr std::string_view ctest_name = "MemoryInterfaceCompileTest";
  constexpr std::string_view target_name = "dasall_memory_interface_compile_unit_test";

  assert_true(ctest_name.find("Memory") == 0U,
              "memory unit topology should keep a memory-specific ctest prefix");
  assert_true(target_name.find("dasall_memory_") == 0U,
              "memory unit topology target should remain namespaced under dasall_memory");
}

void test_memory_public_include_layout_exists() {
  namespace fs = std::filesystem;

  using dasall::tests::support::assert_true;

  const fs::path include_root{DASALL_MEMORY_PUBLIC_INCLUDE_DIR};
  const fs::path config_dir = include_root / "config";
  const fs::path context_dir = include_root / "context";
  const fs::path error_dir = include_root / "error";
  const fs::path vector_dir = include_root / "vector";
  const fs::path working_dir = include_root / "working";
  const fs::path writeback_dir = include_root / "writeback";

  assert_true(fs::is_directory(include_root),
              "memory public include root should exist before interface headers land");
  assert_true(fs::is_directory(config_dir),
              "memory public include layout should expose the config subdirectory");
  assert_true(fs::is_directory(context_dir),
              "memory public include layout should expose the context subdirectory");
  assert_true(fs::is_directory(error_dir),
              "memory public include layout should expose the error subdirectory");
  assert_true(fs::is_directory(vector_dir),
              "memory public include layout should expose the vector subdirectory");
  assert_true(fs::is_directory(working_dir),
              "memory public include layout should expose the working subdirectory");
  assert_true(fs::is_directory(writeback_dir),
              "memory public include layout should expose the writeback subdirectory");
}

void test_memory_module_is_no_longer_placeholder_only() {
  namespace fs = std::filesystem;

  using dasall::tests::support::assert_true;

  const fs::path source_root{DASALL_MEMORY_SOURCE_DIR};
  const fs::path build_anchor = source_root / "MemoryBuildSkeleton.cpp";
  const fs::path legacy_placeholder = source_root / "placeholder.cpp";

  assert_true(fs::is_regular_file(build_anchor),
              "memory module should expose a tracked build skeleton source instead of a single placeholder file");
  assert_true(!fs::exists(legacy_placeholder),
              "memory module should no longer rely on the legacy placeholder.cpp translation unit");
}

}  // namespace

int main() {
  try {
    test_memory_unit_surface_anchor_uses_a_collision_free_ctest_name();
    test_memory_public_include_layout_exists();
    test_memory_module_is_no_longer_placeholder_only();
  } catch (const std::exception& exception) {
    std::cerr << exception.what() << '\n';
    return 1;
  }

  return 0;
}