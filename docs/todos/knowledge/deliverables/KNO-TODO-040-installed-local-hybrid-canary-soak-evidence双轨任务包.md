# KNO-TODO-040 installed local hybrid canary / soak 证据双轨任务包

## 1. 输入与目标

1. 来源任务：`KNO-TODO-040 | 固化 installed local hybrid canary / soak 证据`。
2. 上游前置：`KNO-TODO-039-B` 已完成，daemon/access retrieve payload 与 production telemetry 现已稳定暴露 `vector_backend_ready`、`warning_summary`、`selected_corpora`、`sparse_hit_count`、`dense_hit_count` 等 explain 字段；但 installed local proof / soak artifact 仍只固定 provider-query baseline，没有 hybrid canary request 与 vector explain 证据。
3. 设计锚点：`docs/architecture/DASALL_knowledge子系统详细设计.md` 中 `KnowledgeTelemetry` / KNO-D06；`docs/todos/DASALL_子系统查漏补缺专项记录.md` 中第三阶段 `KNO-TODO-040` 定义；`docs/todos/knowledge/deliverables/KNO-FIX-010-release-runner-local-installed-proof-and-soak-evidence.md`；ADR-006 / ADR-007 / ADR-008 owner boundary。
4. 本轮目标：在不把 qemu 作为完成前提、不中断既有 provider baseline proof owner 的前提下，为 `knowledge_local_installed_proof.sh`、`knowledge_refresh_retrieve_soak.sh` 与 release local workflow 固定 hybrid canary request、返回 mode、dense artifact presence、degraded reasons 与 corpus hit summary 的 artifact，使 release gate 不再只有 lexical-only positive proof。

## 2. 研究证据

### 2.1 本地证据

1. `scripts/packaging/knowledge_local_installed_proof.sh` 当前只接受 `--artifact-dir` / `--timeout-ms`，并把 retrieve 固定为 `dasall-cli knowledge retrieve "$PROVIDER_QUERY" --json --timeout-ms ...`；产物只有 `knowledge-retrieve-provider.json` 与 `knowledge-proof.json` 中的 `provider_*` 汇总，没有 hybrid canary request 或 vector explain 字段。
2. `scripts/packaging/knowledge_refresh_retrieve_soak.sh` 当前每轮只做 `refresh -> provider retrieve -> health`，summary 只统计 `provider_slice_count`、`provider_has_expected_evidence`、health freshness / snapshot 等字段，没有 hybrid canary per-iteration artifact 与汇总。
3. `.github/workflows/release-package-gate.yml` 当前只以 baseline 方式调用上述两个脚本，并归档 `knowledge-proof.json` / `knowledge-soak-summary.json`；workflow 中没有 `--hybrid-canary` 或 vector explain artifact seam。
4. `apps/cli/src/CliCommandParser.cpp` 已支持 `dasall-cli knowledge retrieve <query_text> --preferred-mode <lexical-only|dense-only|hybrid> --allowed-corpus <id> ... --json`；因此 040 不需要发明新的 control-plane 命令，只需把现成 query surface 固定到 packaging proof owner。
5. `tests/integration/knowledge/KnowledgeInstalledAssetHybridProbeIntegrationTest.cpp` 已证明 installed asset service 的 allowlisted hybrid canary 正例语义：`preferred_mode=Hybrid` 且 `allowed_corpora={"adr_normative"}` 时可返回 `mode=Hybrid` 与 `runtime_canary_admitted` reason code。这给本轮 local hybrid canary request 选择 `adr_normative` / `ssot_normative` 等 allowlisted corpus 提供了现成锚点。
6. 既有 repo memory `knowledge-installed-normative-baseline-2026-05-19.md` 与 `packaging-local-lifecycle-gate-2026-05-09.md` 已明确：authoritative local owner 是 `knowledge_local_installed_proof.sh` / `knowledge_refresh_retrieve_soak.sh`，qemu 不是本轮完成前提；本轮应继续把 local proof 当作 release-runner authoritative evidence owner，而不是把 package smoke 或 qemu 混入 acceptance。

