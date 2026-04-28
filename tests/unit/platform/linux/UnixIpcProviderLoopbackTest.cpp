#include <exception>
#include <iostream>

#include "linux/UnixIpcProvider.h"
#include "support/TestAssertions.h"

namespace {

using dasall::platform::IpcEndpoint;
using dasall::platform::IpcPayload;
using dasall::platform::ListenOptions;
using dasall::platform::PlatformErrorCode;
using dasall::platform::linux::UnixIpcProvider;
using dasall::tests::support::assert_equal;
using dasall::tests::support::assert_true;

void test_loopback_transfers_payload_bidirectionally() {
  UnixIpcProvider provider;

  IpcEndpoint endpoint;
  endpoint.socket_path = "/tmp/loopback.sock";

  ListenOptions options;
  options.max_payload_bytes = 16U;
  const auto listener = provider.listen(endpoint, options);
  assert_true(listener.ok(), "listen should succeed for loopback endpoint");

  const auto client = provider.connect(endpoint, 10);
  assert_true(client.ok(), "connect should create client channel for loopback listener");

  const auto server = provider.accept(*listener.value, 10);
  assert_true(server.ok(), "accept should return paired server channel");

  const IpcPayload request{1U, 2U, 3U};
  const auto sent_request = provider.send(*client.value, request);
  assert_true(sent_request.ok(), "client send should succeed for paired server channel");

  const auto server_receive = provider.receive(*server.value, 10);
  assert_true(server_receive.ok(), "server receive should succeed for queued client payload");
  assert_true(server_receive.value->data == request,
              "server should observe client payload through loopback queue");

  const IpcPayload response{9U, 8U};
  const auto sent_response = provider.send(*server.value, response);
  assert_true(sent_response.ok(), "server send should succeed for paired client channel");

  const auto client_receive = provider.receive(*client.value, 10);
  assert_true(client_receive.ok(), "client receive should succeed for queued server payload");
  assert_true(client_receive.value->data == response,
              "client should observe server payload through loopback queue");
}

void test_loopback_reports_peer_closed_after_close_propagation() {
  UnixIpcProvider provider;

  IpcEndpoint endpoint;
  endpoint.socket_path = "/tmp/loopback-close.sock";

  const auto listener = provider.listen(endpoint, ListenOptions{});
  assert_true(listener.ok(), "listen should succeed for close propagation test");

  const auto client = provider.connect(endpoint, 10);
  assert_true(client.ok(), "connect should succeed for close propagation test");

  const auto server = provider.accept(*listener.value, 10);
  assert_true(server.ok(), "accept should succeed for close propagation test");

  const auto closed = provider.close(*server.value);
  assert_true(closed.ok(), "closing server channel should succeed");

  const auto client_receive = provider.receive(*client.value, 10);
  assert_true(client_receive.ok(), "client receive should surface peer_closed after server close");
  assert_true(client_receive.value->peer_closed,
              "close propagation should mark client peer_closed");

  const IpcPayload payload{7U};
  const auto client_send = provider.send(*client.value, payload);
  assert_true(!client_send.ok(), "send should fail after peer close propagation");
  assert_equal(static_cast<int>(PlatformErrorCode::PeerClosed),
               static_cast<int>(client_send.error->code),
               "send after peer close should map to PeerClosed");
}

void test_loopback_enforces_listener_payload_budget() {
  UnixIpcProvider provider;

  IpcEndpoint endpoint;
  endpoint.socket_path = "/tmp/loopback-budget.sock";

  ListenOptions options;
  options.max_payload_bytes = 3U;
  const auto listener = provider.listen(endpoint, options);
  assert_true(listener.ok(), "listen should succeed for payload budget test");

  const auto client = provider.connect(endpoint, 10);
  assert_true(client.ok(), "connect should succeed for payload budget test");

  const auto server = provider.accept(*listener.value, 10);
  assert_true(server.ok(), "accept should succeed for payload budget test");

  const IpcPayload oversized{1U, 2U, 3U, 4U};
  const auto send_result = provider.send(*client.value, oversized);
  assert_true(!send_result.ok(), "loopback send should enforce listener payload budget");
  assert_equal(static_cast<int>(PlatformErrorCode::PayloadTooLarge),
               static_cast<int>(send_result.error->code),
               "oversized loopback payload should map to PayloadTooLarge");
}

}  // namespace

int main() {
  try {
    test_loopback_transfers_payload_bidirectionally();
    test_loopback_reports_peer_closed_after_close_propagation();
    test_loopback_enforces_listener_payload_budget();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << '\n';
    return 1;
  }

  return 0;
}