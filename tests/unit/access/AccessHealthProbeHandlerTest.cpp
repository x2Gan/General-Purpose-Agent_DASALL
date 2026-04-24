/// tests/unit/access/AccessHealthProbeHandlerTest.cpp
///
/// ACC-TODO-028：HealthProbeHandler 单元测试
///
/// 覆盖：
///   - handle_live() 始终返回 200 OK
///   - handle_ready() 未就绪时返回 503 NOT_READY
///   - handle_ready() 就绪后返回 200 READY
///   - handle_startup() 未启动时返回 503 STARTING
///   - handle_startup() 已启动后返回 200 STARTED
///   - ready=false 但 started=true 时 readiness 仍为 NOT_READY

#include <cassert>
#include <string>

#include "HealthProbeHandler.h"
#include "support/TestAssertions.h"

namespace {

using dasall::access::gateway::HealthProbeHandler;
using dasall::tests::support::assert_true;

/// live probe 始终返回 200 OK（不依赖外部状态）
void test_live_always_returns_200() {
  HealthProbeHandler h;
  const auto r = h.handle_live();
  assert_true(r.status_code == 200, "live probe must return 200");
  assert_true(r.body == "OK", "live probe body must be OK");
}

/// handle_ready：未调用 set_ready/set_started → 503 NOT_READY
void test_ready_returns_503_when_not_ready() {
  HealthProbeHandler h;
  const auto r = h.handle_ready();
  assert_true(r.status_code == 503, "ready probe must return 503 when not ready");
  assert_true(r.body == "NOT_READY", "ready probe body must be NOT_READY");
}

/// handle_ready：set_ready(true) + set_started(true) → 200 READY
void test_ready_returns_200_when_ready() {
  HealthProbeHandler h;
  h.set_started(true);
  h.set_ready(true);
  const auto r = h.handle_ready();
  assert_true(r.status_code == 200, "ready probe must return 200 when ready");
  assert_true(r.body == "READY", "ready probe body must be READY");
}

/// handle_startup：未调用 set_started → 503 STARTING
void test_startup_returns_503_when_not_started() {
  HealthProbeHandler h;
  const auto r = h.handle_startup();
  assert_true(r.status_code == 503, "startup probe must return 503 when not started");
  assert_true(r.body == "STARTING", "startup probe body must be STARTING");
}

/// handle_startup：set_started(true) → 200 STARTED
void test_startup_returns_200_when_started() {
  HealthProbeHandler h;
  h.set_started(true);
  const auto r = h.handle_startup();
  assert_true(r.status_code == 200, "startup probe must return 200 when started");
  assert_true(r.body == "STARTED", "startup probe body must be STARTED");
}

/// readiness 要求 started AND ready 均为 true
void test_ready_requires_both_started_and_ready() {
  HealthProbeHandler h;
  h.set_started(true);
  // ready 未置位
  const auto r = h.handle_ready();
  assert_true(r.status_code == 503,
              "ready probe must return 503 when started=true but ready=false");
}

}  // namespace

int main() {
  test_live_always_returns_200();
  test_ready_returns_503_when_not_ready();
  test_ready_returns_200_when_ready();
  test_startup_returns_503_when_not_started();
  test_startup_returns_200_when_started();
  test_ready_requires_both_started_and_ready();
  return 0;
}
