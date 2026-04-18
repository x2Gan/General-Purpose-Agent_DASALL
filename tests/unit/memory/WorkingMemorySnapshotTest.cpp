#include <chrono>
#include <exception>
#include <iostream>

#include "support/TestAssertions.h"
#include "working/IWorkingMemoryBoard.h"

namespace {

std::int64_t now_ms() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}

void test_working_memory_snapshot_export_and_restore_round_trip() {
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  auto board = dasall::memory::create_working_memory_board(8);
  const auto current = now_ms();

  dasall::memory::WorkingMemorySnapshot snapshot;
  snapshot.session_id = "session-restore";
  snapshot.snapshot_at = 42;
  snapshot.slots = {
      dasall::memory::WorkingMemorySlot{
          .key = "goal",
          .value = "complete mem-todo-012",
          .updated_at = current,
          .ttl_ms = 0,
          .source = "agent",
      },
      dasall::memory::WorkingMemorySlot{
          .key = "stage",
          .value = "build",
          .updated_at = current + 10,
          .ttl_ms = 60000,
          .source = "reflection",
      },
  };
  snapshot.open_questions = {"Should export warn on empty sessions?"};
  snapshot.ephemeral_facts = {"manager factory injects working-memory board"};

  board->restore_snapshot(snapshot);

  const auto exported = board->export_snapshot("session-restore");
  assert_equal("session-restore", exported.session_id,
               "restored snapshot export should preserve the session id");
  assert_equal(2, static_cast<int>(exported.slots.size()),
               "restored snapshot export should preserve slot count");
  assert_true(exported.open_questions.size() == 1U &&
                  exported.open_questions.front() ==
                      "Should export warn on empty sessions?",
              "restored snapshot export should preserve open questions");
  assert_true(exported.ephemeral_facts.size() == 1U &&
                  exported.ephemeral_facts.front() ==
                      "manager factory injects working-memory board",
              "restored snapshot export should preserve ephemeral facts");
}

void test_working_memory_snapshot_export_for_missing_session_stays_empty() {
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  auto board = dasall::memory::create_working_memory_board();

  const auto exported = board->export_snapshot("missing-session");
  assert_equal("missing-session", exported.session_id,
               "missing-session export should still preserve the requested session id");
  assert_true(exported.slots.empty(),
              "missing-session export should return an empty slot set");
  assert_true(exported.open_questions.empty(),
              "missing-session export should return empty open questions");
  assert_true(exported.ephemeral_facts.empty(),
              "missing-session export should return empty ephemeral facts");
}

}  // namespace

int main() {
  try {
    test_working_memory_snapshot_export_and_restore_round_trip();
    test_working_memory_snapshot_export_for_missing_session_stays_empty();
  } catch (const std::exception& exception) {
    std::cerr << exception.what() << '\n';
    return 1;
  }

  return 0;
}
