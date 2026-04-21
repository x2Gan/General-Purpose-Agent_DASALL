#include <exception>
#include <iostream>
#include <string>
#include <vector>

#include "index/CorpusCatalog.h"
#include "support/TestAssertions.h"

namespace {

using dasall::knowledge::AuthorityLevel;
using dasall::knowledge::CorpusDescriptor;
using dasall::knowledge::RetrievalMode;
using dasall::knowledge::SourceFormat;
using dasall::knowledge::SourceKind;
using dasall::knowledge::TrustLevel;
using dasall::knowledge::index::CorpusCatalog;
using dasall::tests::support::assert_equal;
using dasall::tests::support::assert_true;

[[nodiscard]] CorpusDescriptor make_descriptor(std::string corpus_id,
                                              std::string display_name,
                                              std::string source_uri,
                                              std::string active_snapshot_id,
                                              std::vector<std::string> tags,
                                              std::vector<RetrievalMode> supported_modes,
                                              AuthorityLevel authority_level) {
  CorpusDescriptor descriptor;
  descriptor.corpus_id = std::move(corpus_id);
  descriptor.display_name = std::move(display_name);
  descriptor.source_uri = std::move(source_uri);
  descriptor.trust_level = TrustLevel::Trusted;
  descriptor.authority_level = authority_level;
  descriptor.source_kind = SourceKind::ConfigSnapshot;
  descriptor.allowed_formats = {SourceFormat::Yaml};
  descriptor.include_globs = {"*/runtime_policy.yaml"};
  descriptor.supported_modes = std::move(supported_modes);
  descriptor.active_snapshot_id = std::move(active_snapshot_id);
  descriptor.last_updated_ms = 1713657600000;
  descriptor.tags = std::move(tags);
  descriptor.metadata = {
      {"baseline_class", "trusted_corpus"},
      {"owner_module", "knowledge"},
      {"refresh_strategy", "lazy_manifest_refresh"},
      {"default_language", "und"},
  };
  return descriptor;
}

void test_corpus_catalog_exposes_empty_but_consistent_cold_start_snapshot() {
  CorpusCatalog catalog;

  const auto cold_start_snapshot = catalog.snapshot();
  assert_true(cold_start_snapshot.empty(),
              "default-constructed corpus catalog should expose an empty cold-start snapshot");
  assert_true(cold_start_snapshot.has_consistent_values(),
              "empty cold-start snapshot should still be internally consistent");
  assert_equal(0, static_cast<int>(cold_start_snapshot.list_all().size()),
               "cold-start snapshot should not expose any corpora before bootstrap");
  assert_true(!cold_start_snapshot.find_by_id("profile_policy_normative").has_value(),
              "cold-start snapshot lookup should return no descriptor for unknown corpora");
  assert_equal(0, static_cast<int>(cold_start_snapshot.filter_by_tags({"normative"}).size()),
               "cold-start snapshot tag filtering should gracefully return no matches");
  assert_equal(0, static_cast<int>(cold_start_snapshot.filter_by_mode(RetrievalMode::Hybrid).size()),
               "cold-start snapshot mode filtering should gracefully return no matches");

  assert_true(catalog.replace_all({make_descriptor("profile_policy_normative",
                                                   "Profile Policy Normative", "profiles/",
                                                   "snapshot-profile-v1",
                                                   {"normative", "profile"},
                                                   {RetrievalMode::LexicalOnly},
                                                   AuthorityLevel::Normative)}),
              "cold-start catalog should bootstrap once the first trusted descriptor arrives");

  const auto bootstrapped_snapshot = catalog.snapshot();
  assert_equal(1, static_cast<int>(bootstrapped_snapshot.size()),
               "bootstrapped catalog should expose the first active corpus descriptor");
  assert_true(bootstrapped_snapshot.find_by_id("profile_policy_normative").has_value(),
              "bootstrapped catalog should serve the newly loaded profile corpus descriptor");
}

}  // namespace

int main() {
  try {
    test_corpus_catalog_exposes_empty_but_consistent_cold_start_snapshot();
  } catch (const std::exception& exception) {
    std::cerr << exception.what() << '\n';
    return 1;
  }

  return 0;
}