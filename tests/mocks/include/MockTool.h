#pragma once

#include <functional>
#include <string>

namespace dasall::tests::mocks {

class MockTool {
 public:
  using Handler = std::function<std::string(const std::string&)>;

  explicit MockTool(std::string name) : name_(std::move(name)) {}

  void set_handler(Handler handler) { handler_ = std::move(handler); }

  std::string execute(const std::string& input) {
    ++call_count_;
    last_input_ = input;
    if (handler_) {
      return handler_(input);
    }
    return name_ + ":ok";
  }

  int call_count() const { return call_count_; }
  const std::string& last_input() const { return last_input_; }

 private:
  std::string name_;
  Handler handler_;
  std::string last_input_;
  int call_count_{0};
};

}  // namespace dasall::tests::mocks
