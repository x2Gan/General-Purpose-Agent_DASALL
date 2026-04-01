#pragma once

#include <algorithm>
#include <cstddef>
#include <span>
#include <string_view>
#include <vector>

namespace dasall::infra::secret {

class SecureBuffer {
 public:
  SecureBuffer() = default;

  explicit SecureBuffer(std::vector<std::byte> bytes, bool zeroize_on_release = true)
      : bytes_(std::move(bytes)), zeroize_on_release_(zeroize_on_release) {}

  SecureBuffer(const SecureBuffer&) = delete;
  SecureBuffer& operator=(const SecureBuffer&) = delete;
  SecureBuffer(SecureBuffer&&) noexcept = default;
  SecureBuffer& operator=(SecureBuffer&&) noexcept = default;

  ~SecureBuffer() {
    release();
  }

  [[nodiscard]] static SecureBuffer from_text_copy(std::string_view plaintext,
                                                   bool zeroize_on_release = true) {
    std::vector<std::byte> bytes;
    bytes.reserve(plaintext.size());
    for (const char character : plaintext) {
      bytes.push_back(static_cast<std::byte>(character));
    }

    return SecureBuffer(std::move(bytes), zeroize_on_release);
  }

  [[nodiscard]] bool is_accessible() const {
    return !released_;
  }

  [[nodiscard]] bool should_zeroize_on_release() const {
    return zeroize_on_release_;
  }

  [[nodiscard]] bool is_zeroized() const {
    return std::all_of(bytes_.begin(), bytes_.end(), [](std::byte value) {
      return value == std::byte{0};
    });
  }

  [[nodiscard]] std::size_t raw_size() const {
    return bytes_.size();
  }

  [[nodiscard]] std::size_t size() const {
    return released_ ? 0 : bytes_.size();
  }

  [[nodiscard]] std::span<const std::byte> bytes() const {
    if (released_) {
      return {};
    }

    return std::span<const std::byte>(bytes_.data(), bytes_.size());
  }

  void release() {
    if (released_) {
      return;
    }

    if (zeroize_on_release_) {
      std::fill(bytes_.begin(), bytes_.end(), std::byte{0});
    }

    released_ = true;
  }

 private:
  std::vector<std::byte> bytes_;
  bool zeroize_on_release_ = true;
  bool released_ = false;
};

}  // namespace dasall::infra::secret