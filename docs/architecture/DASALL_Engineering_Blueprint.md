# DASALL Agent 工程架构蓝图

## 0. 文档信息

### 0.1 文档定位

本文档是 DASALL Agent 的**工程架构蓝图**，以《DASALL Agent 架构设计文档》为基础，将系统分层架构映射为具体的工程目录结构、模块划分、分层职责和接口规划，为工程实现、代码审查和架构演进提供直接参考。

### 0.2 文档范围

- 完整项目目录树（含子目录说明）
- 各目录对应的架构分层、模块定义与职责边界
- 模块间依赖规则与约束
- Build Profile 与平台裁剪策略
- 测试目录规划
- 关键头文件与接口文件分布建议

### 0.3 与架构文档的关系

| 架构文档章节 | 本文档对应内容 |
|---|---|
| 第 3 章 系统总体架构 | 第 2 章 分层到目录的映射 |
| 第 4 章 Agent 智能架构 | 第 3 章 核心模块目录设计 |
| 第 5 章 关键子系统设计 | 第 4 章 子系统目录详细展开 |
| 第 7 章 工程架构设计 | 第 3、4、5 章（完整扩充） |
| 第 8 章 非功能设计 | 第 4.12 章 infra 目录设计 |
| 第 9 章 部署与运维 | 第 5 章 构建与部署蓝图 |

---

## 目录

