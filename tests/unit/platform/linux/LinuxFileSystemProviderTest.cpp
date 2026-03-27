#include <exception>
#include <iostream>

#include "dasall/tests/support/TestAssertions.h"
#include "linux/LinuxFileSystemProvider.h"

namespace {

void test_linux_file_system_provider_reports_not_found_permission_denied_and_no_space() {
  using dasall::platform::FileBuffer;
  using dasall::platform::FileWriteOptions;
  using dasall::platform::PlatformErrorCode;
  using dasall::platform::linux::LinuxFileSystemProvider;
  using dasall::tests::support::assert_true;

  LinuxFileSystemProvider provider;

  const auto not_found = provider.read_file("/tmp/missing.txt", 10);
  assert_true(!not_found.ok(), "read_file should fail for missing path");
  assert_true(not_found.error->code == PlatformErrorCode::NotFound,
              "missing file should map to NotFound");

  FileWriteOptions write_options;
  const FileBuffer blocked_bytes{1U, 2U, 3U};
  const auto permission_denied =
      provider.write_atomic("/forbidden/secret.txt", blocked_bytes, write_options);
  assert_true(!permission_denied.ok(), "write_atomic should fail on blocked path");
  assert_true(permission_denied.error->code == PlatformErrorCode::PermissionDenied,
              "blocked path should map to PermissionDenied");

  const FileBuffer oversized_bytes(70000U, 0xAB);
  const auto no_space = provider.write_atomic("/tmp/huge.bin", oversized_bytes, write_options);
  assert_true(!no_space.ok(), "write_atomic should fail when bytes exceed provider budget");
  assert_true(no_space.error->code == PlatformErrorCode::NoSpace,
              "oversized write should map to NoSpace");
}

void test_linux_file_system_provider_supports_ensure_directory_write_and_stat() {
  using dasall::platform::FileBuffer;
  using dasall::platform::FileWriteOptions;
  using dasall::platform::linux::LinuxFileSystemProvider;
  using dasall::tests::support::assert_true;

  LinuxFileSystemProvider provider;
  const auto ensured = provider.ensure_directory("/tmp/work");
  assert_true(ensured.ok(), "ensure_directory should succeed on normal path");

  const FileBuffer payload{9U, 8U, 7U};
  FileWriteOptions options;
  const auto written = provider.write_atomic("/tmp/work/data.bin", payload, options);
  assert_true(written.ok(), "write_atomic should succeed for regular path");

  const auto stat_result = provider.stat("/tmp/work/data.bin");
  assert_true(stat_result.ok(), "stat should succeed for written file");
  assert_true(stat_result.value->exists, "written file should exist");
  assert_true(stat_result.value->is_regular_file, "written file should be regular file");
  assert_true(stat_result.value->size_bytes == payload.size(),
              "stat size should match written payload length");
}

}  // namespace

int main() {
  try {
    test_linux_file_system_provider_reports_not_found_permission_denied_and_no_space();
    test_linux_file_system_provider_supports_ensure_directory_write_and_stat();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}