### 2.2 外部实践

1. Azure AI Search《Hybrid search using vectors and full text in Azure AI Search》强调 hybrid query 的稳定可观测最小面应包含请求模式、参与 lane 与结果集合，而不是仅返回单个成功/失败状态。对 DASALL 的 release proof 来说，这意味着 artifact 至少要固定 hybrid request、最终 mode、selected corpora 与 lane hit 摘要。
2. OpenSearch《Hybrid search explain》强调深度 explain 成本较高，生产 gate 应优先固定轻量、稳定的 explain surface。对 DASALL 的 installed proof / soak 而言，这意味着应直接复用 039 已发布的 `warning_summary`、`vector_backend_ready`、`dense_hit_count`、`selected_corpora` 等轻量字段，而不是再造第二套 release-only schema。

## 3. 设计结论

### 3.1 边界与不变式

1. 040 的 owner 仍是 packaging local proof / soak script 与 release workflow；不把 canary admission、refresh automation 或 telemetry owner 下沉回 Knowledge。
2. qemu / machine isolation 继续留给 release hardening；040 的完成判定只依赖 local installed artifact、workflow wiring 与必要的 package smoke contract，不把 qemu 作为硬前置。
3. 039 已发布的 JSON explain 字段就是 040 的 artifact source of truth；本轮只做脚本透传、summary 抽取与 workflow 固化，不新增第二套 hybrid explain ABI。
4. 040 必须保持 provider baseline proof owner 不回退：hybrid canary 是 additive artifact，不替换既有 `knowledge-retrieve-provider.json` / `knowledge-proof.json` provider 摘要。

### 3.2 artifact 设计

| 设计点 | Owner | 方案 | 理由 |
|---|---|---|---|
| hybrid canary 开关 | Packaging scripts | 为 `knowledge_local_installed_proof.sh` 与 `knowledge_refresh_retrieve_soak.sh` 增加 `--hybrid-canary` 开关；未开启时保持现有 baseline 行为 | 让 release workflow 与本机手工验收能显式选择 canary 证据，而不破坏既有正向 proof owner |
| canary 请求模板 | Packaging scripts | 固定 `dasall-cli knowledge retrieve <query> --preferred-mode hybrid --allowed-corpus <id> --json --timeout-ms <ms>`，query / corpus 可通过 env 或脚本常量配置，但默认必须命中 runtime allowlisted installed normative corpus | 040 的关键不是新命令，而是把已有 canary query 变成稳定 artifact contract |
| local proof artifact | `knowledge_local_installed_proof.sh` | 新增 `knowledge-retrieve-hybrid-canary.json`，并把 `hybrid_canary_*` 摘要写入 `knowledge-proof.json`：至少包括 request、mode、degraded、reason_codes、warning_summary、selected_corpora、vector_backend_ready、sparse_hit_count、dense_hit_count、has_dense_artifact | 让 local authoritative proof 直接回答“是否真的跑了 hybrid canary，以及 dense lane 有没有实证” |
| soak artifact | `knowledge_refresh_retrieve_soak.sh` | 每轮新增 `iteration-XX-retrieve-hybrid-canary.json`，summary 新增 `hybrid_mode_values`、`all_hybrid_iterations_have_selected_corpora`、`all_hybrid_iterations_record_dense_artifact_presence`、`min_hybrid_dense_hit_count` 等汇总 | 让 soak 不再只证明 provider baseline 可用，而能证明 hybrid canary 在 refresh loop 下可重复观测 |
| release workflow wiring | `.github/workflows/release-package-gate.yml` | local knowledge proof / soak 步骤固定追加 `--hybrid-canary`，并归档新增 hybrid artifact 文件 | 保证 release-runner local 证据默认包含 hybrid canary，而不是靠人工临时 rerun |
| optional package smoke guard | `debian/tests/pkg-smoke-local-control-plane` | 若 local proof artifact contract 需要显式 smoke 断言，则只检查 hybrid artifact 已落盘，不在 autopkgtest 中重做高成本 canary 逻辑 | 保持 smoke 轻量，避免把 authoritative owner 从 proof script 转移到 autopkgtest |

