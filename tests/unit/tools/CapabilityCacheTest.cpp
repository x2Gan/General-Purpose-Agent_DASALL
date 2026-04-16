#include <exception>
#include <iostream>
#include <string>
#include <vector>

#include "mcp/CapabilityCache.h"
#include "support/TestAssertions.h"

namespace {

using dasall::tests::support::assert_equal;
using dasall::tests::support::assert_true;

[[nodiscard]] dasall::tools::CapabilityEntry make_entry(std::string tool_name) {
  return dasall::tools::CapabilityEntry{
      .capability_id = "cap.echo",
      .capability_version = "1.0.0",
      .tool_names = {std::move(tool_name)},
  };
}

[[nodiscard]] dasall::tools::CapabilitySnapshot make_snapshot(
    std::string server_id,
    std::optional<std::string> trust_marker = std::string("trusted")) {
  return dasall::tools::CapabilitySnapshot{
      .server_id = std::move(server_id),
      .entries = {make_entry("remote.echo")},
      .freshness = dasall::tools::CapabilityFreshness::expired,
      .last_refresh_at_ms = 12,
      .last_error = std::string("stale.error"),
      .trust_marker = std::move(trust_marker),
  };
}

[[nodiscard]] dasall::tools::mcp::CapabilityCache make_cache(
    std::int64_t* now_ms,
    bool stale_read_allowed = false,
    std::int64_t expire_after_ms = 1000) {
  return dasall::tools::mcp::CapabilityCache{
      dasall::tools::mcp::CapabilityCacheOptions{
          .expire_after_ms = expire_after_ms,
          .stale_read_allowed = stale_read_allowed,
          .now_ms = [now_ms]() { return *now_ms; },
      }};
}

void test_update_marks_snapshot_fresh_and_clears_last_error() {
  std::int64_t now_ms = 100;
  auto cache = make_cache(&now_ms);

  cache.update(make_snapshot("mcp.echo"));

  const auto current = cache.snapshot("mcp.echo");
  assert_true(current.has_value(), "updated snapshot should be visible by server id");
  assert_true(current->freshness == dasall::tools::CapabilityFreshness::fresh,
              "successful update should stamp the snapshot as fresh");
  assert_equal(100, static_cast<int>(current->last_refresh_at_ms.value_or(-1)),
               "successful update should refresh the last_refresh timestamp");
  assert_true(!current->last_error.has_value(),
              "successful update should clear the previous last_error");
}

void test_mark_failed_transitions_snapshot_to_stale_and_then_expired() {
  std::int64_t now_ms = 100;
  auto cache = make_cache(&now_ms);

  cache.update(make_snapshot("mcp.echo"));

  now_ms = 300;
  cache.mark_failed("mcp.echo", "mcp.timeout");

  const auto stale_snapshot = cache.snapshot("mcp.echo");
  assert_true(stale_snapshot.has_value(), "failed server should still expose a cached snapshot");
  assert_true(stale_snapshot->freshness == dasall::tools::CapabilityFreshness::stale,
              "refresh failure before ttl expiry should mark the snapshot stale");
  assert_equal(std::string("mcp.timeout"), stale_snapshot->last_error.value_or(""),
               "mark_failed should preserve the latest error message");

  now_ms = 1201;
  const auto expired_snapshot = cache.snapshot("mcp.echo");
  assert_true(expired_snapshot.has_value(), "expired server should still keep cache metadata");
  assert_true(expired_snapshot->freshness == dasall::tools::CapabilityFreshness::expired,
              "snapshot should become expired once ttl has elapsed");
}

void test_update_after_failure_restores_fresh_state_and_keeps_trust_marker() {
  std::int64_t now_ms = 100;
  auto cache = make_cache(&now_ms);

  cache.update(make_snapshot("mcp.echo", std::string("trusted")));

  now_ms = 150;
  cache.mark_failed("mcp.echo", "transport.closed");

  now_ms = 200;
  auto recovered = make_snapshot("mcp.echo", std::nullopt);
  recovered.entries = {make_entry("remote.echo.v2")};
  cache.update(std::move(recovered));

  const auto current = cache.snapshot("mcp.echo");
  assert_true(current.has_value(), "recovered server should remain readable");
  assert_true(current->freshness == dasall::tools::CapabilityFreshness::fresh,
              "recovery update should return the snapshot to fresh");
  assert_true(!current->last_error.has_value(),
              "recovery update should clear the failure marker");
  assert_equal(std::string("trusted"), current->trust_marker.value_or(""),
               "update should preserve an existing trust marker when the new snapshot omits it");
  assert_equal(std::string("remote.echo.v2"), current->entries.front().tool_names.front(),
               "recovery update should replace capability entries with the latest view");
}

void test_mark_failed_on_missing_server_creates_expired_error_snapshot() {
  std::int64_t now_ms = 500;
  auto cache = make_cache(&now_ms);

  cache.mark_failed("mcp.unknown", "launch.failed");

  const auto failed_snapshot = cache.snapshot("mcp.unknown");
  assert_true(failed_snapshot.has_value(),
              "missing server failures should still materialize diagnostics metadata");
  assert_true(failed_snapshot->freshness == dasall::tools::CapabilityFreshness::expired,
              "missing server failure should remain expired because no successful refresh exists");
  assert_equal(std::string("launch.failed"), failed_snapshot->last_error.value_or(""),
               "missing server failure should preserve the reported error");
  assert_equal(0, static_cast<int>(failed_snapshot->entries.size()),
               "missing server failure should not fabricate capability entries");
}

void test_list_trusted_and_invalidate_honor_stale_policy() {
  std::int64_t now_ms = 100;
  auto strict_cache = make_cache(&now_ms, false);
  strict_cache.update(make_snapshot("mcp.trusted", std::string("trusted")));
  strict_cache.update(make_snapshot("mcp.untrusted", std::nullopt));

  now_ms = 150;
  strict_cache.mark_failed("mcp.trusted", "refresh.failed");

  const auto strict_trusted = strict_cache.list_trusted();
  assert_equal(0, static_cast<int>(strict_trusted.size()),
               "strict policy should hide stale trusted snapshots from list_trusted");
  assert_true(strict_cache.invalidate("mcp.untrusted"),
              "invalidate should report success for an existing server");
  assert_true(!strict_cache.snapshot("mcp.untrusted").has_value(),
              "invalidate should remove the targeted server snapshot");

  now_ms = 200;
  auto permissive_cache = make_cache(&now_ms, true);
  permissive_cache.update(make_snapshot("mcp.trusted", std::string("trusted")));
  now_ms = 250;
  permissive_cache.mark_failed("mcp.trusted", "refresh.failed");

  const auto permissive_trusted = permissive_cache.list_trusted();
  assert_equal(1, static_cast<int>(permissive_trusted.size()),
               "permissive policy should keep stale trusted snapshots available");
  assert_true(permissive_trusted.front().freshness ==
                  dasall::tools::CapabilityFreshness::stale,
              "permissive stale read policy should expose stale trusted snapshots");
}

}  // namespace

int main() {
  try {
    test_update_marks_snapshot_fresh_and_clears_last_error();
    test_mark_failed_transitions_snapshot_to_stale_and_then_expired();
    test_update_after_failure_restores_fresh_state_and_keeps_trust_marker();
    test_mark_failed_on_missing_server_creates_expired_error_snapshot();
    test_list_trusted_and_invalidate_honor_stale_policy();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}