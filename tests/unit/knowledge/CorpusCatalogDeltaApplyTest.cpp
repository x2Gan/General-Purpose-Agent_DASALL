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
using dasall::knowledge::index::CorpusCatalogDelta;
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

void test_corpus_catalog_applies_delta_without_exposing_partial_state() {
  CorpusCatalog catalog;
  assert_true(catalog.replace_all({
                  make_descriptor("adr_normative", "ADR Normative", "docs/adr/", "snapshot-adr-v1",
                                  {"normative", "architecture"},
                                  {RetrievalMode::LexicalOnly, RetrievalMode::Hybrid},
                                  AuthorityLevel::Normative),
                  make_descriptor("ssot_normative", "SSOT Normative", "docs/ssot/",
                                  "snapshot-ssot-v1", {"normative", "ssot"},
                                  {RetrievalMode::LexicalOnly}, AuthorityLevel::Normative),
              }),
              "catalog baseline should load before delta application");

  CorpusCatalogDelta delta;
  delta.upserted_descriptors = {
      make_descriptor("adr_normative", "ADR Normative", "docs/adr/", "snapshot-adr-v2",
                      {"normative", "architecture", "adr"},
                      {RetrievalMode::LexicalOnly, RetrievalMode::Hybrid},
                      AuthorityLevel::Normative),
      make_descriptor("architecture_reference", "Architecture Reference", "docs/architecture/",
                      "snapshot-architecture-v1", {"reference", "architecture"},
                      {RetrievalMode::LexicalOnly, RetrievalMode::Hybrid},
                      AuthorityLevel::Reference),
  };
  delta.removed_corpus_ids = {"ssot_normative"};

  assert_true(catalog.apply_delta(delta),
              "catalog should accept a valid remove-plus-upsert delta");

  const auto after_success = catalog.snapshot();
  assert_equal(2, static_cast<int>(after_success.size()),
               "valid delta should materialize the new route metadata snapshot");
  const auto adr_descriptor = after_success.find_by_id("adr_normative");
  assert_true(adr_descriptor.has_value(), "updated snapshot should still contain the upserted corpus");
  assert_equal("snapshot-adr-v2", adr_descriptor->active_snapshot_id,
               "delta apply should replace matching corpus descriptors in place");
  assert_true(!after_success.find_by_id("ssot_normative").has_value(),
              "removed corpora should disappear from the active snapshot after a valid delta");
  assert_true(after_success.find_by_id("architecture_reference").has_value(),
              "valid delta should append newly introduced corpora");

  CorpusCatalogDelta invalid_delta;
  invalid_delta.upserted_descriptors = {
      make_descriptor("rogue_bundle", "Rogue Bundle", "docs/adr/", "snapshot-rogue-v1",
                      {"advisory"}, {RetrievalMode::LexicalOnly}, AuthorityLevel::Advisory),
  };

  assert_true(!catalog.apply_delta(invalid_delta),
              "catalog should reject a delta that would create duplicate source URIs");

  const auto after_failure = catalog.snapshot();
  assert_equal(2, static_cast<int>(after_failure.size()),
               "failed delta apply should preserve the previous valid snapshot");
  const auto preserved_adr_descriptor = after_failure.find_by_id("adr_normative");
  assert_true(preserved_adr_descriptor.has_value(),
              "failed delta should keep the previous corpora visible to readers");
  assert_equal("snapshot-adr-v2", preserved_adr_descriptor->active_snapshot_id,
               "failed delta should preserve the previously active descriptor state");
  assert_true(!after_failure.find_by_id("rogue_bundle").has_value(),
              "rejected delta must not leak any partial write into the active snapshot");
}

}  // namespace

int main() {
  try {
    test_corpus_catalog_applies_delta_without_exposing_partial_state();
  } catch (const std::exception& exception) {
    std::cerr << exception.what() << '\n';
    return 1;
  }

  return 0;
}