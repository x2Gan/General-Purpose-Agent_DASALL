#pragma once

#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

#include "IProtocolAdapter.h"

namespace dasall::access {

// ProtocolAdapterRegistry 是 access 内部组件，不进入 public include。
// 它只负责维护 entry/protocol 到 adapter 的稳定绑定视图。
class ProtocolAdapterRegistry {
 public:
  struct AdapterKey {
    std::string entry_type;
    std::string protocol_kind;

    [[nodiscard]] bool has_consistent_values() const;
    [[nodiscard]] bool matches(std::string_view expected_entry_type,
                               std::string_view expected_protocol_kind) const;
  };

  struct EncodeTargetRef {
    std::string entry_type;
    std::string protocol_kind;

    [[nodiscard]] bool has_consistent_values() const;
  };

  struct AdapterBinding {
    std::string source_ref;
    AdapterKey key;
    std::shared_ptr<IProtocolAdapter> adapter;

    [[nodiscard]] bool has_consistent_values() const;
  };

  ProtocolAdapterRegistry();

  // 在初始化/装配期注册某个 source 所有的 adapter 绑定。
  // 若 source_ref 或 key 非法，或 binding 已存在，则返回 false。
  [[nodiscard]] bool register_adapter(std::string_view source_ref,
                                      std::string_view entry_type,
                                      std::string_view protocol_kind,
                                      std::shared_ptr<IProtocolAdapter> adapter);

  // 按 entry_type/protocol_kind 解析 decoder 适配器。
  // 未命中时返回空指针，由上游决定如何映射 unsupported protocol。
  [[nodiscard]] std::shared_ptr<IProtocolAdapter> resolve_decoder(
      std::string_view entry_type,
      std::string_view protocol_kind) const;

  // 发布路径按目标引用查找 encoder 适配器。
  [[nodiscard]] std::shared_ptr<IProtocolAdapter> resolve_encoder(
      const EncodeTargetRef& target) const;

  // 返回当前快照的只读 binding 列表，供组合根与测试观察。
  [[nodiscard]] std::vector<AdapterBinding> list_bindings() const;

  // 撤销某个 source 的全部 bindings，返回被移除的 binding 数量。
  [[nodiscard]] std::size_t revoke_source(std::string_view source_ref);

 private:
  struct AdapterStore {
    std::vector<AdapterBinding> bindings;
  };

  [[nodiscard]] std::shared_ptr<const AdapterStore> load_store() const;

  mutable std::mutex write_mutex_;
  mutable std::shared_ptr<const AdapterStore> store_;
};

}  // namespace dasall::access
