#include <cstddef>
#include <exception>
#include <iostream>
#include <type_traits>

#include "secret/SecureBuffer.h"
#include "support/TestAssertions.h"

namespace {

void test_secure_buffer_zeroizes_and_blocks_access_after_release() {
  using dasall::infra::secret::SecureBuffer;
  using dasall::tests::support::assert_true;

  static_assert(!std::is_copy_constructible_v<SecureBuffer>);
  static_assert(!std::is_copy_assignable_v<SecureBuffer>);
  static_assert(std::is_move_constructible_v<SecureBuffer>);

  auto buffer = SecureBuffer::from_text_copy("secret-token", true);
  const auto before_release = buffer.bytes();

  assert_true(buffer.is_accessible() && buffer.should_zeroize_on_release() &&
                  buffer.raw_size() == 12 && before_release.size() == 12 && !buffer.is_zeroized(),
              "SecureBuffer should expose plaintext bytes only before release and should keep zeroize_on_release enabled by default");

  buffer.release();

  assert_true(!buffer.is_accessible() && buffer.size() == 0 && buffer.raw_size() == 12 &&
                  buffer.bytes().empty() && buffer.is_zeroized(),
              "SecureBuffer should zeroize its stored bytes and block further access after release");
}

void test_secure_buffer_can_model_non_zeroizing_release_without_restoring_access() {
  using dasall::infra::secret::SecureBuffer;
  using dasall::tests::support::assert_true;

  auto buffer = SecureBuffer::from_text_copy("lease-token", false);
  buffer.release();

  assert_true(!buffer.is_accessible() && !buffer.should_zeroize_on_release() &&
                  buffer.bytes().empty() && !buffer.is_zeroized(),
              "SecureBuffer should still block post-release access even when zeroize_on_release is intentionally disabled");
}

}  // namespace

int main() {
  try {
    test_secure_buffer_zeroizes_and_blocks_access_after_release();
    test_secure_buffer_can_model_non_zeroizing_release_without_restoring_access();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}