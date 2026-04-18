#include <exception>
#include <iostream>
#include <thread>
#include <vector>

#include "support/TestAssertions.h"
#include "working/IWorkingMemoryBoard.h"

namespace {

void test_working_memory_board_handles_parallel_writers() {
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  auto board = dasall::memory::create_working_memory_board(256);
  auto* raw_board = board.get();

  std::vector<std::thread> writers;
  for (int thread_index = 0; thread_index < 4; ++thread_index) {
    writers.emplace_back([raw_board, thread_index]() {
      const std::string session_id = "session-concurrency";
      const std::string key = "slot-" + std::to_string(thread_index);
      for (int iteration = 0; iteration < 64; ++iteration) {
        raw_board->set_slot(session_id, dasall::memory::WorkingMemorySlot{
                                            .key = key,
                                            .value = "value-" + std::to_string(iteration),
                                            .updated_at = static_cast<std::int64_t>(1000 + iteration),
                                            .ttl_ms = 0,
                                            .source = "thread",
                                        });
        (void)raw_board->get_slot(session_id, key);
      }
    });
  }

  for (auto& writer : writers) {
    writer.join();
  }

  const auto snapshot = board->export_snapshot("session-concurrency");
  assert_equal(4, static_cast<int>(snapshot.slots.size()),
               "parallel writers should converge to one slot per key without corrupting session state");
  for (int thread_index = 0; thread_index < 4; ++thread_index) {
    const auto slot = board->get_slot("session-concurrency",
                                      "slot-" + std::to_string(thread_index));
    assert_true(slot.has_value(),
                "parallel writers should leave each keyed slot readable after join");
  }
}

}  // namespace

int main() {
  try {
    test_working_memory_board_handles_parallel_writers();
  } catch (const std::exception& exception) {
    std::cerr << exception.what() << '\n';
    return 1;
  }

  return 0;
}
