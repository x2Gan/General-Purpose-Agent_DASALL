#include <exception>
#include <iostream>
#include <memory>
#include <string>
#include <utility>

#include "ProtocolAdapterRegistry.h"
#include "support/TestAssertions.h"

namespace {

class StubProtocolAdapter final : public dasall::access::IProtocolAdapter {
 public:
  explicit StubProtocolAdapter(std::string adapter_id)
      : adapter_id_(std::move(adapter_id)) {}

  bool can_handle(const std::string_view entry_type,
                  const std::string_view protocol_kind) const override {
    return entry_type == expected_entry_type_ && protocol_kind == expected_protocol_kind_;
  }

  dasall::access::InboundPacket decode() override {
    dasall::access::InboundPacket packet;
    packet.entry_type = expected_entry_type_;
    packet.protocol_kind = expected_protocol_kind_;
    packet.packet_id = adapter_id_ + "-packet";
    return packet;
  }

  bool encode(const dasall::access::PublishEnvelope& envelope) override {
    last_encoded_request_id_ = envelope.request_id;
    return true;
  }

  void set_expected_binding(std::string entry_type, std::string protocol_kind) {
    expected_entry_type_ = std::move(entry_type);
    expected_protocol_kind_ = std::move(protocol_kind);
  }

  [[nodiscard]] std::string last_encoded_request_id() const {
    return last_encoded_request_id_;
  }

 private:
  std::string adapter_id_;
  std::string expected_entry_type_;
  std::string expected_protocol_kind_;
  std::string last_encoded_request_id_;
};

void test_protocol_adapter_registry_registers_resolves_and_revokes_bindings() {
  using dasall::access::ProtocolAdapterRegistry;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  ProtocolAdapterRegistry registry;

  auto cli_adapter = std::make_shared<StubProtocolAdapter>("cli-adapter");
  cli_adapter->set_expected_binding("cli", "uds");

  const bool registered =
      registry.register_adapter("apps/cli", "cli", "uds", cli_adapter);
  assert_true(registered,
              "registry should accept a new entry_type/protocol binding");

  const auto resolved_decoder = registry.resolve_decoder("cli", "uds");
  assert_true(static_cast<bool>(resolved_decoder),
              "registry should resolve decoder for registered binding");
  assert_true(resolved_decoder->can_handle("cli", "uds"),
              "resolved decoder should match its registered binding");

  dasall::access::PublishEnvelope envelope;
  envelope.request_id = "req-001";
  const auto resolved_encoder =
      registry.resolve_encoder({.entry_type = "cli", .protocol_kind = "uds"});
  assert_true(static_cast<bool>(resolved_encoder),
              "registry should resolve encoder for registered binding");
  assert_true(resolved_encoder->encode(envelope),
              "resolved encoder should accept publish envelope");

  const auto bindings = registry.list_bindings();
  assert_equal(static_cast<std::size_t>(1), bindings.size(),
               "registry should expose one active binding after registration");
  assert_equal(std::string("apps/cli"), bindings.front().source_ref,
               "binding should preserve source ownership");

  const std::size_t removed_count = registry.revoke_source("apps/cli");
  assert_equal(static_cast<std::size_t>(1), removed_count,
               "revoke_source should remove all bindings owned by the source");
  assert_true(!registry.resolve_decoder("cli", "uds"),
              "revoked binding should no longer resolve");
}

}  // namespace

int main() {
  try {
    test_protocol_adapter_registry_registers_resolves_and_revokes_bindings();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << '\n';
    return 1;
  }

  return 0;
}
