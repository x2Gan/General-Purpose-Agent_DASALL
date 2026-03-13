#include <exception>
#include <iostream>
#include <string>

#include "dasall/tests/mocks/MockLLMAdapter.h"
#include "dasall/tests/mocks/MockMemoryStore.h"
#include "dasall/tests/mocks/MockTool.h"
#include "dasall/tests/support/TestAssertions.h"

int main() {
  using dasall::tests::mocks::MockLLMAdapter;
  using dasall::tests::mocks::MockMemoryStore;
  using dasall::tests::mocks::MockTool;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  try {
    MockMemoryStore memory_store;
    memory_store.write("goal", "stage-a-runtime-smoke");

    MockLLMAdapter llm;
    llm.set_handler([&memory_store](const std::string& prompt) {
      return memory_store.read("goal") + ":" + prompt;
    });

    MockTool tool("diagnostic");
    tool.set_handler([](const std::string& input) {
      return "tool:" + input;
    });

    const std::string llm_result = llm.invoke("ping");
    const std::string tool_result = tool.execute(llm_result);

    assert_true(memory_store.contains("goal"), "memory store should contain goal key");
    assert_equal(1, llm.call_count(), "llm should be called once");
    assert_equal(1, tool.call_count(), "tool should be called once");
    assert_equal("stage-a-runtime-smoke:ping", llm_result, "llm result mismatch");
    assert_equal("tool:stage-a-runtime-smoke:ping", tool_result, "tool result mismatch");
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}
