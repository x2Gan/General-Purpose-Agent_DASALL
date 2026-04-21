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
  descriptor.source_kind = SourceKind::File;
  descriptor.allowed_formats = {SourceFormat::Markdown};
  descriptor.include_globs = {"*.md"};
  descriptor.exclude_globs = {"*评审报告*.md"};
  descriptor.supported_modes = std::move(supported_modes);
  descriptor.active_snapshot_id = std::move(active_snapshot_id);
  descriptor.last_updated_ms = 1713657600000;
  descriptor.tags = std::move(tags);
  descriptor.metadata = {
      {"baseline_class", "trusted_corpus"},
      {"owner_module", "knowledge"},
      {"refresh_strategy", "lazy_manifest_refresh"},
      {"default_language", "zh-CN"},
  };
  return descriptor;
}

void test_corpus_catalog_supports_lookup_and_route_filters() {
  CorpusCatalog catalog;
  assert_true(catalog.replace_all({
                  make_descriptor("adr_normative", "ADR Normative", "docs/adr/", "snapshot-adr-v1",
                                  {"normative", "architecture"},
                                  {RetrievalMode::LexicalOnly, RetrievalMode::Hybrid},
                                  AuthorityLevel::Normative),
                  make_descriptor("ssot_normative", "SSOT Normative", "docs/ssot/",
                                  "snapshot-ssot-v1", {"normative", "ssot"},
                                  {RetrievalMode::LexicalOnly}, AuthorityLevel::Normative),
                  make_descriptor("architecture_reference", "Architecture Reference",
                                  "docs/architecture/", "snapshot-architecture-v1",
                                  {"reference", "architecture"},
                                  {RetrievalMode::LexicalOnly, RetrievalMode::Hybrid},
                                  AuthorityLevel::Reference),
              }),
              "corpus catalog should accept a consistent route metadata baseline");

  const auto snapshot = catalog.snapshot();
  assert_true(snapshot.has_consistent_values(),
              "catalog snapshot should remain internally consistent after bootstrap");
  assert_equal(3, static_cast<int>(snapshot.size()),
               "catalog snapshot should expose all bootstrapped corpora");

  const auto adr_descriptor = snapshot.find_by_id("adr_normative");
  assert_true(adr_descriptor.has_value(), "catalog snapshot should find corpus descriptors by id");
  assert_equal("snapshot-adr-v1", adr_descriptor->active_snapshot_id,
               "catalog lookup should surface the current active snapshot id");

  const auto normative_matches = snapshot.filter_by_tags({"normative"});
  assert_equal(2, static_cast<int>(normative_matches.size()),
               "catalog tag filter should return all corpora carrying the requested route tag");

  const auto architecture_reference_matches =
      snapshot.filter_by_tags({"architecture", "reference"});
  assert_equal(1, static_cast<int>(architecture_reference_matches.size()),
               "catalog tag filter should require every requested tag to be present");
  assert_equal("architecture_reference", architecture_reference_matches.front().corpus_id,
               "catalog tag filter should preserve the matching corpus descriptor");

  const auto hybrid_matches = snapshot.filter_by_mode(RetrievalMode::Hybrid);
  assert_equal(2, static_cast<int>(hybrid_matches.size()),
               "catalog mode filter should expose all corpora that advertise hybrid support");
}

}  // namespace

int main() {
  try {
    test_corpus_catalog_supports_lookup_and_route_filters();
  } catch (const std::exception& exception) {
    std::cerr << exception.what() << '\n';
    return 1;
  }

  return 0;
}