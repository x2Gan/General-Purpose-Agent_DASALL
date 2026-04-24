#include <exception>
#include <iostream>
#include <string>

#include "ResultReplayCache.h"
#include "support/TestAssertions.h"

namespace {

[[nodiscard]] dasall::access::PublishEnvelope make_envelope(
    const std::string& request_id,
    const std::string& payload) {
  dasall::access::PublishEnvelope envelope;
  envelope.request_id = request_id;
  envelope.result_id = "result:" + request_id;
  envelope.session_id = "session:" + request_id;
  envelope.trace_id = "trace:" + request_id;
  envelope.protocol_kind = "http";
  envelope.payload = payload;
  return envelope;
}

void put_and_lookup_should_return_envelope() {
  using dasall::access::ResultReplayCache;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  ResultReplayCache cache(4);
  cache.put("receipt:1", make_envelope("req-1", "payload-1"));

  const auto found = cache.lookup("receipt:1");
  assert_true(found.has_value(), "lookup should return cached envelope");
  assert_equal(std::string("req-1"), found->request_id,
               "cached envelope should preserve request_id");
  assert_equal(std::string("payload-1"), found->payload,
               "cached envelope should preserve payload");
}

}  // namespace

int main() {
  try {
    put_and_lookup_should_return_envelope();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << '\n';
    return 1;
  }

  return 0;
}
