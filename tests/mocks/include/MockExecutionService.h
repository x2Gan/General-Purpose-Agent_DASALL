#pragma once

#include <functional>
#include <string>

namespace dasall::tests::mocks {

class MockExecutionService {
 public:
  using Handler = std::function<bool(const std::string&)>;

  void set_handler(Handler handler) { handler_ = std::move(handler); }

  bool execute(const std::string& action) {
    ++call_count_;
    last_action_ = action;
    if (handler_) {
      return handler_(action);
    }
    return true;
  }

  int call_count() const { return call_count_; }
  const std::string& last_action() const { return last_action_; }

 private:
  Handler handler_;
  std::string last_action_;
  int call_count_{0};
};

}  // namespace dasall::tests::mocks
