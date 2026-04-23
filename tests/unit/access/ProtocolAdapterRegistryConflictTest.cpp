#include <exception>
#include <iostream>
#include <memory>

#include "ProtocolAdapterRegistry.h"
#include "support/TestAssertions.h"

namespace {

class ConflictStubProtocolAdapter final : public dasall::access::IProtocolAdapter {
 public:
  bool can_handle(const std::string_view,
                  const std::string_view) const override {
    return true;
  }

  dasall::access::InboundPacket decode() override {
    return {};
  }

  bool encode(const dasall::access::PublishEnvelope&) override {
    return true;
  }
};

void test_protocol_adapter_registry_rejects_duplicate_binding_conflicts() {
  using dasall::access::ProtocolAdapterRegistry;
  using dasall::tests::support::assert_true;

  ProtocolAdapterRegistry registry;

  const auto first_adapter = std::make_shared<ConflictStubProtocolAdapter>();
  const auto second_adapter = std::make_shared<ConflictStubProtocolAdapter>();

  assert_true(registry.register_adapter("apps/cli", "cli", "uds", first_adapter),
              "first binding registration should succeed");
  assert_true(!registry.register_adapter("apps/daemon", "cli", "uds", second_adapter),
              "registry should reject a second source claiming the same binding");
}

void test_protocol_adapter_registry_rejects_invalid_registration_inputs() {
  using dasall::access::ProtocolAdapterRegistry;
  using dasall::tests::support::assert_true;

  ProtocolAdapterRegistry registry;
  const auto adapter = std::make_shared<ConflictStubProtocolAdapter>();

  assert_true(!registry.register_adapter("", "cli", "uds", adapter),
              "registry should reject empty source ownership");
  assert_true(!registry.register_adapter("apps/cli", "", "uds", adapter),
              "registry should reject empty entry_type");
  assert_true(!registry.register_adapter("apps/cli", "cli", "", adapter),
              "registry should reject empty protocol_kind");
  assert_true(!registry.register_adapter("apps/cli", "cli", "uds", nullptr),
              "registry should reject null adapters");
}

}  // namespace

int main() {
  try {
    test_protocol_adapter_registry_rejects_duplicate_binding_conflicts();
    test_protocol_adapter_registry_rejects_invalid_registration_inputs();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << '\n';
    return 1;
  }

  return 0;
}
