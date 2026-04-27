#include <exception>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "MockCognitionTelemetrySink.h"
#include "observability/CognitionTelemetry.h"
#include "support/TestAssertions.h"

namespace {

using dasall::cognition::CognitionConfig;
using dasall::cognition::observability::CognitionTelemetry;
using dasall::cognition::observability::DegradeTelemetryRecord;
using dasall::cognition::observability::StageTelemetryContext;
using dasall::cognition::observability::TelemetryField;
using dasall::tests::mocks::MockCognitionTelemetrySink;
using dasall::tests::support::assert_true;

[[nodiscard]] std::string find_field_value(const std::vector<TelemetryField>& fields,
                                           const std::string& key) {
  for (const auto& field : fields) {
    if (field.key == key) {
      return field.value;
    }
  }
  return std::string{};
}

void test_emit_response_degraded_redacts_sensitive_payload_fields() {
  auto sink = std::make_shared<MockCognitionTelemetrySink>();
  CognitionConfig config;
  config.observability.redact_context_payload = true;
  CognitionTelemetry telemetry(config, sink);

  const StageTelemetryContext context{
      .request_id = "req-cog-022-redaction",
      .goal_id = "goal-cog-022-redaction",
      .profile_id = "desktop_full",
      .stage = "response",
      .trace_id = "trace-cog-022-redaction",
      .model_hint_tier = "standard",
      .fallback_used = true,
      .result_code = 0,
  };

  const DegradeTelemetryRecord degrade_record{
      .fallback_mode = "template_fallback",
      .reason = "llm unavailable",
      .payload_excerpt =
          std::string{"raw_prompt=secret provider_payload=token reasoning_trace=hidden"},
      .omitted_details = {},
      .audit_refs = {},
  };

  const auto result = telemetry.emit_response_degraded(context, degrade_record);

  assert_true(result.emitted, "redacted degrade events should still emit");
  assert_true(result.redacted, "sensitive payload fields should trigger redaction");
  assert_true(!sink->log_events.empty(), "redacted event should be recorded to the log sink");

  const auto& event = sink->log_events.back();
  const auto payload_excerpt = find_field_value(event.fields, "payload_excerpt");
  const auto omitted_details = find_field_value(event.fields, "omitted_details");
  assert_true(payload_excerpt.find("secret") == std::string::npos,
              "redacted payload excerpt must not expose raw prompt content");
  assert_true(payload_excerpt.find("token") == std::string::npos,
              "redacted payload excerpt must not expose provider payload content");
  assert_true(payload_excerpt.find("[REDACTED]") != std::string::npos,
              "redacted payload excerpt should retain the redaction marker");
  assert_true(omitted_details.find("redacted:raw_prompt") != std::string::npos,
              "redaction metadata should list raw_prompt as omitted");
  assert_true(omitted_details.find("redacted:provider_payload") != std::string::npos,
              "redaction metadata should list provider_payload as omitted");
}

}  // namespace

int main() {
  try {
    test_emit_response_degraded_redacts_sensitive_payload_fields();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << '\n';
    return 1;
  }

  return 0;
}