# LLM-FIX-004 L5 / release runner / provider 长稳态证据收口

日期：2026-05-16
任务：LLM-FIX-004
状态：Done

## 1. 本地证据

1. [docs/todos/DASALL_子系统查漏补缺专项记录.md](../../DASALL_%E5%AD%90%E7%B3%BB%E7%BB%9F%E6%9F%A5%E6%BC%8F%E8%A1%A5%E7%BC%BA%E4%B8%93%E9%A1%B9%E8%AE%B0%E5%BD%95.md) 已把 `LLM-FIX-004` 冻结为“收口 L5 / release runner / provider 长稳态证据”，目标是把 BC-07 的 release evidence 与 BC-16 的 package handoff 证据放回统一口径，而不是继续扩张 llm 产品逻辑或 shared admission。
2. [docs/todos/packaging/deliverables/PKG-TODO-018-Ubuntu-DPKG-Gate与交付证据收口.md](../../packaging/deliverables/PKG-TODO-018-Ubuntu-DPKG-Gate%E4%B8%8E%E4%BA%A4%E4%BB%98%E8%AF%81%E6%8D%AE%E6%94%B6%E5%8F%A3.md) 已记录 authoritative qemu `autopkgtest` PASS、`pkg-smoke-local-control-plane PASS`、`pkg-smoke-common-assets PASS` 与 `lintian RC=0`。这说明 BC-16 的 qemu package gate 历史上已经具备 L5 证据，不是“从未跑过 qemu”。
3. [debian/tests/pkg-smoke-local-control-plane](../../../../debian/tests/pkg-smoke-local-control-plane) 当前显式要求 `DASALL_DEEPSEEK_API_KEY_FILE` 或已导入的 secret 存在，随后执行 installed `dasall run '{"prompt":"package smoke"}' --json --timeout-ms 120000`，并强制断言 `llm.origin=deepseek-prod/`、拒绝 `agent.dataset`。因此只要 PKG-TODO-018 的 qemu gate PASS 成立，同一 authoritative testbed run 也已经给出 BC-07 的 installed LLM positive-path L5 证据。
4. [docs/todos/integration/deliverables/FULLINT-TODO-013-installed-package控制面主功能矩阵.md](../../integration/deliverables/FULLINT-TODO-013-installed-package%E6%8E%A7%E5%88%B6%E9%9D%A2%E4%B8%BB%E5%8A%9F%E8%83%BD%E7%9F%A9%E9%98%B5.md) 与 [docs/todos/integration/deliverables/FULLINT-TODO-015-memory-installed-package持久化风险复验证据包.md](../../integration/deliverables/FULLINT-TODO-015-memory-installed-package%E6%8C%81%E4%B9%85%E5%8C%96%E9%A3%8E%E9%99%A9%E5%A4%8D%E9%AA%8C%E8%AF%81%E6%8D%AE%E5%8C%85.md) 又补齐了 2026-05-11/12 的 L4 local rerun 与 memory row proof，说明历史 L5 packaging gate 与当前 installed local LLM/memory evidence 没有相互冲突。
5. [scripts/packaging/validate_gate_int_10_installed_package_qemu.sh](../../../../scripts/packaging/validate_gate_int_10_installed_package_qemu.sh) 与 [.github/workflows/release-package-gate.yml](../../../../.github/workflows/release-package-gate.yml) 现已固定 release-runner contract：self-hosted runner 必须显式提供 `qemu_image` 与 `deepseek_key_file`，脚本则负责把 `DASALL_DEEPSEEK_API_KEY_FILE`、`DASALL_AUTOPKGTEST_SETUP_COMMANDS` 与 testbed secret path 传入 authoritative `autopkgtest`。
6. provider 抖动 / 失败吸收的当前证据不来自 soak，而来自 llm focused/unit/integration matrix： [tests/integration/llm/LLMFallbackIntegrationTest.cpp](../../../../tests/integration/llm/LLMFallbackIntegrationTest.cpp)、[tests/integration/llm/LLMProfileIntegrationTest.cpp](../../../../tests/integration/llm/LLMProfileIntegrationTest.cpp)、[tests/integration/llm/DeepSeekDualModeSelectionIntegrationTest.cpp](../../../../tests/integration/llm/DeepSeekDualModeSelectionIntegrationTest.cpp)、[tests/integration/llm/LLMProductionObservabilityIntegrationTest.cpp](../../../../tests/integration/llm/LLMProductionObservabilityIntegrationTest.cpp)、[tests/unit/llm/LLMManagerTimeoutPolicyTest.cpp](../../../../tests/unit/llm/LLMManagerTimeoutPolicyTest.cpp)、[tests/unit/llm/LLMManagerRetryBudgetTest.cpp](../../../../tests/unit/llm/LLMManagerRetryBudgetTest.cpp)、[tests/unit/llm/LLMManagerConcurrencyGuardTest.cpp](../../../../tests/unit/llm/LLMManagerConcurrencyGuardTest.cpp) 与 [tests/unit/llm/ModelRouterStabilityTest.cpp](../../../../tests/unit/llm/ModelRouterStabilityTest.cpp)。这些用例已经给出 timeout、retry budget、fallback、route stability、profile projection 与 production observability 的 failure-handling basis。

