#include "AsyncTaskRegistry.h"

#include <array>
#include <charconv>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

namespace dasall::access {

namespace {

constexpr std::size_t kSha256BlockBytes = 64U;
constexpr std::size_t kSha256DigestBytes = 32U;
constexpr char kBase64UrlAlphabet[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

using Sha256Digest = std::array<std::uint8_t, kSha256DigestBytes>;

constexpr std::array<std::uint32_t, 64U> kSha256RoundConstants = {
    0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U, 0x3956c25bU,
    0x59f111f1U, 0x923f82a4U, 0xab1c5ed5U, 0xd807aa98U, 0x12835b01U,
    0x243185beU, 0x550c7dc3U, 0x72be5d74U, 0x80deb1feU, 0x9bdc06a7U,
    0xc19bf174U, 0xe49b69c1U, 0xefbe4786U, 0x0fc19dc6U, 0x240ca1ccU,
    0x2de92c6fU, 0x4a7484aaU, 0x5cb0a9dcU, 0x76f988daU, 0x983e5152U,
    0xa831c66dU, 0xb00327c8U, 0xbf597fc7U, 0xc6e00bf3U, 0xd5a79147U,
    0x06ca6351U, 0x14292967U, 0x27b70a85U, 0x2e1b2138U, 0x4d2c6dfcU,
    0x53380d13U, 0x650a7354U, 0x766a0abbU, 0x81c2c92eU, 0x92722c85U,
    0xa2bfe8a1U, 0xa81a664bU, 0xc24b8b70U, 0xc76c51a3U, 0xd192e819U,
    0xd6990624U, 0xf40e3585U, 0x106aa070U, 0x19a4c116U, 0x1e376c08U,
    0x2748774cU, 0x34b0bcb5U, 0x391c0cb3U, 0x4ed8aa4aU, 0x5b9cca4fU,
    0x682e6ff3U, 0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U,
    0x90befffaU, 0xa4506cebU, 0xbef9a3f7U, 0xc67178f2U,
};

[[nodiscard]] constexpr std::uint32_t rotate_right(const std::uint32_t value,
                                                    const std::uint32_t shift) {
  return (value >> shift) | (value << (32U - shift));
}

[[nodiscard]] constexpr std::uint32_t sha256_choose(const std::uint32_t x,
                                                    const std::uint32_t y,
                                                    const std::uint32_t z) {
  return (x & y) ^ (~x & z);
}

[[nodiscard]] constexpr std::uint32_t sha256_majority(const std::uint32_t x,
                                                      const std::uint32_t y,
                                                      const std::uint32_t z) {
  return (x & y) ^ (x & z) ^ (y & z);
}

[[nodiscard]] constexpr std::uint32_t sha256_big_sigma0(const std::uint32_t value) {
  return rotate_right(value, 2U) ^ rotate_right(value, 13U) ^
         rotate_right(value, 22U);
}

[[nodiscard]] constexpr std::uint32_t sha256_big_sigma1(const std::uint32_t value) {
  return rotate_right(value, 6U) ^ rotate_right(value, 11U) ^
         rotate_right(value, 25U);
}

[[nodiscard]] constexpr std::uint32_t sha256_small_sigma0(const std::uint32_t value) {
  return rotate_right(value, 7U) ^ rotate_right(value, 18U) ^ (value >> 3U);
}

[[nodiscard]] constexpr std::uint32_t sha256_small_sigma1(const std::uint32_t value) {
  return rotate_right(value, 17U) ^ rotate_right(value, 19U) ^ (value >> 10U);
}

[[nodiscard]] std::int64_t current_unix_ms() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}

[[nodiscard]] std::vector<std::uint8_t> to_bytes(std::string_view text) {
  return std::vector<std::uint8_t>(text.begin(), text.end());
}

[[nodiscard]] Sha256Digest sha256_digest(const std::vector<std::uint8_t>& input) {
  std::vector<std::uint8_t> message = input;
  const std::uint64_t bit_length = static_cast<std::uint64_t>(message.size()) * 8U;
  message.push_back(0x80U);
  while ((message.size() % kSha256BlockBytes) != 56U) {
    message.push_back(0U);
  }
  for (int shift = 56; shift >= 0; shift -= 8) {
    message.push_back(static_cast<std::uint8_t>((bit_length >> shift) & 0xffU));
  }

  std::array<std::uint32_t, 8U> hash = {
      0x6a09e667U, 0xbb67ae85U, 0x3c6ef372U, 0xa54ff53aU,
      0x510e527fU, 0x9b05688cU, 0x1f83d9abU, 0x5be0cd19U,
  };

  std::array<std::uint32_t, 64U> schedule{};
  for (std::size_t offset = 0; offset < message.size(); offset += kSha256BlockBytes) {
    for (std::size_t index = 0; index < 16U; ++index) {
      const std::size_t base = offset + index * 4U;
      schedule[index] = (static_cast<std::uint32_t>(message[base]) << 24U) |
                        (static_cast<std::uint32_t>(message[base + 1U]) << 16U) |
                        (static_cast<std::uint32_t>(message[base + 2U]) << 8U) |
                        static_cast<std::uint32_t>(message[base + 3U]);
    }
    for (std::size_t index = 16U; index < schedule.size(); ++index) {
      schedule[index] = sha256_small_sigma1(schedule[index - 2U]) +
                        schedule[index - 7U] +
                        sha256_small_sigma0(schedule[index - 15U]) +
                        schedule[index - 16U];
    }

    std::uint32_t a = hash[0];
    std::uint32_t b = hash[1];
    std::uint32_t c = hash[2];
    std::uint32_t d = hash[3];
    std::uint32_t e = hash[4];
    std::uint32_t f = hash[5];
    std::uint32_t g = hash[6];
    std::uint32_t h = hash[7];

    for (std::size_t index = 0; index < schedule.size(); ++index) {
      const std::uint32_t temp1 = h + sha256_big_sigma1(e) +
                                  sha256_choose(e, f, g) +
                                  kSha256RoundConstants[index] + schedule[index];
      const std::uint32_t temp2 = sha256_big_sigma0(a) + sha256_majority(a, b, c);
      h = g;
      g = f;
      f = e;
      e = d + temp1;
      d = c;
      c = b;
      b = a;
      a = temp1 + temp2;
    }

    hash[0] += a;
    hash[1] += b;
    hash[2] += c;
    hash[3] += d;
    hash[4] += e;
    hash[5] += f;
    hash[6] += g;
    hash[7] += h;
  }

  Sha256Digest digest{};
  for (std::size_t index = 0; index < hash.size(); ++index) {
    digest[index * 4U] = static_cast<std::uint8_t>((hash[index] >> 24U) & 0xffU);
    digest[index * 4U + 1U] = static_cast<std::uint8_t>((hash[index] >> 16U) & 0xffU);
    digest[index * 4U + 2U] = static_cast<std::uint8_t>((hash[index] >> 8U) & 0xffU);
    digest[index * 4U + 3U] = static_cast<std::uint8_t>(hash[index] & 0xffU);
  }

  return digest;
}

[[nodiscard]] std::string base64url_encode(const std::vector<std::uint8_t>& bytes) {
  std::string encoded;
  encoded.reserve(((bytes.size() + 2U) / 3U) * 4U);

  for (std::size_t index = 0; index < bytes.size(); index += 3U) {
    const std::uint32_t first = bytes[index];
    const std::uint32_t second = index + 1U < bytes.size() ? bytes[index + 1U] : 0U;
    const std::uint32_t third = index + 2U < bytes.size() ? bytes[index + 2U] : 0U;
    const std::uint32_t chunk = (first << 16U) | (second << 8U) | third;

    encoded.push_back(kBase64UrlAlphabet[(chunk >> 18U) & 0x3fU]);
    encoded.push_back(kBase64UrlAlphabet[(chunk >> 12U) & 0x3fU]);
    if (index + 1U < bytes.size()) {
      encoded.push_back(kBase64UrlAlphabet[(chunk >> 6U) & 0x3fU]);
    }
    if (index + 2U < bytes.size()) {
      encoded.push_back(kBase64UrlAlphabet[chunk & 0x3fU]);
    }
  }

  return encoded;
}

[[nodiscard]] std::optional<std::vector<std::uint8_t>> base64url_decode(
    std::string_view text) {
  std::array<int, 256U> decode_table{};
  decode_table.fill(-1);
  for (int index = 0; index < 64; ++index) {
    decode_table[static_cast<unsigned char>(kBase64UrlAlphabet[index])] = index;
  }

  std::vector<std::uint8_t> decoded;
  decoded.reserve((text.size() * 3U) / 4U);
  std::uint32_t accumulator = 0U;
  int bits_collected = 0;
  for (const char character : text) {
    const int value = decode_table[static_cast<unsigned char>(character)];
    if (value < 0) {
      return std::nullopt;
    }
    accumulator = (accumulator << 6U) | static_cast<std::uint32_t>(value);
    bits_collected += 6;
    if (bits_collected >= 8) {
      bits_collected -= 8;
      decoded.push_back(
          static_cast<std::uint8_t>((accumulator >> bits_collected) & 0xffU));
    }
  }
  if (bits_collected >= 6) {
    return std::nullopt;
  }
  return decoded;
}

[[nodiscard]] std::string base64url_encode_text(std::string_view text) {
  return base64url_encode(to_bytes(text));
}

[[nodiscard]] std::optional<std::string> base64url_decode_text(std::string_view text) {
  const auto decoded = base64url_decode(text);
  if (!decoded.has_value()) {
    return std::nullopt;
  }
  return std::string(decoded->begin(), decoded->end());
}

[[nodiscard]] std::string hmac_sha256_base64url(std::string_view key,
                                                std::string_view message) {
  std::array<std::uint8_t, kSha256BlockBytes> normalized_key{};
  const auto key_bytes = to_bytes(key);
  if (key_bytes.size() > kSha256BlockBytes) {
    const auto digested_key = sha256_digest(key_bytes);
    for (std::size_t index = 0; index < digested_key.size(); ++index) {
      normalized_key[index] = digested_key[index];
    }
  } else {
    for (std::size_t index = 0; index < key_bytes.size(); ++index) {
      normalized_key[index] = key_bytes[index];
    }
  }

  std::vector<std::uint8_t> inner(kSha256BlockBytes + message.size());
  std::vector<std::uint8_t> outer(kSha256BlockBytes + kSha256DigestBytes);
  for (std::size_t index = 0; index < kSha256BlockBytes; ++index) {
    inner[index] = static_cast<std::uint8_t>(normalized_key[index] ^ 0x36U);
    outer[index] = static_cast<std::uint8_t>(normalized_key[index] ^ 0x5cU);
  }
  for (std::size_t index = 0; index < message.size(); ++index) {
    inner[kSha256BlockBytes + index] = static_cast<std::uint8_t>(message[index]);
  }

  const auto inner_digest = sha256_digest(inner);
  for (std::size_t index = 0; index < inner_digest.size(); ++index) {
    outer[kSha256BlockBytes + index] = inner_digest[index];
  }

  const auto digest = sha256_digest(outer);
  return base64url_encode(std::vector<std::uint8_t>(digest.begin(), digest.end()));
}

[[nodiscard]] std::optional<std::int64_t> parse_int64(std::string_view text) {
  std::int64_t value = 0;
  const auto* begin = text.data();
  const auto* end = begin + text.size();
  const auto [ptr, ec] = std::from_chars(begin, end, value);
  if (ec != std::errc{} || ptr != end) {
    return std::nullopt;
  }
  return value;
}

struct ParsedOwnershipToken {
  std::string version;
  std::string key_id;
  std::int64_t issued_at_unix_ms = 0;
  std::int64_t expires_at_unix_ms = 0;
  std::string receipt_id;
  std::string actor_ref;
  std::string request_id;
  std::string signature;
  std::string signed_payload;
};

[[nodiscard]] std::optional<ParsedOwnershipToken> parse_ownership_token(
    std::string_view token) {
  std::array<std::string_view, 8U> parts{};
  std::size_t part_index = 0U;
  std::size_t start = 0U;
  for (std::size_t index = 0; index <= token.size(); ++index) {
    if (index == token.size() || token[index] == '.') {
      if (part_index >= parts.size()) {
        return std::nullopt;
      }
      parts[part_index++] = token.substr(start, index - start);
      start = index + 1U;
    }
  }
  if (part_index != parts.size()) {
    return std::nullopt;
  }

  const auto key_id = base64url_decode_text(parts[1]);
  const auto receipt_id = base64url_decode_text(parts[4]);
  const auto actor_ref = base64url_decode_text(parts[5]);
  const auto request_id = base64url_decode_text(parts[6]);
  const auto issued_at = parse_int64(parts[2]);
  const auto expires_at = parse_int64(parts[3]);
  if (!key_id.has_value() || !receipt_id.has_value() || !actor_ref.has_value() ||
      !request_id.has_value() || !issued_at.has_value() || !expires_at.has_value()) {
    return std::nullopt;
  }

  ParsedOwnershipToken parsed;
  parsed.version = std::string(parts[0]);
  parsed.key_id = *key_id;
  parsed.issued_at_unix_ms = *issued_at;
  parsed.expires_at_unix_ms = *expires_at;
  parsed.receipt_id = *receipt_id;
  parsed.actor_ref = *actor_ref;
  parsed.request_id = *request_id;
  parsed.signature = std::string(parts[7]);
  parsed.signed_payload = std::string(token.substr(0U, token.rfind('.')));
  return parsed;
}

[[nodiscard]] std::string build_token_payload(std::string_view key_id,
                                              const std::int64_t issued_at_unix_ms,
                                              const std::int64_t expires_at_unix_ms,
                                              std::string_view receipt_id,
                                              std::string_view actor_ref,
                                              std::string_view request_id) {
  return std::string("v1.") + base64url_encode_text(key_id) + "." +
         std::to_string(issued_at_unix_ms) + "." +
         std::to_string(expires_at_unix_ms) + "." +
         base64url_encode_text(receipt_id) + "." +
         base64url_encode_text(actor_ref) + "." +
         base64url_encode_text(request_id);
}

[[nodiscard]] std::string context_or_default(
    const RuntimeDispatchRequest& request,
    const std::string& key,
    const std::string& fallback) {
  const auto it = request.request_context.find(key);
  if (it == request.request_context.end() || it->second.empty()) {
    return fallback;
  }

  return it->second;
}

}  // namespace

AsyncTaskRegistry::AsyncTaskRegistry(
    std::string ownership_secret,
    const std::chrono::milliseconds receipt_ttl)
    : AsyncTaskRegistry(OwnershipTokenKey{.key_id = "legacy-v1",
                                          .secret = std::move(ownership_secret)},
                        std::nullopt,
                        receipt_ttl) {}

AsyncTaskRegistry::AsyncTaskRegistry(
    OwnershipTokenKey current_key,
    std::optional<OwnershipTokenKey> previous_key,
    const std::chrono::milliseconds receipt_ttl)
    : receipt_ttl_(receipt_ttl),
      current_key_(std::move(current_key)),
      previous_key_(std::move(previous_key)) {
  if (!current_key_.is_valid()) {
    current_key_ = {};
  }
  if (previous_key_.has_value() && !previous_key_->is_valid()) {
    previous_key_.reset();
  }

  if (receipt_ttl_ <= std::chrono::milliseconds::zero()) {
    receipt_ttl_ = std::chrono::minutes(10);
  }
}

std::optional<AsyncTaskReceipt> AsyncTaskRegistry::register_async_accept(
    const RuntimeDispatchRequest& request,
    const RuntimeDispatchResult& result) {
  if (result.disposition != AccessDisposition::AcceptedAsync) {
    return std::nullopt;
  }

  std::lock_guard<std::mutex> lock(mutex_);
  if (!current_key_.is_valid()) {
    return std::nullopt;
  }

  const auto receipt_id = build_receipt_id(request, result);
  const auto request_id = context_or_default(request, "request_id", request.packet.packet_id);
  const auto session_id = context_or_default(request, "session_id", "session:" + request_id);
  const auto issued_at_unix_ms = current_unix_ms();
  const auto expires_at_unix_ms = issued_at_unix_ms + receipt_ttl_.count();

  AsyncTaskReceipt receipt;
  receipt.receipt_id = receipt_id;
  receipt.request_id = request_id;
  receipt.session_id = session_id;
  receipt.actor_ref = request.subject_identity.actor_ref;
  receipt.task_ref = result.receipt_ref.value_or("task:" + request_id);
  receipt.expires_at = std::chrono::steady_clock::now() + receipt_ttl_;
  receipt.ownership_token = build_ownership_token(
      receipt.receipt_id,
      receipt.actor_ref,
      receipt.request_id,
      issued_at_unix_ms,
      expires_at_unix_ms);
  receipt.initial_status = std::string("pending");
  receipts_[receipt.receipt_id] = StoredReceipt{
      .receipt = receipt,
      .task_status = "pending",
  };

  return receipt;
}

AsyncTaskRegistry::QueryResult AsyncTaskRegistry::query_receipt(
    const std::string& receipt_id) {
  std::lock_guard<std::mutex> lock(mutex_);

  const auto it = receipts_.find(receipt_id);
  if (it == receipts_.end()) {
    return QueryResult{.status = QueryStatus::NotFound, .receipt = std::nullopt};
  }

  const auto now = std::chrono::steady_clock::now();
  if (now >= it->second.receipt.expires_at) {
    erase_expired_locked(receipt_id);
    return QueryResult{.status = QueryStatus::Expired, .receipt = std::nullopt};
  }

  return QueryResult{.status = QueryStatus::Found, .receipt = it->second.receipt};
}

bool AsyncTaskRegistry::validate_ownership(
    const std::string& receipt_id,
    const std::string_view actor_ref,
    const std::string_view ownership_token) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!current_key_.is_valid()) {
    return false;
  }
  const auto removed = prune_expired_locked(std::chrono::steady_clock::now());
  (void)removed;

  const auto it = receipts_.find(receipt_id);
  if (it == receipts_.end()) {
    return false;
  }

  const auto now = std::chrono::steady_clock::now();
  if (now >= it->second.receipt.expires_at) {
    erase_expired_locked(receipt_id);
    return false;
  }

  if (it->second.receipt.actor_ref != actor_ref) {
    return false;
  }

  const auto parsed_token = parse_ownership_token(ownership_token);
  if (!parsed_token.has_value() || parsed_token->version != "v1" ||
      parsed_token->receipt_id != it->second.receipt.receipt_id ||
      parsed_token->actor_ref != it->second.receipt.actor_ref ||
      parsed_token->request_id != it->second.receipt.request_id ||
      parsed_token->issued_at_unix_ms <= 0 ||
      parsed_token->expires_at_unix_ms < parsed_token->issued_at_unix_ms ||
      current_unix_ms() > parsed_token->expires_at_unix_ms) {
    return false;
  }

  const OwnershipTokenKey* signing_key = nullptr;
  if (parsed_token->key_id == current_key_.key_id) {
    signing_key = &current_key_;
  } else if (previous_key_.has_value() && parsed_token->key_id == previous_key_->key_id) {
    signing_key = &*previous_key_;
  }
  if (signing_key == nullptr || !signing_key->is_valid()) {
    return false;
  }

  const auto expected_signature = hmac_sha256_base64url(
      signing_key->secret,
      parsed_token->signed_payload);

  // 采用常量时序比较，避免通过提前返回暴露 token 差异位置。
  return constant_time_equals(expected_signature, parsed_token->signature) &&
         constant_time_equals(it->second.receipt.ownership_token, ownership_token);
}

