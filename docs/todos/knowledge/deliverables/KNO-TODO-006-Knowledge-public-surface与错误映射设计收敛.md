# KNO-TODO-006 Knowledge public surface 与错误映射设计收敛

- 日期：2026-04-21
- 任务：KNO-TODO-006
- 状态：已收敛
- 对应 Blocker：无

## 1. 输入与约束

1. 005 已经把 `knowledge/include`、unit/integration discoverability 与 file set 骨架落稳；006 的任务必须限定在 root public headers 内冻结 ABI，而不是继续改目录/CMake 拓扑。
2. 详细设计 6.5/6.6 已明确 `KnowledgeQuery`、`EvidenceSlice`、`EvidenceBundle`、`CorpusDescriptor`、`KnowledgeRetrieveResult`、`RefreshResult` 与 `IKnowledgeService` 的 runtime-facing 角色；006 要把这些对象的字段和接口签名一次性收敛到代码。
3. 详细设计 6.8 已冻结 `KnowledgeErrorCode -> ErrorInfo.failure_type/source_ref` 的四类映射边界：只能使用 `Validation` / `Policy` / `Provider` / `Runtime`，不新增 shared contracts 错误模型。
4. `KnowledgeQueryKind::MultiHop`、`latest_observation_digest_summary`、`belief_state_summary` 在 v1 只允许“声明但不消费”；006 必须把这些保留为 ABI 槽位，而不能伪装成已实现功能。
5. `IKnowledgeService::init()` 已在设计上依赖 `KnowledgeConfigSnapshot`，因此 006 需要先冻结 snapshot 结构；但 `KnowledgeHealthSnapshot` 的完整字段仍留给后续 health probe 任务实现，当前只保留接口前向声明。

## 2. 本地证据

| 证据 | 观察结果 | 结论 |
|---|---|---|
| 详细设计 6.5/6.6 public interface 代码块 | 已列出 `KnowledgeQuery`、`EvidenceBundle`、`KnowledgeRetrieveResult`、`RefreshResult` 与 `IKnowledgeService` 四方法 | 006 可以直接把这些对象头文件化 |
| 详细设计 6.8 错误映射表 | 已列出 12 个 `KnowledgeErrorCode`、四类 `failure_type` 和组件锚点 | 006 必须把错误映射 helper 一次落盘并用单测守住 |
| 005 当前仓库状态 | `KnowledgeTypes.h` / `KnowledgeErrors.h` / `IKnowledgeService.h` 已存在，但仍是 skeleton declarations | 006 只需在既有 headers 上增量填实，不需要再改拓扑 |

## 3. 设计结论

### 3.1 Root public headers 冻结范围

1. `knowledge/include/KnowledgeTypes.h` 冻结以下 public types：
   - `KnowledgeQueryKind`、`RetrievalMode`、`FreshnessState`、`TrustLevel`、`AuthorityLevel`、`SourceKind`、`SourceFormat`、`RefreshStatus`
   - `KnowledgeQuery`、`EvidenceSlice`、`EvidenceBundle`、`CorpusDescriptor`
   - `KnowledgeConfigSnapshot`、`KnowledgeRetrieveResult`、`CorpusChangeSet`、`RefreshResult`
2. `KnowledgeConfigSnapshot` 在 006 进入 public surface，是因为 `IKnowledgeService::init()` 需要稳定参数类型；007 只负责实现 projector，不再重新争论 snapshot 字段面。
3. `KnowledgeHealthSnapshot` 在 006 保持前向声明，不在本轮写死字段；这样 025/026 仍可在不破坏 `IKnowledgeService` 签名的前提下补齐 health 实现。

### 3.2 IKnowledgeService 正式签名

`knowledge/include/IKnowledgeService.h` 冻结为：

```cpp
class IKnowledgeService {
public:
  virtual ~IKnowledgeService() = default;

  virtual bool init(const KnowledgeConfigSnapshot& config) = 0;
  virtual KnowledgeRetrieveResult retrieve(const KnowledgeQuery& query) = 0;
  virtual KnowledgeHealthSnapshot health_snapshot() const = 0;
  virtual RefreshResult request_refresh(const CorpusChangeSet& changes) = 0;
};
```

约束：

