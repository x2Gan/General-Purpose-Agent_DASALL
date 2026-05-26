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

[[nodiscard]] bool contains_string(const std::vector<std::string>& values,
                                   std::string_view value) {
  return std::find(values.begin(), values.end(), value) != values.end();
}

void append_unique(std::vector<std::string>& values, std::string value) {
  if (value.empty()) {
    return;
  }
  if (std::find(values.begin(), values.end(), value) == values.end()) {
    values.push_back(std::move(value));
  }
}

void append_reason_code(std::vector<std::string>& reason_codes,
                        std::string reason_code) {
  if (reason_code.empty()) {
    return;
  }

  if (!contains_string(reason_codes, reason_code)) {
    reason_codes.push_back(std::move(reason_code));
  }
}

[[nodiscard]] bool requests_runtime_canary(
    const std::optional<RetrievalMode>& preferred_mode) {
  return preferred_mode.has_value() &&
         *preferred_mode != RetrievalMode::LexicalOnly;
}

[[nodiscard]] bool all_corpora_allowlisted(
    const std::vector<std::string>& requested_corpora,
    const std::vector<std::string>& allowed_corpora) {
  return !requested_corpora.empty() &&
         std::all_of(requested_corpora.begin(),
                     requested_corpora.end(),
                     [&](const std::string& corpus_id) {
                       return contains_string(allowed_corpora, corpus_id);
                     });
}

struct RuntimeCanaryDecision {
  bool requested = false;
  bool admitted = false;
  std::vector<std::string> reason_codes;
};

[[nodiscard]] RuntimeCanaryDecision decide_runtime_canary(
    const query::NormalizedQuery& query,
    const KnowledgeConfigSnapshot& config,
    const std::vector<std::string>& allowed_corpora,
    bool vector_backend_ready) {
  RuntimeCanaryDecision decision;
  decision.requested = requests_runtime_canary(query.preferred_mode);
  if (!decision.requested) {
    return decision;
  }

  if (!config.vector_enabled) {
    decision.reason_codes.push_back("runtime_canary_vector_unavailable");
    return decision;
  }

  if (!vector_backend_ready) {
    decision.reason_codes.push_back("runtime_canary_backend_not_ready");
    return decision;
  }

  if (allowed_corpora.empty()) {
    decision.reason_codes.push_back("runtime_canary_not_admitted");
    return decision;
  }

  if (query.allowed_corpora.empty()) {
    decision.reason_codes.push_back("runtime_canary_scope_required");
    return decision;
  }

  if (!all_corpora_allowlisted(query.allowed_corpora, allowed_corpora)) {
    decision.reason_codes.push_back("runtime_canary_allowlist_miss");
    return decision;
  }

  decision.admitted = true;
  decision.reason_codes.push_back("runtime_canary_admitted");
  return decision;
}

void append_runtime_canary_reason_codes(query::RoutePlanResult& result,
                                        const RuntimeCanaryDecision& decision) {
  if (!decision.requested) {
    return;
  }

  for (const auto& reason_code : decision.reason_codes) {
    append_reason_code(result.route_reason_codes, reason_code);
  }

  if (result.plan.has_value()) {
    result.plan->route_reason_codes = result.route_reason_codes;
  }
}

[[nodiscard]] std::vector<std::string> normalize_runtime_canary_allowlist(
    const std::vector<std::string>& corpora) {
  std::vector<std::string> normalized;
  normalized.reserve(corpora.size());
  for (const auto& corpus_id : corpora) {
    append_unique(normalized, corpus_id);
  }
  return normalized;
}

  [[nodiscard]] CorpusDescriptor make_descriptor(
    std::string corpus_id,
    std::string display_name,
    const fs::path& source_root,
    std::vector<std::string> tags,
    AuthorityLevel authority_level,
    SourceKind source_kind,
    std::vector<SourceFormat> allowed_formats,
    std::vector<std::string> include_globs,
    std::vector<std::string> exclude_globs,
    std::vector<RetrievalMode> supported_modes,
    std::string default_language) {
  CorpusDescriptor descriptor;
  descriptor.corpus_id = std::move(corpus_id);
  descriptor.display_name = std::move(display_name);
  descriptor.source_uri = normalized_path_string(source_root);
  descriptor.trust_level = TrustLevel::Trusted;
  descriptor.authority_level = authority_level;
    descriptor.source_kind = source_kind;
    descriptor.allowed_formats = std::move(allowed_formats);
    descriptor.include_globs = std::move(include_globs);
    descriptor.exclude_globs = std::move(exclude_globs);
    descriptor.supported_modes = std::move(supported_modes);
  descriptor.tags = std::move(tags);
  descriptor.metadata = {
      {"baseline_class", "installed_package"},
      {"owner_module", "knowledge"},
      {"refresh_strategy", "full_scan"},
      {"default_language", std::move(default_language)},
  };
  return descriptor;
}

