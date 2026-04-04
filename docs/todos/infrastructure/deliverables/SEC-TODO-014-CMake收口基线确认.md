# SEC-TODO-014 CMake 收口基线确认

日期：2026-04-04
任务：SEC-TODO-014
状态：已完成

## 1. 输入依据

1. docs/todos/infrastructure/DASALL_infrastructure_secret组件专项TODO.md 已将 SEC-TODO-014 定义为“接线 infra/secret 到 CMake”，完成判定是 placeholder 不再是唯一功能入口且 secret 文件入图。
2. SEC-TODO-006~013 已逐步落盘 Mock/File backend、manager、lease、rotation、audit、health 等 secret 骨架，因此本轮需要把分散接入过的源/头文件收敛为明确的 CMake 基线。
3. 当前 `infra/CMakeLists.txt` 已存在 `DASALL_INFRA_SECRET_SOURCES`，但 secret 公共头和私有头尚未形成显式分组，无法清晰证明 `infra/include/secret` 与 `infra/src/secret` 已整体入图。

## 2. 研究学习结果

### 2.1 本地证据

1. `infra/src/secret` 目录现已包含 SecretManagerFacade、SecretLeaseRegistry、SecretRotationCoordinator、SecretAuditBridge、SecretHealthProbe 及 backend 头/源，说明 014 的关键不再是新增实现，而是集中声明“这些文件全部属于 dasall_infra 的 secret 子图”。
2. `infra/include/secret` 公共头已稳定在 `ISecretBackend`、`ISecretManager`、`ISecretHealthSource`、`SecureBuffer`、`SecretErrors`、`SecretTypes` 六个入口，适合单列成 public header 组。
3. CMake 当前通过 `PUBLIC_HEADER` 暴露公共头，通过 `target_sources` 暴露源码，因此把 secret public/private 头显式分组后即可完成“文件入图”收口，而不需要改动构建产物边界。

### 2.2 可落地启发

1. `DASALL_INFRA_SECRET_PUBLIC_HEADERS` 与 `DASALL_INFRA_SECRET_PRIVATE_HEADERS` 分组能把 secret 接口面和内部骨架分开声明，既满足 014 的收口目标，又不影响后续 015/016 的测试注册。
2. 把 private headers 纳入 `target_sources(dasall_infra PRIVATE ...)` 能让 `infra/src/secret` 整个子树显式挂到 `dasall_infra` 目标上，避免“源码进图、头文件游离”的歧义。

## 3. Design -> Build 映射

| Design 结论 | Build 落地 |
|---|---|
| secret 公共头集中声明 | infra/CMakeLists.txt 中新增 `DASALL_INFRA_SECRET_PUBLIC_HEADERS` |
| secret 私有头集中声明并入图 | infra/CMakeLists.txt 中新增 `DASALL_INFRA_SECRET_PRIVATE_HEADERS` 并挂到 `target_sources` |
| secret 源码继续保持独立 source list | infra/CMakeLists.txt 中保留 `DASALL_INFRA_SECRET_SOURCES` |

## 4. Build 落地结果

1. 更新 infra/CMakeLists.txt，新增 `DASALL_INFRA_SECRET_PUBLIC_HEADERS`，把 `infra/include/secret` 六个公共入口从大列表中抽出单列。
2. 更新 infra/CMakeLists.txt，新增 `DASALL_INFRA_SECRET_PRIVATE_HEADERS`，显式纳入 SecretAuditBridge / SecretHealthProbe / SecretManagerFacade / SecretLeaseRegistry / SecretRotationCoordinator / SecretRotationValidator 及 backend 私有头。
3. 更新 `target_sources(dasall_infra ...)`，把 secret 私有头一并挂到 `dasall_infra`，完成 `infra/src/secret` 整棵树的显式入图。

## 5. 验证结果

1. `cmake -S . -B build-ci -G "Unix Makefiles"`：通过。
2. `cmake --build build-ci --target dasall_infra`：通过。

## 6. 结论

1. SEC-TODO-014 已把 infra/secret 的公共头、私有头和源码从“逐任务增量接入”收敛为“集中声明、整体入图”的 CMake 基线。
2. `dasall_infra` 现已不再依赖 placeholder-only 入口来代表 secret 子域，后续 015 可以直接在这一基线之上收口 unit/contract 测试矩阵。