#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

#include "IKnowledgeService.h"
#include "evidence/EvidenceAssembler.h"
#include "health/FreshnessController.h"
#include "health/KnowledgeTelemetry.h"
#include "index/CorpusCatalog.h"
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
  ~KnowledgeServiceFacade() override = default;

  bool init(const KnowledgeConfigSnapshot& config) override;
  KnowledgeRetrieveResult retrieve(const KnowledgeQuery& query) override;
  KnowledgeHealthSnapshot health_snapshot() const override;
  RefreshResult request_refresh(const CorpusChangeSet& changes) override;

  [[nodiscard]] StageBudget compute_stage_budget(std::int64_t deadline_ms) const;

 private:
  [[nodiscard]] KnowledgeRetrieveResult fail_closed(const KnowledgeQuery& query,
                                                    RetrievalMode mode,
                                                    KnowledgeErrorCode error_code,
                                                    std::string_view reason) const;
  [[nodiscard]] std::int64_t now_ms() const;

  LifecycleState lifecycle_state_ = LifecycleState::Created;
  KnowledgeConfigSnapshot config_{};
  KnowledgeServiceDeps deps_{};
  mutable std::atomic_bool refresh_in_flight_{false};
};

}  // namespace dasall::knowledge::facade