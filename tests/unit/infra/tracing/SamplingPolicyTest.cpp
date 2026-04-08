#include <exception>
#include <iostream>
#include <memory>
#include <string>

#include "tracing/SamplingPolicyEngine.h"
#include "tracing/SpanImpl.h"
#include "tracing/TracerImpl.h"
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

[[nodiscard]] dasall::infra::tracing::SamplingInput make_input(
    std::string trace_id,
    dasall::infra::tracing::TraceContext parent_context =
        dasall::infra::tracing::TraceContext::invalid()) {
  return dasall::infra::tracing::SamplingInput{
      .trace_id = std::move(trace_id),
      .span_name = std::string("runtime.sample"),
      .span_kind = dasall::infra::tracing::SpanKind::Internal,
      .attrs = {{"component", dasall::infra::tracing::TraceAttributeValue{std::string("tracing")}}},
      .parent_context = std::move(parent_context),
  };
}

[[nodiscard]] dasall::infra::tracing::TraceContext make_parent_context(bool sampled) {
  return dasall::infra::tracing::TraceContext{
      .trace_id = std::string("00000000000000000000000000000011"),
      .span_id = std::string("0000000000000011"),
      .trace_flags = sampled ? static_cast<std::uint8_t>(0x01U) : static_cast<std::uint8_t>(0x00U),
      .trace_state = std::string(),
      .parent_span_id = std::string(),
      .state = dasall::infra::tracing::TraceContextState::Active,
      .is_remote = false,
  };
}

void test_sampling_policy_engine_always_on_always_samples() {
  using dasall::infra::tracing::SamplingDecisionKind;
  using dasall::infra::tracing::SamplingPolicyEngine;
  using dasall::infra::tracing::TraceSamplerConfig;
  using dasall::tests::support::assert_true;

  SamplingPolicyEngine engine(TraceSamplerConfig{
      .type = std::string("always_on"),
      .ratio = 0.1,
  });

  const auto decision = engine.should_sample(
      make_input("00000000000000000000000000000001"));

  assert_true(decision.decision == SamplingDecisionKind::RecordAndSample &&
                  decision.reason == "always_on" &&
                  decision.sampler_desc == "AlwaysOnSampler",
              "SamplingPolicyEngine should map always_on to RecordAndSample with a stable description");
}

void test_sampling_policy_engine_always_off_always_drops() {
  using dasall::infra::tracing::SamplingDecisionKind;
  using dasall::infra::tracing::SamplingPolicyEngine;
  using dasall::infra::tracing::TraceSamplerConfig;
  using dasall::tests::support::assert_true;

  SamplingPolicyEngine engine(TraceSamplerConfig{
      .type = std::string("always_off"),
      .ratio = 0.1,
  });

  const auto decision = engine.should_sample(
      make_input("00000000000000000000000000000002"));

  assert_true(decision.decision == SamplingDecisionKind::Drop &&
                  decision.reason == "always_off" &&
                  decision.sampler_desc == "AlwaysOffSampler",
              "SamplingPolicyEngine should map always_off to Drop with a stable description");
}

void test_sampling_policy_engine_parent_based_follows_parent_and_root() {
  using dasall::infra::tracing::SamplingDecisionKind;
  using dasall::infra::tracing::SamplingPolicyEngine;
  using dasall::infra::tracing::TraceSamplerConfig;
  using dasall::tests::support::assert_true;

  SamplingPolicyEngine engine(TraceSamplerConfig{});

  const auto root_decision = engine.should_sample(
      make_input("00000000000000000000000000000003"));
  const auto sampled_child = engine.should_sample(
      make_input("00000000000000000000000000000004", make_parent_context(true)));
  const auto unsampled_child = engine.should_sample(
      make_input("00000000000000000000000000000005", make_parent_context(false)));

  assert_true(root_decision.decision == SamplingDecisionKind::RecordAndSample &&
                  sampled_child.decision == SamplingDecisionKind::RecordAndSample &&
                  sampled_child.reason == "parent_sampled" &&
                  unsampled_child.decision == SamplingDecisionKind::Drop &&
                  unsampled_child.reason == "parent_not_sampled",
              "SamplingPolicyEngine should keep ParentBased root spans sampled and follow the parent sampled flag for children");
}

void test_sampling_policy_engine_ratio_is_deterministic_and_tracer_applies_drop() {
  using dasall::infra::tracing::SamplingDecisionKind;
  using dasall::infra::tracing::SamplingPolicyEngine;
  using dasall::infra::tracing::SpanImpl;
  using dasall::infra::tracing::TraceConfig;
  using dasall::infra::tracing::TraceSamplerConfig;
  using dasall::infra::tracing::TraceContextState;
  using dasall::infra::tracing::TracerImpl;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  SamplingPolicyEngine ratio_engine(TraceSamplerConfig{
      .type = std::string("ratio"),
      .ratio = 0.5,
  });

  const auto sampled = ratio_engine.should_sample(
      make_input("00000000000000000000000000000010"));
  const auto sampled_again = ratio_engine.should_sample(
      make_input("00000000000000000000000000000010"));
  const auto dropped = ratio_engine.should_sample(
      make_input("fffffffffffffffffffffffffffffffe"));

  assert_true(sampled.decision == SamplingDecisionKind::RecordAndSample &&
                  sampled_again.decision == sampled.decision &&
                  dropped.decision == SamplingDecisionKind::Drop,
              "SamplingPolicyEngine should keep ratio decisions deterministic for the same trace id and vary across low/high suffixes");

  TraceConfig config;
  config.sampler.type = std::string("always_off");
  TracerImpl tracer(make_scope(), config);

  const auto span_base = tracer.start_span(make_descriptor("runtime.off"), nullptr);
  const auto span = std::dynamic_pointer_cast<SpanImpl>(span_base);
  assert_true(static_cast<bool>(span) && span->get_context().state == TraceContextState::Active &&
                  !span->is_recording() && !span->is_sampled() &&
                  span->get_context().trace_flags == 0U,
              "TracerImpl should apply SamplingPolicyEngine drop decisions to new spans while keeping a valid unsampled TraceContext");

  span->set_attribute("late_attr", dasall::infra::tracing::TraceAttributeValue{std::string("ignored")});
  assert_equal(static_cast<std::size_t>(0), span->accepted_attribute_count(),
               "Dropped spans should not retain descriptor or late attributes once sampling rejects recording");
}

}  // namespace

int main() {
  try {
    test_sampling_policy_engine_always_on_always_samples();
    test_sampling_policy_engine_always_off_always_drops();
    test_sampling_policy_engine_parent_based_follows_parent_and_root();
    test_sampling_policy_engine_ratio_is_deterministic_and_tracer_applies_drop();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}