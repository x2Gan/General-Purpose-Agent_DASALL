#include <exception>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>

#include "tracing/ISpan.h"
#include "dasall/tests/support/TestAssertions.h"

namespace {

class NullSpan final : public dasall::infra::tracing::ISpan {
 public:
  explicit NullSpan(dasall::infra::tracing::TraceContext context)
      : context_(std::move(context)) {}

  void set_attribute(
      std::string_view key,
      const dasall::infra::tracing::TraceAttributeValue& value) override {
    if (ended_ || !dasall::infra::tracing::is_valid_trace_attr_key(key) ||
        !dasall::infra::tracing::is_valid_trace_attribute_value(value)) {
      return;
    }

    ++accepted_attribute_count_;
  }

  void add_event(
      std::string_view name,
      const dasall::infra::tracing::TraceAttributeMap& attrs) override {
    if (ended_ || name.empty() || !dasall::infra::tracing::is_printable_ascii(name) ||
        !dasall::infra::tracing::trace_attributes_are_serializable(attrs)) {
      return;
    }

    ++accepted_event_count_;
  }

  void set_status(
      dasall::infra::tracing::SpanStatusCode code,
      std::string_view message) override {
    if (ended_) {
      return;
    }

    status_code_ = code;
    status_message_ =
        code == dasall::infra::tracing::SpanStatusCode::Error ? std::string(message) : std::string();
  }

  dasall::infra::tracing::SpanEndResult end(
      std::optional<std::int64_t> end_ts_unix_ms = std::nullopt) override {
    if (!ended_) {
      ended_ = true;
      end_result_ = dasall::infra::tracing::SpanEndResult{
          .end_ts_unix_ms = end_ts_unix_ms.value_or(1711958400100),
          .status_code = status_code_,
          .status_message = status_message_,
          .dropped_attr_count = 0,
      };
    }

    return end_result_;
  }

  [[nodiscard]] dasall::infra::tracing::TraceContext get_context() const override {
    return context_;
  }

  [[nodiscard]] std::size_t accepted_attribute_count() const {
    return accepted_attribute_count_;
  }

  [[nodiscard]] std::size_t accepted_event_count() const {
    return accepted_event_count_;
  }

  [[nodiscard]] dasall::infra::tracing::SpanStatusCode status_code() const {
    return status_code_;
  }

  [[nodiscard]] const std::string& status_message() const {
    return status_message_;
  }

 private:
  dasall::infra::tracing::TraceContext context_;
  bool ended_ = false;
  std::size_t accepted_attribute_count_ = 0;
  std::size_t accepted_event_count_ = 0;
  dasall::infra::tracing::SpanStatusCode status_code_ =
      dasall::infra::tracing::SpanStatusCode::Unset;
  std::string status_message_;
  dasall::infra::tracing::SpanEndResult end_result_{};
};

[[nodiscard]] dasall::infra::tracing::TraceContext make_context() {
  return dasall::infra::tracing::TraceContext{
      .trace_id = std::string("4bf92f3577b34da6a3ce929d0e0e4736"),
      .span_id = std::string("00f067aa0ba902b7"),
      .trace_flags = 0x01,
      .trace_state = std::string("rojo=00f067aa0ba902b7"),
      .parent_span_id = std::string("b7ad6b7169203331"),
      .state = dasall::infra::tracing::TraceContextState::Active,
      .is_remote = false,
  };
}

void test_span_interface_keeps_frozen_method_surface() {
  using dasall::infra::tracing::ISpan;
  using dasall::infra::tracing::SpanEndResult;
  using dasall::infra::tracing::SpanStatusCode;

  static_assert(std::is_same_v<decltype(&ISpan::set_attribute),
                               void (ISpan::*)(
                                   std::string_view,
                                   const dasall::infra::tracing::TraceAttributeValue&)>);
  static_assert(std::is_same_v<decltype(&ISpan::add_event),
                               void (ISpan::*)(
                                   std::string_view,
                                   const dasall::infra::tracing::TraceAttributeMap&)>);
  static_assert(std::is_same_v<decltype(&ISpan::set_status),
                               void (ISpan::*)(SpanStatusCode, std::string_view)>);
  static_assert(std::is_same_v<decltype(&ISpan::end),
                               SpanEndResult (ISpan::*)(std::optional<std::int64_t>)>);
}

void test_span_interface_accepts_mutations_before_end() {
  using dasall::infra::tracing::TraceAttributeMap;
  using dasall::infra::tracing::TraceAttributeValue;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  NullSpan span(make_context());
  span.set_attribute("tool_name", TraceAttributeValue{std::string("search")});
  span.add_event(
      "tool.started",
      TraceAttributeMap{{"request_id", TraceAttributeValue{std::string("req-span-001")}}});
  span.set_status(dasall::infra::tracing::SpanStatusCode::Error, "timeout");

  const auto end_result = span.end(1711958400200);
  assert_equal(static_cast<std::size_t>(1), span.accepted_attribute_count(),
               "ISpan should accept attribute writes before the span is ended");
  assert_equal(static_cast<std::size_t>(1), span.accepted_event_count(),
               "ISpan should accept event writes before the span is ended");
  assert_true(end_result.is_valid(),
              "ISpan::end should return a valid SpanEndResult once an explicit end timestamp is supplied");
  assert_true(span.get_context().is_valid(),
              "ISpan::get_context should keep returning the frozen TraceContext for the span lifetime");
  assert_true(span.status_code() == dasall::infra::tracing::SpanStatusCode::Error &&
                  span.status_message() == "timeout",
              "ISpan::set_status should preserve explicit error status and message before end() is called");
}

void test_span_interface_rejects_mutations_after_end() {
  using dasall::infra::tracing::TraceAttributeMap;
  using dasall::infra::tracing::TraceAttributeValue;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  NullSpan span(make_context());
  const auto first_end = span.end(1711958400300);

  span.set_attribute("tool_name", TraceAttributeValue{std::string("search")});
  span.add_event(
      "tool.finished",
      TraceAttributeMap{{"result", TraceAttributeValue{std::string("ok")}}});
  span.set_status(dasall::infra::tracing::SpanStatusCode::Error, "late-error");

  const auto second_end = span.end(1711958400400);
  assert_equal(static_cast<std::size_t>(0), span.accepted_attribute_count(),
               "ISpan should reject attribute mutations after end() to keep terminal span state binary");
  assert_equal(static_cast<std::size_t>(0), span.accepted_event_count(),
               "ISpan should reject event mutations after end() to keep terminal span state binary");
  assert_true(span.status_code() == dasall::infra::tracing::SpanStatusCode::Unset &&
                  span.status_message().empty(),
              "ISpan should reject status mutations after end() and preserve the terminal status snapshot");
  assert_true(first_end.end_ts_unix_ms == second_end.end_ts_unix_ms,
              "ISpan::end should remain idempotent once the first terminal SpanEndResult has been materialized");
}

}  // namespace

int main() {
  try {
    test_span_interface_keeps_frozen_method_surface();
    test_span_interface_accepts_mutations_before_end();
    test_span_interface_rejects_mutations_after_end();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}
