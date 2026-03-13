#pragma once

#include <stdexcept>
#include <string>

namespace dasall::tests::support {

inline void assert_true(bool condition, const std::string& message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

inline void assert_equal(const std::string& expected,
                         const std::string& actual,
                         const std::string& message) {
  if (expected != actual) {
    throw std::runtime_error(message + ": expected='" + expected + "' actual='" + actual + "'");
  }
}

inline void assert_equal(int expected, int actual, const std::string& message) {
  if (expected != actual) {
    throw std::runtime_error(message + ": expected=" + std::to_string(expected) +
                             " actual=" + std::to_string(actual));
  }
}

}  // namespace dasall::tests::support
