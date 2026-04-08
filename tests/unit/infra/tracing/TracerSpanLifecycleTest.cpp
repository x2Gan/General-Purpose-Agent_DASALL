#include <exception>
#include <iostream>
#include <memory>
#include <string>

#include "tracing/SpanImpl.h"
#include "tracing/TracerImpl.h"
#include "tracing/TracerProviderImpl.h"
#include "support/TestAssertions.h"

namespace {

[[nodiscard]] dasall::infra::tracing::TracerScope make_scope() {
  return dasall::infra::tracing::TracerScope{
      .name = std::string("infra.tracing"),
      .version = std::string("1.0.0"),
      .schema_url = std::string("https://opentelemetry.io/schemas/1.26.0"),
  };
}

[[nodiscard]] dasall::infra::tracing::SpanDescriptor make_descriptor(std::string name) {
  return dasall::infra::tracing::SpanDescriptor{
      .name = std::move(name),
      .kind = dasall::infra::tracing::SpanKind::Internal,
      .start_ts_unix_ms = 1712534400000,
      .attrs = {{"component", dasall::infra::tracing::TraceAttributeValue{std::string("tracing")}}},
      .links = {},
  };
}

[[nodiscard]] dasall::infra::tracing::TraceConfig make_config() {
  return dasall::infra::tracing::TraceConfig{
      .enabled = true,
      .provider_type = std::string("internal"),
      .force_flush_on_stop = true,
  };
}

void test_tracer_impl_builds_root_and_child_relationships() {
  using dasall::infra::tracing::SpanImpl;
  using dasall::infra::tracing::TraceContextState;
  using dasall::infra::tracing::TracerImpl;
  using dasall::infra::tracing::TracerProviderImpl;
  using dasall::tests::support::assert_true;

  TracerProviderImpl provider;
  assert_true(provider.init(make_config()).ok,
              "TracerProviderImpl should initialize before tracer/span lifecycle checks");

  const auto tracer_base = provider.get_tracer(make_scope());
  const auto tracer = std::dynamic_pointer_cast<TracerImpl>(tracer_base);
  assert_true(static_cast<bool>(tracer),
              "TracerProviderImpl should materialize TracerImpl instances for valid tracing scopes");

  const auto root_base = tracer->start_span(make_descriptor("runtime.root"), nullptr);
  const auto root_span = std::dynamic_pointer_cast<SpanImpl>(root_base);
  assert_true(static_cast<bool>(root_span) && root_span->get_context().state == TraceContextState::Active &&
                  root_span->get_context().parent_span_id.empty(),
              "TracerImpl should create a root span with a fresh active TraceContext and no parent span id");

  assert_true(tracer->current_context().state == dasall::infra::tracing::TraceContextState::Noop,
              "TracerImpl should not activate a span implicitly outside with_active_span()");

  tracer->with_active_span(root_span, [&] {
    const auto active_root_context = tracer->current_context();
    assert_true(active_root_context.trace_id == root_span->get_context().trace_id &&
                    active_root_context.span_id == root_span->get_context().span_id,
                "TracerImpl should expose the root span context while the callback scope is active");

    const auto child_base = tracer->start_span(make_descriptor("runtime.child"), nullptr);
    const auto child_span = std::dynamic_pointer_cast<SpanImpl>(child_base);
    assert_true(static_cast<bool>(child_span),
                "TracerImpl should create a child span from the active context when no explicit parent is supplied");
    assert_true(child_span->get_context().trace_id == root_span->get_context().trace_id &&
                    child_span->get_context().parent_span_id == root_span->get_context().span_id &&
                    child_span->parent_context().span_id == root_span->get_context().span_id,
                "Child spans should inherit the parent trace_id and capture the parent span id explicitly");

    tracer->with_active_span(child_span, [&] {
      assert_true(tracer->current_context().span_id == child_span->get_context().span_id,
                  "Nested with_active_span() should swap the active context to the child span inside the nested callback");
    });

    assert_true(tracer->current_context().span_id == root_span->get_context().span_id,
                "TracerImpl should restore the previous active root span after the child callback exits");
  });

  assert_true(tracer->current_context().state == dasall::infra::tracing::TraceContextState::Noop,
              "TracerImpl should restore the thread-local context to noop after the callback scope exits");
}

void test_span_impl_keeps_terminal_state_after_end() {
  using dasall::infra::tracing::SpanImpl;
  using dasall::infra::tracing::SpanStatusCode;
  using dasall::infra::tracing::TracerImpl;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  TracerImpl tracer(make_scope());
  const auto span_base = tracer.start_span(make_descriptor("runtime.lifecycle"), nullptr);
  const auto span = std::dynamic_pointer_cast<SpanImpl>(span_base);
  assert_true(static_cast<bool>(span),
              "TracerImpl should materialize SpanImpl for valid descriptors");

  span->set_attribute("tool_name", dasall::infra::tracing::TraceAttributeValue{std::string("search")});
  span->add_event("tool.started", {{"request_id", dasall::infra::tracing::TraceAttributeValue{std::string("req-009")}}});
  span->set_status(SpanStatusCode::Error, "timeout");
  span->set_status(SpanStatusCode::Ok, "ignored");

  const auto first_end = span->end(1712534400100);
  const auto context_before_late_mutation = span->get_context();

  span->set_attribute("late_attr", dasall::infra::tracing::TraceAttributeValue{std::string("ignored")});
  span->add_event("tool.finished", {{"result", dasall::infra::tracing::TraceAttributeValue{std::string("ignored")}}});
  span->set_status(SpanStatusCode::Error, "late-error");
  const auto second_end = span->end(1712534400200);

  assert_equal(static_cast<std::size_t>(2), span->accepted_attribute_count(),
               "SpanImpl should preserve only the pre-end attribute set once the span is ended");
  assert_equal(static_cast<std::size_t>(1), span->accepted_event_count(),
               "SpanImpl should reject post-end events and preserve the terminal event count");
  assert_true(span->has_ended() && first_end.end_ts_unix_ms == second_end.end_ts_unix_ms,
              "SpanImpl::end should be idempotent after the terminal end snapshot is materialized");
  assert_true(span->status_code() == SpanStatusCode::Ok && span->status_message().empty(),
              "SpanImpl should honor the OTel status ordering where Ok becomes final and clears any prior error message");
  assert_true(span->get_context().trace_id == context_before_late_mutation.trace_id &&
                  span->get_context().span_id == context_before_late_mutation.span_id,
              "SpanImpl should keep a stable TraceContext before and after the span is ended");
  assert_true(first_end.is_valid(),
              "SpanImpl::end should return a valid SpanEndResult for an explicitly ended span");
}

}  // namespace

int main() {
  try {
    test_tracer_impl_builds_root_and_child_relationships();
    test_span_impl_keeps_terminal_state_after_end();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}