bool AsyncTaskRegistry::mark_completed(
    const std::string& receipt_id,
    const std::string_view task_status) {
  std::lock_guard<std::mutex> lock(mutex_);
  const auto removed = prune_expired_locked(std::chrono::steady_clock::now());
  (void)removed;

  const auto it = receipts_.find(receipt_id);
  if (it == receipts_.end()) {
    return false;
  }

  const auto now = std::chrono::steady_clock::now();
  if (now >= it->second.receipt.expires_at) {
    erase_expired_locked(receipt_id);
    return false;
  }

  it->second.task_status = std::string(task_status);
  it->second.receipt.initial_status = it->second.task_status;
  return true;
}

std::size_t AsyncTaskRegistry::prune_expired() {
  std::lock_guard<std::mutex> lock(mutex_);
  return prune_expired_locked(std::chrono::steady_clock::now());
}

std::size_t AsyncTaskRegistry::size() const {
  std::lock_guard<std::mutex> lock(mutex_);
  const auto removed = const_cast<AsyncTaskRegistry*>(this)->prune_expired_locked(
      std::chrono::steady_clock::now());
  (void)removed;
  return receipts_.size();
}

bool AsyncTaskRegistry::enabled() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return current_key_.is_valid();
}

void AsyncTaskRegistry::rotate_ownership_keys(
    OwnershipTokenKey current_key,
    std::optional<OwnershipTokenKey> previous_key) {
  std::lock_guard<std::mutex> lock(mutex_);
  current_key_ = current_key.is_valid() ? std::move(current_key) : OwnershipTokenKey{};
  previous_key_ = (previous_key.has_value() && previous_key->is_valid())
                      ? std::move(previous_key)
                      : std::nullopt;
}

