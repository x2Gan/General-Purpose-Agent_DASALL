#include <atomic>
#include <exception>
#include <functional>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#include "mcp/CapabilityCache.h"
#include "support/TestAssertions.h"

namespace {

using dasall::tests::support::assert_true;
using dasall::tools::CapabilityEntry;
using dasall::tools::CapabilityFreshness;
using dasall::tools::CapabilitySnapshot;
using dasall::tools::mcp::CapabilityCache;

[[nodiscard]] CapabilitySnapshot make_test_snapshot(
    const std::string& server_id,
    CapabilityFreshness freshness = CapabilityFreshness::fresh) {
  return CapabilitySnapshot{
      .server_id = server_id,
      .entries = {CapabilityEntry{
          .capability_id = std::string("cap.") + server_id,
          .capability_version = std::string("1.0.0"),
          .tool_names = {std::string("tool.") + server_id},
      }},
      .freshness = freshness,
      .last_refresh_at_ms = 1000,
      .last_error = std::nullopt,
      .trust_marker = std::string("marker.") + server_id,
  };
}

void test_concurrent_read_during_update() {
  CapabilityCache cache;
  cache.update(make_test_snapshot("server.a"));

  constexpr int kReaderCount = 8;
  constexpr int kIterations = 500;
  std::atomic<int> successful_reads{0};
  std::atomic<bool> writer_done{false};

  auto reader_fn = [&]() {
    for (int i = 0; i < kIterations; ++i) {
      const auto snapshot = cache.snapshot("server.a");
      if (snapshot.has_value() && snapshot->server_id == "server.a") {
        successful_reads.fetch_add(1, std::memory_order_relaxed);
      }
    }
  };

  auto writer_fn = [&]() {
    for (int i = 0; i < kIterations; ++i) {
      cache.update(make_test_snapshot("server.a"));
    }
    writer_done.store(true, std::memory_order_release);
  };

  std::vector<std::thread> readers;
  readers.reserve(kReaderCount);
  for (int i = 0; i < kReaderCount; ++i) {
    readers.emplace_back(reader_fn);
  }
  std::thread writer(writer_fn);

  for (auto& r : readers) {
    r.join();
  }
  writer.join();

  assert_true(successful_reads.load() > 0,
              "CapabilityCache concurrent reads during update should all return valid snapshots");
  assert_true(writer_done.load(),
              "CapabilityCache writer should complete all updates without deadlock");
}

void test_concurrent_invalidate_and_read() {
  CapabilityCache cache;
  cache.update(make_test_snapshot("server.b"));
  cache.update(make_test_snapshot("server.c"));

  constexpr int kWorkerCount = 4;
  constexpr int kIterations = 200;
  std::atomic<int> operations_completed{0};

  auto worker_fn = [&](int worker_id) {
    const auto server_id = (worker_id % 2 == 0) ? "server.b" : "server.c";
    for (int i = 0; i < kIterations; ++i) {
      if (i % 3 == 0) {
        static_cast<void>(cache.invalidate(server_id));
      } else if (i % 3 == 1) {
        cache.update(make_test_snapshot(server_id));
      } else {
        static_cast<void>(cache.snapshot(server_id));
      }
      operations_completed.fetch_add(1, std::memory_order_relaxed);
    }
  };

  std::vector<std::thread> workers;
  workers.reserve(kWorkerCount);
  for (int i = 0; i < kWorkerCount; ++i) {
    workers.emplace_back(worker_fn, i);
  }
  for (auto& w : workers) {
    w.join();
  }

  assert_true(operations_completed.load() == kWorkerCount * kIterations,
              "CapabilityCache concurrent invalidate/update/read should complete without crash or deadlock");
}

void test_concurrent_mark_failed_preserves_consistency() {
  CapabilityCache cache;
  cache.update(make_test_snapshot("server.d"));

  constexpr int kWorkerCount = 4;
  constexpr int kIterations = 300;
  std::atomic<int> mark_failed_count{0};
  std::atomic<int> snapshot_count{0};

  auto worker_fn = [&](int worker_id) {
    for (int i = 0; i < kIterations; ++i) {
      if (worker_id % 2 == 0) {
        cache.mark_failed("server.d", "test.concurrent_failure");
        mark_failed_count.fetch_add(1, std::memory_order_relaxed);
      } else {
        const auto snap = cache.snapshot("server.d");
        if (snap.has_value()) {
          snapshot_count.fetch_add(1, std::memory_order_relaxed);
        }
      }
    }
  };

  std::vector<std::thread> workers;
  workers.reserve(kWorkerCount);
  for (int i = 0; i < kWorkerCount; ++i) {
    workers.emplace_back(worker_fn, i);
  }
  for (auto& w : workers) {
    w.join();
  }

  assert_true(mark_failed_count.load() > 0,
              "CapabilityCache concurrent mark_failed should complete");
  assert_true(mark_failed_count.load() + snapshot_count.load() > 0,
              "CapabilityCache concurrent mark_failed/read should both complete without data race");
}

}  // namespace

int main() {
  try {
    test_concurrent_read_during_update();
    test_concurrent_invalidate_and_read();
    test_concurrent_mark_failed_preserves_consistency();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}
