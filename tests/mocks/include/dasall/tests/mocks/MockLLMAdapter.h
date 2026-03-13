#pragma once

#include <functional>
#include <string>

namespace dasall::tests::mocks {

class MockLLMAdapter {
 public:
  using Handler = std::function<std::string(const std::string&)>;

  void set_handler(Handler handler) { handler_ = std::move(handler); }

  std::string invoke(const std::string& prompt) {
    ++call_count_;
    last_prompt_ = prompt;
    if (handler_) {
      return handler_(prompt);
    }
    return default_response_;
  }

  void set_default_response(std::string response) { default_response_ = std::move(response); }
  int call_count() const { return call_count_; }
  const std::string& last_prompt() const { return last_prompt_; }

 private:
  Handler handler_;
  std::string default_response_{"mock-llm-response"};
  std::string last_prompt_;
  int call_count_{0};
};

}  // namespace dasall::tests::mocks
