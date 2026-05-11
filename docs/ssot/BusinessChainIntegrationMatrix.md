# BusinessChainIntegrationMatrix (Full Business Chain SSOT)

关联任务：FULLINT-TODO-001  
最近更新时间：2026-05-11  
适用阶段：System Integration -> Full Business Chain Verification

## 1. 目标

本文件冻结 DASALL 全量业务链编号、证据层级、Gate 映射与不外推规则，作为 `docs/todos/integration/DASALL_全量业务链集成验证专项TODO-2026-05-11.md` 的业务链证据矩阵 SSOT。

冻结目标不是宣布 production ready，而是把当前代码、安装包运行结果、已注册 Gate、已知缺口和后继任务放在同一张可复验矩阵中，避免继续出现以下偏差：

1. 用 focused / fixture / subsystem smoke 替代真实业务链结论。
2. 用 build-tree `release-preflight` 替代 installed-package / qemu / production 结论。
3. 用历史文档绿灯替代本轮实际包运行状态。
4. 把 `agent.dataset` fallback 当作 installed-package `run` 成功语义。
5. 把 profile 中的 `enabled_modules.multi_agent` 解释成 multi_agent GA runtime-ready。

## 2. 本轮真实证据快照

### 2.1 代码与 Gate 注册证据

本轮以 `rg` 检查当前工作树，而不是采信历史完成状态：

1. `tests/CMakeLists.txt` 当前注册 `dasall_gate_int_03`、`dasall_gate_int_04`、`dasall_gate_int_05`、`dasall_gate_int_06`、`dasall_gate_int_07`、`dasall_gate_int_08`、`dasall_gate_int_09`、`dasall_gate_int_10` 与 `dasall_packaging_preflight_tests`。
2. `tests/integration/access/CMakeLists.txt` 当前将 `CliDaemonSocketPathIntegrationTest`、`DaemonBinaryUnarySmokeTest`、`GatewayBinaryUnarySmokeTest`、`GatewayBinaryMissingBackendRegressionTest` 标记为 `gate-int-10` / `release-preflight-gate`，并把 daemon/gateway diagnostics tests 标记为 `release-preflight-gate`。
3. `scripts/packaging/validate_gate_int_10_installed_package_qemu.sh` 当前按顺序执行 `dasall_gate_int_10`、`dasall_packaging_preflight_tests`、`dpkg-buildpackage -us -uc -b`、`validate_autopkgtest_metadata.py` 与调用方传入的 qemu `autopkgtest`。
4. `runtime/src/AgentOrchestrator.cpp` 当前存在 production LLM direct path、`llm.origin=` 输出、`agent.dataset fallback is disabled` 失败文案、`make_knowledge_query()` 与 memory `write_back()` 调用点。
5. `llm/src/LLMProductionFactory.cpp` 与 `apps/runtime_support/src/RuntimeLiveDependencyComposition.cpp` 当前提供 daemon/runtime live composition 的 LLM 生产装配入口。
6. `multi_agent/` 当前只有 `multi_agent/src/placeholder.cpp` 与静态库骨架；未发现 `IMultiAgentCoordinator`、`NullMultiAgentCoordinator` 或 Runtime 装配点的真实实现。
7. `profiles/desktop_full/runtime_policy.yaml` 与 `profiles/cloud_full/runtime_policy.yaml` 已校准为 `enabled_modules.multi_agent: false`；当前 installed package `0.1.0-1` 中的 `/usr/share/dasall/profiles/{desktop_full,cloud_full}/runtime_policy.yaml` 仍保留旧版 `true`，需等下一次 package rebuild/reinstall 才能完成安装态资产更新。

### 2.2 installed-package 运行证据

本轮在同一工作机上直接采集安装态命令输出：

