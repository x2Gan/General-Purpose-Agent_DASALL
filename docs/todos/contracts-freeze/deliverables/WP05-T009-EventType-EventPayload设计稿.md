# WP05-T009 EventType / EventPayload 设计稿

最近更新时间：2026-03-20
任务状态：Done
任务编号：WP05-T009
上游输入：WP-02 EventEnvelope 冻结对象（WP02-T011）、WP05-T001 Wave3 rollout 约束、ADR-006/007/008 边界约束

## 0. Phase 0 研究学习证据链

### 本地证据清单

1. EventEnvelope 已冻结头部通用字段：event_id、event_type、event_version、occurred_at_ms、request_id、trace_id；并要求私有语义留在 payload。
2. EventEnvelopeGuards 已具备 header 白名单校验，但缺少“事件类型对象”与“payload 对象”的独立契约与分层守卫。
3. WP05 Wave3 要求 task+event 子域继续执行“对象细化 + 越权阻断”纪律，避免把封套头字段混入 payload 语义面。

### 外部参考清单

1. Protocol Buffers Language Guide（proto3, Updating A Message Type）：新增字段是 wire-safe，变更/复用字段编号会破坏兼容；强调稳定字段演进与向后兼容。
2. JSON Schema Object Reference（Properties/AdditionalProperties）：建议显式声明已知字段并控制额外属性，以避免对象边界漂移与语义泄漏。

### 对本任务的可落地启发

1. EventType 需要冻结最小稳定标识面，避免携带运行时控制与 provider 私有字段。
2. EventPayload 需要冻结 payload 承载面，并提供字段别名审计入口用于阻断头字段混入。
3. 守卫需要分别覆盖 required、field hygiene、forbidden alias 三层，确保可二值判定并产出稳定 reason。

## 1. 任务理解

本任务只处理 WP05-T009：

1. 新增 `contracts/include/event/EventType.h`，冻结事件类型对象边界。
2. 新增 `contracts/include/event/EventPayload.h`，冻结 payload 对象边界。
3. 新增 `contracts/include/event/EventPayloadGuards.h`，实现 EventType/EventPayload 守卫与头字段越权阻断。
4. 新增并注册 `tests/contract/event/EventTypePayloadContractTest.cpp`。

本任务不处理：

1. EventEnvelope 既有字段定义回改。
2. runtime 事件路由、序列化框架细节、provider 私有投递协议。

## 2. 约束与边界

### 2.1 EventType 允许承载

1. 类型标识：`type_key`。
2. 子域归属：`domain`。
3. 版本锚点：`major_version`、`schema_revision`。
4. 生命周期标签：`stability_tier`。

### 2.2 EventPayload 允许承载

1. payload 类型：`payload_type`。
2. payload 内容承载：`payload_json`。
3. schema 追踪锚点：`schema_ref`。
4. 观测审计辅助：`field_aliases`、`producer_module`、`payload_version`。

### 2.3 EventPayload 禁止承载

1. EventEnvelopeHeader 冻结字段别名：`event_id`、`event_type`、`event_version`、`occurred_at_ms`、`request_id`、`trace_id`。
2. EventEnvelope 载体字段别名：`header_keys`。

## 3. Design 原子清单

1. D1：冻结 EventType 最小稳定字段与枚举降级位。
- 输入依据：WP02 EventEnvelope + 枚举生命周期规范。
- 产出路径：`contracts/include/event/EventType.h`。
- 完成判定：required 字段可守卫校验，枚举包含 Unspecified 哨兵。
- 风险与回退：若出现运行态控制字段，回退到最小类型识别面。

2. D2：冻结 EventPayload 承载对象与字段别名审计位。
- 输入依据：WP02 payload 分层原则。
- 产出路径：`contracts/include/event/EventPayload.h`。
- 完成判定：payload 承载字段齐备，未混入封套头字段。
- 风险与回退：若对象直接携带 header 语义，回退并移入 forbidden 守卫。

3. D3：新增 EventType/EventPayload 守卫与 contract test。
- 输入依据：WP05-T009 三件套要求。
- 产出路径：`contracts/include/event/EventPayloadGuards.h`、`tests/contract/event/EventTypePayloadContractTest.cpp`、`tests/contract/CMakeLists.txt`。
- 完成判定：至少 1 个正例 + 1 个负例，decision/reason 可稳定断言。
- 风险与回退：若仅 happy-path，补充 forbidden alias 与字段 hygiene 负例。

## 4. Design -> Build 映射

| D 原子项 | 设计结论 | 对应 Build 动作 | 代码目标 | 测试目标 | 验收命令 |
|---|---|---|---|---|---|
| D1 | EventType 只承载稳定类型标识与版本锚点 | 新增 EventType 对象与 required/field-rules 校验 | contracts/include/event/EventType.h；contracts/include/event/EventPayloadGuards.h | required 缺失、枚举越界负例断言 | cmake --build build-ci --target dasall_contract_tests && ctest --test-dir build-ci -R EventTypePayloadContractTest --output-on-failure |
| D2 | EventPayload 只承载 payload 面并保留字段审计位 | 新增 EventPayload 对象与 field hygiene 校验 | contracts/include/event/EventPayload.h；contracts/include/event/EventPayloadGuards.h | payload_json 缺失、field_aliases 空串/重复负例断言 | 同上 |
| D3 | payload 不得复用封套头字段别名 | 新增 forbidden alias 决策守卫 + contract test + CMake 注册 | contracts/include/event/EventPayloadGuards.h；tests/contract/event/EventTypePayloadContractTest.cpp；tests/contract/CMakeLists.txt | decision/reason 稳定断言（RejectEnvelopeHeaderAlias） | 同上 |

## 5. D Gate 结果

1. D 文档已落盘。
2. Design 原子清单具备二值完成判定。
3. Build 三件套已锁定：
- 代码目标：`contracts/include/event/EventType.h`、`contracts/include/event/EventPayload.h`、`contracts/include/event/EventPayloadGuards.h`
- 测试目标：`tests/contract/event/EventTypePayloadContractTest.cpp`
- 验收命令：`cmake --build build-ci --target dasall_contract_tests && ctest --test-dir build-ci -R EventTypePayloadContractTest --output-on-failure`
4. 范围未越界，满足进入 Build 条件。

Gate 结论：PASS。

## 6. Build 执行清单

1. B1：新增 EventType 契约对象与 required/field-rules 守卫。
2. B2：新增 EventPayload 契约对象与 required/field-rules 守卫。
3. B3：新增 payload forbidden alias 守卫，阻断封套头字段混入。
4. B4：新增并注册 EventTypePayloadContractTest（正例 + 负例 + 决策断言）。

## 7. Build 合规复核

1. 新增代码已补充字段级与守卫语义注释。
2. 测试覆盖正例与负例，并断言 stable decision/reason。
3. 测试已注册到 contract tests，具备可发现性。
4. TODO 已回写状态与验收证据。

## 8. Blocker 状态

当前无 blocker。
