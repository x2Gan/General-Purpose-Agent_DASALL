# CLCFG-TODO-022 config 专项 Gate 与交付证据收口

日期：2026-05-09
状态：Done
任务来源：`docs/todos/cli/DASALL_cli_config交互式部署配置专项TODO.md`

## 1. 收口目标

`CLCFG-TODO-022` 只负责 config 专项的 closeout 收口，不新增新的 `config` workflow 功能面。

本轮收口只做三件事：

1. 把 `CLCFG-GATE-01` 到 `CLCFG-GATE-06` 的正式命令、证据位置与当前结论统一回写到专项 TODO、deliverable 与 worklog。
2. 修复 noble / `autopkgtest` 5.47 环境下裸 `autopkgtest --validate .` 不能作为稳定 metadata gate 的问题，补入基于 `parse_debian_source()` 的本地兼容 shim，并让 `validate_cli_config_v1.sh` 回到真实可执行状态。
3. 用当前主机上的 live evidence 关闭 `CLCFG-BLK-005`，把 config 专项从“build-tree ready”推进到“close-ready”。

## 2. 收口结论

config 专项当前可以确认进入 close-ready，结论边界如下：

1. P0 的 `show/plan/validate/apply --from-file`、交互式 wizard、canonical 文件事务写入、service action 与安装态 `dasall config` onboarding 已全部落地，并由 build-tree 与 installed-package 两层 gate 锁定。
2. P1 的 LLM secret bootstrap seam、`auth_ref=secret://llm/providers/<provider_ref>` redacted summary，以及安装态 `dasall config` secret onboarding smoke 已经与统一 validator 闭环。
3. `scripts/packaging/validate_autopkgtest_metadata.py` 已把 noble 上失效的裸 `autopkgtest --validate .` 调用收敛为真实 metadata parse gate；`scripts/packaging/validate_cli_config_v1.sh` 已在当前主机实跑通过，说明 `CLCFG-GATE-05` 与 `CLCFG-GATE-06` 都有正式命令和 live evidence。
4. package 级 authoritative qemu `autopkgtest` 仍属于 packaging 专项 gate，不在本次 CLCFG closeout 的边界内；该项继续由 `PKG-TODO-018` 跟踪，不再作为 CLCFG 残余 blocker。

## 3. 证据索引

| 范围 | 结论 | 主要证据 |
|---|---|---|
| `CLCFG-TODO-001` ~ `CLCFG-TODO-005` | Done | `docs/todos/cli/deliverables/CLCFG-TODO-001-config-grammar与TTY语义冻结.md`、`CLCFG-TODO-002-socket与operator-model冻结.md`、`CLCFG-TODO-003-state-plan-transaction冻结.md`、`CLCFG-TODO-004-secret-bootstrap-seam冻结.md`、`CLCFG-TODO-005-toolskill-capability边界冻结.md` |
| `CLCFG-TODO-006` ~ `CLCFG-TODO-020` | Done | `docs/todos/cli/DASALL_cli_config交互式部署配置专项TODO.md` 的任务矩阵、`docs/worklog/DASALL_开发执行记录.md` 中对应 config focused build 记录 |
| `CLCFG-TODO-021` | Done | `debian/tests/pkg-smoke-local-control-plane`、`scripts/packaging/pkg_smoke_install.sh`、`scripts/packaging/validate_cli_config_v1.sh`、`docs/worklog/DASALL_开发执行记录.md` 中 `#606` |
| `CLCFG-TODO-022` | Done | `scripts/packaging/validate_autopkgtest_metadata.py`、更新后的 `scripts/packaging/validate_cli_config_v1.sh`、本 deliverable、`docs/worklog/DASALL_开发执行记录.md` 中 `#608` |

## 4. Gate / Blocker / Risk / OQ 收口状态

### 4.1 Gate

| Gate | 状态 | 收口说明 |
|---|---|---|
| `CLCFG-GATE-01` | PASS | design freeze deliverables 已锁定 grammar、operator model、transaction、secret seam 与 ToolSkillPage scope。 |
| `CLCFG-GATE-02` | PASS | config skeleton、discoverability 与测试拓扑已完成，并可由 `ctest -N` 发现。 |
| `CLCFG-GATE-03` | PASS | non-interactive `show/plan/validate/apply --from-file` focused gate 已完成。 |
| `CLCFG-GATE-04` | PASS | fresh install、modify existing、drift repair 与 service-action 集成 gate 已完成。 |
| `CLCFG-GATE-05` | PASS | metadata shim、secret-focused build-tree gate 与 installed-package lifecycle smoke 已形成同一条 one-shot 验收链。 |
| `CLCFG-GATE-06` | PASS | 专项 TODO、deliverable、worklog 与统一 validator 入口已完成一致性回写。 |

### 4.2 Blocker / Risk / OQ

1. `CLCFG-BLK-001` 至 `CLCFG-BLK-004` 维持已解阻状态；`CLCFG-BLK-005` 已在本轮解除，因为当前主机已具备 `autopkgtest`、passwordless sudo 与同源包产物，且 `validate_cli_config_v1.sh` 实跑通过。
2. `CLCFG-RISK-005` 不再是 closeout 阶段的 live blocker，当前只保留为回归性风险：后续必须持续维护 `validate_cli_config_v1.sh` 与 `validate_autopkgtest_metadata.py`，避免 installed-package gate 再次退化为仅 build-tree 证明。
3. `CLCFG-OQ-001` 至 `CLCFG-OQ-005` 已全部在设计冻结或实现阶段收口；本专项没有剩余未决问题需要继续挂在 closeout 之后。

## 5. 统一验收与结果摘要

config 专项 close-ready 的统一验收入口冻结为：

```bash
bash scripts/packaging/validate_cli_config_v1.sh
```

当前入口的使用前提：

1. 工作树旁存在与当前源码同源的 `.deb` / `.changes` 产物，通常由最近一次 `dpkg-buildpackage -us -uc -b` 提供。
2. 当前主机具备 `autopkgtest`、`python3` 与 passwordless sudo。

本轮 live 结果摘要：

1. `python3 scripts/packaging/validate_autopkgtest_metadata.py`
   - 结果：通过；成功解析 `debian/tests/control` 并发现 `pkg-smoke-local-control-plane`、`pkg-smoke-common-assets` 两个 test entry。
2. `bash scripts/packaging/validate_cli_config_v1.sh`
   - 结果：通过；secret-focused build-tree gate 只构建并执行 `SecretBootstrapWriterIntegrationTest`、`FileSecretBackendTest`、`SecretManagerFacadeTest` 三个 target/test，不再被全量 integration 聚合 target 的无关失败污染。
3. `bash scripts/packaging/validate_ubuntu_dpkg_v1.sh`
   - 结果：通过；fresh install、explicit enable/start、upgrade/conffile、remove/purge 三段 installed-package lifecycle smoke 全绿，并且由 `validate_cli_config_v1.sh` 统一串联。

## 6. 最终边界声明

`CLCFG-TODO-022` 完成后，config 专项后续不再以 closeout 名义继续扩张范围：

1. 不把 package-level authoritative qemu `autopkgtest`、`lintian` 或其他 packaging closeout 动作并入 CLCFG；这些项继续留在 `PKG-TODO-018`。
2. 不把 `0600 root/sudo-only` operator model 回退成组访问承诺；如果未来要引入 `0660 dasall group`，必须另立 daemon/platform/packaging 联动任务。
3. 不把 tools/skills editable operator surface 借 closeout 名义提前打开；P2 能力仍需等待 owner surface 冻结后再进入新的专项任务。