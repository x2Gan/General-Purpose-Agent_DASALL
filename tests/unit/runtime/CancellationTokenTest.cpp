#include <atomic>
#include <chrono>
#include <exception>
#include <iostream>
#include <thread>

#include "CancellationToken.h"
#include "support/TestAssertions.h"

int main() {
  using dasall::runtime::CancellationToken;
  using dasall::tests::support::assert_true;

  try {
    CancellationToken token;
    assert_true(!token.is_cancelled(), "new token should not start cancelled");

    token.bind_deadline(std::chrono::system_clock::now() + std::chrono::minutes(5));
    assert_true(token.has_deadline(), "bound deadline should be visible on token");
    assert_true(!token.is_cancelled(), "future deadline must not cancel token immediately");

    CancellationToken token_copy = token;
    token_copy.cancel();
    assert_true(token.is_cancelled(), "manual cancellation should propagate across copies");

    CancellationToken expired_token;
    expired_token.bind_deadline(std::chrono::system_clock::now() - std::chrono::milliseconds(1));
    assert_true(expired_token.is_cancelled(),
                "expired deadline should transition token into cancelled state");

    CancellationToken cross_thread_token;
    std::atomic<bool> observed_cancel{false};
    std::thread worker([cross_thread_token, &observed_cancel]() mutable {
      for (int attempt = 0; attempt < 100000; ++attempt) {
        if (cross_thread_token.is_cancelled()) {
          observed_cancel.store(true, std::memory_order_release);
          return;
        }
        std::this_thread::yield();
      }
    });

    cross_thread_token.cancel();
    worker.join();
    assert_true(observed_cancel.load(std::memory_order_acquire),
                "cancellation should be visible across worker threads");
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}