[[nodiscard]] std::vector<CorpusDescriptor> make_installed_asset_descriptors(
    const fs::path& assets_root) {
  std::vector<CorpusDescriptor> descriptors;
    descriptors.push_back(make_descriptor(
      "architecture_reference",
      "DASALL architecture reference",
      assets_root / "docs" / "architecture",
      {"installed", "architecture", "reference"},
      AuthorityLevel::Reference,
      SourceKind::File,
      {SourceFormat::Markdown},
      {"DASALL_Agent_architecture.md",
       "DASALL_Engineering_Blueprint.md",
       "DASALL_*详细设计*.md",
       "platform_linux_detailed_design.md"},
      {"*评审报告*.md",
       "*迁移影响清单*.md",
       "DASALL_boundary治理与优化说明.md"},
      {RetrievalMode::LexicalOnly, RetrievalMode::Hybrid},
      "zh-CN"));
    descriptors.push_back(make_descriptor(
      "adr_normative",
      "DASALL ADR normative corpus",
      assets_root / "docs" / "adr",
      {"installed", "adr", "normative"},
      AuthorityLevel::Normative,
      SourceKind::File,
      {SourceFormat::Markdown},
      {"ADR-*.md"},
      {},
      {RetrievalMode::LexicalOnly, RetrievalMode::Hybrid},
      "zh-CN"));
    descriptors.push_back(make_descriptor(
      "ssot_normative",
      "DASALL SSOT normative corpus",
      assets_root / "docs" / "ssot",
      {"installed", "ssot", "normative"},
      AuthorityLevel::Normative,
      SourceKind::File,
      {SourceFormat::Markdown},
      {"*.md"},
      {},
      {RetrievalMode::LexicalOnly, RetrievalMode::Hybrid},
      "zh-CN"));
    descriptors.push_back(make_descriptor(
      "profile_policy_normative",
      "DASALL profile policy normative corpus",
      assets_root / "profiles",
      {"installed", "profile", "normative"},
      AuthorityLevel::Normative,
      SourceKind::ConfigSnapshot,
      {SourceFormat::Yaml},
      {"*/runtime_policy.yaml"},
      {},
      {RetrievalMode::LexicalOnly},
      "en"));
    descriptors.push_back(make_descriptor(
      "dasall_llm_providers",
      "DASALL LLM provider manifests",
      assets_root / "llm" / "providers",
      {"installed", "llm", "provider"},
      AuthorityLevel::Reference,
      SourceKind::File,
      {SourceFormat::Yaml},
      {"*.yaml", "*.yml"},
      {},
      {RetrievalMode::LexicalOnly},
      "en"));
  return descriptors;
}

[[nodiscard]] std::string normalize_profile_id(std::string profile_id) {
  return profile_id.empty() ? std::string("unknown") : std::move(profile_id);
}

