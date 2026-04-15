# DASALL Access 方案3迁移影响清单

版本：v1.0
日期：2026-04-15
阶段：Architecture Sync
适用范围：access/、apps/、Layer 7 Product & Access Layer

## 1. 迁移目标

方案3的正式口径为：

1. 顶层 `access/` 作为 Access 子系统的独立工程根，承载共享 access core、module public interface、Admission pipeline、Runtime bridge、Publish/Replay/Observability 核心实现。
2. `apps/cli`、`apps/daemon`、`apps/gateway`、`apps/simulator` 退回为入口壳层与装配面，只负责进程生命周期、entry-specific bootstrap、监听绑定和协议适配器挂接。
3. Runtime 继续是唯一全局主控；Access 继续只承接 Access Channel，不新增 `communication/` 顶层模块，也不把 Service/Field/LLM Channel 收回自己。

## 2. 完整影响清单

| 影响域 | 具体对象 | 必须动作 | 本轮状态 |
|---|---|---|---|
| Access 详细设计 | `docs/architecture/DASALL_access子系统详细设计.md` | 把工程根、约束、现状、方案结论、目录树、Design -> Build 映射、里程碑判定统一改为 `access/ + apps/` 双层落点 | 已同步 |
| 总架构文档 | `docs/architecture/DASALL_Agent_architecture.md` | 把 Layer 7 工程落点从“只有 apps”改成“access core + apps 壳层”，更新运行时调用链落点与目录树 | 已同步 |
| 工程蓝图 | `docs/architecture/DASALL_Engineering_Blueprint.md` | 在顶层目录树加入 `access/`；把 Layer 7 目录映射改为 `access/、apps/`；重写 3.2 节，使 access 持共享 core、apps 持入口壳层 | 已同步 |
| 架构总览 | `docs/architecture/DASALL_架构设计文档.md` | 更新 Layer 7 行、Access 子系统工程落点、目录映射表和工程结构图 | 已同步 |
| Runtime 边界文档 | `docs/architecture/DASALL_runtime子系统详细设计.md` | 把上游表述改成“apps 入口壳层经 access/ 接入 Runtime”，去掉“apps/gateway 单点持入口职责”的旧口径 | 已同步 |
| 根构建图 | `CMakeLists.txt` | 新增 `add_subdirectory(access)`，让顶层 access 成为正式子系统 | 已同步 |
| Access 工程骨架 | `access/CMakeLists.txt`、`access/include/*`、`access/src/*` | 建立最小独立库目标 `dasall_access` 与 module public interface 占位骨架 | 已同步 |
| 入口壳层依赖 | `apps/cli/CMakeLists.txt`、`apps/daemon/CMakeLists.txt`、`apps/gateway/CMakeLists.txt`、`apps/simulator/CMakeLists.txt` | 四个入口改为链接 `dasall_access`，体现“壳层依赖共享 core” | 已同步 |
| 验证面 | 文本搜索、诊断、构建 | 清除规范性文档中的旧路径 `apps/include/access`、`apps/src/access` 和“apps 作为 access 根”的旧说法；验证 CMake/C++ 无新增错误 | 已同步 |
| 历史记录类文档 | `docs/worklog/**`、已完成 TODO、历史评审记录 | 不回写历史事实，只在新的规范性文档与当前实施文档中收口 | 保持不改 |
| 提示词/流程模板 | `docs/development/**` 中与本次落地无直接工程真源关系的模板 | 不作为本轮同步对象，避免把架构决策与写作模板耦合 | 保持不改 |

## 3. 落地顺序

1. 先统一规范性文档口径，否则 `access/` 与 `apps/` 的职责会在总架构、蓝图和详细设计之间互相打架。
2. 再落顶层 `access/` 骨架和根 CMake/入口链接关系，确保工程结构与文档一致。
3. 最后做搜索和诊断校验，确认旧口径已从规范性文档中清除，且新的库目标不会引入构建错误。

## 4. 本轮不做的事

1. 不在本轮实现完整 Admission pipeline、Runtime bridge、ResultPublisher 或 async/query 主链。
2. 不在本轮补齐 `tests/unit/access` 与 `tests/integration/access` 的真实测试目标。
3. 不修改历史 worklog/TODO 中已经记录的既往事实。

## 5. 验收基线

1. 规范性文档中不再把 `apps/` 表述为 access shared core 的唯一根目录。
2. 根 CMake 与四个入口目标能识别 `dasall_access`。
3. 仓库中实际存在 `access/` 顶层目录、独立 `CMakeLists.txt` 和最小 public interface 骨架。