## 2. Design 结论

1. `LLM-FIX-004` 是证据收口任务，不再要求新增 llm 产品代码。repo 侧真正缺失的是“把已存在的 qemu PASS、installed `llm.origin` 断言、provider failure-handling matrix 与 release-runner contract 写成统一结论”，而不是再发明一条新的 qemu smoke。
2. BC-07 当前可从“L4 local installed LLM origin”升级为“L5 authoritative qemu positive path + L4 local rerun 双证据”：L5 来自 PKG-TODO-018 的 qemu `autopkgtest` PASS 与 `pkg-smoke-local-control-plane` 的 `llm.origin` / `agent.dataset` 断言，L4 来自 FULLINT-TODO-013 / 015 的近期本机 rerun。
3. BC-16 当前同样可记录为 L5：PKG-TODO-018 已给出 qemu `autopkgtest` + `lintian RC=0`，而本轮新增的 workflow/script contract 又解决了 `FULLINT-BLK-001` 中“image / virt args / secret injection / provider preflight / log archive 未固定”的仓库级 blocker。
4. provider “长稳态”在本轮只能收口为 failure-handling basis，而不是 L6 soak：timeout、retry budget、fallback、route stability、profile diff 与 observability 都已有 focused / integration evidence，但尚未进行长时 soak、真实外部抖动统计或 chaos。换言之，本轮可以记录“已具备 fail-closed / absorb-jitter 基础证据”，不能记录“已具备 production confidence”。
5. `FULLINT-TODO-019` 在本轮从“被 blocker 卡住的前置任务”转为“可执行但尚未重跑的 release rerun 任务”。它仍然是下一轮 production installed-package release-ready 候选的必要条件，但不再阻止 BC-07 / BC-16 记录既有 L5 历史证据。

## 3. 证据矩阵

| 证据项 | 当前层级 | 证据锚点 | 本轮结论 | 不外推 |
|---|---:|---|---|---|
| installed-package LLM positive path（qemu authoritative） | L5 | PKG-TODO-018 Gate 07 + `pkg-smoke-local-control-plane` 中 `llm.origin=deepseek-prod/` / reject `agent.dataset` | BC-07 已具 authoritative qemu positive-path 历史证据 | 不等于 L6 soak、provider SLA 或 streaming ready |
| package handoff（qemu + lintian） | L5 | PKG-TODO-018 Gate 07/08 | BC-16 已具 qemu + lintian 历史收口证据 | 不等于当前 release runner 已复跑 |
| release-runner contract | L0 / L5 precondition | `validate_gate_int_10_installed_package_qemu.sh` + `release-package-gate.yml` | `FULLINT-BLK-001` 已解阻，真实 runner 现有固定入口 | 不等于 `FULLINT-TODO-019` 已完成 |
| installed local rerun / memory writeback | L4 | FULLINT-TODO-013 / 015 | 历史 L5 与近期 L4 rerun 口径一致，无回退 | 不等于 qemu authoritative rerun |
| provider failure-handling / absorb-jitter basis | L1 / L2 | fallback、timeout、retry budget、concurrency、route stability、profile diff、observability tests | 已有“失败吸收基础证据”，可支撑 release 风险描述 | 不等于 L6 soak / chaos / real-world latency SLO |