1. `command -v dasall` 返回 `/usr/bin/dasall`。
2. `dpkg-query -W -f='${binary:Package} ${Version} ${Status}\n' 'dasall*'` 返回 `dasall`、`dasall-cli`、`dasall-common`、`dasall-daemon` 均为 `0.1.0-1 install ok installed`。
3. `systemctl is-active dasall-daemon.service` 返回 `active`；`systemctl is-enabled dasall-daemon.service` 返回 `enabled`。
4. 普通用户执行 `dasall ping --json` 与 `dasall readiness --json` 均因 `/run/dasall/daemon.sock` `Permission denied` 返回 `daemon_unavailable` / `exit_code=3`；说明本轮不能把普通用户控制面宣称为 ready。
5. `sudo -n dasall ping --json` 返回 `disposition=completed`、`task_completed=true`，daemon payload 包含 `profile_id=desktop_full` 与 `readiness=READY`。
6. `sudo -n dasall readiness --json` 返回 `disposition=completed`、`state=READY`、`lifecycle_ready=true`、`listener_ready=true`、`gateway_ready=true`、`bridge_reachable=true`。

### 2.3 外部参考

1. Anthropic, Building effective agents：强调先用简单可组合模式，复杂 agent / orchestrator-workers 模式应以可观测、可验证、受控的中心编排为前提；映射到 DASALL 时，BC-17 不能仅凭 profile enablement 宣称多 Agent runtime-ready。
2. Debian `autopkgtest(1)` man page：`autopkgtest` 用指定虚拟化 testbed 测试已安装二进制包，`.changes` 可驱动源码测试与二进制包安装验证；映射到 DASALL 时，BC-16 的 L5 证据必须来自 release runner / qemu / `autopkgtest`，不能由 build-tree Gate 代替。

## 3. 证据层级

| 层级 | 名称 | 可采信结论 | 不可外推 |
|---|---|---|---|
| L0 | source-code evidence | 当前代码中存在或缺失的入口、接口、装配点、脚本、CMake target | 不代表可运行或 package-ready |
| L1 | focused / fixture | 局部实现或夹具路径可回归 | 不代表 true integration、app-binary 或 installed-package |
| L2 | true integration | 跨模块 shared contract / runtime-facing 行为成立 | 不代表真实二进制入口或安装态 |
| L3 | app-binary / build-tree release-preflight | build tree 下 daemon/gateway/CLI 二进制入口与 release-preflight 可执行 | 不代表 installed-package / qemu / production |
| L4 | installed-package local | 当前机器安装态生命周期、控制面或主功能可执行 | 不代表 release runner、qemu、外部依赖长期稳定 |
| L5 | release runner / qemu | qemu testbed 中 installed-package gate、lintian、artifact 归档可复验 | 不代表 soak / chaos |
| L6 | soak / chaos / production confidence | 长稳态、外部依赖抖动与恢复策略可观测 | 不替代 L2-L5 前置 Gate |

## 4. 业务链矩阵

