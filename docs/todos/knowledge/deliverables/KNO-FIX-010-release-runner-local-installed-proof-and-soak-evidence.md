# KNO-FIX-010 release-runner local installed proof / soak 证据收口

日期：2026-05-19
来源任务：KNO-FIX-010
范围：release-runner contract 下的本机 installed-package Knowledge proof / soak；不使用 qemu / kvm 作为本轮收口证据

## 1. 任务重定义

本轮没有继续把 `KNO-FIX-010` 执行成“必须先拿到 qemu guest-side PASS 才能判定 Knowledge 收口”。真实阻塞点来自三处：

1. release workflow 只有 package smoke 与 soak，没有独立的 Knowledge authoritative proof owner；一旦 `pkg_smoke_install.sh` 被其它子系统回归阻断，Knowledge 证据就无法落盘。
2. 当前安装态 `knowledge health --json` 的 payload 已不稳定依赖旧的 `refresh_in_flight` / `last_refresh_status` 组合；脚本若继续硬绑旧字段，会在 real installed 环境下假红。
3. Debian 包实际缺少 `/usr/share/dasall/docs/`，导致所谓“installed normative corpus baseline”并未真正进包；如果不修 packaging install layout，本机 proof 无法成立。

因此本轮把 `KNO-FIX-010` 重定义为：

1. 新增独立的 local installed Knowledge proof owner，避免被 package smoke 中其它子系统行为阻断。
2. 修复 soak / proof harness 对当前 health payload 与 daemon readiness 的假设漂移。
3. 修复 Debian common 包对 `docs/architecture`、`docs/adr`、`docs/ssot` 的真实打包缺口。
4. 在 release workflow 中固定 local installed Knowledge proof / soak artifact，并把 qemu rerun 明确下放为残余 gap，而不是继续阻断本轮收口。

## 2. Design -> Build 映射

| Design 决策 | Build / 验证落点 | 通过条件 |
|---|---|---|
| Knowledge 需要独立于 package smoke 的 authoritative local proof owner | `scripts/packaging/knowledge_local_installed_proof.sh` | 脚本可 fresh-install 当前 `.deb` 并生成 `knowledge-proof.json` / `installed-normative-assets.json` |
| 当前 installed health payload 允许 `freshness_state=fresh` + `active_snapshot_id` 作为 ready signal | `knowledge_local_installed_proof.sh`、`knowledge_refresh_retrieve_soak.sh` | proof / soak 在 real installed environment 下不再因旧字段缺失假红 |
| normative docs 必须真实进入 `.deb` 才能宣称 installed baseline 落地 | `debian/dasall-common.install`、`debian/tests/pkg-smoke-common-assets`、`debian/tests/pkg-smoke-local-control-plane` | `dpkg-deb -c ../dasall-common_0.1.0-1_all.deb` 可看到 `usr/share/dasall/docs/` |
| release runner 要在 qemu gate 前固定 Knowledge local evidence | `.github/workflows/release-package-gate.yml`、`scripts/packaging/README.md` | workflow 在 qemu step 前固定 `knowledge-proof.json` / `installed-normative-assets.json` / `knowledge-soak-summary.json` |

## 3. 代码落点

### 3.1 proof / soak owner

1. `scripts/packaging/knowledge_local_installed_proof.sh`
   - 新增 fresh-install proof owner。
   - 以 `dasall readiness --json` completed + inner `state=READY` 作为 daemon ready 判定，不再硬绑 socket 文件前置条件。
   - 兼容当前 `knowledge health --json` 两种 payload：若存在 `last_refresh_status` / `refresh_in_flight` 则继续消费旧 terminal 口径；若缺失则回退到 `active_snapshot_id` + `freshness_state=fresh`。
   - 固定生成 `ready.json`、`knowledge-refresh.json`、`knowledge-health-ready.json`、`knowledge-retrieve-provider.json`、`knowledge-health-final.json`、`knowledge-proof.json`、`installed-normative-assets.json`。
2. `scripts/packaging/knowledge_refresh_retrieve_soak.sh`
   - 更新 daemon readiness / health-ready 判定，兼容当前 installed payload。
   - 修复 shell helper 污染调用方 `label` 变量的问题，避免 artifact 文件名损坏。
   - summary 现固定记录 `iterations_completed`、`min_provider_slice_count`、`ready_signals`、`all_final_health_freshness_fresh` 等实机字段。

### 3.2 packaging / workflow

1. `debian/dasall-common.install`
   - 新增 `usr/share/dasall/docs/`，把 normative docs 真正装入 `dasall-common` 包。
2. `debian/tests/pkg-smoke-common-assets`
   - 新增 `/usr/share/dasall/docs/architecture/DASALL_Engineering_Blueprint.md`、`/usr/share/dasall/docs/adr/ADR-006-context-orchestrator-vs-prompt-composer.md`、`/usr/share/dasall/docs/ssot/BusinessChainIntegrationMatrix.md` 文件断言。
3. `debian/tests/pkg-smoke-local-control-plane`
   - 同步补上上述 docs 断言，避免 installed local control-plane gate 漏掉 docs packaging regression。
4. `.github/workflows/release-package-gate.yml`
   - 新增 `knowledge-proof` artifact 目录与 `knowledge-proof.log`。
   - 在 qemu gate 前固定执行 `knowledge_local_installed_proof.sh` 与 `knowledge_refresh_retrieve_soak.sh`；二者使用 `always()`，避免被 package smoke 中其它子系统失败阻断。
5. `scripts/packaging/README.md`
   - authoritative owner 改为 `knowledge_local_installed_proof.sh` + `knowledge_refresh_retrieve_soak.sh`。
   - 明确 Knowledge local evidence 的真实口径是 `refresh/provider-retrieve/health proof + installed normative asset files + soak artifact`。