## 4. Design -> Build 映射

| Design 项 | Build / 文档落点 |
|---|---|
| authoritative qemu L5 positive path 回链到 BC-07 / BC-16 | [docs/ssot/BusinessChainIntegrationMatrix.md](../../../ssot/BusinessChainIntegrationMatrix.md)、[docs/todos/integration/DASALL_全量业务链集成验证专项TODO-2026-05-11.md](../../integration/DASALL_%E5%85%A8%E9%87%8F%E4%B8%9A%E5%8A%A1%E9%93%BE%E9%9B%86%E6%88%90%E9%AA%8C%E8%AF%81%E4%B8%93%E9%A1%B9TODO-2026-05-11.md) |
| llm 专项 release evidence owner 收口 | [docs/todos/llm/DASALL_llm子系统专项TODO.md](../DASALL_llm%E5%AD%90%E7%B3%BB%E7%BB%9F%E4%B8%93%E9%A1%B9TODO.md)、[docs/todos/DASALL_子系统查漏补缺专项记录.md](../../DASALL_%E5%AD%90%E7%B3%BB%E7%BB%9F%E6%9F%A5%E6%BC%8F%E8%A1%A5%E7%BC%BA%E4%B8%93%E9%A1%B9%E8%AE%B0%E5%BD%95.md) |
| release-runner contract 与 blocker 解组 | [scripts/packaging/validate_gate_int_10_installed_package_qemu.sh](../../../../scripts/packaging/validate_gate_int_10_installed_package_qemu.sh)、[.github/workflows/release-package-gate.yml](../../../../.github/workflows/release-package-gate.yml)、[docs/worklog/DASALL_开发执行记录.md](../../../worklog/DASALL_%E5%BC%80%E5%8F%91%E6%89%A7%E8%A1%8C%E8%AE%B0%E5%BD%95.md) |

## 5. Build 三件套

1. 代码目标：不改 llm 产品逻辑；新增 `LLM-FIX-004` 证据交付物，并同步 SSOT / TODO / worklog，使 BC-07、BC-16 与 `FULLINT-BLK-001` 的当前态不再冲突。
2. 测试目标：
   - release-runner contract 关键词在 workflow / packaging README / integration TODO 中一致。
   - llm failure-handling basis 相关 focused tests 继续可执行。
3. 验收命令：
   - `rg -n "LLM-FIX-004|BC-07|BC-16|FULLINT-BLK-001|DASALL-Release-Package-Gate|llm.origin=deepseek-prod" docs/todos docs/ssot docs/worklog scripts/packaging .github/workflows`
   - `ctest --test-dir build-ci --output-on-failure -R "(LLMFallbackIntegration|LLMProfileIntegration|DeepSeekDualModeSelectionIntegration|LLMProductionObservabilityIntegration|LLMManager(TimeoutPolicy|RetryBudget|ConcurrencyGuard)|ModelRouterStability)Test"`

## 6. 风险与边界

1. 本轮不把 PKG-TODO-018 的历史 qemu PASS 伪装为“当前 runner 已重跑”；`FULLINT-TODO-019` 仍需要在真实 self-hosted runner 上重新执行并归档当轮日志。
2. 本轮不把 focused timeout/retry/fallback/route stability 证据伪装为 L6 soak；若后续需要 production confidence，应另起 owner 验证长时外部 provider 抖动、secret rotate、network loss、retry budget exhaustion 与 observability trend。
3. `LLM-FIX-004` 不改变 ADR-006/007/008 边界：Memory 继续拥有上下文，Runtime 继续拥有恢复准入与全局主控，llm 只负责 provider/prompt/route 的 module-local owner 与对应 release evidence。