#include <exception>
#include <iostream>

#include "AccessTypes.h"
#include "support/TestAssertions.h"

namespace {

void publish_envelope_separates_protocol_from_result() {
  using dasall::access::PublishEnvelope;

  PublishEnvelope envelope;
  // 跟踪 ID
  envelope.request_id = "req-001";
  envelope.result_id = "res-001";
  envelope.session_id = "sess-001";
  envelope.trace_id = "trace-x";

  // 协议元数据（module-local）
  envelope.channel_ref = "ch://http/gateway";
  envelope.protocol_kind = "http";
  envelope.protocol_status_hint = "200 OK";
  envelope.protocol_metadata = "Content-Type: application/json";

  // 业务结果占位（shared，但在本测试中仅验证结构）
  envelope.payload = "{}";
  envelope.is_final = true;

  dasall::tests::support::assert_equal(
      "200 OK",
      envelope.protocol_status_hint,
      "protocol_status_hint should not be mixed with business result");
  dasall::tests::support::assert_equal(
      "ch://http/gateway",
      envelope.channel_ref,
      "channel_ref should be module-local");
}

void publish_envelope_is_final_flag_distinguishes_stream_frames() {
  using dasall::access::PublishEnvelope;

  // 流式中间帧
  PublishEnvelope intermediate_frame;
  intermediate_frame.request_id = "req-001";
  intermediate_frame.is_final = false;
  intermediate_frame.payload = "chunk1";

  // 流式最终帧
  PublishEnvelope final_frame;
  final_frame.request_id = "req-001";
  final_frame.is_final = true;
  final_frame.payload = "chunk2";

  dasall::tests::support::assert_true(
      !intermediate_frame.is_final,
      "intermediate frame should have is_final=false");
  dasall::tests::support::assert_true(
      final_frame.is_final,
      "final frame should have is_final=true");
}

void publish_envelope_preserves_trace_context() {
  using dasall::access::PublishEnvelope;

  PublishEnvelope envelope;
  envelope.request_id = "req-abc";
  envelope.session_id = "sess-xyz";
  envelope.trace_id = "trace-123";

  // 验证所有追踪字段都能设置和读取
  dasall::tests::support::assert_equal(
      "trace-123",
      envelope.trace_id,
      "trace_id should be preserved for end-to-end correlation");
  dasall::tests::support::assert_equal(
      "sess-xyz",
      envelope.session_id,
      "session_id should be preserved for multi-request association");
}

}  // namespace

int main() {
  try {
    publish_envelope_separates_protocol_from_result();
    publish_envelope_is_final_flag_distinguishes_stream_frames();
    publish_envelope_preserves_trace_context();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}
