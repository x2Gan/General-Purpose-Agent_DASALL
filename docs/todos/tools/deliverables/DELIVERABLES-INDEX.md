# Tools 子系统交付物索引矩阵

> 本文档为 D-2 交付物索引，交叉索引 TOOL-TODO-001~042 各任务与其核心产出文件的映射关系。

## 一、公共头文件与契约层

| 任务 ID | 核心产出 | 路径 |
|---|---|---|
| TOOL-TODO-001 | CMake 骨架 + 公共 include 布局 | `tools/CMakeLists.txt`, `tools/include/` |
| TOOL-TODO-002 | ToolInvocationContext | `contracts/include/ToolInvocationContext.h` |
| TOOL-TODO-003 | ToolInvocationEnvelope | `contracts/include/ToolInvocationEnvelope.h` |
| TOOL-TODO-004 | ITool / IToolManager 接口 | `tools/include/ITool.h`, `tools/include/IToolManager.h` |
| TOOL-TODO-005 | IPolicyGate / ICapabilityCache 接口 | `tools/include/IPolicyGate.h`, `tools/include/ICapabilityCache.h` |
| TOOL-TODO-006 | IMCPAdapter / IMCPTransport 接口 | `tools/include/IMCPAdapter.h`, `tools/include/IMCPTransport.h` |
| TOOL-TODO-007 | IToolPluginProvider / ToolPluginExtensionCatalog 接口 | `tools/include/IToolPluginProvider.h`, `tools/include/ToolPluginExtensionCatalog.h` |

## 二、注册与配置层

| 任务 ID | 核心产出 | 路径 |
|---|---|---|
| TOOL-TODO-008 | 源码骨架 + 单元测试入口 | `tools/src/`, `tests/unit/tools/CMakeLists.txt` |
| TOOL-TODO-009 | ToolRegistry 描述符目录 | `tools/src/registry/ToolRegistry.h`, `tools/src/registry/ToolRegistry.cpp` |
| TOOL-TODO-010 | PluginExtensionBridge | `tools/src/plugin/PluginExtensionBridge.h`, `tools/src/plugin/PluginExtensionBridge.cpp` |
| TOOL-TODO-011 | ToolValidator | `tools/src/validation/ToolValidator.h`, `tools/src/validation/ToolValidator.cpp` |
| TOOL-TODO-012 | ToolConfigAdapter / ToolPolicyView | `tools/src/config/ToolConfigAdapter.h`, `tools/src/policy/ToolPolicyView.h` |

## 三、策略与路由层

| 任务 ID | 核心产出 | 路径 |
|---|---|---|
| TOOL-TODO-013 | ToolPolicyGate | `tools/src/policy/ToolPolicyGate.h`, `tools/src/policy/ToolPolicyGate.cpp` |
| TOOL-TODO-014 | ToolRouteSelector | `tools/src/route/ToolRouteSelector.h`, `tools/src/route/ToolRouteSelector.cpp` |

## 四、核心管线

| 任务 ID | 核心产出 | 路径 |
|---|---|---|
| TOOL-TODO-015a | ToolManager 骨架 | `tools/src/ToolManager.h`, `tools/src/ToolManager.cpp` |
| TOOL-TODO-015b | ToolManager 完整治理管线 | `tools/src/ToolManager.cpp` (invoke_batch pipeline) |
| TOOL-TODO-016 | ToolServiceBridge | `tools/src/bridge/ToolServiceBridge.h`, `tools/src/bridge/ToolServiceBridge.cpp` |
| TOOL-TODO-017 | BuiltinExecutorLane | `tools/src/execution/BuiltinExecutorLane.h`, `tools/src/execution/BuiltinExecutorLane.cpp` |
| TOOL-TODO-018 | ResultProjector | `tools/src/projection/ResultProjector.h`, `tools/src/projection/ResultProjector.cpp` |

## 五、可观测性层

| 任务 ID | 核心产出 | 路径 |
|---|---|---|
| TOOL-TODO-019 | ToolAuditBridge | `tools/src/ops/ToolAuditBridge.h`, `tools/src/ops/ToolAuditBridge.cpp` |
| TOOL-TODO-020 | ToolMetricsBridge | `tools/src/ops/ToolMetricsBridge.h`, `tools/src/ops/ToolMetricsBridge.cpp` |
| TOOL-TODO-021 | ToolTraceBridge | `tools/src/ops/ToolTraceBridge.h`, `tools/src/ops/ToolTraceBridge.cpp` |
| TOOL-TODO-022 | ToolHealthProbe | `tools/src/ops/ToolHealthProbe.h`, `tools/src/ops/ToolHealthProbe.cpp` |

## 六、集成测试基础设施

| 任务 ID | 核心产出 | 路径 |
|---|---|---|
| TOOL-TODO-023 | Runtime caller fixture | `tests/mocks/include/support/ToolsIntegrationFixture.h` |
| TOOL-TODO-024 | 集成测试拓扑 | `tests/integration/tools/CMakeLists.txt` |
| TOOL-TODO-025 | ToolServicesSmokeIntegration | `tests/integration/tools/ToolServicesSmokeIntegrationTest.cpp` |
| TOOL-TODO-026 | ToolObservabilityIntegration | `tests/integration/tools/ToolObservabilityIntegrationTest.cpp` |

## 七、工作流引擎

