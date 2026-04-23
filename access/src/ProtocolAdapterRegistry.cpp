#include "ProtocolAdapterRegistry.h"

#include <atomic>
#include <algorithm>
#include <memory>
#include <utility>

namespace dasall::access {

bool ProtocolAdapterRegistry::AdapterKey::has_consistent_values() const {
  return !entry_type.empty() && !protocol_kind.empty();
}

bool ProtocolAdapterRegistry::AdapterKey::matches(
    const std::string_view expected_entry_type,
    const std::string_view expected_protocol_kind) const {
  return entry_type == expected_entry_type && protocol_kind == expected_protocol_kind;
}

bool ProtocolAdapterRegistry::EncodeTargetRef::has_consistent_values() const {
  return !entry_type.empty() && !protocol_kind.empty();
}

bool ProtocolAdapterRegistry::AdapterBinding::has_consistent_values() const {
  return !source_ref.empty() && key.has_consistent_values() && static_cast<bool>(adapter);
}

ProtocolAdapterRegistry::ProtocolAdapterRegistry()
    : store_(std::make_shared<const AdapterStore>()) {}

bool ProtocolAdapterRegistry::register_adapter(
    const std::string_view source_ref,
    const std::string_view entry_type,
    const std::string_view protocol_kind,
    std::shared_ptr<IProtocolAdapter> adapter) {
  AdapterBinding candidate{
      .source_ref = std::string(source_ref),
      .key = {
          .entry_type = std::string(entry_type),
          .protocol_kind = std::string(protocol_kind),
      },
      .adapter = std::move(adapter),
  };
  if (!candidate.has_consistent_values()) {
    return false;
  }

  std::lock_guard<std::mutex> lock(write_mutex_);
  const auto current_store = load_store();
  auto next_store = std::make_shared<AdapterStore>(*current_store);

  const auto conflicting_binding = std::find_if(
      next_store->bindings.begin(), next_store->bindings.end(),
      [&candidate](const AdapterBinding& existing_binding) {
        return existing_binding.key.matches(candidate.key.entry_type,
                                            candidate.key.protocol_kind);
      });
  if (conflicting_binding != next_store->bindings.end()) {
    return false;
  }

  next_store->bindings.push_back(std::move(candidate));

  // 写路径只做快照替换，不调用 adapter 逻辑，避免在持锁区域做 I/O。
  std::atomic_store_explicit(&store_, std::shared_ptr<const AdapterStore>(next_store),
                             std::memory_order_release);
  return true;
}

std::shared_ptr<IProtocolAdapter> ProtocolAdapterRegistry::resolve_decoder(
    const std::string_view entry_type,
    const std::string_view protocol_kind) const {
  const auto current_store = load_store();
  const auto it = std::find_if(
      current_store->bindings.begin(), current_store->bindings.end(),
      [entry_type, protocol_kind](const AdapterBinding& binding) {
        return binding.key.matches(entry_type, protocol_kind);
      });

  if (it == current_store->bindings.end()) {
    return nullptr;
  }

  return it->adapter;
}

std::shared_ptr<IProtocolAdapter> ProtocolAdapterRegistry::resolve_encoder(
    const EncodeTargetRef& target) const {
  if (!target.has_consistent_values()) {
    return nullptr;
  }

  return resolve_decoder(target.entry_type, target.protocol_kind);
}

std::vector<ProtocolAdapterRegistry::AdapterBinding>
ProtocolAdapterRegistry::list_bindings() const {
  return load_store()->bindings;
}

std::size_t ProtocolAdapterRegistry::revoke_source(const std::string_view source_ref) {
  if (source_ref.empty()) {
    return 0U;
  }

  std::lock_guard<std::mutex> lock(write_mutex_);
  const auto current_store = load_store();
  auto next_store = std::make_shared<AdapterStore>();
  next_store->bindings.reserve(current_store->bindings.size());

  std::size_t removed_count = 0U;
  for (const auto& binding : current_store->bindings) {
    if (binding.source_ref == source_ref) {
      ++removed_count;
      continue;
    }

    next_store->bindings.push_back(binding);
  }

  if (removed_count == 0U) {
    return 0U;
  }

  std::atomic_store_explicit(&store_, std::shared_ptr<const AdapterStore>(next_store),
                             std::memory_order_release);
  return removed_count;
}

std::shared_ptr<const ProtocolAdapterRegistry::AdapterStore>
ProtocolAdapterRegistry::load_store() const {
  auto current_store = std::atomic_load_explicit(&store_, std::memory_order_acquire);
  if (!current_store) {
    current_store = std::make_shared<const AdapterStore>();
  }

  return current_store;
}

}  // namespace dasall::access
