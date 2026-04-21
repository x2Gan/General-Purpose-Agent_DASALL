#include "index/IndexReader.h"

#include <atomic>
#include <exception>
#include <memory>
#include <utility>

namespace dasall::knowledge::index {

namespace {

[[nodiscard]] retrieve::SparseIndexSearchResult make_search_error(KnowledgeErrorCode code,
                                                                  std::string message,
                                                                  std::string stage,
                                                                  std::string ref_id) {
  retrieve::SparseIndexSearchResult result;
  result.ok = false;
  result.error = make_knowledge_error_info(code, std::move(message), std::move(stage),
                                           std::move(ref_id));
  return result;
}

}  // namespace

bool IndexSnapshot::has_consistent_values() const {
  return manifest.has_consistent_values() && !checksum.empty() && static_cast<bool>(search);
}

IndexReader::IndexReader() = default;

IndexReader::IndexReader(std::shared_ptr<const IndexSnapshot> initial_snapshot) {
  const auto installed = swap_active_snapshot(std::move(initial_snapshot));
  (void)installed;
}

retrieve::SparseIndexSearchResult IndexReader::search_sparse(
    const retrieve::SparseIndexSearchRequest& request) const {
  if (!request.has_consistent_values()) {
    return make_search_error(KnowledgeErrorCode::QueryValidationFailed,
                             "sparse index search request is inconsistent",
                             "index_reader.search_sparse", "search_request_invalid");
  }

  const auto snapshot = load_active_snapshot();
  if (!snapshot) {
    return make_search_error(KnowledgeErrorCode::IndexUnavailable,
                             "active snapshot is unavailable", "index_reader.search_sparse",
                             "active_snapshot_missing");
  }

  if (!snapshot->has_consistent_values()) {
    return make_search_error(KnowledgeErrorCode::IndexUnavailable,
                             "active snapshot is inconsistent", "index_reader.search_sparse",
                             "active_snapshot_inconsistent");
  }

  try {
    auto result = snapshot->search(request);
    if (!result.has_consistent_values()) {
      return make_search_error(KnowledgeErrorCode::InternalError,
                               "active snapshot search result is inconsistent",
                               "index_reader.search_sparse", "search_result_inconsistent");
    }

    return result;
  } catch (const std::exception& exception) {
    return make_search_error(KnowledgeErrorCode::IndexUnavailable,
                             std::string("active snapshot search failed: ") + exception.what(),
                             "index_reader.search_sparse", "search_failed");
  } catch (...) {
    return make_search_error(KnowledgeErrorCode::IndexUnavailable,
                             "active snapshot search failed", "index_reader.search_sparse",
                             "search_failed");
  }
}

std::optional<IndexManifest> IndexReader::current_manifest() const {
  const auto snapshot = load_active_snapshot();
  if (!snapshot || !snapshot->has_consistent_values()) {
    return std::nullopt;
  }

  return snapshot->manifest;
}

std::optional<std::string> IndexReader::read_snapshot_checksum(std::string_view snapshot_id) const {
  const auto snapshot = load_active_snapshot();
  if (!snapshot || !snapshot->has_consistent_values() || snapshot->manifest.snapshot_id != snapshot_id) {
    return std::nullopt;
  }

  return snapshot->checksum;
}

bool IndexReader::swap_active_snapshot(std::shared_ptr<const IndexSnapshot> snapshot) {
  if (snapshot && !snapshot->has_consistent_values()) {
    return false;
  }

  std::atomic_store_explicit(&active_snapshot_, std::move(snapshot), std::memory_order_release);
  return true;
}

std::shared_ptr<const IndexSnapshot> IndexReader::load_active_snapshot() const {
  return std::atomic_load_explicit(&active_snapshot_, std::memory_order_acquire);
}

}  // namespace dasall::knowledge::index