1. `init()` 只吃已投影好的 `KnowledgeConfigSnapshot`。
2. `retrieve()` 是唯一同步读入口。
3. `health_snapshot()` 保留只读 health 出口，但不在 006 冻结具体字段。
4. `request_refresh()` 只接受 `CorpusChangeSet`，不在 006 暴露 refresh worker / queue / token 等内部机制。

### 3.3 ErrorInfo 映射收敛

1. `KnowledgeErrorCode` 直接使用与 `ResultCodeCategory` 对齐的数值段，确保 module-local 细粒度错误码与四类 failure domain 可程序化对应。
2. `make_knowledge_error_info()` 统一负责填充：
   - `failure_type`
   - `retryable`
   - `safe_to_replan`
   - `details.code/message/stage`
   - `source_ref.ref_type/ref_id`
3. `source_ref.ref_type` 采用 `knowledge::{component}` 形式，例如：
   - `knowledge::facade`
   - `knowledge::config`
   - `knowledge::normalizer`
   - `knowledge::vector_bridge`
4. `source_ref.ref_id` 默认使用稳定的 snake_case 错误名，例如 `not_initialized`、`query_validation_failed`；调用方如需更细粒度锚点，可显式 override。
5. 退化成功路径仍不构造 `ErrorInfo`；006 只冻结失败映射 helper，不把 degrade 成功写成 failure。

## 4. Design -> Build 映射

| Design 项 | Build / 文档落点 |
|---|---|
| `KnowledgeQuery` / `EvidenceBundle` / `CorpusDescriptor` / `KnowledgeRetrieveResult` ABI | `knowledge/include/KnowledgeTypes.h` |
| `KnowledgeErrorCode -> ErrorInfo` 映射 helper | `knowledge/include/KnowledgeErrors.h` |
| `IKnowledgeService` 正式签名 | `knowledge/include/IKnowledgeService.h` |
| interface surface + error mapping unit gate | `tests/unit/knowledge/KnowledgeInterfaceSurfaceSkeletonTest.cpp` |
| TODO / worklog 回写 | `docs/todos/knowledge/DASALL_knowledge子系统专项TODO.md`、`docs/worklog/DASALL_开发执行记录.md` |

## 5. 本任务三件套

- 代码目标：冻结 Knowledge root public types、`IKnowledgeService` 正式签名与 `KnowledgeErrorCode -> ErrorInfo` 映射 helper。
- 测试目标：`dasall_knowledge_interface_surface_unit_test` 覆盖 ABI 字段、错误域映射和 `ErrorInfo` 投影。
- 验收命令：

```bash
cmake -S . -B build-ci -G "Unix Makefiles" && \
cmake --build build-ci --target dasall_knowledge dasall_knowledge_interface_surface_unit_test && \
ctest --test-dir build-ci -R dasall_knowledge_interface_surface_unit_test --output-on-failure
```

## 6. 风险与回退

1. 风险：`KnowledgeHealthSnapshot` 若在 006 就被写死，后续 health probe 任务会被迫围绕半成品字段工作。
   - 处置：当前只在 `IKnowledgeService` 中保留前向声明，完整字段继续留给后续任务。
2. 风险：如果 `KnowledgeErrorCode` 直接复用 shared `ResultCode` 枚举，会把 module-local 细粒度错误域重新推回 contracts。
   - 处置：保留独立 `KnowledgeErrorCode`，仅把 `failure_type` 投影到 contracts 四类 failure domain。
3. 风险：如果 006 把保留字段 `MultiHop` / `latest_observation_digest_summary` / `belief_state_summary` 当成功能完成，会误导后续执行顺序。
   - 处置：明确这些字段在 v1 只冻结 ABI，不进入执行链。

## 7. 收敛结论

1. Knowledge 的 root public surface 已从 skeleton declarations 升级为正式 ABI：查询、证据、语料、refresh 和 config snapshot 类型均已落盘。
2. `IKnowledgeService` 的四个方法签名已冻结，后续 007/025/026 只做实现，不再改接口形状。
3. `KnowledgeErrorCode -> ErrorInfo` 的 category/source/retry 语义已形成单一 helper，006 之后不需要再在 facade/telemetry/test 中分散拼装错误对象。