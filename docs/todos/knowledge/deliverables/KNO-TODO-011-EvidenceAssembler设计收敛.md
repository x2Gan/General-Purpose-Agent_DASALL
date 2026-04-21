# KNO-TODO-011 EvidenceAssembler 设计收敛

## 1. 输入与目标

1. 来源任务：`KNO-TODO-011 | 实现 EvidenceAssembler 与 ContextProjectionMapper`。
2. 设计锚点：`docs/architecture/DASALL_knowledge子系统详细设计.md` 中 6.13.2 `EvidenceAssembler` 卡片与 7 KNO-D04。
3. 前置满足：`KNO-TODO-006` 已冻结 `EvidenceSlice` / `EvidenceBundle` public ABI，`KNO-TODO-007` 已冻结 `KnowledgeConfigSnapshot`，`KNO-TODO-010` 已提供稳定的 `RankedHitSet` 输入。
4. 本轮目标：只落纯计算证据组装层，把 `RankedHitSet` 收敛为 `EvidenceBundle` 与 `context_projection`，不提前实现 facade、recall 或 runtime handoff。

## 2. 设计结论

### 2.1 边界与职责

1. `EvidenceAssembler` 负责：
   - 把 `RankedHit` 转成结构化 `EvidenceSlice`；
   - 基于 token budget 和 `max_context_projection_items` 生成 `context_projection`；
   - 记录 `omitted_sources`、`degraded` 与 `evidence_insufficient`；
   - 显式保留 stale 证据的 freshness 语义。
2. `EvidenceAssembler` 不负责：
   - 发起 recall；
   - 重新排序 hit；
   - 直接写 `ContextPacket`；
   - 生成最终回答或跨层 token 精裁剪。

### 2.2 数据与接口

1. 新增 `knowledge/include/evidence/EvidenceAssembler.h`：
   - `EvidenceAssemblePolicy`
   - `EvidenceAssembler::assemble()`
2. `EvidenceAssemblePolicy` 固定以下语义：
   - `retrieval_evidence_budget_hint > 0` 时优先使用 hint；
   - 否则使用 `KnowledgeConfigSnapshot.evidence_budget_tokens`；
   - `max_context_projection_items` 受 config 上限约束。
3. `ContextProjectionMapper` 保持 module-local，不另建 shared contracts；v1 以 `build_projection_line()` 私有 helper 固定映射规则。

### 2.3 规则收敛

1. `EvidenceSlice.confidence` 采用：

$$
\text{confidence} = \text{clamp}\left(\frac{\text{fused\_score}}{\text{max\_fused\_score}} \times (1 - \text{stale\_confidence\_penalty}), 0, 1\right)
$$

其中 stale penalty 在 fresh 情况下视为 `0`，默认 stale penalty 为 `0.1`。

2. token 估算采用 `chars / 4` 粗估；为避免 CJK 误差导致超支，v1 固定预留 `10%` 安全余量。
3. budget 不足时优先裁掉 `context_projection`，不得优先删除结构化 `slices`。
4. stale 证据必须同时体现在：
   - `EvidenceSlice.freshness = FreshnessState::StaleAllowed`
   - `context_projection` 文本中显式追加 `[stale]` 标记
5. 单条 hit 数据不完整时只丢弃该条，并把结果标记为 `degraded`；不得污染整个 bundle。

### 2.4 评审补充

1. `context_projection` 只允许单行、可追溯的文本投影，格式固定为 authority prefix + snippet + citation；不得把 raw payload 或全文文档直接塞入共享投影面。
2. `omitted_sources` 记录被 budget 或 item cap 裁掉的 citation ref，为 Runtime 后续澄清或审计留 trace。

## 3. Design -> Build 映射

1. `knowledge/include/evidence/EvidenceAssembler.h`
   - 定义 `EvidenceAssemblePolicy` 与 `EvidenceAssembler`。
2. `knowledge/src/evidence/EvidenceAssembler.cpp`
   - 实现 `EvidenceSlice` 构造、projection line 生成、budget clamp 与 degraded/evidence_insufficient 语义。
3. `tests/unit/knowledge/EvidenceAssemblerTest.cpp`
   - 验证结构化 slice 生成、confidence 公式与 policy derive。
4. `tests/unit/knowledge/ContextProjectionMapperTest.cpp`
   - 验证单行 projection、authority prefix 与 stale marker。
5. `tests/unit/knowledge/EvidenceBudgetClampTest.cpp`
   - 验证 budget clamp、omitted_sources 与 empty projection 回退语义。

## 4. 验证计划

1. Build_CMakeTools：`dasall_knowledge`、`dasall_evidence_assembler_unit_test`、`dasall_context_projection_mapper_unit_test`、`dasall_evidence_budget_clamp_unit_test`。
2. RunCtest / 显式 `ctest`：`EvidenceAssemblerTest`、`ContextProjectionMapperTest`、`EvidenceBudgetClampTest`。
3. build-ci 验收：
   - `cmake -S . -B build-ci -G "Unix Makefiles"`
   - `cmake --build build-ci --target dasall_knowledge dasall_evidence_assembler_unit_test dasall_context_projection_mapper_unit_test dasall_evidence_budget_clamp_unit_test`
   - `ctest --test-dir build-ci -R "(EvidenceAssembler|ContextProjectionMapper|EvidenceBudgetClamp).*Test" --output-on-failure`

## 5. 完成判定

1. `EvidenceBundle.slices`、`context_projection` 与 `omitted_sources` 可分离验证。
2. budget clamp 先裁 projection，不裁结构化 `slices`。
3. 011 交付后，012/027 可直接消费稳定的 `EvidenceBundle` / `context_projection`，而不再重写 shared projection 规则。