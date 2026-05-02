#pragma once

#include <condition_variable>
#include <cstddef>
#include <functional>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

#include "DaemonInProcessFixture.h"

namespace dasall::tests::integration::access_support {

struct DaemonLoadScenarioResult {
  std::size_t index = 0U;
  dasall::apps::cli::DaemonClientResponse response;
};

class DaemonLoadScenario {
 public:
  using ClientAction = std::function<dasall::apps::cli::DaemonClientResponse(
  const DaemonInProcessFixture& fixture,
      std::size_t index)>;

  static std::vector<DaemonLoadScenarioResult> run_parallel(
      const DaemonInProcessFixture& fixture,
      const std::size_t request_count,
      const ClientAction& action) {
    std::vector<DaemonLoadScenarioResult> results(request_count);
    std::vector<std::thread> workers;
    workers.reserve(request_count);

    std::mutex mutex;
    std::condition_variable ready_cv;
    std::condition_variable start_cv;
    std::size_t ready_count = 0U;
    bool start = false;

    for (std::size_t index = 0U; index < request_count; ++index) {
      workers.emplace_back([&, index]() {
        {
          std::unique_lock<std::mutex> lock(mutex);
          ++ready_count;
          ready_cv.notify_one();
          start_cv.wait(lock, [&start]() { return start; });
        }

        results[index].index = index;
        results[index].response = action(fixture, index);
      });
    }

    {
      std::unique_lock<std::mutex> lock(mutex);
      ready_cv.wait(lock, [&]() { return ready_count == request_count; });
      start = true;
    }
    start_cv.notify_all();

    for (auto& worker : workers) {
      if (worker.joinable()) {
        worker.join();
      }
    }

    return results;
  }
};

}  // namespace dasall::tests::integration::access_support