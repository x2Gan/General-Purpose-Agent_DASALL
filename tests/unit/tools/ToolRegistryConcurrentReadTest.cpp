#include <atomic>
#include <barrier>
#include <exception>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "registry/ToolRegistry.h"
#include "support/TestAssertions.h"

namespace {

using dasall::tests::support::assert_true;

[[nodiscard]] dasall::contracts::ToolDescriptor make_terminal_descriptor() {
  return dasall::contracts::ToolDescriptor{
      .tool_name = std::string("agent.terminal"),
      .display_name = std::string("Agent Terminal"),
      .category = dasall::contracts::ToolCategory::Action,
      .capability_tier = dasall::contracts::ToolCapabilityTier::Preview,
      .is_read_only = false,
      .supports_compensation = false,
      .default_timeout_ms = 30000U,
      .input_schema_ref = std::string("schema://tools/agent.terminal/input/v1"),
      .output_schema_ref = std::string("schema://tools/agent.terminal/output/v1"),
      .required_scopes = std::vector<std::string>{"tools.execute"},
      .tags = std::vector<std::string>{"builtin", "terminal"},
      .version = std::string("1.0.0"),
  };
}

[[nodiscard]] dasall::tools::mcp::MCPToolBinding make_loopback_binding(int revision) {
  return dasall::tools::mcp::MCPToolBinding{
      .internal_tool_name = std::string("agent.terminal"),
      .remote_tool_name = std::string("terminal.remote.") + std::to_string(revision),
      .server_id = std::string("loopback"),
      .remote_capability_id = std::string("capability://loopback/terminal"),
      .input_schema_ref = std::string("schema://mcp/terminal/input/v1"),
  };
}

void record_failure(std::atomic<bool>& failed,
                    std::mutex& failure_mutex,
                    std::string& failure_message,
                    const std::string& message) {
  if (!failed.exchange(true)) {
    std::lock_guard<std::mutex> guard(failure_mutex);
    failure_message = message;
  }
}

void test_snapshot_reads_remain_consistent_while_writes_publish_new_snapshots() {
  using dasall::tools::registry::ToolRegistry;

  ToolRegistry registry(std::vector<dasall::contracts::ToolDescriptor>{});
  assert_true(registry.register_builtin(make_terminal_descriptor()),
              "precondition: concurrent-read test requires a builtin terminal descriptor");

  constexpr int kReaderCount = 4;
  constexpr int kReadIterations = 200;
  constexpr int kWriteIterations = 120;
  std::barrier start_gate(kReaderCount + 1);
  std::atomic<bool> failed = false;
  std::mutex failure_mutex;
  std::string failure_message;
  std::vector<std::thread> readers;
  readers.reserve(kReaderCount);

  for (int reader_index = 0; reader_index < kReaderCount; ++reader_index) {
    readers.emplace_back([&registry, &start_gate, &failed, &failure_mutex, &failure_message]() {
      start_gate.arrive_and_wait();

      for (int iteration = 0; iteration < kReadIterations && !failed.load(); ++iteration) {
        const auto descriptor = registry.resolve_descriptor("agent.terminal");
        if (!descriptor.has_value()) {
          record_failure(failed, failure_mutex, failure_message,
                         "resolve_descriptor lost the builtin descriptor during concurrent reads");
          return;
        }

        const auto bindings = registry.list_mcp_bindings("agent.terminal");
        if (bindings.size() > 1U) {
          record_failure(failed, failure_mutex, failure_message,
                         "list_mcp_bindings observed more than one binding for a single source-scoped batch");
          return;
        }

        if (!bindings.empty() &&
            (bindings.front().internal_tool_name != "agent.terminal" ||
             bindings.front().server_id != "loopback")) {
          record_failure(failed, failure_mutex, failure_message,
                         "list_mcp_bindings observed a partially published binding record");
          return;
        }

        const auto current_snapshot = registry.snapshot();
        if (!current_snapshot || current_snapshot->descriptors_by_name.empty()) {
          record_failure(failed, failure_mutex, failure_message,
                         "snapshot should always expose a non-empty descriptor catalog");
          return;
        }
      }
    });
  }

  std::thread writer([&registry, &start_gate, &failed, &failure_mutex, &failure_message]() {
    start_gate.arrive_and_wait();

    for (int iteration = 0; iteration < kWriteIterations && !failed.load(); ++iteration) {
      if ((iteration % 2) == 0) {
        if (!registry.upsert_mcp_bindings("plugin:loopback", {make_loopback_binding(iteration)})) {
          record_failure(failed, failure_mutex, failure_message,
                         "upsert_mcp_bindings should publish valid loopback batches");
          return;
        }
      } else if (!registry.revoke_source("plugin:loopback")) {
        record_failure(failed, failure_mutex, failure_message,
                       "revoke_source should remove the loopback batch after it was published");
        return;
      }
    }
  });

  for (auto& reader : readers) {
    reader.join();
  }
  writer.join();

  if (failed.load()) {
    throw std::runtime_error(failure_message);
  }

  const auto final_descriptor = registry.resolve_descriptor("agent.terminal");
  assert_true(final_descriptor.has_value(),
              "concurrent publish cycles must not revoke the builtin descriptor");
  assert_true(registry.snapshot()->revision >= static_cast<std::uint64_t>(kWriteIterations / 2),
              "repeated publishes should advance the registry snapshot revision");
}

}  // namespace

int main() {
  try {
    test_snapshot_reads_remain_consistent_while_writes_publish_new_snapshots();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}