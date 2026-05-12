#include "KnowledgeServiceFactory.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "KnowledgeTypes.h"
#include "evidence/EvidenceAssembler.h"
#include "facade/KnowledgeService.h"
#include "health/FreshnessController.h"
#include "health/KnowledgeHealthProbe.h"
#include "health/KnowledgeTelemetry.h"
#include "index/CorpusCatalog.h"
#include "index/IndexReader.h"
#include "index/IndexWriter.h"
#include "index/VersionLedger.h"
#include "ingest/IngestionCoordinator.h"
#include "ingest/SourceScanner.h"
#include "query/CorpusRouter.h"
#include "query/QueryNormalizer.h"
#include "rerank/Reranker.h"
#include "retrieve/RecallCoordinator.h"
#include "retrieve/SparseRetriever.h"

namespace dasall::knowledge {

namespace {

namespace fs = std::filesystem;

constexpr std::int64_t kCatalogRefreshIntervalMs = 24LL * 60LL * 60LL * 1000LL;
constexpr std::int64_t kCatalogExpireAfterMs = 7LL * kCatalogRefreshIntervalMs;

using InstalledInventory =
  std::map<std::string, std::vector<ingest::SourceRecord>, std::less<>>;

[[nodiscard]] std::int64_t now_ms() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}

[[nodiscard]] std::string normalized_path_string(const fs::path& path) {
  return path.lexically_normal().generic_string();
}

void append_unique(std::vector<std::string>& values, std::string value) {
  if (value.empty()) {
    return;
  }
  if (std::find(values.begin(), values.end(), value) == values.end()) {
    values.push_back(std::move(value));
  }
}

[[nodiscard]] CorpusDescriptor make_yaml_descriptor(std::string corpus_id,
                                                    std::string display_name,
                                                    const fs::path& source_root,
                                                    std::vector<std::string> tags,
                                                    AuthorityLevel authority_level) {
  CorpusDescriptor descriptor;
  descriptor.corpus_id = std::move(corpus_id);
  descriptor.display_name = std::move(display_name);
  descriptor.source_uri = normalized_path_string(source_root);
  descriptor.trust_level = TrustLevel::Trusted;
  descriptor.authority_level = authority_level;
  descriptor.source_kind = SourceKind::File;
  descriptor.allowed_formats = {SourceFormat::Yaml};
  descriptor.include_globs = {"*.yaml", "*.yml"};
  descriptor.supported_modes = {RetrievalMode::LexicalOnly};
  descriptor.tags = std::move(tags);
  descriptor.metadata = {
      {"baseline_class", "installed_package"},
      {"owner_module", "knowledge"},
      {"refresh_strategy", "full_scan"},
      {"default_language", "en"},
  };
  return descriptor;
}

[[nodiscard]] std::vector<CorpusDescriptor> make_installed_asset_descriptors(
    const fs::path& assets_root) {
  std::vector<CorpusDescriptor> descriptors;
  descriptors.push_back(make_yaml_descriptor(
      "dasall_profiles",
      "DASALL runtime profiles",
      assets_root / "profiles",
      {"installed", "profile", "runtime"},
      AuthorityLevel::Reference));
  descriptors.push_back(make_yaml_descriptor(
      "dasall_llm_providers",
      "DASALL LLM provider manifests",
      assets_root / "llm" / "providers",
      {"installed", "llm", "provider"},
      AuthorityLevel::Reference));
  return descriptors;
}

[[nodiscard]] KnowledgeConfigSnapshot make_installed_asset_config() {
  KnowledgeConfigSnapshot config;
  config.knowledge_enabled = true;
  config.vector_enabled = false;
  config.retrieval_mode_default = RetrievalMode::LexicalOnly;
  config.evidence_budget_tokens = 512U;
  config.max_context_projection_items = 6U;
  config.catalog_refresh_interval_ms = kCatalogRefreshIntervalMs;
  config.catalog_expire_after_ms = kCatalogExpireAfterMs;
  config.allow_stale_read = false;
  config.failure_backoff_ms = 1000;
  config.request_deadline_ms = 2000;
  config.allow_budget_degrade = true;
  config.max_parallel_recall = 1U;
  config.sparse_recall_timeout_ms = 1000;
  config.dense_recall_timeout_ms = 1;
  config.ingest_timeout_ms = 5000;
  return config;
}

[[nodiscard]] query::QueryNormalizePolicy make_query_policy(
    const std::vector<CorpusDescriptor>& descriptors) {
  query::QueryNormalizePolicy policy;
  policy.max_query_text_bytes = 1024U;
  policy.max_lexical_terms = 16U;
  policy.max_top_k = 12U;
  policy.max_context_projection_items = 8U;
  for (const auto& descriptor : descriptors) {
    append_unique(policy.allowed_corpora, descriptor.corpus_id);
    for (const auto& tag : descriptor.tags) {
      append_unique(policy.allowed_domain_tags, tag);
    }
  }
  return policy;
}

[[nodiscard]] std::vector<ingest::SourceRecord> scan_installed_inventory_for_descriptor(
    const CorpusDescriptor& descriptor,
    const fs::path& assets_root) {
  ingest::SourceScanner scanner(ingest::SourceScannerDeps{
      .lookup_corpus = [descriptor](std::string_view corpus_id) -> std::optional<CorpusDescriptor> {
        if (corpus_id == descriptor.corpus_id) {
          return descriptor;
        }
        return std::nullopt;
      },
      .load_inventory = [](std::string_view) {
        return std::vector<ingest::SourceRecord>{};
      },
      .repository_root = [assets_root] {
        return assets_root;
      },
      .now_ms = now_ms,
  });

  ingest::CorpusScanPlan plan;
  plan.corpus_id = descriptor.corpus_id;
  plan.root_uri = descriptor.source_uri;
  plan.source_kind = descriptor.source_kind;
  plan.include_globs = descriptor.include_globs;
  plan.exclude_globs = descriptor.exclude_globs;
  plan.allowed_formats = descriptor.allowed_formats;
  plan.full_scan = true;

  const auto delta = scanner.scan(plan);
  std::vector<ingest::SourceRecord> records;
  records.reserve(delta.added.size() + delta.updated.size());
  records.insert(records.end(), delta.added.begin(), delta.added.end());
  records.insert(records.end(), delta.updated.begin(), delta.updated.end());
  return records;
}

[[nodiscard]] InstalledInventory rebuild_installed_inventory(
    const index::CorpusCatalogSnapshot& snapshot,
    const fs::path& assets_root) {
  InstalledInventory inventory;
  for (const auto& descriptor : snapshot.list_all()) {
    if (descriptor.trust_level != TrustLevel::Trusted) {
      continue;
    }
    inventory[descriptor.corpus_id] = scan_installed_inventory_for_descriptor(descriptor,
                                                                              assets_root);
  }
  return inventory;
}

[[nodiscard]] KnowledgeServiceFactoryResult make_error(std::string error) {
  return KnowledgeServiceFactoryResult{.service = nullptr, .error = std::move(error)};
}

}  // namespace