| 任务 ID | 核心产出 | 路径 |
|---|---|---|
| TOOL-TODO-027 | WorkflowPlan / WorkflowReceipt | `tools/src/execution/WorkflowPlan.h`, `tools/src/execution/WorkflowReceipt.h` |
| TOOL-TODO-028 | WorkflowEngine | `tools/src/execution/WorkflowEngine.h`, `tools/src/execution/WorkflowEngine.cpp` |
| TOOL-TODO-029 | CompensationLedger | `tools/src/execution/CompensationLedger.h`, `tools/src/execution/CompensationLedger.cpp` |
| TOOL-TODO-030 | ToolWorkflowFailureIntegration | `tests/integration/tools/ToolWorkflowFailureIntegrationTest.cpp` |

## 八、MCP 协议层

| 任务 ID | 核心产出 | 路径 |
|---|---|---|
| TOOL-TODO-031 | MCP loopback / stdio launch 样本 | `tests/mocks/include/MCPLoopbackServerFixture.h` |
| TOOL-TODO-032 | CapabilityCache | `tools/src/mcp/CapabilityCache.h`, `tools/src/mcp/CapabilityCache.cpp` |
| TOOL-TODO-033 | MCPLane / MCPAdapter / StdioMCPTransport | `tools/src/mcp/MCPAdapter.h`, `tools/src/mcp/MCPAdapter.cpp`, `tools/src/mcp/MCPLane.*`, `tools/src/mcp/StdioMCPTransport.*` |
| TOOL-TODO-034 | CapabilityDiscovery / StdioMCPServerLauncher | `tools/src/mcp/CapabilityDiscovery.*`, `tools/src/mcp/StdioMCPServerLauncher.*` |
| TOOL-TODO-035 | ToolMCPFallback + StdioMCP Integration | `tests/integration/tools/ToolMCPFallbackIntegrationTest.cpp`, `tests/integration/tools/ToolPluginStdioMCPIntegrationTest.cpp` |

## 九、技能层

| 任务 ID | 核心产出 | 路径 |
|---|---|---|
| TOOL-TODO-036 | SkillSpec + external dialect 样本 | `skills/specs/`, `skills/external_dialects/` |
| TOOL-TODO-037 | SkillRegistry | `tools/src/skill/SkillRegistry.h`, `tools/src/skill/SkillRegistry.cpp` |
| TOOL-TODO-038 | SkillRuntime | `tools/src/skill/SkillRuntime.h`, `tools/src/skill/SkillRuntime.cpp` |
| TOOL-TODO-039 | ExternalSkillImporter / PluginSkillBundleImporter | `tools/src/skill/ExternalSkillImporter.*`, `tools/src/skill/PluginSkillBundleImporter.*` |
| TOOL-TODO-040 | ToolSkillRuntime Integration | `tests/integration/tools/ToolSkillRuntimeIntegrationTest.cpp` |

## 十、Profile 集成与交付

| 任务 ID | 核心产出 | 路径 |
|---|---|---|
| TOOL-TODO-041 | ToolProfileIntegration + discoverability gate | `tests/integration/tools/ToolProfileIntegrationTest.cpp` |
| TOOL-TODO-042 | Gate 报告与交付证据 | `docs/todos/tools/deliverables/TOOL-TODO-042-*` |

## 评审补充交付物

| 编号 | 描述 | 产出 |
|---|---|---|
| T-1 | build_failure_envelope 可读性修复 | `tools/src/ToolManager.cpp` |
| T-2 | normalize_arguments JSON sanity check | `tools/src/validation/ToolValidator.cpp` |
| T-3 | visibility rule glob/prefix 匹配 | `tools/src/policy/ToolPolicyGate.cpp` |
| T-4 | MCPAdapter shared_mutex 优化 | `tools/src/mcp/MCPAdapter.h`, `tools/src/mcp/MCPAdapter.cpp` |
| T-5 | BuiltinExecutorLane deadline enforcement | `tools/src/execution/BuiltinExecutorLane.cpp` |
| T-6 | Route scoring named constants | `tools/src/route/ToolRouteSelector.cpp` |
| T-7 | 清除残余 placeholder.cpp | 10 文件删除 + `tools/CMakeLists.txt` |
| F-1 | TOOL-FIX-007 production observability sink 收敛 | `docs/todos/tools/deliverables/TOOL-FIX-007-production-tools-observability-sink收敛.md` |
| F-2 | TOOL-FIX-008 concrete builtin wrapper 布局收敛 | `docs/todos/tools/deliverables/TOOL-FIX-008-concrete-builtin-wrapper布局收敛.md` |
| F-3 | TOOL-FIX-009 tools installed / release 本机证据收敛 | `docs/todos/tools/deliverables/TOOL-FIX-009-tools-installed-release本机证据收敛.md` |
| F-4 | TOOL-FIX-010 knowledge.search / Knowledge 与 Tools 关系收敛 | `docs/todos/tools/deliverables/TOOL-FIX-010-knowledge-search关系收敛.md` |
| UT-2 | CapabilityCache 并发测试 | `tests/unit/tools/CapabilityCacheConcurrencyTest.cpp` |
| UT-3 | 集成测试共享 fixture | `tests/mocks/include/support/ToolsIntegrationFixture.h` |
| UT-4 | ToolValidator 边界测试 | `tests/unit/tools/ToolValidatorBoundaryTest.cpp` |
| UT-5 | invoke_batch 独立性集成测试 | `tests/integration/tools/ToolBatchInvokeIndependenceIntegrationTest.cpp` |
| S-1 | device-health-check 技能样本 | `skills/specs/device-health-check.skill.yaml`, `skills/workflows/device-health-check.workflow.yaml`, `skills/evals/device-health-check.eval.yaml`, `skills/external_dialects/claude/device-health/`, `skills/external_dialects/github/device-health/` |
| S-2 | Workflow YAML 格式文档 | `skills/README.md` |
| S-3 | External dialect 字段映射文档 | `skills/README.md` |
| D-2 | 交付物索引矩阵 | 本文档 |
