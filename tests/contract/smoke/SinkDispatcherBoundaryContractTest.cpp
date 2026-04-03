#include <exception>
#include <iostream>
#include <string>
#include <type_traits>

#include "logging/LogTypes.h"
#include "logging/SinkDispatcher.h"
#include "dasall/tests/support/TestAssertions.h"

namespace {

template <typename T>
concept HasSinkRouteMember = requires {
  &T::sink_route;
};

template <typename T>
concept HasRoutePolicyMember = requires {
  &T::route_policy;
};

void test_sink_dispatcher_keeps_route_state_outside_log_event_boundary() {
  using dasall::infra::logging::LogEvent;
  using dasall::infra::logging::SinkDispatcher;
  using dasall::infra::logging::SinkRoute;
  using dasall::tests::support::assert_true;

  static_assert(std::is_same_v<LogEvent, dasall::infra::LogEvent>);
  static_assert(!HasSinkRouteMember<LogEvent>);
  static_assert(!HasRoutePolicyMember<LogEvent>);

  SinkDispatcher dispatcher;
  const LogEvent event{
      .level = dasall::infra::logging::LogLevel::Info,
      .module = std::string("runtime"),
      .message = std::string("dispatcher contract route check"),
      .attrs = {{"request_id", "req-contract-007"}},
      .ts = 1711968607100,
  };

  const auto result = dispatcher.dispatch(event);
  assert_true(result.ok,
              "sink dispatcher should accept a serializable runtime event at the contract boundary");
  assert_true(dispatcher.last_route() == SinkRoute::BasicFile,
              "runtime events should remain on the basic route without exposing a top-level route field");
  assert_true(!dispatcher.last_record().event.attrs.contains("sink_route"),
              "sink dispatcher must not leak internal route labels into public attrs");
  assert_true(!dispatcher.last_record().event.attrs.contains("route_policy"),
              "sink dispatcher must not leak route-policy internals into public attrs");
}

void test_sink_dispatcher_audit_route_uses_existing_attr_surface_only() {
  using dasall::infra::logging::LogEvent;
  using dasall::infra::logging::SinkDispatcher;
  using dasall::infra::logging::SinkRoute;
  using dasall::tests::support::assert_true;

  SinkDispatcher dispatcher;
  const LogEvent audit_event{
      .level = dasall::infra::logging::LogLevel::Warn,
      .module = std::string("audit"),
      .message = std::string("dispatcher audit route check"),
      .attrs = {
          {"request_id", "req-contract-007-audit"},
          {"evidence_ref", "audit-evidence-007"},
      },
      .ts = 1711968607101,
  };

  const auto result = dispatcher.dispatch(audit_event);
  assert_true(result.ok,
              "sink dispatcher should route audit events without changing the frozen log-event shape");
  assert_true(dispatcher.last_route() == SinkRoute::Audit,
              "audit module events should use the explicit audit route");
  assert_true(dispatcher.last_record().event.attrs.at("evidence_ref") == "audit-evidence-007",
              "sink dispatcher should preserve the existing evidence_ref attr instead of expanding the top-level contract");
}

}  // namespace

int main() {
  try {
    test_sink_dispatcher_keeps_route_state_outside_log_event_boundary();
    test_sink_dispatcher_audit_route_uses_existing_attr_surface_only();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}