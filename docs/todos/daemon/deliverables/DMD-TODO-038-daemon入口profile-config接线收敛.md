# DMD-TODO-038 daemon 入口 profile/config 接线收敛

状态：Done
日期：2026-05-02
来源 TODO：docs/todos/daemon/DASALL-daemon本地控制面专项TODO.md

## 1. 任务边界

1. 本任务只收敛 daemon binary entry 的 profile/config snapshot 真接线，不提前实现 SIGHUP fresh snapshot reload；后者继续留给 DMD-TODO-039。
2. 本任务复用现有 `DaemonProfileProjection`、`RuntimePolicyProvider` 与 `DaemonConfigValidator`，不在 `apps/daemon` 私造 profile 表或绕开四层配置来源。
3. 本任务只开放最小入口参数面：`--profile-id` 选择 profile、`--config-file` 加载 deployment snapshot、`--socket-path` 作为显式 override；不把所有 daemon 键摊平成 flags。

## 2. 根因与设计结论

### 2.1 回归根因

1. `apps/daemon/src/main.cpp` 之前直接把 `daemon_profile_id` / `effective_profile_id` 写死为 `daemon.direct_bind.v1`，而真实仓库 profile 资产是 `desktop_full`、`edge_balanced` 等五档 baseline。
2. ping/readiness 相关的 `daemon_listener_ready`、`daemon_gateway_ready`、`daemon_bridge_reachable` 在 `main.cpp` 中直接写死为 `true`，入口没有任何 profile/config 归一化结果可用。
3. deployment snapshot 只停留在 `docs/deploy/daemon/daemon.example.yaml/json` 样例，真实 daemon binary entry 既不读 profile snapshot，也不读 deployment snapshot，因此 helper/harness 级能力没有进入真实入口组合根。

### 2.2 本轮收敛结论

1. daemon entry 需要一个单一 loader，把 defaults、profile projection、runtime snapshot generation、deployment snapshot 与 CLI override 合成为一个结构化入口结果。
2. `main.cpp` 只消费该结构化结果，不再手写 `daemon_profile_id`、`effective_profile_id` 和 readiness bool 常量。
3. deployment snapshot 入口只接受 `--config-file` 指向的受控 YAML/JSON 文件，并保持 `flags vs config file` 冲突拒绝语义，继续符合 6.10.3 的 validator 约束。

## 3. Design -> Build 映射

| 设计结论 | Build 落点 | 验收信号 |
|---|---|---|
| binary entry 需要统一 profile/config loader | `apps/daemon/src/DaemonEntryConfigLoader.{h,cpp}` | `DaemonEntryConfigProjectionTest` 断言默认 profile、yaml/json deployment snapshot 与冲突语义 |
| `main.cpp` 不再写死 profile/readiness 常量 | `apps/daemon/src/main.cpp` | `daemon_profile_id` / `effective_profile_id` 由 loader 输出驱动，readiness bool 由入口状态推导 |
| profile helper 能力必须进入 entry-level compatibility smoke | `tests/integration/access/DaemonProfileCompatibilityTest.cpp` | integration 改为通过 `DaemonEntryConfigLoader` 装配 baseline profiles |
| 部署文档要与新入口 surface 对齐 | `docs/deploy/daemon/README.md`、`ACCEPTANCE_CHECKLIST.md` | 文档不再保留“当前二进制不读 YAML/JSON”的旧口径 |

## 4. 落盘结果

1. 新增 `apps/daemon/src/DaemonEntryConfigLoader.h/.cpp`：
   - 默认请求 profile 固定为 `desktop_full`；
   - 通过 `ProfileCatalog + DaemonProfileProjection` 读取 daemon profile 键；
   - 通过 `RuntimePolicyProvider` 读取 runtime snapshot，并把 `effective_profile_id + generation` 投影为 `config_revision`；
   - 受控解析 `--config-file` 指向的 YAML/JSON deployment snapshot；
   - 在 `--socket-path` 与 config file 的 `daemon.socket_path` 冲突时记录 `DaemonConfigConflict`。
2. 更新 `apps/daemon/src/main.cpp`：
   - 入口参数新增 `--profile-id` 与 `--config-file`；
   - `validate-only` 与正常启动路径都先走 `DaemonEntryConfigLoader`；
   - `DaemonBootstrap::build()` 现在接收真实 `effective_profile_id` 与 `config_revision`；
   - ping/readiness 所需的 profile/readiness 元数据不再由 `main.cpp` 手写常量。
3. 更新 `apps/daemon/CMakeLists.txt`，把 `DaemonEntryConfigLoader.cpp` 编进 `dasall-daemon`，并补齐 `dasall_profiles` 依赖。
4. 新增 `tests/unit/apps/daemon/DaemonEntryConfigProjectionTest.cpp`：
   - 验证默认 `desktop_full` baseline；
   - 验证 YAML deployment snapshot overlay；
   - 验证 JSON deployment snapshot 与 `--socket-path` 冲突被结构化记录。
5. 更新 `tests/integration/access/DaemonProfileCompatibilityTest.cpp` 与对应 CMake：
   - 不再直接消费 `DaemonProfileProjection`；
   - 改为通过 `DaemonEntryConfigLoader` 装配 baseline profiles，证明 helper 能力已进入 entry-level assembly path。
6. 更新 `docs/deploy/daemon/README.md` 与 `ACCEPTANCE_CHECKLIST.md`，同步新的 `--profile-id` / `--config-file` surface 与 flags/config file 冲突规则。

## 5. Validation

1. `cmake --build build-ci --target dasall-daemon dasall-daemon_entry_config_projection_unit_test dasall-daemon_profile_projection_unit_test dasall_access_daemon_profile_compatibility_integration_test`
2. `ctest --test-dir build-ci -R "DaemonEntryConfigProjectionTest|DaemonProfileProjectionTest|DaemonProfileCompatibilityTest|DaemonBootstrapTest" --output-on-failure`

结果摘要：

1. `DaemonEntryConfigProjectionTest` 通过，证明 daemon entry loader 可以：
   - 默认选择 `desktop_full`；
   - 消费 YAML/JSON deployment snapshot；
   - 记录 `--socket-path` 与 config file 的冲突。
2. `DaemonProfileProjectionTest` 继续通过，说明原有 profile projection 仍保持稳定，并被 entry loader 复用而非替换。
3. `DaemonProfileCompatibilityTest` 通过，说明 baseline profiles 现在可以通过 entry loader 进入 daemon compatibility smoke，而不再停留在 helper 级 direct call。
4. `DaemonBootstrapTest` 继续通过，说明入口 loader 接线没有回归 `DaemonBootstrap::build()` 的 process context 语义。

## 6. 完成判定

DMD-TODO-038 已完成。判定依据：

1. daemon binary entry 不再依赖 `main.cpp` 中写死的 `daemon_profile_id` / `effective_profile_id` 常量。
2. profile projection 与 runtime snapshot generation 已通过真实 `main/bootstrap` 路径进入 process context。
3. deployment snapshot 已拥有真实 binary entry surface（`--config-file`），并继续遵守 flags/config file 冲突拒绝规则。
4. 038 已为 039 提供单一的 initial snapshot source，后续 SIGHUP fresh snapshot reload 只需复用同一路径，不再重新发明入口配置解析链。