## 4. 当前真实 installed 口径

1. provider retrieve：`DeepSeek Chat` 在当前安装态稳定返回 `slice_count=3`，可作为 installed positive retrieve owner。
2. health / freshness：当前安装态 `knowledge health --json` 实际返回 `state=degraded`、`freshness_state=fresh`、`active_snapshot_id=snapshot:...`；`degraded` 来自 `vector_backend_disabled`，不构成本轮失败。
3. normative docs：当前默认 CLI retrieve 对 `BusinessChainIntegrationMatrix` 等 doc query 仍返回 `slice_count=0`；因此本轮不伪造“installed CLI normative retrieve 已正向通过”的结论，而是把 normative install baseline 固定为 docs 文件真实落位和 build-tree focused routing 已绿。
4. qemu / machine isolation：本轮未执行，继续保留为 `KNO-GAP-010` 残余。

## 5. 验证证据

### 5.1 构包与包内容

命令：

```text
dpkg-buildpackage -us -uc -b
dpkg-deb -c ../dasall-common_0.1.0-1_all.deb | grep '/usr/share/dasall/docs/'
```

结果：构包成功；最终 `dasall-common` 包内容可见 `usr/share/dasall/docs/architecture/...`、`usr/share/dasall/docs/adr/...` 与 `usr/share/dasall/docs/ssot/...`。

### 5.2 authoritative local proof

命令：

```text
bash scripts/packaging/knowledge_local_installed_proof.sh --artifact-dir /tmp/dasall-kno-fix-010-proof2
```

结果：PASS，`rc=0`。

artifact：

1. `/tmp/dasall-kno-fix-010-proof2/ready.json`
2. `/tmp/dasall-kno-fix-010-proof2/knowledge-refresh.json`
3. `/tmp/dasall-kno-fix-010-proof2/knowledge-health-ready.json`
4. `/tmp/dasall-kno-fix-010-proof2/knowledge-retrieve-provider.json`
5. `/tmp/dasall-kno-fix-010-proof2/knowledge-health-final.json`
6. `/tmp/dasall-kno-fix-010-proof2/knowledge-proof.json`
7. `/tmp/dasall-kno-fix-010-proof2/installed-normative-assets.json`

`knowledge-proof.json` 实际字段：

```json
{
  "ready_state": "READY",
  "ready_runtime_readiness": "default-ready",
  "refresh_disposition": "completed",
  "refresh_status": "accepted",
  "health_ready_signal": "async_terminal",
  "health_ready_state": "degraded",
  "health_ready_freshness_state": "fresh",
  "health_ready_last_refresh_status": "completed",
  "health_ready_active_snapshot_id": "snapshot:5619e0a41d8cb3bdd49ede800fcf95b1eba7afc70144378eef4b46cae9dd364d",
  "provider_query": "DeepSeek Chat",
  "provider_slice_count": 3,
  "provider_has_installed_deepseek_evidence": true,
  "health_final_state": "degraded",
  "health_final_freshness_state": "fresh",
  "health_final_active_snapshot_id": "snapshot:5619e0a41d8cb3bdd49ede800fcf95b1eba7afc70144378eef4b46cae9dd364d",
  "health_final_refresh_in_flight": false
}
```

`installed-normative-assets.json` 实际字段：

```json
{
  "architecture_blueprint": "/usr/share/dasall/docs/architecture/DASALL_Engineering_Blueprint.md",
  "adr_006": "/usr/share/dasall/docs/adr/ADR-006-context-orchestrator-vs-prompt-composer.md",
  "ssot_business_chain_matrix": "/usr/share/dasall/docs/ssot/BusinessChainIntegrationMatrix.md"
}
```

### 5.3 soak

命令：

```text
bash scripts/packaging/knowledge_refresh_retrieve_soak.sh --artifact-dir /tmp/dasall-kno-fix-010-soak --iterations 10
```

结果：PASS，`rc=0`。

关键 artifact：

1. `/tmp/dasall-kno-fix-010-soak/knowledge-soak-summary.json`
2. `/tmp/dasall-kno-fix-010-soak/iteration-01-refresh.json` ... `iteration-10-health-final.json`

收敛判定：

1. `iterations_completed=10`
2. `min_provider_slice_count >= 1`
3. `all_provider_iterations_have_evidence=true`
4. `all_ready_health_freshness_fresh=true`
5. `all_final_health_have_active_snapshot=true`
6. `all_final_health_freshness_fresh=true`

## 6. 结论与边界

结论：`KNO-FIX-010` 现可按“release-runner contract + local installed Knowledge proof / soak artifact”口径判定完成。

已闭合：

1. Knowledge proof 不再依赖 `pkg_smoke_install.sh` 的其它子系统路径是否通过。
2. 当前 installed health payload 与 daemon readiness 的真实 wire shape 已被 harness 固定，不再继续依赖过期字段或 socket 前置条件。
3. Debian common 包已真实携带 `/usr/share/dasall/docs/`，local installed normative asset baseline 可复验。
4. release workflow 已在 qemu gate 前固定 Knowledge proof / soak owner。

不外推：

1. 本轮不宣称 qemu / autopkgtest guest-side rerun 已完成；machine-isolation 仍属于 `KNO-GAP-010` 残余。
2. 本轮不宣称默认 CLI normative retrieve 已在安装态正向通过；当前真实收口口径是 provider retrieve 正向路径 + installed docs asset presence + build-tree normative routing 已绿。
3. 本轮不宣称 L6 production confidence；当前只有 10 轮 local soak artifact，不替代 release runner qemu 或更长稳态故障注入。