#include <exception>
#include <iostream>
#include <memory>
#include <type_traits>

#include "IExperienceStore.h"
#include "IFactStore.h"
#include "IMaintenanceStore.h"
#include "IMemoryStore.h"
#include "ISessionStore.h"
#include "ISummaryStore.h"
#include "ITransactionalStore.h"
#include "MaintenanceReport.h"
#include "config/MemoryConfig.h"
#include "conflict/MemoryConflictResolver.h"
#include "context/CandidateCollector.h"
#include "maintenance/MemoryMaintenanceWorker.h"
#include "writeback/CompressionCoordinator.h"
#include "writeback/HierarchicalSummarizationCoordinator.h"
#include "writeback/WritebackCoordinator.h"

#include "FakeMemoryStore.h"
#include "support/TestAssertions.h"

namespace {

void test_mini_store_aliases_now_resolve_to_imemory_store() {
  using dasall::memory::IExperienceStore;
  using dasall::memory::IFactStore;
  using dasall::memory::IMaintenanceStore;
  using dasall::memory::IMemoryStore;
  using dasall::memory::ISessionStore;
  using dasall::memory::ISummaryStore;
  using dasall::memory::ITransactionalStore;

  static_assert(std::is_same_v<ISessionStore, IMemoryStore>,
                "session store compatibility alias should resolve to IMemoryStore");
  static_assert(std::is_same_v<ISummaryStore, IMemoryStore>,
                "summary store compatibility alias should resolve to IMemoryStore");
  static_assert(std::is_same_v<IFactStore, IMemoryStore>,
                "fact store compatibility alias should resolve to IMemoryStore");
  static_assert(std::is_same_v<IExperienceStore, IMemoryStore>,
                "experience store compatibility alias should resolve to IMemoryStore");
  static_assert(std::is_same_v<IMaintenanceStore, IMemoryStore>,
                "maintenance store compatibility alias should resolve to IMemoryStore");
  static_assert(std::is_base_of_v<ITransactionalStore, IMemoryStore>,
                "IMemoryStore should continue aggregating the transactional seam");
}

void test_internal_components_construct_against_unified_store_surface() {
  using dasall::memory::CandidateCollector;
  using dasall::memory::CompressionCoordinator;
  using dasall::memory::HierarchicalSummarizationCoordinator;
  using dasall::memory::MemoryConfig;
  using dasall::memory::MemoryConflictResolver;
  using dasall::memory::MemoryMaintenanceWorker;
  using dasall::memory::WritebackCoordinator;
  using dasall::tests::mocks::FakeMemoryStore;
  using dasall::tests::support::assert_true;

  FakeMemoryStore store;
  MemoryConfig config;
  auto board = dasall::memory::create_working_memory_board();

  CandidateCollector collector(*board, store, config, nullptr);
  CompressionCoordinator compressor(store, nullptr, nullptr);
  HierarchicalSummarizationCoordinator hierarchy(store, config, nullptr, nullptr);
  MemoryMaintenanceWorker maintenance(store, config, nullptr, nullptr, nullptr);
  auto conflict_resolver = std::make_unique<MemoryConflictResolver>(store);
  WritebackCoordinator writeback(
      store,
      store,
      std::unique_ptr<HierarchicalSummarizationCoordinator>{},
      std::move(conflict_resolver),
      *board,
      nullptr,
      nullptr,
      nullptr,
      nullptr);

  assert_true(std::is_abstract_v<dasall::memory::IMemoryStore>,
              "IMemoryStore should remain abstract after interface unification");
  (void)collector;
  (void)compressor;
  (void)hierarchy;
  (void)maintenance;
  (void)writeback;
}

}  // namespace

int main() {
  try {
    test_mini_store_aliases_now_resolve_to_imemory_store();
    test_internal_components_construct_against_unified_store_surface();
  } catch (const std::exception& exception) {
    std::cerr << exception.what() << '\n';
    return 1;
  }

  return 0;
}