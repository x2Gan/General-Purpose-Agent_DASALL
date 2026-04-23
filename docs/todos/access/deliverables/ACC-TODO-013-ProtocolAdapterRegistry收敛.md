# ACC-TODO-013 设计收敛文档

## 1. 任务定义

实现 `ProtocolAdapterRegistry`，为 access 主链提供 module-local 的协议适配器注册、解析与 source 撤销能力。

本任务范围：

1. 落盘 `access/src/ProtocolAdapterRegistry.h` 与 `access/src/ProtocolAdapterRegistry.cpp`。
2. 将 registry 接入 `dasall_access` 静态库。
3. 新增 `ProtocolAdapterRegistryTest.cpp` 与 `ProtocolAdapterRegistryConflictTest.cpp`。
4. 回写 TODO 状态与证据，并完成提交推送。

## 2. 边界与职责

### 2.1 职责

1. 在启动/装配期注册 `entry_type + protocol_kind` 到 adapter 的 binding。
2. 在请求入口阶段解析 decoder adapter。
3. 在发布阶段解析 encoder adapter。
4. 按 source 批量撤销 bindings，支撑组合根重装或入口卸载。

### 2.2 非职责

1. 不管理 socket、IPC、线程或 transport 生命周期。
2. 不执行认证、授权、Admission 或 runtime 调用。
3. 不把 registry 内部结构暴露到 `access/include/` 公共 ABI。
4. 不在持锁路径里执行 adapter 的 decode/encode 或任何 I/O。

## 3. 数据与接口说明

### 3.1 内部数据模型

1. `AdapterKey`
   - 字段：`entry_type`、`protocol_kind`
   - 用途：标识唯一的 decoder/encoder binding。

2. `EncodeTargetRef`
   - 字段：`entry_type`、`protocol_kind`
   - 用途：发布路径查找 encoder 的目标引用。

3. `AdapterBinding`
   - 字段：`source_ref`、`key`、`adapter`
   - 用途：记录某个 source 对某个 binding 的所有权。

4. `AdapterStore`
   - 不对外暴露；内部保存 bindings 快照。
   - 更新方式采用 snapshot-and-swap，读路径读取只读快照。

### 3.2 内部接口

1. `register_adapter(source_ref, entry_type, protocol_kind, adapter)`
   - 成功注册返回 `true`。
   - 若 binding 已存在或 source 试图重复注册同一 key，返回 `false`。

2. `resolve_decoder(entry_type, protocol_kind)`
   - 返回匹配 adapter；无匹配返回空指针。

3. `resolve_encoder(EncodeTargetRef)`
   - 返回匹配 adapter；无匹配返回空指针。

4. `list_bindings()`
   - 返回当前快照中的 binding 列表，供组合根与测试观察。

5. `revoke_source(source_ref)`
   - 撤销该 source 的全部 bindings，返回移除数量。

## 4. 流程与时序

1. 启动期：entry 壳层向 registry 注册本 entry 可用 adapters。
2. 请求期：入口回调基于 `entry_type/protocol_kind` 解析 decoder。
3. 发布期：publisher 基于 `EncodeTargetRef` 解析 encoder。
4. 热更新/卸载期：组合根调用 `revoke_source(source_ref)`，registry 复制快照、删除 source 绑定并原子替换新快照。

## 5. 冲突与失败语义

1. 重复 binding 冲突必须在注册期失败，禁止静默覆盖。
2. source 撤销只影响新快照，不回写旧快照，不破坏已获取的读视图。
3. resolve 无匹配时返回空指针，由上游映射为 unsupported protocol。
4. 写路径只做快照复制与替换，不执行 adapter 方法，避免持锁 I/O。

## 6. Design -> Build 映射

| 设计项 | Build 落点 |
|---|---|
| module-local registry 类型 | `access/src/ProtocolAdapterRegistry.h` |
| snapshot-and-swap 注册/撤销实现 | `access/src/ProtocolAdapterRegistry.cpp` |
| access 库接线 | `access/CMakeLists.txt` |
| 正向注册/解析/撤销验证 | `tests/unit/access/ProtocolAdapterRegistryTest.cpp` |
| 冲突拒绝验证 | `tests/unit/access/ProtocolAdapterRegistryConflictTest.cpp` |
| 测试注册 | `tests/unit/access/CMakeLists.txt` |

## 7. 文件范围

1. `access/src/ProtocolAdapterRegistry.h`
2. `access/src/ProtocolAdapterRegistry.cpp`
3. `access/CMakeLists.txt`
4. `tests/unit/access/ProtocolAdapterRegistryTest.cpp`
5. `tests/unit/access/ProtocolAdapterRegistryConflictTest.cpp`
6. `tests/unit/access/CMakeLists.txt`
7. `docs/todos/access/DASALL_access子系统专项TODO.md`
8. 本文档

## 8. 验收三件套

### 8.1 代码目标

1. 实现 module-local registry 与 source ownership 管理。
2. 读路径提供 decoder/encoder 查找，写路径提供注册与撤销。

### 8.2 测试目标

1. `ProtocolAdapterRegistryTest`：验证 register -> resolve -> revoke 正向路径。
2. `ProtocolAdapterRegistryConflictTest`：验证重复 binding / duplicate source-key 冲突拒绝。

### 8.3 验收命令

```bash
cmake --build build-ci --target \
  dasall_access_protocol_adapter_registry_unit_test \
  dasall_access_protocol_adapter_registry_conflict_unit_test && \
ctest --test-dir build-ci -R "ProtocolAdapterRegistryTest|ProtocolAdapterRegistryConflictTest" --output-on-failure
```

说明：当前仓库全量 `dasall_unit_tests` 仍受 knowledge 既有编译问题影响，本任务使用定向目标验收。

## 9. 风险与回退

1. 若后续需要更高并发读优化，可在不改接口的前提下替换 store 容器实现。
2. 若 adapter 生命周期后续改为工厂模式，`AdapterBinding` 保持 source/key 语义不变即可平滑迁移。
3. 本任务仅落 registry，不推进 entry adapter 真实 decode/encode 行为。