std::string AsyncTaskRegistry::build_receipt_id(
    const RuntimeDispatchRequest& request,
    const RuntimeDispatchResult& result) {
  if (result.receipt_ref.has_value() && !result.receipt_ref->empty()) {
    return *result.receipt_ref;
  }

  const auto request_id = context_or_default(request, "request_id", request.packet.packet_id);
  return "receipt:" + request_id;
}

std::string AsyncTaskRegistry::build_ownership_token(
    const std::string_view receipt_id,
    const std::string_view actor_ref,
    const std::string_view request_id,
    const std::int64_t issued_at_unix_ms,
    const std::int64_t expires_at_unix_ms) const {
  if (!current_key_.is_valid()) {
    return {};
  }

  const std::string payload = build_token_payload(current_key_.key_id,
                                                  issued_at_unix_ms,
                                                  expires_at_unix_ms,
                                                  receipt_id,
                                                  actor_ref,
                                                  request_id);
  return payload + "." + hmac_sha256_base64url(current_key_.secret, payload);
}

bool AsyncTaskRegistry::constant_time_equals(
    const std::string_view lhs,
    const std::string_view rhs) {
  const std::size_t max_size = lhs.size() > rhs.size() ? lhs.size() : rhs.size();
  std::uint8_t diff = static_cast<std::uint8_t>(lhs.size() ^ rhs.size());

  for (std::size_t index = 0; index < max_size; ++index) {
    const std::uint8_t left = index < lhs.size()
                                  ? static_cast<std::uint8_t>(lhs[index])
                                  : static_cast<std::uint8_t>(0U);
    const std::uint8_t right = index < rhs.size()
                                   ? static_cast<std::uint8_t>(rhs[index])
                                   : static_cast<std::uint8_t>(0U);
    diff = static_cast<std::uint8_t>(diff | static_cast<std::uint8_t>(left ^ right));
  }

  return diff == 0U;
}

void AsyncTaskRegistry::erase_expired_locked(const std::string& receipt_id) {
  receipts_.erase(receipt_id);
}

std::size_t AsyncTaskRegistry::prune_expired_locked(
    const std::chrono::steady_clock::time_point now) {
  std::size_t removed = 0U;
  for (auto it = receipts_.begin(); it != receipts_.end();) {
    if (now >= it->second.receipt.expires_at) {
      it = receipts_.erase(it);
      ++removed;
      continue;
    }

    ++it;
  }

  return removed;
}

}  // namespace dasall::access