- [DASALL Agent 工程架构蓝图](#dasall-agent-工程架构蓝图)
  - [0. 文档信息](#0-文档信息)
    - [0.1 文档定位](#01-文档定位)
    - [0.2 文档范围](#02-文档范围)
    - [0.3 与架构文档的关系](#03-与架构文档的关系)
  - [目录](#目录)
  - [1. 工程根目录总览](#1-工程根目录总览)
  - [2. 分层到目录的映射](#2-分层到目录的映射)
  - [3. 顶层目录详细展开](#3-顶层目录详细展开)
    - [3.1 contracts/](#31-contracts)
    - [3.2 apps/](#32-apps)
    - [3.3 runtime/](#33-runtime)
    - [3.4 cognition/](#34-cognition)
    - [3.5 llm/](#35-llm)
    - [3.6 tools/](#36-tools)
    - [3.7 memory/](#37-memory)
    - [3.8 knowledge/](#38-knowledge)
    - [3.9 services/](#39-services)
    - [3.10 multi\_agent/](#310-multi_agent)
    - [3.11 platform/](#311-platform)
    - [3.12 infra/](#312-infra)
    - [3.13 profiles/](#313-profiles)
    - [3.14 skills/](#314-skills)
    - [3.15 tests/](#315-tests)
    - [3.16 third\_party/](#316-third_party)
    - [3.17 cmake/ 与 scripts/](#317-cmake-与-scripts)
    - [3.18 docs/](#318-docs)
  - [4. 模块依赖规则](#4-模块依赖规则)
    - [4.1 依赖方向原则](#41-依赖方向原则)
    - [4.2 依赖禁止规则（不可违反）](#42-依赖禁止规则不可违反)
    - [4.3 接口优先原则](#43-接口优先原则)
  - [5. Build Profile 与平台裁剪](#5-build-profile-与平台裁剪)
    - [5.1 Profile 与模块启用矩阵](#51-profile-与模块启用矩阵)
    - [5.2 Profile 激活方式](#52-profile-激活方式)
  - [6. 关键接口文件分布](#6-关键接口文件分布)
  - [7. 测试规划](#7-测试规划)
    - [7.1 测试层次与覆盖目标](#71-测试层次与覆盖目标)
    - [7.2 必须覆盖的端到端场景](#72-必须覆盖的端到端场景)
    - [7.3 Mock 原则](#73-mock-原则)
  - [8. 工程约束与演进原则](#8-工程约束与演进原则)
    - [8.1 必须遵守的工程约束](#81-必须遵守的工程约束)
    - [8.2 演进原则](#82-演进原则)
    - [8.3 代码组织约定](#83-代码组织约定)

---

## 1. 工程根目录总览

```text
DASALL-OS/
├── automation/                     #   项目开发循环自动化的控制面目录
├── apps/                           #   产品入口与运行壳层
│   ├── cli/                        #   命令行交互入口
│   ├── daemon/                     #   后台守护进程入口
│   ├── gateway/                    #   HTTP/WebSocket/MQTT 网关入口
│   └── simulator/                  #   桌面模拟/集成联调入口
│
├── contracts/                      # 跨模块稳定契约层（核心冻结对象）
│   ├── include/
│   │   ├── boundary/          #   WP-01 边界守卫与对象边界名册（contract gates）
│   │   ├── agent/             #   AgentRequest, AgentResult, GoalContract
│   │   ├── context/           #   ContextPacket, ContextAssembleRequest/Result
│   │   ├── observation/       #   Observation, ObservationDigest, BeliefState
│   │   ├── memory/            #   Turn, Session, SummaryMemory, MemoryFact
│   │   ├── tool/              #   ToolRequest, ToolResult, ToolDescriptor, ToolIR
│   │   ├── llm/               #   LLMRequest, LLMResponse, ModelRoute
│   │   ├── prompt/            #   PromptSpec, PromptRelease, PromptComposeRequest/Result
│   │   ├── policy/            #   PolicyDecision, PromptPolicyDecision
│   │   ├── task/              #   TaskRequest, TaskState, TaskGraph
│   │   ├── event/             #   EventEnvelope, EventType
│   │   ├── error/             #   ErrorInfo, ResultCode
│   │   └── checkpoint/        #   Checkpoint, ResumeToken
│   │
│   ├── src/
│   │   ├── agent/
│   │   ├── context/
│   │   ├── observation/
│   │   ├── memory/
│   │   ├── tool/
│   │   ├── llm/
│   │   ├── prompt/
│   │   ├── policy/
│   │   ├── task/
│   │   ├── event/
│   │   ├── error/
│   │   └── checkpoint/
│   │
│   └── CMakeLists.txt
│
├── runtime/                       # Agent Control Plane（运行时控制平面）
│   ├── include/
│   ├── src/
│   └── CMakeLists.txt
│
├── cognition/                     # 认知层（Perception/Planner/Reasoner/Reflection）
│   ├── include/
│   ├── src/
│   └── CMakeLists.txt
│
├── llm/                           # LLM 适配与路由子系统
│   ├── include/
│   ├── src/
│   │   ├── adapters/
│   │   └── prompt/
│   └── CMakeLists.txt
│
├── tools/                         # 工具治理与执行子系统
│   ├── include/
│   │   └── mcp/
│   ├── src/
│   │   ├── builtin/               #   内置工具实现（只读查询、系统动作等）
│   │   └── mcp/
│   └── CMakeLists.txt
│
├── memory/                        # 记忆、摘要与上下文编排子系统
│   ├── include/
│   ├── src/
│   │   ├── store/
│   │   └── compression/
│   └── CMakeLists.txt
│
├── knowledge/                     # 知识检索与 RAG 子系统
│   ├── include/
│   ├── src/
│   └── CMakeLists.txt
│
├── services/                      # 能力服务层（执行控制、数据服务、业务封装）
│   ├── include/
│   ├── src/
│   │   ├── execution/
│   │   ├── data/
│   │   └── system/
│   └── CMakeLists.txt
│
├── multi_agent/                   # 多 Agent 协同子系统
│   ├── include/
│   ├── src/
│   └── CMakeLists.txt
│
├── platform/                      # 平台抽象层（OS、外设、IPC 差异屏蔽）
│   ├── include/
│   │   └── hal/                   #   硬件抽象接口（GPIO/UART/I2C/SPI/CAN）
│   ├── src/
│   │   ├── linux/                 #   Linux 通用实现（x86 + ARM 共用）
│   │   ├── x86/                   #   x86 平台特化扩展（如有）
│   │   └── arm/                   #   ARM 嵌入式平台特化扩展
│   └── CMakeLists.txt
│
├── infra/                         # 基础设施子系统（日志/配置/监控/安全/OTA）
│   ├── include/
│   ├── src/
│   │   ├── logging/
│   │   ├── tracing/
│   │   ├── metrics/
│   │   ├── config/
│   │   ├── secret/
│   │   ├── health/
│   │   └── ota/
│   └── CMakeLists.txt
│
├── profiles/                      # 平台与部署形态 Profile（编译裁剪 + 运行治理）
│   ├── desktop_full/
│   │   ├── profile.cmake
│   │   └── runtime_policy.yaml
│   ├── cloud_full/
│   │   ├── profile.cmake
│   │   └── runtime_policy.yaml
│   ├── edge_balanced/
│   │   ├── profile.cmake
│   │   └── runtime_policy.yaml
│   ├── edge_minimal/
│   │   ├── profile.cmake
│   │   └── runtime_policy.yaml
│   └── factory_test/
│       ├── profile.cmake
│       └── runtime_policy.yaml
│
├── skills/                        # Skill 资产目录（任务级编排资产）
│   ├── specs/                     #   SkillSpec 定义文件（YAML/JSON）
│   ├── prompts/                   #   PromptBundle 与 PromptRelease 资产
│   ├── workflows/                 #   WorkflowTemplate 定义
│   └── evals/                     #   Skill 评测套件
│
├── tests/                         # 测试目录（多层次）
│   ├── unit/                      #   单元测试（各模块独立验证）
│   │   ├── runtime/
│   │   ├── cognition/
│   │   ├── llm/
│   │   ├── tools/
│   │   ├── memory/
│   │   └── knowledge/
│   ├── integration/               #   集成测试（跨模块联动）
│   │   ├── agent_loop/
│   │   ├── tool_chain/
│   │   └── memory_context/
│   ├── contract/                  #   契约测试（schema、错误码、事件格式稳定性）
│   ├── e2e/                       #   端到端测试（真实任务流程）
│   ├── stress/                    #   压力与长稳测试
│   └── mocks/                     #   可复用 Mock 与 Stub 实现
│
├── third_party/                   # 第三方依赖（源码树或 submodule）
│   ├── nlohmann_json/
│   ├── spdlog/
│   ├── faiss/
│   ├── sqlite3/
│   ├── googletest/
│   └── openssl/
│
├── debian/                        # dpkg打包构建
│
├── cmake/                         # CMake 公共模块与工具函数
│
├── sysroots/                      # 交叉编译目标平台依赖
│
├── scripts/                       # 构建、部署、诊断脚本
│
├── docs/                          # 文档目录
│   ├── architecture/              #   架构设计文档
│   ├── api/                       #   接口设计文档
│   └── adr/                       #   架构决策记录（ADR）
│
├── CMakeLists.txt                 # 根构建文件
└── DASALL.code-workspace       # VS Code 工作区配置
```

---

## 2. 分层到目录的映射

架构文档定义了 7 层分层结构，各层与工程目录的映射关系如下：

| 架构层 | 编号 | 对应工程目录 | 说明 |
|---|---|---|---|
| Product & Access Layer | Layer 7 | `apps/` | 产品入口、协议适配、请求归一化 |
| Agent Control Plane | Layer 6 | `runtime/` | 状态机、调度、预算、恢复、会话 |
| Cognition Layer | Layer 5 | `cognition/` | 感知、规划、推理、反思、回复构建 |
| Execution & Collaboration Layer | Layer 4 | `tools/`, `memory/`, `knowledge/`, `multi_agent/` | 工具治理、记忆编排、知识检索、多 Agent |
| Capability Services Layer | Layer 3 | `services/` | 执行控制、数据、系统服务抽象 |
| Platform Abstraction Layer | Layer 2 | `platform/` | OS、外设、IPC、线程、网络抽象 |
| Infrastructure Layer | Layer 1 | `infra/` | 日志、追踪、监控、配置、安全、OTA |

**水平横跨所有层的目录：**

| 目录 | 横跨范围 | 说明 |
|---|---|---|
| `contracts/` | 全部层 | 冻结核心数据契约，不包含业务逻辑 |
| `profiles/` | Layer 6 ~ Layer 2 | 按平台和档位控制编译裁剪与运行策略 |
| `skills/` | Layer 5 ~ Layer 4 | 认知层选择、执行层执行的任务级复用资产 |
| `tests/` | 全部层 | 各层独立与跨层联合测试 |

---

## 3. 顶层目录详细展开

### 3.1 contracts/

**对应架构层：** 全局契约，无分层归属

**职责：**
跨模块共享的核心数据对象定义，所有模块间传递的标准结构体、枚举和错误码均在此处冻结。本目录只包含头文件（`*.h`），不包含任何实现代码。

**设计约束：**
- 不依赖任何其他业务模块
- 不包含可执行逻辑，只定义数据结构与接口枚举
- 每个子目录冻结一类契约，修改必须全局同步

**关键冻结对象：**

| 子目录 | 关键对象 |
|---|---|
| `boundary/` | `ObjectBoundaryCatalog`, `BoundaryGuards`, `ContextBoundaryGuards`, `RecoveryBoundaryGuards`, `MultiAgentBoundaryGuards`, `CompatibilityGuards` |
| `agent/` | `AgentRequest`, `AgentResult`, `AgentInitConfig`, `ResumeToken` |
| `context/` | `ContextPacket`, `ContextAssembleRequest`, `ContextAssembleResult`, `CompressionRequest`, `CompressionResult` |
| `observation/` | `Observation`, `ObservationDigest`, `BeliefState` |
| `memory/` | `Turn`, `Session`, `SummaryMemory`, `MemoryFact`, `SessionSnapshot` |
| `tool/` | `ToolRequest`, `ToolResult`, `ToolDescriptor`, `ToolIR`, `ToolRoute`, `CompensationAction` |
| `llm/` | `LLMRequest`, `LLMResponse`, `ModelRoute`, `StreamHandle` |
| `prompt/` | `PromptSpec`, `PromptRelease`, `PromptComposeRequest`, `PromptComposeResult` |
| `policy/` | `PolicyDecision`, `PromptPolicyDecision`, `ReflectionDecision` |
| `task/` | `TaskRequest`, `TaskState`, `TaskGraph`, `WorkerTask` |
| `event/` | `EventEnvelope`, `EventType` |
| `error/` | `ErrorInfo`, `ResultCode` |
| `checkpoint/` | `Checkpoint`, `RuntimeBudget` |

---

### 3.2 apps/

**对应架构层：** Layer 7 — Product & Access Layer

**职责：**
产品入口与运行壳层，负责接收不同协议的外部流量，执行认证鉴权、协议转换和请求归一化，最终构造 `AgentRequest` 传递给 `runtime/`。每个子目录是一个独立的可执行程序。

**子目录说明：**

| 子目录 | 作用 | 典型协议 |
|---|---|---|
| `cli/` | 命令行交互入口，适合开发调试与运维操作 | stdin/stdout |
| `daemon/` | 后台守护进程，长期运行，支持信号处理与优雅退出 | IPC / UNIX Domain Socket |
| `gateway/` | 网络网关入口，适合桌面端、远程客户端、上位机接入 | HTTP / WebSocket / MQTT |
| `simulator/` | 桌面模拟器，用于集成联调和 e2e 场景回放 | 内部直调 |

**关键组件：**
- `Protocol Adapter`：各协议 -> 统一 `InboundPacket`
- `AuthN/AuthZ Middleware`：认证与权限校验
- `Request Normalizer`：构造 `AgentRequest`（含 `request_id`, `session_id`, `trace_id`）
- `Stream Gateway`：管理流式响应出口

**依赖关系：**
- 依赖：`contracts/`, `runtime/`（接口），`infra/`（日志、认证）
- 不依赖：`cognition/`, `llm/`, `tools/`（不直接感知推理细节）

---

### 3.3 runtime/

**对应架构层：** Layer 6 — Agent Control Plane

**职责：**
Agent 运行时控制平面，驱动 Agent 主循环，是整个系统的主控中枢。负责状态机推进、会话管理、调度、预算控制、checkpoint 和多 Agent 协调的调度权。

**关键组件及职责：**

| 组件 | 职责 |
|---|---|
| `AgentFacade` | 对外统一暴露 `IAgent` 接口，隐藏 Orchestrator 复杂度 |
| `AgentOrchestrator` | 驱动 Agent 主循环，控制认知、工具、记忆、知识的调用顺序 |
| `SessionManager` | 会话生命周期管理，加载/保存 checkpoint 和 Profile 策略 |
| `SkillRouter` | 根据目标、会话态和策略域匹配 `SkillProfile` |
| `AgentFSM` | 显式状态机（Idle → Receiving → Planning → ... → Completed） |
| `Scheduler` | 任务排队与优先级调度（前台交互 > 执行安全 > 后台维护） |
| `BudgetController` | 管理 `RuntimeBudget`（轮次、工具调用次数、Token、延迟、功耗） |
| `RecoveryManager` | 超时、重试、熔断、降级和恢复策略执行 |
| `CheckpointManager` | 每次状态转移前后持久化 checkpoint，支持 resume |

**状态机核心状态：**
```
Idle → Receiving → Planning → Reasoning
Reasoning → WaitingClarify → Receiving
Reasoning → WaitingConfirm → ToolCalling
Reasoning → ToolCalling → WaitingExternal → Reflecting → Reasoning
Reflecting → FailedSafe → Responding
Responding → Auditing → Persisting → Completed → Idle
Any → Failed → Degraded → SafeMode
```

**主循环防护项（必须在所有 Profile 中生效）：**
- `MAX_TOOL_CALLS`
- `MAX_REPLAN_COUNT`
- `STEP_TIMEOUT_SECONDS`
- `SESSION_TIMEOUT_SECONDS`
- 每次状态转移前的 checkpoint 持久化

**依赖关系：**
- 依赖：`contracts/`, `cognition/`（接口），`llm/`（接口），`tools/`（接口），`memory/`（接口），`knowledge/`（接口），`multi_agent/`（接口），`infra/`
- 不依赖：`platform/`（直接驱动层不由 runtime 感知，通过 `services/` 和 `platform/` 解耦）

---

### 3.4 cognition/

**对应架构层：** Layer 5 — Cognition Layer

**职责：**
认知层，负责意图理解、计划生成、推理决策、失败反思和回复构建五段认知链路。产出动作意图（`ActionDecision`），不直接执行外部动作。

**关键组件及职责：**

| 组件 | 输入 | 输出 | 说明 |
|---|---|---|---|
| `PerceptionEngine` | `ContextPacket`, 用户输入 | 意图、实体、约束、目标 | 抽取并理解用户目标 |
| `Planner` | `GoalContract`, `ContextPacket` | `PlanGraph` | 将目标拆分为可执行步骤 DAG |
| `Reasoner` | `PlanGraph`, `Observation` | `ActionDecision` | 决定下一步动作意图 |
| `ReflectionEngine` | `Observation`, `ErrorInfo` | `ReflectionDecision` | 分析失败并给出重试/重规划/终止决策 |
| `ResponseBuilder` | `ContextPacket`, 执行结果 | `AgentResult` | 汇总上下文生成最终回复 |

**自评信号（必须显式输出）：**
- 当前决策 `confidence`
- 是否存在高不确定性假设 `high_uncertainty_assumption`
- 是否需要用户澄清 `clarification_needed`
- 建议的控制信号：`retry_step` / `replan` / `abort_safe`

**边界约束：**
- 只消费 `ContextPacket` 与 `Observation`，不直接访问底层驱动或执行端
- 不承担线程调度、超时控制和重试，这些由 `runtime/` 负责
- `Planner` 和 `Reasoner` 必须读取 `GoalContract` 与 `BeliefState`，而非仅靠最近一轮用户输入

**依赖关系：**
- 依赖：`contracts/`，`llm/`（接口，通过 `ILLMManager` 调用模型）
- 不依赖：`tools/` 实现、`memory/` 实现、`platform/`

---

### 3.5 llm/

**对应架构层：** Layer 5 支撑系统 — LLM 子系统

**职责：**
屏蔽模型差异，为 Cognition 提供统一 LLM 调用接口，同时管理 Prompt 资产的注册、装配和策略校验。

**子模块划分：**

```text
llm/
├── src/
│   ├── LLMManager.cpp           # 统一调度入口，路由到对应 Adapter
│   ├── ModelRouter.cpp          # 按 stage/cost/latency 选择模型
│   ├── adapters/
│   │   ├── OpenAIAdapter.cpp    # 云端 OpenAI-compatible API
│   │   ├── OllamaAdapter.cpp    # 局域网 Ollama 服务
│   │   └── LocalLLMAdapter.cpp  # 本地推理库（llama.cpp / ggml）
│   └── prompt/
│       ├── PromptRegistry.cpp   # 管理 PromptSpec/PromptRelease
│       ├── PromptComposer.cpp   # 按 stage/context_packet 装配消息
│       └── PromptPolicy.cpp     # 发送前执行裁剪、注入限制、来源过滤
```

**路由原则：**

| 场景 | 优先模型 |
|---|---|
| 复杂推理、多步规划 | 云端模型 |
| 隐私数据处理 | 局域网或本地模型 |
| 低时延场景 | 局域网模型 |
| 简单抽取与分类 | 本地小模型 |
| 默认降级链路 | Cloud → LAN → Local |

**Prompt 三段治理：**

| 组件 | 职责 |
|---|---|
| `PromptRegistry` | 管理 PromptSpec、PromptRelease、版本、评测状态 |
| `PromptComposer` | 按 stage/task_type/context_packet 装配最终 messages |
| `PromptPolicy` | 裁剪、注入限制、来源过滤、工具可见范围管控 |

**LLM 输出语义（统一映射）：**
- `DirectResponse`：直接回复
- `ToolCallIntent`：触发工具调用意图
- `ClarificationRequest`：请求用户澄清
- `ReplanSuggestion`：建议重规划

**依赖关系：**
- 依赖：`contracts/`, `infra/`（日志、监控）
- 不依赖：`cognition/` 实现、`tools/` 实现

---

### 3.6 tools/

**对应架构层：** Layer 4 — Execution & Collaboration Layer（Tool System）

**职责：**
统一工具治理与执行，从 Cognition 接收动作意图，经过治理链路后落地执行，并将结果归一化为 `Observation` 回传。

**治理链路：**
```
ActionDecision（来自 Cognition）
  → Tool Route（Function vs MCP）
  → Tool Registry（查询 ToolDescriptor）
  → Validator（参数校验与归一化）
  → Policy Gate（权限判断、风险控制）
  → Tool Executor / Workflow Engine / MCP Adapter
  → Tool Audit（记录副作用）
  → Observation Digest（面向 LLM 的摘要压缩）
```

**Tool 分类：**

| 类型 | 说明 | 示例 |
|---|---|---|
| `Information Tool` | 只读查询，无副作用 | 文件读取、知识查询 |
| `Action Tool` | 外部动作，有副作用 | 设备控制、文件写入 |
| `Workflow Tool` | 跨工具复合流程 | 数据采集→分析→上报 |
| `Agent Tool` | 代理间协作 | 调用 Worker Agent |
| `Diagnostic Tool` | 诊断与状态查询 | 健康检查、日志查询 |

**MCP 接入原则：**
- `MCP Adapter` 负责发现、握手、健康检查和协议转换
- 远程能力必须先映射为 `MCPToolBinding`，再注册到 `ToolRegistry`
- `CapabilityDiscovery` 周期拉取 MCP tools/resources/prompts 并更新 `CapabilityCache`
- MCP 故障优先回退到最近可信能力快照，不直接导致能力消失

**Observation Digest 结构（必须提供）：**
```cpp
struct ObservationDigest {
    std::string summary;           // 压缩后的短摘要
    std::vector<std::string> key_facts;  // 高价值事实
    std::vector<std::string> citations;  // 原始证据引用
    std::vector<std::string> omitted_details; // 被裁剪的内容提示
    float confidence;              // 摘要可信度
};
```

**工具扩展方式：**
1. 静态编译注册（`REGISTER_TOOL` 宏）
2. 配置驱动运行时注册
3. 插件动态加载
4. MCP 远程能力绑定

**依赖关系：**
- 依赖：`contracts/`, `services/`（执行控制接口），`infra/`（审计、日志）
- 不依赖：`cognition/` 实现、`llm/` 实现

---

### 3.7 memory/

**对应架构层：** Layer 4 — Execution & Collaboration Layer（Memory System）

**职责：**
向 Cognition 提供已组装好的 `ContextPacket`，并将执行结果和经验摘要回写。管理五层记忆结构和上下文编排闭环。

**五层记忆结构：**

| 层 | 存储介质 | 生命周期 | 说明 |
|---|---|---|---|
| `WorkingMemory` | 内存 KV + checkpoint | 任务级 | 当前任务黑板，供本轮推理共享状态 |
| `ShortTermMemory` | SQLite | 会话级 | 最近对话与观察，多轮上下文延续 |
| `LongTermMemory` | 结构化 SQLite | 跨会话 | 偏好、用户事实、终端画像 |
| `VectorMemory` | FAISS / SQLite-vss | 持久化 | 知识向量索引，语义检索与证据召回 |
| `ExperienceMemory` | 结构化事件存储 | 持久化 | 经验、故障、策略沉淀，用于重规划 |

**Long-Term Memory 细化：**
- `EpisodicMemory`：历史会话和任务摘要
- `SemanticMemory`：稳定知识与长期事实
- `ProgrammaticMemory`：Skill、Prompt 资产和行为约束

**Context Orchestrator 装配顺序（必须遵守）：**
1. 系统策略和安全边界
2. 当前目标和未完成任务状态
3. 最相关历史和知识片段
4. 一般性历史内容

**Token 预算不足时优先级（必须遵守）：**
- 保留：`goal`、`constraints`、`latest_observation`
- 裁剪：冗余历史、低相关知识片段

**写回规则：**
- `WorkingMemory` 高价值结论 → `LongTermMemory`
- 失败恢复、补偿结果、重规划结论 → `ExperienceMemory`
- `SummaryMemory` 记录 `decisions_made`、`confirmed_facts`、`tool_outcomes`（不仅是聊天压缩）

**依赖关系：**
- 依赖：`contracts/`, `infra/`（持久化、配置）
- 不依赖：`cognition/`，`llm/`，`tools/`

---

### 3.8 knowledge/

**对应架构层：** Layer 4 — Execution & Collaboration Layer（Knowledge System）

**职责：**
负责查询理解、检索召回、重排和证据组装，将检索原始结果压缩为可被 Cognition 消费的证据包，并维护知识索引新鲜度。

**关键组件：**

| 组件 | 职责 |
|---|---|
| `Retriever` | 语义检索召回（向量 + 关键词混合） |
| `Reranker` | 对召回结果按相关性重排 |
| `EvidenceBuilder` | 将重排结果压缩为结构化证据包 |
| `IndexManager` | 维护索引新鲜度（增量更新、版本管理） |

**依赖关系：**
- 依赖：`contracts/`, `memory/`（VectorMemory 后端），`infra/`
- 不依赖：`cognition/`，`llm/`

---

### 3.9 services/

**对应架构层：** Layer 3 — Capability Services Layer

**职责：**
封装外部执行控制目标、数据查询接口和系统服务，为 `tools/` 提供稳定的服务接口，屏蔽驱动、硬件、业务系统细节。

**子目录：**

| 子目录 | 职责 |
|---|---|
| `execution/` | 外部执行目标控制抽象（状态读取、动作执行、安全模式切换） |
| `data/` | 数据查询与业务接口（数据库查询、业务数据获取） |
| `system/` | 系统信息与状态查询（资源占用、进程状态） |

**风险治理要求：**
- 危险动作必须 require confirmation（通过 `PolicyDecision`）
- 关键动作必须串行执行
- 连续失败触发熔断
- 所有关键动作执行前后写审计日志

**依赖关系：**
- 依赖：`contracts/`, `platform/`（硬件/OS 接口），`infra/`（审计）
- 不依赖：`cognition/`，`llm/`，`tools/`（被 tools 单向依赖）

---

### 3.10 multi_agent/

**对应架构层：** Layer 4 — Execution & Collaboration Layer（Multi-Agent Workers）；调度权归属 Layer 6

**职责：**
管理 Worker Agent 实例，执行多 Agent 协同机制：子任务分派、结果合并、冲突仲裁和失败回收。调度指令由 `runtime/` 中的 `AgentOrchestrator` 发出，Worker 实例在本层运行。

**关键组件：**

| 组件 | 职责 |
|---|---|
| `AgentRegistry` | 按 capability/cost_class/max_concurrency/permission_domain 管理 Worker |
| `MultiAgentCoordinator` | 子任务拆分、派发、汇聚（由 runtime 控制）|
| `ResultMerger` | 按来源可信度、时间新鲜度合并结果，保留 conflicts 字段 |
| `WorkerAgent` | Worker 实例，独立上下文窗口、允许工具列表、租约时限 |

**协作模式：**

| 模式 | 适用场景 |
|---|---|
| `Orchestrator-Worker` | 主 Agent 规划汇总，Worker 执行（最常用） |
| `Pipeline` | 线性加工链（解析→草稿→审校→格式化） |
| `Debate` | 批判/验证/裁决（不作为默认模式） |

**失败处理策略：**
- `RESCHEDULE`：同类 Worker 短暂失败，重新调度
- `REPLAN`：关键子任务失败，整体重规划
- `SKIP`：非关键可选子任务失败，降级跳过
- `ABORT_AND_ROLLBACK`：无法恢复，终止并执行补偿

**日志追踪字段（必须包含）：**
`trace_id`, `agent_id`, `task_id`, `worker_type`, `lease_id`, `parent_task_id`

**依赖关系：**
- 依赖：`contracts/`, `runtime/`（接受调度指令），`tools/`（Worker 执行工具），`infra/`
- 不依赖：`cognition/` 直接实现（Worker 内部可有独立 cognition 实例）

---

### 3.11 platform/

**对应架构层：** Layer 2 — Platform Abstraction Layer

**职责：**
抽象 OS、线程、队列、定时器、文件系统、网络、IPC 和硬件外设接口，隔离 x86 桌面 Linux 与 ARM Embedded Linux 之间的差异，使上层业务模块不感知平台具体实现。

**接口分类：**

| 接口类 | 说明 | 平台相关性 |
|---|---|---|
| `IThread` / `ITimer` / `IQueue` | 线程与并发原语 | Linux 通用（POSIX） |
| `IFileSystem` | 文件操作抽象 | Linux 通用 |
| `INetwork` | 网络 I/O 抽象 | Linux 通用 |
| `IIPC` | 进程间通信（Unix Socket / Pipe） | Linux 通用 |
| `IGPIO` / `IUART` / `II2C` / `ISPI` / `ICAN` | 硬件接口（HAL） | ARM 嵌入式平台特有 |

**平台目录说明：**

| 目录 | 内容 |
|---|---|
| `src/linux/` | Linux 通用实现（x86 和 ARM 共用） |
| `src/x86/` | x86 平台特化（如 SIMD 优化、PCIe 设备） |
| `src/arm/hal/` | ARM 嵌入式 HAL 实现（GPIO/UART/I2C 等） |

**约束：**
- `platform/` 不依赖任何上层业务模块
- HAL 接口在 `edge_minimal` / `edge_balanced` Profile 下启用，在 `desktop_full` Profile 下可关闭

---

### 3.12 infra/

**对应架构层：** Layer 1 — Infrastructure Layer

**职责：**
提供系统级基础设施能力，包含四类观测能力（日志/追踪/监控/审计）和三类治理能力（配置/密钥/OTA），以及健康检查和 Watchdog。

**子模块：**

| 子模块 | 关键组件 | 说明 |
|---|---|---|
| `logging/` | `Logger`, `AuditLogger` | 结构化日志（INFO/WARN/ERROR/AUDIT），带 trace_id |
| `tracing/` | `Tracer` | 分布式追踪（OpenTelemetry 友好） |
| `metrics/` | `MetricsExporter` | P50/P95/P99 时延、成功率、队列积压等指标 |
| `config/` | `ConfigCenter` | 四层配置（默认→Profile→部署→运行时覆盖） |
| `secret/` | `SecretManager` | 凭证与敏感配置分离存储 |
| `health/` | `HealthMonitor`, `Watchdog` | 关键线程接入 Watchdog，熔断信号上报 |
| `ota/` | `OTAManager` | A/B 升级、回滚、升级前健康检查 |

**审计日志必须覆盖：**
- `prompt_id`, `prompt_version`, `model_name`（LLM 调用链路）
- `tool_name`, `tool_version`, `side_effects`（工具执行链路）
- `agent_id`, `task_id`, `lease_id`（多 Agent 链路）

**依赖关系：**
- 不依赖任何上层业务模块（被所有层单向依赖）

---

### 3.13 profiles/

**对应架构层：** 横跨 Layer 6 ~ Layer 2

**职责：**
定义按平台档位和部署形态裁剪的构建配置与运行策略，每个 Profile 同时覆盖"编译裁剪"和"运行治理"两层。

**Profile 档位：**

| Profile | 平台 | 特点 |
|---|---|---|
| `desktop_full` | x86 桌面/工作站 | 完整能力集，多模型路由，完整调试观测 |
| `cloud_full` | 高资源边缘服务器 | 云端模型优先，能力完整 |
| `edge_balanced` | ARM Embedded Linux（≥512MB） | 局域网模型优先，云端回退，控制并发和上下文预算 |
| `edge_minimal` | ARM Embedded Linux（受限） | 本地轻量模型，精简工具链，关键链路 only |
| `factory_test` | 产测/现场调试 | 诊断优先，审计可见性放宽，不放宽高风险执行确认 |

**每个 Profile 必须冻结的策略域：**

| 策略域 | 说明 |
|---|---|
| `target_platform` | 目标平台标识 |
| `enabled_modules` | 启用的子系统、工具集、插件集和观测能力 |
| `runtime_budget` | 线程数、内存水位、上下文窗口、工具并发、模型超时 |
| `model_route` | 各 stage 使用的模型、回退链路、是否允许云端 |
| `token_budget_policy` | 各 stage 的 max_input/output_tokens、压缩阈值 |
| `prompt_policy` | 允许的 PromptRelease、可信来源、工具可见范围 |
| `capability_cache_policy` | 刷新间隔、TTL、过期容忍策略 |
| `degrade_policy` | 模型/MCP/预算超限时的回退与收敛策略 |
| `timeout_policy` | LLM/Tool/MCP/Workflow 的超时与熔断阈值 |
| `execution_policy` | 执行权限、确认门槛、安全模式、审计等级 |
| `ops_policy` | 日志等级、监控粒度、远程诊断开关、升级策略 |

**Profile 约束（不可违反）：**
- 同一 `contracts/` 层对象在不同 Profile 中保持一致
- Profile 只能裁剪能力和替换实现，不能绕过 Policy Gate、Audit 和 Runtime 主控链路
- 新平台接入时优先补 Platform Adapter 和 Profile，不分叉新的 Agent 主流程
- Profile 差异通过配置、注册表和依赖注入落地，不在主流程中散落平台分支

---

### 3.14 skills/

**对应架构层：** Layer 5（Cognition 选择）/ Layer 4（Tool 系统执行）

**职责：**
存放任务级复用资产（Skill），封装某类任务常用的 Prompt、Tool、Workflow、输入输出契约和降级策略。Skill 由 Planner/Reasoner 选择实例化，但不绕过 ToolRegistry、PolicyGate 和 Executor。

**子目录：**

| 子目录 | 内容 |
|---|---|
| `specs/` | `SkillSpec` YAML/JSON 定义（skill_id/version/intent_patterns/allowed_tools/workflow_template/prompt_bundle/fallback_strategy） |
| `prompts/` | `PromptBundle` 与 `PromptRelease` 资产 |
| `workflows/` | `WorkflowTemplate` 步骤图定义 |
| `evals/` | Skill 评测套件（输入样例、期望输出、验收标准） |

**Skill 运行时对象（在 tools/ 中实现）：**
- `SkillRegistry`：负责注册和匹配
- `SkillRuntime`：负责实例化和生命周期控制
- `SkillInstance`：运行时实例，绑定本次请求的工具范围和上下文

---

### 3.15 tests/

**职责：**
多层次测试体系，覆盖单元、集成、契约、端到端和长稳五类测试。

**测试分类：**

| 目录 | 类型 | 说明 |
|---|---|---|
| `unit/` | 单元测试 | 各模块独立行为验证，依赖全 Mock |
| `integration/` | 集成测试 | LLM + Tool + Memory + Runtime 联动 |
| `contract/` | 契约测试 | schema 稳定性、错误码一致性、事件格式 |
| `e2e/` | 端到端测试 | 真实/回放任务流程，含多 Agent 场景 |
| `stress/` | 长稳测试 | 长期运行、恢复能力、内存泄漏监控 |
| `mocks/` | Mock/Stub | 提供可复用的 Mock ILLMAdapter、MockTool 等；头文件位于 `tests/mocks/include/` 扁平 include 根，断言辅助位于 `tests/mocks/include/support/` |

**测试原则：**
- 每个核心接口（IAgent、ITool、IMemoryStore 等）必须有对应的 Mock 实现
- 契约测试覆盖 `contracts/` 中所有核心对象的序列化/反序列化稳定性
- 端到端测试场景必须覆盖：单 Agent 问答、工具调用、多 Agent Orchestrator-Worker、checkpoint 恢复

---

### 3.16 third_party/

**职责：**
管理第三方依赖，优先以 Git submodule 或 CMake FetchContent 方式引入。

| 库 | 用途 |
|---|---|
| `nlohmann_json` | JSON 序列化/反序列化 |
| `spdlog` | 高性能结构化日志 |
| `faiss` | 向量索引（可选，edge_minimal 下可关闭） |
| `sqlite3` | 轻量嵌入式数据库 |
| `googletest` | 单元测试框架 |
| `openssl` | TLS / 密钥管理 |

---

### 3.17 cmake/ 与 scripts/

**cmake/：** CMake 公共模块，包含编译器选项、Profile 工具函数、依赖查找和安装规则。

**scripts/：** 构建、打包、部署和诊断的 Shell 脚本。

| 脚本 | 用途 |
|---|---|
| `build.sh` | 统一构建入口（指定 Profile） |
| `package.sh` | 打包为目标产物（OTA 包、安装包） |
| `deploy.sh` | 部署到目标设备 |
| `health_check.sh` | 运行时健康检查探针 |
| `generate_coverage.sh` | 测试覆盖率报告生成 |

---

### 3.18 docs/

**职责：**
存放所有文档资产，按类型分目录管理。

| 子目录 | 内容 |
|---|---|
| `architecture/` | 系统架构设计文档（含本蓝图文档） |
| `api/` | 接口设计与 API 参考文档 |
| `adr/` | 架构决策记录（ADR-001 ~ ADR-N） |

---

## 4. 模块依赖规则

### 4.1 依赖方向原则

```text
apps/
  → runtime/ contracts/ infra/

runtime/
  → contracts/ cognition/(接口) llm/(接口) tools/(接口)
    memory/(接口) knowledge/(接口) multi_agent/(接口)
    infra/ services/(接口)

cognition/
  → contracts/ llm/(接口)

llm/
  → contracts/ infra/

tools/
  → contracts/ services/(接口) infra/

memory/
  → contracts/ infra/

knowledge/
  → contracts/ memory/(VectorStore接口) infra/

services/
  → contracts/ platform/ infra/

multi_agent/
  → contracts/ tools/(接口) infra/

platform/
  → contracts/ 【不依赖上层任何模块】

infra/
  → contracts/ 【不依赖上层任何业务模块】

skills/ （资产，无依赖关系）
contracts/ 【不依赖任何模块】
```

### 4.2 依赖禁止规则（不可违反）

| 禁止的依赖方向 | 原因 |
|---|---|
| `cognition/` → `tools/` 实现 | 认知层只产生意图，不直接执行 |
| `cognition/` → `platform/` | 认知层不感知 OS 和硬件差异 |
| `tools/` → `cognition/` 实现 | 工具不访问 Cognition 内部状态 |
| `tools/` → `llm/` 实现 | 工具不直接调用模型 |
| `services/` → `llm/` 或 `cognition/` | 能力服务层不反向依赖推理层 |
| `platform/` → 任何业务模块 | 平台层不感知业务逻辑 |
| `infra/` → 任何业务模块 | 基础设施层不反向依赖业务 |
| 工具之间直接互调 | 跨工具编排必须通过 WorkflowEngine |

### 4.3 接口优先原则

所有跨模块调用必须通过 `contracts/` 中定义的接口（`IXxx.h`）进行，禁止直接引用其他模块的具体实现类。依赖注入在 `runtime/` 的装配层（或 `apps/` 的启动入口）完成。

---

## 5. Build Profile 与平台裁剪

### 5.1 Profile 与模块启用矩阵

| 模块 / Profile | desktop_full | cloud_full | edge_balanced | edge_minimal | factory_test |
|---|:---:|:---:|:---:|:---:|:---:|
| `runtime/` | ✅ | ✅ | ✅ | ✅ | ✅ |
| `cognition/` | ✅ | ✅ | ✅ | ✅ | ✅ |
| `llm/` (cloud adapter) | ✅ | ✅ | ✅（回退）| ❌ | 可选 |
| `llm/` (LAN adapter) | ✅ | ✅ | ✅（主路）| 可选 | ✅ |
| `llm/` (local adapter) | ✅ | ❌ | ✅（回退）| ✅（主路）| ✅ |
| `tools/` (builtin) | ✅ | ✅ | ✅ | ✅（精简）| ✅ |
| `tools/` (MCP) | ✅ | ✅ | ✅ | ❌ | 可选 |
| `memory/` (vector) | ✅ | ✅ | ✅ | ❌ | ❌ |
| `memory/` (experience) | ✅ | ✅ | ✅ | ❌ | ❌ |
| `knowledge/` | ✅ | ✅ | ✅ | ❌ | ❌ |
| `multi_agent/` | ✅ | ✅ | 可选 | ❌ | ❌ |
| `platform/` HAL | ❌ | ❌ | ✅ | ✅ | ✅ |
| `infra/` (full obs) | ✅ | ✅ | 部分 | 最小 | ✅ |

### 5.2 Profile 激活方式

```bash
# 示例：构建 edge_balanced Profile
cmake -B build \
  -DDASALL_PROFILE=edge_balanced \
  -DCMAKE_TOOLCHAIN_FILE=cmake/arm-linux-gnueabihf.cmake
cmake --build build
```

---

## 6. 关键接口文件分布

以下列出各模块主要对外暴露的接口头文件路径，这些文件是跨模块依赖的唯一合法入口：

```text
contracts/include/
  boundary/BoundaryGuards.h, CompatibilityGuards.h
  boundary/ContextBoundaryGuards.h, RecoveryBoundaryGuards.h
  boundary/MultiAgentBoundaryGuards.h, ObjectBoundaryCatalog.h
  agent/AgentRequest.h, AgentResult.h, GoalContract.h
  context/ContextPacket.h, ContextAssembleRequest.h
  observation/Observation.h, ObservationDigest.h, BeliefState.h
  tool/ToolRequest.h, ToolResult.h, ToolIR.h, ToolRoute.h
  llm/LLMRequest.h, LLMResponse.h, ModelRoute.h
  prompt/PromptSpec.h, PromptRelease.h, PromptComposeRequest.h
  policy/PolicyDecision.h, ReflectionDecision.h
  error/ErrorInfo.h, ResultCode.h
  checkpoint/Checkpoint.h, RuntimeBudget.h
  event/EventEnvelope.h

runtime/include/dasall/runtime/
  IAgent.h, IAgentOrchestrator.h, ISessionManager.h

cognition/include/dasall/cognition/
  ICognitionEngine.h, IPlanner.h, IReasoner.h, IReflectionEngine.h

llm/include/dasall/llm/
  ILLMAdapter.h, ILLMManager.h, IPromptRegistry.h, IPromptComposer.h

tools/include/dasall/tools/
  ITool.h, IToolManager.h, IPolicyGate.h, ICapabilityCache.h
  mcp/IMCPAdapter.h

memory/include/dasall/memory/
  IMemoryStore.h, IMemoryManager.h, IContextOrchestrator.h

knowledge/include/dasall/knowledge/
  IKnowledgeService.h

services/include/dasall/services/
  IExecutionService.h, IDataService.h

multi_agent/include/dasall/multi_agent/
  IAgentRegistry.h, IResultMerger.h

platform/include/dasall/platform/
  IThread.h, ITimer.h, IQueue.h, IFileSystem.h, INetwork.h
  hal/IGPIO.h, IUART.h, II2C.h

infra/include/dasall/infra/
  ILogger.h, ITracer.h, IMetricsExporter.h, IAuditLogger.h
  IConfigCenter.h, ISecretManager.h, IHealthMonitor.h
```

---

## 7. 测试规划

### 7.1 测试层次与覆盖目标

| 测试类型 | 覆盖目标 | 工具建议 |
|---|---|---|
| 单元测试 | 每个类/函数的独立行为 | GoogleTest + GMock |
| 集成测试 | Runtime + Cognition + Tool + Memory 联动 | GoogleTest（集成测试组） |
| 契约测试 | contracts/ 全部对象的序列化稳定性、错误码一致性 | 自定义 Schema Validator |
| 端到端测试 | 完整任务流程（单 Agent / 多 Agent / checkpoint 恢复） | GoogleTest + 模拟服务桩 |
| 长稳测试 | 72h+ 连续运行，内存水位、泄漏率、恢复成功率 | Valgrind、AddressSanitizer |

### 7.2 必须覆盖的端到端场景

1. 单 Agent 简单问答（无工具调用）
2. 单 Agent 工具调用（Information Tool）
3. 单 Agent 工具调用（Action Tool + 确认门 + 审计）
4. 单 Agent 失败反思与重规划
5. 单 Agent checkpoint 保存与 resume
6. 多 Agent Orchestrator-Worker 子任务分派与汇聚
7. 工具执行失败 + 补偿回滚
8. LLM 故障降级（Cloud → LAN → Local 切换）
9. 会话超时与安全收敛（SafeMode）
10. `edge_minimal` Profile 下的完整最小链路验证

### 7.3 Mock 原则

- `MockLLMAdapter`：可配置输出 DirectResponse / ToolCallIntent / ClarificationRequest
- `MockTool`：可配置成功/失败/超时/副作用输出
- `MockExecutionService`：用于工具执行控制层集成测试
- `MockMemoryStore`：可注入预置的会话状态和历史
- 测试代码默认从 `tests/mocks/include/` 直接 include mock 头；断言辅助统一使用 `support/TestAssertions.h`，不保留旧的嵌套 include 前缀

---

## 8. 工程约束与演进原则

### 8.1 必须遵守的工程约束

1. **契约优先**：先在 `contracts/` 冻结接口和数据对象，再在各模块实现。修改 contracts 必须同步更新所有消费方。
2. **接口隔离**：跨模块调用只能通过 `IXxx.h` 接口，禁止直接依赖具体实现类。
3. **不绕过治理链路**：LLM 输出不能直接执行，工具调用必须经过 Validator → PolicyGate → Executor。
4. **Profile 不分叉主流程**：平台差异通过 Profile + Adapter + DI 落地，不在 runtime / cognition 主流程中散落 `#ifdef` 分支。
5. **副作用必须审计**：所有 Action Tool 和外部执行动作，执行前后必须写审计日志，并定义补偿策略。
6. **状态机必须完备**：等待澄清、等待确认、安全失败必须作为显式状态，不能是隐藏分支。

### 8.2 演进原则

1. **单模块演进**：新平台适配优先补 `platform/` 和对应 Profile，不修改核心主流程。
2. **新工具接入**：通过 `ToolRegistry` 注册，按扩展方式（静态/配置/插件/MCP）接入，不修改 `tools/` 核心代码。
3. **新模型接入**：实现 `ILLMAdapter`，更新 `ModelRouter` 路由策略和对应 Profile 的 model_route 配置。
4. **能力裁剪**：在 Profile 的 `enabled_modules` 中关闭，不删除代码。
5. **ADR 留痕**：所有架构关键决策在 `docs/adr/` 中记录 ADR，包含背景、决策、权衡和影响。

### 8.3 代码组织约定

1. 每个模块的 `include/` 目录只暴露 `IXxx.h` 接口，具体实现放在 `src/`。
2. 头文件路径约定：`dasall/<module_name>/IXxx.h`，例如 `#include "dasall/tools/ITool.h"`。
3. 命名空间约定：`namespace dasall::<module_name>`。
4. 所有跨线程共享状态必须通过受控队列或 Working Memory 黑板管理，禁止裸共享可变对象。
5. 取消与超时必须使用 `CancelToken + Deadline` 贯穿 LLM 与 Tool 调用链。

---

*文档版本：v1.0 | 日期：2026-03-12 | 基于 DASALL Agent 架构设计文档 v2.0*