| 链路 ID | 业务链 | 当前最高实证层 | 代码 / 运行证据 | 冻结结论 | 缺口与后继任务 |
|---|---|---:|---|---|---|
| BC-01 | CLI / daemon 本地控制面 | L4 | `/usr/bin/dasall` 已安装；daemon `active/enabled`；普通用户 ping/readiness 因 socket 权限失败；`sudo -n dasall ping/readiness --json` 完成并返回 READY | rootful local control-plane 可用；普通用户控制面权限仍需独立治理，不能外推为所有本地用户 ready | FULLINT-TODO-006、007、013；关注 readiness 语义和 socket 访问策略 |
| BC-02 | HTTP gateway unary ingress | L3 | `GatewayBinaryUnarySmokeTest`、`GatewayBinaryMissingBackendRegressionTest` 当前注册到 `gate-int-10`；gateway binary smoke 属 build-tree app-binary 证据 | build-tree gateway unary / missing-backend fail-closed 有入口；本轮未执行 HTTP installed-package 正向请求 | FULLINT-TODO-007、008、010、012 |
| BC-03 | Access admission / policy / idempotency | L2 | `dasall_gate_int_08` 注册 Access focused ingress；access integration CMake 含 production submit / readiness / profile guard 目标 | Access v1 focused ingress 有 true-integration owner；不能用 ping/liveness 替代安全治理 | FULLINT-TODO-006、013、017 |
| BC-04 | Async receipt / query / cancel / replay | L2/L4 partial | Access async focused path已注册；installed package CLI 暴露 `status` / `cancel`；本轮未产生真实 receipt，只确认控制面命令存在 | receipt ownership 与 missing receipt 语义需要独立 evidence bundle；不能把命令存在视为 async ready | FULLINT-TODO-011、013 |
| BC-05 | Runtime single-agent unary 主链 | L2/L4 partial | `AgentOrchestrator` 当前有 live unary、production LLM direct path、knowledge query、memory writeback 调用点；本轮未执行 LLM run | 单 Agent 主链不再只是空骨架，但 installed LLM 成功需由 FULLINT-TODO-002/003 当轮命令记录 | FULLINT-TODO-003、007、013 |
| BC-06 | Cognition decision / reflection / response | L2 | cognition/runtime integration fixtures 与 `AgentOrchestrator` cognition 调用点存在；本轮未执行 cognition focused tests | cognition 是主链组成段，但 belief/update/failure 回流仍需主链证据包断言 | FULLINT-TODO-003、012 |
| BC-07 | LLM production generation | L0/L4 historical, L4 rerun pending | `LLMProductionFactory`、runtime response-stage LLM request 与 `llm.origin=` 输出点存在；本轮 001 未跑 provider 调用 | LLM production path 有代码落点；installed-provider origin 必须在 002/003 用实际 `dasall run` 复核 | FULLINT-TODO-012、013、019 |
| BC-08 | Knowledge retrieve / evidence projection | L2 partial | `make_knowledge_query()` 与 knowledge focused gates存在；CLI help 未显示 knowledge retrieve/refresh/health 子命令 | build-tree evidence projection 可追溯；installed-package 缺独立正向入口，不能宣称 package-ready | FULLINT-TODO-014 |
| BC-09 | Memory context assembly | L2 | `IMemoryManager::prepare_context()` 与 memory integration tests 存在；runtime live path通过 dependency set 调用 memory | memory 上下文组装有 runtime-facing 代码落点；installed state 持久化仍需独立验证 | FULLINT-TODO-003、012、015 |
| BC-10 | Memory writeback / compression / maintenance | L2 | `MemoryManager::write_back()` 与 `AgentOrchestrator` writeback 调用点存在；memory writeback/failure tests存在 | writeback 不是文档空口径，但 package lifecycle 下 state path / persistence 仍未在本轮证明 | FULLINT-TODO-011、015 |
| BC-11 | Tools governed execution | L2 | `agent.dataset` 仍作为 tool path 资产存在；runtime production LLM path明确禁用 dataset fallback | tools 作为受控工具链保留；不得再把 `agent.dataset` 作为 installed `run` 成功语义 | FULLINT-TODO-012、016 |
| BC-12 | Capability Services execution/data/system | L2 | tools/services focused integration 文件存在；本轮未执行真实 platform/remote adapter | services 语义仍是 focused 层证据；高风险和真实 remote adapter 不进入当前 ready 结论 | FULLINT-TODO-012、016 |
| BC-13 | Infra config / policy / secret / plugin / diagnostics / health | L3/L4 partial | daemon active/enabled；sudo readiness payload 含 lifecycle/listener/gateway/bridge；startup diagnostics tests 注册到 release-preflight | health/readiness 有安装态信号，但 default/degraded/stub 语义仍需 FULLINT-TODO-007 加严 | FULLINT-TODO-009、017 |
| BC-14 | Profiles build/runtime policy activation | L0/L2 partial | `RuntimePolicySnapshot` 存在；profile YAML 有 `enabled_modules`；provider 当前未把 enabled modules 暴露为 typed snapshot 字段；source full profiles 已禁用 `multi_agent` | profile 策略激活可追溯；未实现 coordinator 前，source profile 不再声明 multi_agent enabled | FULLINT-TODO-004、018 |
| BC-15 | Recovery / safe-mode / resume | L2 partial | runtime recovery/checkpoint types 与 tests存在；本轮未执行 checkpoint/resume installed path | recovery owner 仍归 Runtime；全链 retry/checkpoint/writeback continuity 后续验证 | FULLINT-TODO-011、018 |
| BC-16 | Packaging / installed-package / release handoff | L4 | Debian packages `0.1.0-1` 已安装；daemon active/enabled；串联脚本存在；本轮 local sudo ping/readiness 成功 | local installed control-plane rootful 可用；qemu / lintian / LLM origin 仍需按层复验，不能由 L4 覆盖 L5 | FULLINT-TODO-002、013、019 |
| BC-17 | Multi-agent coordination | L0 | `multi_agent` 当前仅静态库骨架和 placeholder；未发现 Null/Real coordinator 实现；source desktop/cloud profile 已改为禁用态；installed package 仍是旧资产 | 多 Agent 目前是设计/声明层，不是 runtime-ready；FULLINT-BLK-004 已完成 source 层最小解阻，Real/Null coordinator 仍归 FULLINT-TODO-018 | FULLINT-TODO-004、018 |