KnowledgeServiceFactoryResult create_installed_asset_knowledge_service(
    const InstalledAssetKnowledgeServiceOptions& options) {
  if (!options.has_consistent_values()) {
    return make_error("installed asset knowledge service options are inconsistent");
  }

  try {
    const fs::path assets_root = options.readonly_assets_root.lexically_normal();
    const fs::path snapshots_root =
        (options.state_root / "knowledge" / "snapshots").lexically_normal();
    auto descriptors = make_installed_asset_descriptors(assets_root);
    const auto config = make_installed_asset_config();
    if (!config.has_consistent_values()) {
      return make_error("installed asset knowledge config is inconsistent");
    }

    auto corpus_catalog = std::make_unique<index::CorpusCatalog>();
    if (!corpus_catalog->replace_all(descriptors)) {
      return make_error("installed asset knowledge corpus catalog is inconsistent");
    }

    auto config_state = std::make_shared<KnowledgeConfigSnapshot>(config);
    auto telemetry = std::make_shared<KnowledgeTelemetry>(TelemetrySinks{});
    auto inventory_state = std::make_shared<InstalledInventory>();

    facade::KnowledgeServiceDeps deps;
    deps.query_normalizer =
        std::make_unique<query::QueryNormalizer>(make_query_policy(descriptors));
    deps.corpus_catalog = std::move(corpus_catalog);
    deps.index_reader = std::make_unique<index::IndexReader>();
    deps.freshness_controller = std::make_unique<FreshnessController>();
    deps.corpus_router = std::make_unique<query::CorpusRouter>();
    deps.reranker = std::make_unique<rerank::Reranker>();
    deps.evidence_assembler = std::make_unique<evidence::EvidenceAssembler>();
    deps.now_ms = now_ms;

    auto* catalog = deps.corpus_catalog.get();
    auto* reader = deps.index_reader.get();
    auto* freshness_controller = deps.freshness_controller.get();
    auto ledger = std::make_shared<index::VersionLedger>(index::VersionLedgerDeps{
        .read_snapshot_checksum = [reader](std::string_view snapshot_id) {
          return reader->read_snapshot_checksum(snapshot_id);
        },
    });

    auto sparse_retriever = std::make_shared<retrieve::SparseRetriever>(
        retrieve::SparseRetrieverDeps{
            .search_index = [reader](const retrieve::SparseIndexSearchRequest& request) {
              return reader->search_sparse(request);
            },
        });
    deps.recall_coordinator = std::make_unique<retrieve::RecallCoordinator>(
        retrieve::RecallCoordinatorDeps{
            .sparse_lane = [sparse_retriever](const retrieve::SparseRetrieveRequest& request) {
              return sparse_retriever->retrieve(request);
            },
        },
        retrieve::RecallCoordinatorPolicy{
            .max_parallel_recall = 1U,
            .sparse_lane_timeout_ms = 1000,
            .dense_lane_timeout_ms = 1,
        });

    deps.ingestion_coordinator = std::make_unique<ingest::IngestionCoordinator>(
        ingest::IngestionCoordinatorDeps{
            .load_catalog_snapshot = [catalog] {
              return catalog->snapshot();
            },
            .load_inventory = [inventory_state](std::string_view corpus_id) {
              const auto iterator = inventory_state->find(corpus_id);
              return iterator != inventory_state->end()
                         ? iterator->second
                         : std::vector<ingest::SourceRecord>{};
            },
            .repository_root = [assets_root] {
              return assets_root;
            },
            .now_ms = now_ms,
        });

    index::IndexWriterDeps writer_deps;
    writer_deps.snapshots_root = [snapshots_root] {
      return snapshots_root;
    };
    writer_deps.now_ms = now_ms;
    writer_deps.record_candidate = [ledger](const index::VersionLedgerEntry& entry) {
      return ledger->record_candidate(entry);
    };
    writer_deps.mark_active = [ledger](std::string_view snapshot_id,
                                       std::int64_t activated_at) {
      return ledger->mark_active(snapshot_id, activated_at);
    };
    writer_deps.refresh_catalog = [catalog, inventory_state, assets_root](const IndexManifest& manifest) {
      auto active_descriptors = catalog->snapshot().list_all();
      for (auto& descriptor : active_descriptors) {
        descriptor.active_snapshot_id = manifest.snapshot_id;
        descriptor.last_updated_ms = manifest.effective_at;
      }
      if (!catalog->replace_all(std::move(active_descriptors))) {
        return false;
      }
      *inventory_state = rebuild_installed_inventory(catalog->snapshot(), assets_root);
      return true;
    };
    deps.index_writer =
        std::make_unique<index::IndexWriter>(*reader, *ledger, std::move(writer_deps));

    deps.health_probe = std::make_unique<KnowledgeHealthProbe>(HealthProbeDeps{
        .knowledge_enabled = [config_state] {
          return config_state->knowledge_enabled;
        },
        .lifecycle_ready = [] {
          return true;
        },
        .active_manifest = [reader] {
          return reader->current_manifest();
        },
        .freshness_snapshot = [reader, freshness_controller, config_state] {
          return freshness_controller->evaluate(reader->current_manifest(),
                                                *config_state,
                                                now_ms(),
                                                false);
        },
        .vector_backend_available = [] {
          return false;
        },
        .last_known_good_available = [ledger] {
          return ledger->last_known_good().has_value();
        },
        .telemetry_status = [telemetry] {
          return telemetry->status();
        },
        .degraded_return_count = [] {
          return 0U;
        },
        .recent_reason_codes = [] {
          return std::vector<std::string>{};
        },
    });

    auto service = std::make_shared<facade::KnowledgeServiceFacade>(std::move(deps));
    if (!service->init(config)) {
      return make_error("installed asset knowledge service init failed");
    }

    return KnowledgeServiceFactoryResult{.service = std::move(service), .error = {}};
  } catch (const std::exception& exception) {
    return make_error(std::string("installed asset knowledge service composition failed: ") +
                      exception.what());
  }
}

}  // namespace dasall::knowledge