# AUD-TODO-016 Audit 构建接线收敛

日期：2026-04-03  
任务：AUD-TODO-016  
状态：已完成

## 1. 输入依据

1. [docs/todos/infrastructure/DASALL_infrastructure_audit组件专项TODO.md](docs/todos/infrastructure/DASALL_infrastructure_audit组件专项TODO.md) 已将 `AUD-TODO-016` 定义为“注册 audit 源码到 infra CMake”，完成标准是 placeholder 不再是唯一源码入口，且 audit 文件正式进入 `dasall_infra` 构建图。
2. [docs/architecture/DASALL_infra_audit模块详细设计.md](docs/architecture/DASALL_infra_audit模块详细设计.md) 7/8.1 已给出 audit 源码落盘建议，要求主链路骨架在 build 图中可被独立追踪与验证。
3. `AUD-TODO-008` 到 `AUD-TODO-011` 虽然已经顺带把 `AuditValidator.cpp`、`AuditPipeline.cpp`、`AuditFallbackPipeline.cpp`、`AuditService.cpp` 接入构建，但当前 [infra/CMakeLists.txt](infra/CMakeLists.txt) 仍把这些文件混在 `DASALL_INFRA_CORE_SOURCES` 与通用 public header 列表中，缺少 audit 专项入口。

## 2. 研究学习结果

### 2.1 本地证据

1. 当前 `infra/CMakeLists.txt` 已能编译 audit 实现，但 audit source/header 没有独立变量，难以作为专项 TODO 的 Design -> Build 证据回链。
2. `AuditInterfaceCompileTest` 已存在且可作为 016 的最小验收出口，用来确认 `dasall_infra` 和 audit public header 接线仍然成立。
3. 017 将继续处理 audit 测试注册与标签 discoverability，因此 016 不需要扩张到测试标签收口，只需把构建入口做清晰分层。

### 2.2 外部参考

1. OpenTelemetry 项目布局指南强调，稳定组件边界应在构建层也保持显式分组，而不是长期埋在笼统的 core/source 聚合列表中；这有助于维护者理解组件归属和演进路径。

### 2.3 可落地启发

1. 016 的核心不是“让 audit 首次可编译”，而是把已接入的 audit build surface 正式收敛成独立 CMake 入口。
2. 最小改法是新增 `DASALL_INFRA_AUDIT_SOURCES` 与 `DASALL_INFRA_AUDIT_PUBLIC_HEADERS`，避免无关源码排序重排或 target 结构变更。

## 3. Design 原子清单

| D 子项 | 设计目标 | 输入依据 | 产出 | 完成判定 |
|---|---|---|---|---|
| D1 | 冻结 audit 源码的独立 CMake source 入口 | audit 设计 7/8.1；当前 infra/CMakeLists | `DASALL_INFRA_AUDIT_SOURCES` | audit 骨架源码不再散落在 core 列表 |
| D2 | 冻结 audit public header 的独立导出入口 | audit public headers 现状 | `DASALL_INFRA_AUDIT_PUBLIC_HEADERS` | audit 公共头从通用 header 列表分层可见 |
| D3 | 锁定 Build 三件套 | 016 任务行；现有 unit 出口 | 代码目标、测试目标、验收命令 | `dasall_infra` 与 `AuditInterfaceCompileTest` 有明确验证出口 |

## 4. D Gate 结论

### 4.1 Design -> Build 映射

| Design 结论 | Build 落地 |
|---|---|
| audit 源码从通用 core 列表抽为独立入口 | 更新 [infra/CMakeLists.txt](infra/CMakeLists.txt)，新增 `DASALL_INFRA_AUDIT_SOURCES` |
| audit public headers 从通用 header 列表抽为独立入口 | 更新 [infra/CMakeLists.txt](infra/CMakeLists.txt)，新增 `DASALL_INFRA_AUDIT_PUBLIC_HEADERS` |
| `dasall_infra` 仍通过同一 target 暴露 audit public API | `target_sources` 和 `PUBLIC_HEADER` 同步纳入 audit 变量 |

### 4.2 Build 三件套

1. 代码目标：更新 [infra/CMakeLists.txt](infra/CMakeLists.txt)，把 audit 的 source/header 收口为独立变量并接入 `dasall_infra`。
2. 测试目标：复用 `AuditInterfaceCompileTest`，确认 audit public 头与 `dasall_infra` 构建图接线稳定。
3. 验收命令：
   - `cmake -S . -B build-ci -G "Unix Makefiles"`
   - `cmake --build build-ci --target dasall_infra dasall_audit_logger_interface_unit_test`
   - `ctest --test-dir build-ci -N -R "AuditInterfaceCompileTest"`
   - `ctest --test-dir build-ci -R "AuditInterfaceCompileTest" --output-on-failure`

说明：专项 TODO 基线命令写的是 Ninja，但当前 `build-ci` 已锁定为 Unix Makefiles，本轮沿用现有生成器验证，不构成 blocker。

### 4.3 D Gate

结论：PASS。

理由：

1. 构建收敛边界已明确，且不依赖额外 blocker。
2. 本轮仅触及 [infra/CMakeLists.txt](infra/CMakeLists.txt) 与证据文档，不扩张到测试标签或新实现。

## 5. Build 落地结果

1. 更新 [infra/CMakeLists.txt](infra/CMakeLists.txt)，新增 `DASALL_INFRA_AUDIT_SOURCES`，收口 `AuditValidator.cpp`、`AuditPipeline.cpp`、`AuditFallbackPipeline.cpp`、`AuditService.cpp`。
2. 更新 [infra/CMakeLists.txt](infra/CMakeLists.txt)，新增 `DASALL_INFRA_AUDIT_PUBLIC_HEADERS`，收口 `AuditTypes.h`、`AuditExporterTypes.h`、`AuditErrors.h`、`AuditService.h`、`IAuditLogger.h`。
3. `target_sources(dasall_infra ...)` 与 `PUBLIC_HEADER` 已显式接入 audit 专项变量，audit build surface 不再混在通用 core/public 列表中不可追踪。

## 6. 验证结果

1. `ctest --test-dir build-ci -N -R "AuditInterfaceCompileTest"`：发现 1 个定向测试。
2. `ctest --test-dir build-ci -R "AuditInterfaceCompileTest" --output-on-failure`：1/1 通过。
3. `dasall_infra` 与 `dasall_audit_logger_interface_unit_test` 定向构建通过。

## 7. 结论

1. `AUD-TODO-016` 已从“audit 源码虽可编译但未形成专项构建入口”推进到“audit source/header 在 infra CMake 中具备显式分层入口”。
2. 016 完成后，017 可以专注于 audit unit/contract 测试注册与 discoverability 收口，而不必再兼顾构建入口清理。