## 5. Gate 映射

| Gate | 覆盖链路 | 矩阵解释 |
|---|---|---|
| Gate-INT-03 | BC-05、BC-06、BC-09 | default unary true integration；不能替代 installed-package LLM origin |
| Gate-INT-04 | BC-08、BC-09 | structured evidence preservation；不能替代 knowledge installed CLI / daemon positive entry |
| Gate-INT-05 | BC-13 | diagnostics retained snapshot；不能替代 app startup stage 全覆盖 |
| Gate-INT-06 | BC-05、BC-07、BC-14 | required/optional ports、degraded semantics、profile compatibility；不能替代 default-ready 对外投影 |
| Gate-INT-07 | BC-11、BC-12 | tools/services result semantics；不能替代 runtime production caller adapter |
| Gate-INT-08 | BC-01、BC-02、BC-03、BC-04 | Access v1 focused ingress；不能替代 app-binary / installed-package |
| Gate-INT-09 | BC-01~BC-16 的 focused discoverability | one-shot focused evidence closure；不包含 multi_agent ready，也不覆盖 package qemu |
| Gate-INT-10 | BC-01、BC-02、BC-13、BC-16 | build-tree app-binary / release-preflight；不等于 installed-package qemu / production ready |

## 6. Design -> Build 映射

| Design 决策 | Build / 验证落点 | 后继任务 |
|---|---|---|
| BC-01~BC-17 编号唯一，且每条链路只能记录当前最高实证层 | 本文件 §4；TODO §3 回链 | FULLINT-TODO-005、021、022 |
| installed-package 证据必须分 local、LLM origin、qemu、lintian | `pkg_smoke_install.sh`、`sudo dasall run`、`validate_gate_int_10_installed_package_qemu.sh` | FULLINT-TODO-002、013、019 |
| runtime/cognition/memory/llm 主链不能用 `agent.dataset` 代替 LLM 主功能 | `AgentOrchestrator` production LLM path 与 installed `dasall run` | FULLINT-TODO-003 |
| multi_agent profile enablement 必须与实现装配一致 | `profiles/*/runtime_policy.yaml`、`RuntimePolicySnapshot`、`multi_agent/`、`AgentOrchestrator` | FULLINT-TODO-004、018 |

## 7. D Gate / B Gate

| Gate | 判定 | 证据 |
|---|---|---|
| D Gate | PASS | 17 条 BC 均有唯一编号、当前最高实证层、代码/运行证据、冻结结论、缺口与后继任务 |
| B Gate | PASS | 本文件落盘；source TODO 可回写为 Done；worklog 可记录本轮真实 `dasall` 包状态与普通用户权限限制 |
