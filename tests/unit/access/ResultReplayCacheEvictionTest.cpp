#include <exception>
#include <iostream>
#include <string>

#include "ResultReplayCache.h"
#include "support/TestAssertions.h"

namespace {

[[nodiscard]] dasall::access::PublishEnvelope make_envelope(const std::string& request_id) {
  dasall::access::PublishEnvelope envelope;
  envelope.request_id = request_id;
  envelope.result_id = "result:" + request_id;
  envelope.session_id = "session:" + request_id;
  envelope.trace_id = "trace:" + request_id;
  envelope.protocol_kind = "http";
  envelope.payload = "payload:" + request_id;
  return envelope;
}

void evicts_lru_entry_when_capacity_exceeded() {
  using dasall::access::ResultReplayCache;
  using dasall::tests::support::assert_true;

  ResultReplayCache cache(1);
  cache.put("receipt:1", make_envelope("req-1"));
  cache.put("receipt:2", make_envelope("req-2"));

  assert_true(!cache.lookup("receipt:1").has_value(),
              "oldest entry should be evicted when capacity is exceeded");
  assert_true(cache.lookup("receipt:2").has_value(),
              "latest entry should remain after eviction");
}

}  // namespace

int main() {
  try {
    evicts_lru_entry_when_capacity_exceeded();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << '\n';
    return 1;
  }

  return 0;
}
