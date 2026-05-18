#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <thread>

#include "IKnowledgeService.h"
#include "evidence/EvidenceAssembler.h"
#include "health/FreshnessController.h"
#include "health/KnowledgeHealthProbe.h"
#include "health/KnowledgeTelemetry.h"
#include "index/CorpusCatalog.h"
#include "index/IndexReader.h"
#include "index/IndexWriter.h"
#include "ingest/IngestionCoordinator.h"
#include "query/CorpusRouter.h"
#include "query/QueryNormalizer.h"
#include "rerank/Reranker.h"
#include "retrieve/RecallCoordinator.h"

namespace dasall::knowledge::facade {

enum class LifecycleState : std::uint8_t {
  Created = 0,
  Running = 1,
  Stopped = 2,
};

struct StageBudget {
  std::int64_t absolute_deadline_ms = 0;
  std::int64_t normalize_route_ms = 0;
  std::int64_t sparse_recall_ms = 0;
  std::int64_t dense_recall_ms = 0;
  std::int64_t rerank_evidence_ms = 0;
  std::int64_t telemetry_wrap_ms = 0;

  [[nodiscard]] bool has_consistent_values() const;
};

struct KnowledgeServiceDeps {
  std::unique_ptr<query::QueryNormalizer> query_normalizer;
  std::unique_ptr<index::CorpusCatalog> corpus_catalog;
  std::unique_ptr<index::IndexReader> index_reader;
  std::unique_ptr<FreshnessController> freshness_controller;
  std::unique_ptr<query::CorpusRouter> corpus_router;
  std::unique_ptr<retrieve::RecallCoordinator> recall_coordinator;
  std::unique_ptr<rerank::Reranker> reranker;
  std::unique_ptr<evidence::EvidenceAssembler> evidence_assembler;
  std::unique_ptr<ingest::IngestionCoordinator> ingestion_coordinator;
  std::unique_ptr<index::IndexWriter> index_writer;
  std::unique_ptr<KnowledgeHealthProbe> health_probe;
  std::function<std::int64_t()> now_ms;
  std::function<query::NormalizeResult(const KnowledgeQuery& query)> normalize_query;
  std::function<index::CorpusCatalogSnapshot()> catalog_snapshot;
  std::function<std::optional<IndexManifest>()> current_manifest;
  std::function<FreshnessSnapshot(const std::optional<IndexManifest>& manifest,
                                  const KnowledgeConfigSnapshot& config,
                                  std::int64_t now_ms,
                                  bool query_allow_stale)>
      evaluate_freshness;
  std::function<query::RoutePlanResult(const query::NormalizedQuery& query,
                                       const KnowledgeConfigSnapshot& config,
                                       const index::CorpusCatalogSnapshot& catalog,
                                       const FreshnessSnapshot& freshness)>
      build_plan;
  std::function<retrieve::RecallCoordinatorResult(const retrieve::RecallRequest& request)> recall;
  std::function<rerank::RankedHitSet(const retrieve::RecallCandidateSet& candidates,
                                     const FreshnessSnapshot& freshness,
                                     const rerank::RerankPolicy& policy)>
      rerank;
  std::function<EvidenceBundle(const rerank::RankedHitSet& hits,
                               const evidence::EvidenceAssemblePolicy& policy)>
      assemble_evidence;
  std::function<KnowledgeHealthSnapshot()> collect_health_snapshot;
  std::function<RefreshResult(const CorpusChangeSet& changes)> request_refresh;
  std::function<void(const KnowledgeTelemetryEvent& event)> emit_retrieve_event;
};

class KnowledgeServiceFacade final : public IKnowledgeService {
 public:
  explicit KnowledgeServiceFacade(KnowledgeServiceDeps deps);
  ~KnowledgeServiceFacade() override;

  bool init(const KnowledgeConfigSnapshot& config) override;
  KnowledgeRetrieveResult retrieve(const KnowledgeQuery& query) override;
  KnowledgeHealthSnapshot health_snapshot() const override;
  RefreshResult request_refresh(const CorpusChangeSet& changes) override;
  RefreshResult request_refresh_sync_for_tests(const CorpusChangeSet& changes);

  [[nodiscard]] StageBudget compute_stage_budget(std::int64_t deadline_ms) const;

 private:
  [[nodiscard]] KnowledgeRetrieveResult fail_closed(const KnowledgeQuery& query,
                                                    RetrievalMode mode,
                                                    KnowledgeErrorCode error_code,
                                                    std::string_view reason) const;
    void bind_default_component_seams();
  [[nodiscard]] std::int64_t now_ms() const;
    [[nodiscard]] RefreshResult run_refresh_delegate(const CorpusChangeSet& changes,
                                                     bool allow_busy_result);
    [[nodiscard]] RefreshResult run_real_refresh(const CorpusChangeSet& changes);
    [[nodiscard]] std::string next_refresh_job_id();
    void join_previous_refresh_worker();

  LifecycleState lifecycle_state_ = LifecycleState::Created;
  KnowledgeConfigSnapshot config_{};
  KnowledgeServiceDeps deps_{};
  mutable std::atomic_bool refresh_in_flight_{false};
  mutable std::atomic<int> last_refresh_status_code_{-1};
  std::atomic<std::uint64_t> refresh_job_sequence_{0U};
  std::thread refresh_worker_;
};

}  // namespace dasall::knowledge::facade