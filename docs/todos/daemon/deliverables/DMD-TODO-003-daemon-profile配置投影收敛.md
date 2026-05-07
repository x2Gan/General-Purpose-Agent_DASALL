# DMD-TODO-003 daemon profile 配置投影收敛

状态：Done
日期：2026-04-28
来源 TODO：docs/todos/daemon/DASALL-daemon本地控制面专项TODO.md

## 1. 任务边界

1. 本任务在 `profiles/` 内补齐 daemon 配置键资产与投影 helper，不让 `profiles` 反向依赖 `apps/daemon`。
2. DMD-BLK-001 的最小解法是“五档 profile 显式补齐 daemon 键，并提供 profiles 侧 projection helper 验证这些键可被稳定加载”；不扩张到 `RuntimePolicySnapshot` 契约改造。
3. `DaemonBootstrapConfig` 与 `DaemonProfileSettings` 继续分属 `apps/daemon` 和 `profiles` 两个模块，当前只冻结 profile 侧 settings 投影与资产一致性。

## 2. Design -> Build 映射

| 设计结论 | Build 落点 | 验收信号 |
|---|---|---|
| profiles 不能反向依赖 apps，因此 daemon profile projection 必须在 profiles 内独立落型 | `profiles/include/DaemonProfileProjection.h` | `DaemonProfileProjectionTest` 可在不包含 apps 头文件的前提下加载 profile 资产 |
| DMD-BLK-001 要求五档 profile 具有 daemon 运行键或显式安全默认投影 | 五个 `profiles/*/runtime_policy.yaml` 的 `daemon.*` 段 | `ProfileMatrixConsistencyTest` 断言所有 baseline profile 都包含关键 daemon 键 |
| profile helper 要为后续 daemon config validator 提供可复用载体 | `DaemonProfileSettings` + `DaemonProfileProjection::load()` | helper 输出的 socket_path/backlog/timeout/diag/watchdog 具备稳定读取语义 |

## 3. 落盘结果

1. 新增 `profiles/include/DaemonProfileProjection.h` 与 `profiles/src/DaemonProfileProjection.cpp`，定义 `DaemonProfileSettings`、`DaemonProfileProjectionRequest/Result` 与 `DaemonProfileProjection`。
2. 五个 baseline profile 的 `runtime_policy.yaml` 全部新增 `daemon.socket_path`、`daemon.listen_backlog`、`daemon.dispatch_timeout_ms`、`daemon.diag.enabled`、`daemon.watchdog.enabled`。
3. 新增 `tests/unit/profiles/DaemonProfileProjectionTest.cpp` 并更新 `tests/unit/profiles/CMakeLists.txt`，覆盖五档 profile 投影成功、显式键承载与非法 daemon key 拒绝。
4. 更新 `tests/unit/profiles/ProfileMatrixConsistencyTest.cpp`，将 DMD-BLK-001 转成自动化断言：所有 baseline profile 必须包含 daemon 关键键，且 projection helper 不得依赖隐式默认值。

## 4. Validation

1. `cmake -S . -B build-ci -G Ninja`
2. `cmake --build build-ci --target dasall-daemon_profile_projection_unit_test dasall_profile_matrix_consistency_unit_test`
3. `ctest --test-dir build-ci -R "^(DaemonProfileProjectionTest|ProfileMatrixConsistencyTest)$" --output-on-failure`

结果摘要：

1. `DaemonProfileProjectionTest` 通过，helper 能加载五档 baseline profile，并对非法 daemon key 值返回 `SchemaInvalid`。
2. `ProfileMatrixConsistencyTest` 通过，五档 profile 的 `daemon.*` 资产和 build/runtime matrix 现在可同时验证。
3. DMD-BLK-001 已从“profile 资产缺失”转为已验证闭合状态，后续 DMD-TODO-027 可以直接复用这组 profile 基线证据。

## 5. 完成判定

DMD-TODO-003 已完成。判定依据：

1. daemon 配置不再依赖 `apps/daemon/main.cpp` 常量，profile 侧已经有显式配置键和稳定投影 helper。
2. 五档 profile 都已具备 daemon 关键键，且 focused tests 证明不需要隐式默认值才能通过 baseline 验证。
3. `profiles` 与 `apps/daemon` 的依赖方向保持正确，未引入反向模块耦合。