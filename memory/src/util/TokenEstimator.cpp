#include "util/TokenEstimator.h"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_set>
#include <utility>
#include <vector>

#if defined(__linux__)
#include <unistd.h>
#endif

#include "emdedded_resource_reader.h"
#include "encoding.h"
#include "modelparams.h"

namespace dasall::memory::util {
namespace {

constexpr std::string_view kCl100kAssetName = "cl100k_base.tiktoken";

[[nodiscard]] int heuristic_estimate_text_tokens(std::string_view text) {
  if (text.empty()) {
    return 0;
  }

  int ascii_bytes = 0;
  int multibyte_characters = 0;

  for (const unsigned char byte : text) {
    if (byte < 0x80U) {
      ++ascii_bytes;
      continue;
    }

    if ((byte & 0xC0U) != 0x80U) {
      ++multibyte_characters;
    }
  }

  return std::max(1, ((ascii_bytes + 3) / 4) + (multibyte_characters * 2));
}

[[nodiscard]] std::shared_ptr<const ITokenEstimator> heuristic_estimator_singleton() {
  static const auto estimator = std::make_shared<HeuristicTokenEstimator>();
  return estimator;
}

[[nodiscard]] std::optional<std::filesystem::path> executable_dir() {
#if defined(__linux__)
  std::vector<char> buffer(4096, '\0');
  const auto byte_count = ::readlink("/proc/self/exe", buffer.data(), buffer.size() - 1U);
  if (byte_count <= 0) {
    return std::nullopt;
  }

  return std::filesystem::path(std::string(buffer.data(), static_cast<std::size_t>(byte_count)))
      .parent_path();
#else
  return std::nullopt;
#endif
}

[[nodiscard]] std::vector<std::filesystem::path> candidate_asset_roots() {
  std::vector<std::filesystem::path> roots;

  if (const char* env_dir = std::getenv("DASALL_MEMORY_TIKTOKEN_ASSET_DIR");
      env_dir != nullptr && env_dir[0] != '\0') {
    roots.emplace_back(env_dir);
  }

#ifdef DASALL_MEMORY_TIKTOKEN_BUILD_ASSET_DIR
  roots.emplace_back(DASALL_MEMORY_TIKTOKEN_BUILD_ASSET_DIR);
#endif

#ifdef DASALL_MEMORY_TIKTOKEN_INSTALL_ASSET_DIR
  roots.emplace_back(DASALL_MEMORY_TIKTOKEN_INSTALL_ASSET_DIR);
#endif

  if (const auto bin_dir = executable_dir(); bin_dir.has_value()) {
    roots.push_back((*bin_dir / "tokenizers").lexically_normal());
    roots.push_back((bin_dir->parent_path() / "share" / "dasall" / "memory" /
                     "tokenizers")
                        .lexically_normal());
  }

  std::error_code error_code;
  const auto current_dir = std::filesystem::current_path(error_code);
  if (!error_code) {
    roots.push_back((current_dir / "share" / "dasall" / "memory" / "tokenizers")
                        .lexically_normal());
  }

  std::vector<std::filesystem::path> deduplicated_roots;
  std::unordered_set<std::string> seen;
  for (auto& root : roots) {
    const auto normalized = root.lexically_normal();
    const auto key = normalized.string();
    if (key.empty() || !seen.insert(key).second) {
      continue;
    }
    deduplicated_roots.push_back(normalized);
  }

  return deduplicated_roots;
}

[[nodiscard]] std::optional<std::filesystem::path> resolve_asset_path(
    std::string_view asset_name) {
  const auto asset_file_name = std::string(asset_name);
  for (const auto& root : candidate_asset_roots()) {
    const auto direct_file = root;
    if (std::filesystem::is_regular_file(direct_file) &&
        direct_file.filename() == asset_file_name) {
      return direct_file;
    }

    const auto candidate = root / asset_file_name;
    if (std::filesystem::is_regular_file(candidate)) {
      return candidate;
    }
  }

  return std::nullopt;
}

class FileResourceReader final : public IResourceReader {
 public:
  explicit FileResourceReader(std::filesystem::path path)
      : path_(std::move(path)) {}

  std::vector<std::string> readLines() override {
    std::ifstream input(path_);
    if (!input.is_open()) {
      throw std::runtime_error("failed to open tokenizer asset: " + path_.string());
    }

    std::vector<std::string> lines;
    std::string line;
    while (std::getline(input, line)) {
      if (!line.empty() && line.back() == '\r') {
        line.pop_back();
      }
      lines.push_back(line);
    }

    return lines;
  }

 private:
  std::filesystem::path path_;
};

[[nodiscard]] std::shared_ptr<GptEncoding> load_cl100k_encoding() {
  const auto asset_path = resolve_asset_path(kCl100kAssetName);
  if (!asset_path.has_value()) {
    return nullptr;
  }

  try {
    FileResourceReader resource_reader(*asset_path);
    const auto params =
        ModelParamsGenerator::get_model_params(LanguageModel::CL100K_BASE, &resource_reader);
    return std::make_shared<GptEncoding>(
        params.pat_str(),
        params.mergeable_ranks(),
        params.special_tokens(),
        params.explicit_n_vocab());
  } catch (...) {
    return nullptr;
  }
}

class TiktokenTokenEstimator final : public ITokenEstimator {
 public:
  explicit TiktokenTokenEstimator(std::shared_ptr<GptEncoding> encoding)
      : encoding_(std::move(encoding)) {}

  [[nodiscard]] int estimate_text_tokens(std::string_view text) const override {
    if (text.empty()) {
      return 0;
    }

    try {
      std::scoped_lock lock(encode_mutex_);
      const auto tokens = encoding_->encode(std::string(text));
      return std::max(1, static_cast<int>(tokens.size()));
    } catch (...) {
      return heuristic_estimate_text_tokens(text);
    }
  }

 private:
  std::shared_ptr<GptEncoding> encoding_;
  mutable std::mutex encode_mutex_;
};

[[nodiscard]] std::shared_ptr<const ITokenEstimator> tiktoken_estimator_singleton() {
  static std::mutex cache_mutex;
  static std::weak_ptr<const ITokenEstimator> cached_estimator;

  std::scoped_lock lock(cache_mutex);
  if (const auto cached = cached_estimator.lock()) {
    return cached;
  }

  const auto encoding = load_cl100k_encoding();
  if (encoding == nullptr) {
    return nullptr;
  }

  const auto estimator =
      std::make_shared<TiktokenTokenEstimator>(std::move(encoding));
  cached_estimator = estimator;
  return estimator;
}

}  // namespace

int HeuristicTokenEstimator::estimate_text_tokens(std::string_view text) const {
  return heuristic_estimate_text_tokens(text);
}

std::shared_ptr<const ITokenEstimator> create_token_estimator(
    TokenEstimatorBackend backend) {
  if (backend == TokenEstimatorBackend::Tiktoken) {
    if (const auto estimator = tiktoken_estimator_singleton(); estimator != nullptr) {
      return estimator;
    }
  }

  return heuristic_estimator_singleton();
}

std::shared_ptr<const ITokenEstimator> create_token_estimator(
    const MemoryConfig& config) {
  return create_token_estimator(config.token_estimator);
}

int estimate_text_tokens(std::string_view text) {
  return heuristic_estimate_text_tokens(text);
}

}  // namespace dasall::memory::util