### 3.3 最小实现面

1. `scripts/packaging/knowledge_local_installed_proof.sh`
   - 增加 `--hybrid-canary` 开关、hybrid canary retrieve artifact、`knowledge-proof.json` 的 `hybrid_canary_*` 摘要提取。
2. `scripts/packaging/knowledge_refresh_retrieve_soak.sh`
   - 增加 `--hybrid-canary` 开关、per-iteration hybrid canary artifact、`knowledge-soak-summary.json` 的 hybrid 汇总字段。
3. `.github/workflows/release-package-gate.yml`
   - local installed knowledge proof / soak 步骤固定追加 `--hybrid-canary`，并确保 upload-artifact 继续包含新增文件。
4. 可选收口
   - `debian/tests/pkg-smoke-local-control-plane` 或 `scripts/packaging/README.md` 如需同步 artifact contract，则只补轻量断言 / 文档，不新增第二条 proof owner。

## 4. Design 原子清单

| D 原子项 | 设计目标 | 输入依据 | 产出 | 完成判定 | 风险与回退 |
|---|---|---|---|---|---|
| D1 | 固定 local proof 的 hybrid canary artifact contract | 本地证据 1/3/4/5；KNO-FIX-010 deliverable | 本文 §3.1 / §3.2 / §3.3 | `knowledge-proof.json` 与新增 hybrid canary artifact 的字段边界明确 | 若需要另造 release-only JSON schema 才能表达 039 explain 字段，则 D Gate 失败 |
| D2 | 固定 soak 的 hybrid repeatability contract | 本地证据 2/3/6；040 TODO 行 | 本文 §3.2 / §5 / §7 | per-iteration canary artifact 与 summary 汇总口径明确 | 若 soak 仍只能记录 provider baseline，而无法表达 hybrid canary repeatability，则 D Gate 失败 |
| D3 | 固定 workflow / package smoke 的最小接线 | 本地证据 3/6；packaging local lifecycle baseline | 本文 §3.2 / §5 / §7 | workflow 是否追加 `--hybrid-canary`、是否需要 smoke 轻断言已经清楚 | 若需要把 qemu 或 autopkgtest 当作 040 完成前提，则 D Gate 失败 |

## 5. Design -> Build 映射

| Design 项 | Build 原子项 | 代码目标 | 测试目标 | 验收命令 |
|---|---|---|---|---|
| D1 local proof hybrid artifact | B1 扩展 installed proof 脚本与 summary | `scripts/packaging/knowledge_local_installed_proof.sh` | 脚本语法检查；local proof artifact contract | `sh -n scripts/packaging/knowledge_local_installed_proof.sh`；`dpkg-buildpackage -us -uc -b`；`bash scripts/packaging/knowledge_local_installed_proof.sh --artifact-dir /tmp/dasall-kno-todo-040-proof --hybrid-canary` |
| D2 soak hybrid repeatability | B2 扩展 soak 脚本与 summary | `scripts/packaging/knowledge_refresh_retrieve_soak.sh` | 脚本语法检查；local soak artifact contract | `sh -n scripts/packaging/knowledge_refresh_retrieve_soak.sh`；`bash scripts/packaging/knowledge_refresh_retrieve_soak.sh --artifact-dir /tmp/dasall-kno-todo-040-soak --iterations 10 --hybrid-canary` |
| D3 workflow / smoke 接线 | B3 固定 release workflow 与可选 smoke 断言 | `.github/workflows/release-package-gate.yml`；可选 `debian/tests/pkg-smoke-local-control-plane` | workflow wiring review；必要时 package smoke 轻断言 | `rg -n -- '--hybrid-canary|knowledge-retrieve-hybrid-canary|hybrid_canary_' scripts/packaging .github/workflows/release-package-gate.yml debian/tests/pkg-smoke-local-control-plane` |

