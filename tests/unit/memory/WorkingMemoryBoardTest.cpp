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

void test_working_memory_board_supports_set_get_remove_and_clear() {
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  auto board = dasall::memory::create_working_memory_board(4);

  board->set_slot("session-a", dasall::memory::WorkingMemorySlot{
                                   .key = "active_goal",
                                   .value = "stabilize lifecycle",
                                   .updated_at = 10,
                                   .ttl_ms = 0,
                                   .source = "agent",
                               });

  const auto slot = board->get_slot("session-a", "active_goal");
  assert_true(slot.has_value(),
              "working-memory board should return a slot after set_slot");
  assert_equal("stabilize lifecycle", slot->value,
               "working-memory board should preserve the slot value");

  board->remove_slot("session-a", "active_goal");
  assert_true(!board->get_slot("session-a", "active_goal").has_value(),
              "working-memory board should remove slots by key");

  board->set_slot("session-a", dasall::memory::WorkingMemorySlot{
                                   .key = "temp",
                                   .value = "value",
                                   .updated_at = 20,
                                   .ttl_ms = 0,
                                   .source = "tool",
                               });
  board->clear_session("session-a");

  const auto snapshot = board->export_snapshot("session-a");
  assert_equal("session-a", snapshot.session_id,
               "working-memory export should preserve the requested session id");
  assert_true(snapshot.slots.empty(),
              "working-memory clear_session should remove all slots");
}

void test_working_memory_board_evicts_expired_slots_and_preserves_session_isolation() {
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  auto board = dasall::memory::create_working_memory_board(8);
  const auto current = now_ms();

  board->set_slot("session-a", dasall::memory::WorkingMemorySlot{
                                   .key = "shared-key",
                                   .value = "expired",
                                   .updated_at = current - 5000,
                                   .ttl_ms = 1000,
                                   .source = "tool",
                               });
  board->set_slot("session-b", dasall::memory::WorkingMemorySlot{
                                   .key = "shared-key",
                                   .value = "fresh",
                                   .updated_at = current,
                                   .ttl_ms = 1000,
                                   .source = "tool",
                               });

  board->evict_expired("session-a");

  assert_true(!board->get_slot("session-a", "shared-key").has_value(),
              "working-memory board should evict expired slots for the target session only");

  const auto other_session_slot = board->get_slot("session-b", "shared-key");
  assert_true(other_session_slot.has_value(),
              "working-memory board should preserve slots in other sessions");
  assert_equal("fresh", other_session_slot->value,
               "working-memory board should keep isolated session values intact");
}

void test_working_memory_board_applies_lru_pressure_with_ttl_preference() {
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  auto board = dasall::memory::create_working_memory_board(2);
  const auto current = now_ms();

  board->set_slot("session-a", dasall::memory::WorkingMemorySlot{
                                   .key = "persistent",
                                   .value = "keep",
                                   .updated_at = current,
                                   .ttl_ms = 0,
                                   .source = "user",
                               });
  board->set_slot("session-a", dasall::memory::WorkingMemorySlot{
                                   .key = "ttl-old",
                                   .value = "evict-me",
                                   .updated_at = current + 10,
                                   .ttl_ms = 60000,
                                   .source = "tool",
                               });
  board->set_slot("session-a", dasall::memory::WorkingMemorySlot{
                                   .key = "ttl-new",
                                   .value = "keep-too",
                                   .updated_at = current + 20,
                                   .ttl_ms = 60000,
                                   .source = "tool",
                               });

  const auto snapshot = board->export_snapshot("session-a");
  assert_equal(2, static_cast<int>(snapshot.slots.size()),
               "working-memory board should enforce the max-slots-per-session limit");
  assert_true(board->get_slot("session-a", "persistent").has_value(),
              "LRU pressure should preserve the oldest non-TTL slot when a TTL slot is available for eviction");
  assert_true(!board->get_slot("session-a", "ttl-old").has_value(),
              "LRU pressure should evict the oldest TTL-bearing slot first");
  assert_true(board->get_slot("session-a", "ttl-new").has_value(),
              "LRU pressure should keep the newest slot in the session");
}

}  // namespace

int main() {
  try {
    test_working_memory_board_supports_set_get_remove_and_clear();
    test_working_memory_board_evicts_expired_slots_and_preserves_session_isolation();
    test_working_memory_board_applies_lru_pressure_with_ttl_preference();
  } catch (const std::exception& exception) {
    std::cerr << exception.what() << '\n';
    return 1;
  }

  return 0;
}