[[nodiscard]] KnowledgeConfigSnapshot make_installed_asset_config(
    const InstalledAssetKnowledgeServiceOptions& options) {
  const bool vector_runtime_available =
      static_cast<bool>(options.build_dense_snapshot) &&
      static_cast<bool>(options.create_vector_recall_store);

  KnowledgeConfigSnapshot config;
  config.knowledge_enabled = true;
  config.vector_enabled = vector_runtime_available;
  config.retrieval_mode_default = RetrievalMode::LexicalOnly;
  config.profile_id = normalize_profile_id(options.profile_id);
  config.evidence_budget_tokens = 512U;
  config.max_context_projection_items = 6U;
  config.catalog_refresh_interval_ms = kCatalogRefreshIntervalMs;
  config.catalog_expire_after_ms = kCatalogExpireAfterMs;
  config.allow_stale_read = false;
  config.failure_backoff_ms = 1000;
  config.request_deadline_ms = 2000;
  config.allow_budget_degrade = true;
  config.max_parallel_recall = vector_runtime_available ? 2U : 1U;
  config.sparse_recall_timeout_ms = 1000;
  config.dense_recall_timeout_ms = vector_runtime_available ? 1000 : 1;
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

[[nodiscard]] std::vector<CorpusDescriptor> merge_persisted_catalog_runtime_state(
    std::vector<CorpusDescriptor> descriptors,
    const index::CorpusCatalogSnapshot& persisted_snapshot) {
  for (auto& descriptor : descriptors) {
    const auto persisted_descriptor = persisted_snapshot.find_by_id(descriptor.corpus_id);
    if (!persisted_descriptor.has_value() ||
        persisted_descriptor->source_uri != descriptor.source_uri) {
      descriptor.active_snapshot_id.clear();
      continue;
    }

    descriptor.active_snapshot_id = persisted_descriptor->active_snapshot_id;
    descriptor.last_updated_ms = persisted_descriptor->last_updated_ms;
  }

  return descriptors;
}

[[nodiscard]] bool align_catalog_to_restored_manifest(
    index::CorpusCatalog& catalog,
    const std::optional<IndexManifest>& manifest) {
  auto descriptors = catalog.snapshot().list_all();
  for (auto& descriptor : descriptors) {
    descriptor.active_snapshot_id = manifest.has_value() ? manifest->snapshot_id : std::string{};
    if (manifest.has_value()) {
      descriptor.last_updated_ms = manifest->effective_at;
    }
  }

  return catalog.replace_all(std::move(descriptors));
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
    const fs::path knowledge_state_root =
      (options.state_root / "knowledge").lexically_normal();
    const fs::path snapshots_root =
      (knowledge_state_root / "snapshots").lexically_normal();
    const fs::path catalog_path =
      (knowledge_state_root / "corpus_catalog.json").lexically_normal();
    const fs::path ledger_path =
      (knowledge_state_root / "version_ledger.jsonl").lexically_normal();
    auto descriptors = make_installed_asset_descriptors(assets_root);
    const auto config = make_installed_asset_config(options);
    if (!config.has_consistent_values()) {
      return make_error("installed asset knowledge config is inconsistent");
    }

    auto corpus_catalog =
      std::make_unique<index::CorpusCatalog>(index::CorpusCatalogDeps{.catalog_path = catalog_path});
    descriptors = merge_persisted_catalog_runtime_state(std::move(descriptors),
                              corpus_catalog->snapshot());
    if (!corpus_catalog->replace_all(descriptors)) {
      return make_error("installed asset knowledge corpus catalog is inconsistent");
    }

    auto config_state = std::make_shared<KnowledgeConfigSnapshot>(config);
    auto telemetry = std::make_shared<KnowledgeTelemetry>(options.telemetry_sinks);
    auto inventory_state = std::make_shared<InstalledInventory>();
    const auto runtime_canary_allowed_corpora =
      normalize_runtime_canary_allowlist(options.runtime_canary_allowed_corpora);

    facade::KnowledgeServiceDeps deps;
    deps.query_normalizer =
        std::make_unique<query::QueryNormalizer>(make_query_policy(descriptors));
    deps.corpus_catalog = std::move(corpus_catalog);
    deps.index_reader = std::make_unique<index::IndexReader>();
    deps.freshness_controller = std::make_unique<FreshnessController>();
    deps.corpus_router = std::make_unique<query::CorpusRouter>();
    deps.reranker = std::make_unique<rerank::Reranker>();
    deps.evidence_assembler = std::make_unique<evidence::EvidenceAssembler>();
    deps.startup_prewarm_on_init = true;
    deps.now_ms = now_ms;
    deps.emit_retrieve_event = [telemetry](const KnowledgeTelemetryEvent& event) {
      telemetry->emit_retrieve_event(event);
    };

    auto* catalog = deps.corpus_catalog.get();
    auto* reader = deps.index_reader.get();
    auto* freshness_controller = deps.freshness_controller.get();
    auto* router = deps.corpus_router.get();
    auto ledger = std::make_shared<index::VersionLedger>(index::VersionLedgerDeps{
        .read_snapshot_checksum = [snapshots_root](std::string_view snapshot_id) {
          return index::IndexWriter::read_persisted_snapshot_checksum(snapshots_root,
                                                                      snapshot_id);
        },
        .ledger_path = ledger_path,
    });

    auto sparse_retriever = std::make_shared<retrieve::SparseRetriever>(
        retrieve::SparseRetrieverDeps{
            .search_index = [reader](const retrieve::SparseIndexSearchRequest& request) {
              return reader->search_sparse(request);
            },
        });
    std::shared_ptr<const retrieve::VectorRetrieverBridge> dense_bridge;
    if (options.create_vector_recall_store) {
      const DenseStoreFactoryContext dense_store_context{
          .snapshots_root = snapshots_root,
          .active_manifest = [reader] {
            return reader->current_manifest();
          },
      };
      auto vector_store = options.create_vector_recall_store(dense_store_context);
      if (vector_store != nullptr) {
        auto query_encoder = options.create_query_encoder
                                 ? options.create_query_encoder()
                                 : nullptr;
        dense_bridge = std::make_shared<retrieve::VectorRetrieverBridge>(
            std::move(query_encoder),
            std::move(vector_store));
      }
    }
    deps.recall_coordinator = std::make_unique<retrieve::RecallCoordinator>(
        retrieve::RecallCoordinatorDeps{
            .sparse_lane = [sparse_retriever](const retrieve::SparseRetrieveRequest& request) {
              return sparse_retriever->retrieve(request);
            },
            .dense_bridge = dense_bridge,
        },
        retrieve::RecallCoordinatorPolicy{
            .max_parallel_recall = config.max_parallel_recall,
            .sparse_lane_timeout_ms = config.sparse_recall_timeout_ms,
            .dense_lane_timeout_ms = config.dense_recall_timeout_ms,
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
    writer_deps.build_dense_snapshot = options.build_dense_snapshot;
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
    const auto active_entry = ledger->active();
    const auto last_known_good_entry = ledger->last_known_good();
    (void)deps.index_writer->restore_startup_state(
        active_entry.has_value() ? active_entry->snapshot_id : std::string_view{},
        last_known_good_entry.has_value() ? last_known_good_entry->snapshot_id :
                                            std::string_view{});
    const auto restored_manifest = reader->current_manifest();
    if (!align_catalog_to_restored_manifest(*catalog, restored_manifest)) {
      return make_error("installed asset knowledge startup catalog restore failed");
    }
    if (restored_manifest.has_value()) {
      *inventory_state = rebuild_installed_inventory(catalog->snapshot(), assets_root);
    } else {
      inventory_state->clear();
    }

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
        .vector_backend_available = [dense_bridge] {
          return dense_bridge != nullptr && dense_bridge->available();
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

    deps.build_plan = [router, runtime_canary_allowed_corpora, dense_bridge](
                          const query::NormalizedQuery& query,
                          const KnowledgeConfigSnapshot& config,
                          const index::CorpusCatalogSnapshot& catalog,
                          const FreshnessSnapshot& freshness) {
      auto effective_config = config;
      const bool vector_backend_ready =
          dense_bridge != nullptr && dense_bridge->available();
      const auto canary_decision = decide_runtime_canary(query,
                                                         config,
                                                         runtime_canary_allowed_corpora,
                                                         vector_backend_ready);
      if (canary_decision.admitted && query.preferred_mode.has_value()) {
        effective_config.retrieval_mode_default = *query.preferred_mode;
      }

      auto route_result = router->build_plan(query,
                                             effective_config,
                                             catalog,
                                             freshness);
      append_runtime_canary_reason_codes(route_result, canary_decision);
      return route_result;
    };

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