## 6. D Gate 结果

结论：PASS。

通过依据：

1. 040 的控制点已收敛到 packaging proof / soak owner 与 workflow wiring，不需要修改 Knowledge / Runtime 主链行为。
2. hybrid canary 所需的 query surface 与 explain 字段已经分别由 034/035/038/039 完成，本轮只需把它们固化为 installed artifact contract。
3. 验收出口明确：脚本语法检查、`.deb` 重打包、本机 local installed proof / soak 运行即可形成 authoritative evidence，不需要 qemu。
4. 若 package smoke 需要参与，也只需做“artifact 已落盘”的轻量断言，不改变 authoritative owner。

进入 `KNO-TODO-040-B` 的前提：

1. 不新增 qemu / autopkgtest 硬前置。
2. 不替换既有 provider baseline artifact owner。
3. hybrid canary artifact 必须直接复用 039 已发布的 explain 字段，而不是定义 release-only schema。

## 7. Build 原子清单

1. B1：扩展 installed proof 脚本
   - 代码目标：`scripts/packaging/knowledge_local_installed_proof.sh`
   - 测试目标：脚本语法检查；fresh-install local proof artifact contract
   - 验收命令：`sh -n scripts/packaging/knowledge_local_installed_proof.sh`；`dpkg-buildpackage -us -uc -b`；`bash scripts/packaging/knowledge_local_installed_proof.sh --artifact-dir /tmp/dasall-kno-todo-040-proof --hybrid-canary`
   - 风险与回退：若 release runner secrets / encoder 不可用，artifact 仍需记录 mode / degraded / reason / dense artifact presence，不得把“无混合证据”静默吞掉
2. B2：扩展 soak 脚本
   - 代码目标：`scripts/packaging/knowledge_refresh_retrieve_soak.sh`
   - 测试目标：脚本语法检查；10 轮 refresh + hybrid canary artifact contract
   - 验收命令：`sh -n scripts/packaging/knowledge_refresh_retrieve_soak.sh`；`bash scripts/packaging/knowledge_refresh_retrieve_soak.sh --artifact-dir /tmp/dasall-kno-todo-040-soak --iterations 10 --hybrid-canary`
   - 风险与回退：若某轮 hybrid canary 只能 lexical fallback，也必须把 mode / reason / dense artifact presence 写入 summary，不得让 soak 摘要只剩 provider baseline 成功
3. B3：固定 release workflow / smoke 接线
   - 代码目标：`.github/workflows/release-package-gate.yml`；可选 `debian/tests/pkg-smoke-local-control-plane`
   - 测试目标：workflow wiring review；必要时 package smoke 轻断言
   - 验收命令：`rg -n -- '--hybrid-canary|knowledge-retrieve-hybrid-canary|hybrid_canary_' scripts/packaging .github/workflows/release-package-gate.yml debian/tests/pkg-smoke-local-control-plane`
   - 风险与回退：若 package smoke 不适合承载 hybrid canary 实跑，则只断言 artifact 已落盘，避免把 owner 转移到 autopkgtest

## 8. 回退与后继

1. 回退基线：KNO-FIX-010 已固定的 provider-query proof / soak artifact 继续保留，040 只做 additive hybrid canary 扩展。
2. 兼容基线：040 不修改 Knowledge retrieve owner、runtime canary allowlist owner、refresh owner 或 qemu gate 归属。
3. 测试基线：若本机缺少 qemu / kvm 或 autopkgtest 条件，不构成 040 blocker；但本机 local installed proof / soak 必须能产出 hybrid canary artifact。
4. 后继顺序：`KNO-TODO-040-B -> KNO-TODO-041`。
5. 禁区：040 不在 release workflow 里新增第二套 proof owner，也不把 scheduled refresh automation 偷渡到 Knowledge；这些留给 041。

## 9. 完成判定

`KNO-TODO-040-B` 仅当以下条件同时满足时完成：

