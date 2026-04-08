#include <exception>
#include <iostream>
#include <string>

#include "tracing/TraceConfig.h"
#include "support/TestAssertions.h"

namespace {

void test_trace_config_keeps_frozen_defaults() {
  using dasall::infra::tracing::TraceConfig;
  using dasall::tests::support::assert_true;

  const TraceConfig config;

  assert_true(config.enabled && config.provider_type == "internal" &&
                  config.sampler.type == "parent_based_always_on" &&
                  config.sampler.ratio == 0.1 && config.batch.enabled &&
                  config.batch.max_queue_size == 2048U &&
                  config.batch.max_export_batch_size == 512U &&
                  config.batch.schedule_delay_ms == 5000U &&
                  config.export_timeout_ms == 30000U && config.exporter.type == "noop" &&
                  config.exporter.otlp_endpoint.empty() &&
                  config.overflow_policy == "drop_oldest" && config.force_flush_on_stop &&
                  config.is_valid(),
              "TraceConfig should preserve the frozen 6.9 defaults for provider/sampler/batch/export/overflow layering");
}

void test_trace_config_merges_profile_deploy_runtime_in_order() {
  using dasall::infra::tracing::TraceConfigPatch;
  using dasall::infra::tracing::merge_trace_config;
  using dasall::tests::support::assert_true;

  TraceConfigPatch profile_patch;
  profile_patch.sampler.type = std::string("ratio");
  profile_patch.sampler.ratio = 0.25;
  profile_patch.batch.max_queue_size = 1024U;
  profile_patch.batch.max_export_batch_size = 256U;
  profile_patch.exporter.type = std::string("file");

  TraceConfigPatch deploy_patch;
  deploy_patch.batch.max_queue_size = 512U;
  deploy_patch.export_timeout_ms = 12000U;
  deploy_patch.exporter.otlp_endpoint = std::string("http://collector:4318");

  TraceConfigPatch runtime_patch;
  runtime_patch.enabled = false;
  runtime_patch.export_timeout_ms = 9000U;
  runtime_patch.overflow_policy = std::string("block");
  runtime_patch.force_flush_on_stop = false;

  const auto resolved = merge_trace_config(profile_patch, deploy_patch, runtime_patch);

  assert_true(!resolved.enabled && resolved.sampler.type == "ratio" &&
                  resolved.sampler.ratio == 0.25 && resolved.batch.max_queue_size == 512U &&
                  resolved.batch.max_export_batch_size == 256U &&
                  resolved.export_timeout_ms == 9000U && resolved.exporter.type == "file" &&
                  resolved.exporter.otlp_endpoint == "http://collector:4318" &&
                  resolved.overflow_policy == "block" && !resolved.force_flush_on_stop &&
                  resolved.is_valid(),
              "TraceConfig should apply default -> profile -> deploy -> runtime overrides in order without dropping frozen nested values");
}

void test_trace_config_rejects_invalid_nested_constraints() {
  using dasall::infra::tracing::TraceConfig;
  using dasall::tests::support::assert_true;

  TraceConfig invalid_config;
  invalid_config.batch.max_queue_size = 128U;
  invalid_config.batch.max_export_batch_size = 256U;

  assert_true(!invalid_config.is_valid(),
              "TraceConfig should reject batch settings where max_export_batch_size exceeds max_queue_size");
}

}  // namespace

int main() {
  try {
    test_trace_config_keeps_frozen_defaults();
    test_trace_config_merges_profile_deploy_runtime_in_order();
    test_trace_config_rejects_invalid_nested_constraints();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}