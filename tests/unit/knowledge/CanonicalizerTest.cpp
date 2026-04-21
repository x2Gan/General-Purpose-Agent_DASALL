#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>

#include "ingest/Canonicalizer.h"
#include "support/TestAssertions.h"

namespace {

using dasall::knowledge::AuthorityLevel;
using dasall::knowledge::SourceFormat;
using dasall::knowledge::SourceKind;
using dasall::knowledge::ingest::Canonicalizer;
using dasall::knowledge::ingest::CanonicalizerPolicy;
using dasall::knowledge::ingest::SourceRecord;
using dasall::tests::support::assert_equal;
using dasall::tests::support::assert_true;

class TempDirectory {
 public:
  TempDirectory()
      : path_(std::filesystem::temp_directory_path() /
              "dasall-canonicalizer-yaml-test") {
    std::error_code error;
    std::filesystem::remove_all(path_, error);
    std::filesystem::create_directories(path_);
  }

  ~TempDirectory() {
    std::error_code error;
    std::filesystem::remove_all(path_, error);
  }

  [[nodiscard]] const std::filesystem::path& path() const {
    return path_;
  }

 private:
  std::filesystem::path path_;
};

void write_file(const std::filesystem::path& path, std::string_view content) {
  std::filesystem::create_directories(path.parent_path());
  std::ofstream output(path, std::ios::binary);
  output << content;
}

[[nodiscard]] SourceRecord make_yaml_source(std::string source_uri,
                                            std::string hash_seed) {
  SourceRecord source;
  source.source_id = std::string("profile_policy_normative::") + source_uri;
  source.corpus_id = "profile_policy_normative";
  source.source_uri = std::move(source_uri);
  source.content_hash = std::move(hash_seed);
  source.version = std::string("sha256:") + source.content_hash;
  source.updated_at_ms = 1713657600000;
  source.kind = SourceKind::ConfigSnapshot;
  source.format = SourceFormat::Yaml;
  source.authority_level = AuthorityLevel::Normative;
  source.language = "en";
  source.tags = {"profile", "runtime-policy"};
  return source;
}

void test_canonicalizer_flattens_runtime_policy_yaml_deterministically() {
  TempDirectory temp_directory;
  const auto repository_root = temp_directory.path();
  const auto yaml_path = repository_root / "profiles/desktop_full/runtime_policy.yaml";

  write_file(yaml_path,
             "runtime_budget:\n"
             "    max_latency_ms: 7000\n"
             "profile_meta:\n"
             "    profile_id: desktop_full\n"
             "prompt_policy:\n"
             "    allowed_prompt_releases:\n"
             "        - stable\n"
             "        - canary\n");

  Canonicalizer canonicalizer({.repository_root = repository_root});
  const auto first_result = canonicalizer.canonicalize(
      make_yaml_source("profiles/desktop_full/runtime_policy.yaml",
                       "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"));

  write_file(yaml_path,
             "prompt_policy:\n"
             "    allowed_prompt_releases:\n"
             "        - stable\n"
             "        - canary\n"
             "profile_meta:\n"
             "    profile_id: desktop_full\n"
             "runtime_budget:\n"
             "    max_latency_ms: 7000\n");

  const auto second_result = canonicalizer.canonicalize(
      make_yaml_source("profiles/desktop_full/runtime_policy.yaml",
                       "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"));

  assert_true(first_result.has_consistent_values() && first_result.ok,
              "Canonicalizer should produce a consistent yaml canonicalization result");
  assert_true(second_result.has_consistent_values() && second_result.ok,
              "Canonicalizer should keep yaml canonicalization deterministic across key reordering");
  assert_equal(first_result.document->canonical_text, second_result.document->canonical_text,
               "yaml canonical text should ignore mapping order differences");
  assert_equal(first_result.document->version, second_result.document->version,
               "yaml canonical version should be derived from canonical text, not raw key order");
  assert_equal(first_result.document->document_id, second_result.document->document_id,
               "document id should remain stable when yaml semantics do not change");
  assert_equal("desktop_full", first_result.document->metadata.at("profile_name"),
               "yaml canonicalization should derive profile_name from profile_meta.profile_id");
  assert_equal("runtime_policy", first_result.document->metadata.at("policy_domain"),
               "yaml canonicalization should stamp runtime_policy metadata");
  assert_true(first_result.document->canonical_text.find("runtime_budget.max_latency_ms=7000") != std::string::npos,
              "yaml canonicalization should flatten scalar keys into key=value lines");
}

}  // namespace

int main() {
  try {
    test_canonicalizer_flattens_runtime_policy_yaml_deterministically();
  } catch (const std::exception& exception) {
    std::cerr << exception.what() << '\n';
    return 1;
  }

  return 0;
}