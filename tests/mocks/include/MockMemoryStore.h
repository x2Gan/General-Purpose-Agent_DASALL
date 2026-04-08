#pragma once

#include <string>
#include <unordered_map>

namespace dasall::tests::mocks {

class MockMemoryStore {
 public:
  void write(const std::string& key, const std::string& value) {
    data_[key] = value;
  }

  std::string read(const std::string& key) const {
    auto it = data_.find(key);
    return it == data_.end() ? std::string{} : it->second;
  }

  bool contains(const std::string& key) const {
    return data_.find(key) != data_.end();
  }

  std::size_t size() const { return data_.size(); }

 private:
  std::unordered_map<std::string, std::string> data_;
};

}  // namespace dasall::tests::mocks