1. local installed proof 已固定 hybrid canary request，并把 mode、degraded、reason_codes、warning_summary、selected_corpora、vector_backend_ready、sparse/dense hit 摘要写入 artifact。
2. soak summary 已能覆盖 hybrid canary repeatability，不再只有 provider baseline success 结论。
3. release workflow 已默认执行 `--hybrid-canary` 的 local proof / soak，并归档新增 hybrid artifact。
4. 验收命令 `dpkg-buildpackage -us -uc -b`、`bash scripts/packaging/knowledge_local_installed_proof.sh --artifact-dir /tmp/dasall-kno-todo-040-proof --hybrid-canary`、`bash scripts/packaging/knowledge_refresh_retrieve_soak.sh --artifact-dir /tmp/dasall-kno-todo-040-soak --iterations 10 --hybrid-canary` 能形成 authoritative local evidence。
5. 未把 qemu / autopkgtest 变成 040 的完成前提，且未回退 039 的 explain surface owner 边界。

## 10. Build 完成证据

1. `scripts/packaging/knowledge_local_installed_proof.sh` 已新增 `--hybrid-canary` additive path、临时 `DASALL_DETACHED_VECTOR_LOCAL_FALLBACK=1` systemd drop-in、`knowledge-retrieve-hybrid-canary.json` raw artifact 与 `knowledge-proof.json` 的 `hybrid_canary_*` 摘要；同时修正 summary 对 escaped JSON array 的解析与 corpus summary 断言，避免 valid fallback artifact 被误判为失败。
2. `scripts/packaging/knowledge_refresh_retrieve_soak.sh` 已为每轮新增 `iteration-XX-retrieve-hybrid-canary.json`，并把 `hybrid_mode_values`、`all_hybrid_iterations_have_selected_corpora`、`all_hybrid_iterations_record_dense_artifact_presence`、`any_hybrid_iteration_has_dense_artifact`、`min_hybrid_dense_hit_count` 与 `hybrid_reason_code_union` 固化到 `knowledge-soak-summary.json`；末尾 summary 不再错误强制 `Hybrid` / `runtime_canary_admitted`，而是按 040 D 包要求记录 fallback 事实。
3. `.github/workflows/release-package-gate.yml` 与 `scripts/packaging/README.md` 已同步到新的 owner contract：release-runner local proof / soak 步骤默认追加 `--hybrid-canary`，归档与文档说明现在都把 raw hybrid artifact、summary 统计与 fallback-friendly evidence 当作 knowledge local authoritative owner 的一部分。
4. 2026-05-26 已在本机 real installed-package 环境通过 `dpkg-buildpackage -us -uc -b`、`bash scripts/packaging/knowledge_local_installed_proof.sh --artifact-dir /tmp/dasall-kno-todo-040-proof-pass2 --hybrid-canary` 与 `bash scripts/packaging/knowledge_refresh_retrieve_soak.sh --artifact-dir /tmp/dasall-kno-todo-040-soak-pass2 --iterations 10 --hybrid-canary`；`knowledge-proof.json` 记录 `hybrid_canary_mode=lexical_only`、`hybrid_canary_reason_codes` 含 `runtime_canary_backend_not_ready`、`hybrid_canary_selected_corpora=["adr_normative"]`、`hybrid_canary_vector_backend_ready=false`、`hybrid_canary_dense_hit_count=0`，`knowledge-soak-summary.json` 记录 `iterations_completed=10`、`hybrid_mode_values=["lexical_only"]`、`all_hybrid_iterations_have_selected_corpora=true`、`all_hybrid_iterations_record_dense_artifact_presence=true`、`any_hybrid_iteration_has_dense_artifact=false`、`min_hybrid_dense_hit_count=0`，并保留 `runtime_canary_backend_not_ready` 在 `hybrid_reason_code_union` 中；final health 全程保持 `state=degraded`、`freshness_state=fresh` 与 `reason_codes=["vector_backend_disabled"]`，说明当前主机 fallback 事实已被完整落盘，而不是被静默吞掉。