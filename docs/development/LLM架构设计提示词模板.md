# Role
你是一名具有15年以上经验的嵌入式系统架构师，擅长：
- 嵌入式系统架构设计
- C/C++工程架构
- AI Agent系统
- LLM Agent Runtime
- Edge AI / 边缘智能
- 多模块复杂系统设计
- RTOS / Linux嵌入式系统
- 驱动层 / 中间件 / 应用层架构
- 可维护性、可扩展性、模块解耦设计
- 大型嵌入式项目的工程化落地

你曾经设计过：
- Agent运行时系统
- Tool Calling 框架
- Edge AI 推理系统
- LLM Runtime
- 多线程高可靠系统

你的任务是：
根据我提供的资料与需求，设计一份 **完整、工程化、可落地的Agent软件架构方案**。

要求设计具有：
- 清晰的分层结构
- 明确的模块职责
- 良好的解耦
- 适合长期维护和迭代


---

# Input

我会提供以下内容：

## 1. 技术调研资料 / 学习总结
- docs/LLM Agent学习.md
- 开源MCP / Tool Calling资料
- 开源Copilot Agent资料
- 其它在线的资料

---

## 2. 架构目标

系统的主要设计目标：设计一个完备的LLM Agent智能体

- 支持局域网 LLM
- 支持本地 LLM
- 支持云端 LLM（默认）
- 支持 Tool 调用
- 支持多 Agent
- 支持设备控制
- 支持知识库
- 支持长期运行
- 等等完整Agent具备的所有特性


---

## 3. 功能需求

系统需要实现的功能：完整的Agent智能体

例如：

- 用户输入
- LLM推理
- 工具调用
- 设备控制
- 数据查询
- 任务执行
- 多轮对话

---

## 4. 非功能需求
- 项目名称+Agent名称：DASALL
- 分层设计
- 模块化设计
- 完整清晰的职责边界
- 高可靠
- 低资源占用
- 可扩展
- 可维护
- 可升级
- 长期稳定运行

- 最终生成的文档保存在：docs/architecture/DASSALL_Agent_architecture.md

---

## 5. 已有初步架构设想
- 参考docs/DASALL工程蓝图.md

---

# Task

请基于以上信息，设计完整的嵌入式软件架构方案。该方案将指导AI和我进行具体的分层/分模块方案设计。

请按以下步骤进行：

---

# Step1 系统整体架构

给出：

1. 系统架构设计理念
2. 描述系统整体架构
3. 分层架构图（文字描述即可）
4. 每一层的职责


例如：

User
↓
Agent
↓
Cognition Layer
↓
Runtime Layer
↓
Tool System
↓
Hardware / Service


---

- 模块划分
说明每个模块：
1. 职责
2. 输入
3. 输出
4. 依赖关系


---

# Step3 Agent运行流程

描述完整 Agent 运行流程：

例如：

User Input
↓
Context Build
↓
LLM Reasoning
↓
Tool Decision
↓
Tool Execution
↓
Result Integration
↓
Response


同时描述：

- LLM何时调用工具
- 如何处理工具结果

---

# Step4 LLM适配层设计

设计 LLM Adapter。

要求支持：

- 本地 LLM
- 云 LLM

例如：

OpenAI Adapter

Local LLM Adapter

HTTP LLM Adapter

说明：

- 接口设计
- 统一调用方式


---

# Step5 Tool 系统设计

设计 Tool 框架。

例如：

Tool Manager

Tool Registry

Tool Executor

Tool Interface


说明：

- Tool 注册
- Tool 调用
- Tool 结果返回


---

# Step6 Memory 系统

设计 Memory：

例如：

Short Term Memory

Long Term Memory

Vector Memory


说明：

- 存储方式
- 查询方式


---

# Step7 C/C++工程目录结构

设计完整工程结构：

例如：

DASALL-Agent/
    core/
    cognition/
    runtime/
    tools/
    llm/
    memory/
    utils/
    platform/
    third_party/
    tests/
docs/
    architecture/
        system_architecture.md
        agent_architecture.md
        runtime_architecture.md
    design/
        module_design.md
    protocol/
        tool_protocol.md
    diagrams/
        architecture.png
        agent_flow.png

README.md


说明：

- 每个目录职责


---

# Step8 C++核心接口设计

为关键模块设计接口。

例如：

Agent

init()

handle_input()

process()

execute_tool()

respond()


Tool Interface

execute()

name()

description()


LLM Interface

generate()

stream_generate()


---

# Step9 系统运行模型

说明系统运行模式：

例如：

- 任务模型
- 线程模型
- 事件驱动
- 消息队列
- Reactor / Actor / Pipeline


例如：

main loop
↓
event dispatcher
↓
service modules

---

# Step10 事件系统

设计 Event Bus：

例如：

event types：

USER_INPUT

LLM_RESPONSE

TOOL_REQUEST

TOOL_RESULT

---

# Step11 Agent状态机

设计 Agent 状态机：

例如：

Idle

Thinking

ToolCalling

Responding


说明状态转换。


---

---

# Step12 关键设计模式

分析应该使用的设计模式，例如：

- Facade
- Adapter
- Observer
- State
- Factory
- Dependency Injection

说明使用原因。


---

# Step13 关键工程机制

说明系统工程能力：

1. 日志系统
2. 配置系统
3. 错误处理
4. 监控诊断
5. OTA升级
6. 插件扩展机制


---

# Step14 线程模型

说明：

- 线程划分
- 各线程职责
- 通信方式

例如：

main thread

device thread

network thread

algorithm thread


---

# Step15 资源管理

分析：

- CPU
- RAM
- IO
- 功耗

如何优化。


---

# Step16 可扩展设计

说明：

未来增加以下能力时如何扩展：

- 新设备
- 新通信协议
- 新算法
- 新服务
- 新工具
- 新Agent
- 新LLM
- 新设备


---

# Output要求

输出必须：

1. 结构化
2. 工程化
3. 可落地
4. 面向C/C++
5. 适合嵌入式项目
6. 面向Agent系统

必要时可以给出：

- 伪代码
- 接口示例
- 目录示例
- 类图
- 模块图