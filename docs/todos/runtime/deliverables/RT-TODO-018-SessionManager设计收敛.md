# RT-TODO-018 SessionManager 设计收敛

## 本地证据

1. `docs/architecture/DASALL_runtime子系统详细设计.md` 的 6.24.5 已把 `SessionManager` 固定为：加载/保存 Session、管理 waiting state 恢复锚点、构建 `ResumeSeed`，但不承担 checkpoint compatibility 或 recovery 裁定。
2. 018 的 TODO 行虽然标记 `RT-BLK-01`，但完成判定已经明确写出“fail-closed stub 下可测”；同时 blocker 解阻条件包含“完成 RT-TODO-003，或相邻模块提供 stub”，而 003 已完成。
3. `runtime/include/session/ISessionManager.h` 与 `SessionTypes.h` 已冻结 5 个 public 方法和 supporting types，说明 018 应只落地 module-local 控制器，不扩 public ABI。
4. `tests/unit/runtime/SessionTypeSurfaceTest.cpp` 当前 fake 已覆盖 load/prepare/persist/bind/resume seed 的最小行为，是 018 的直接实现蓝图。
5. 016 已固定 checkpoint compatibility 由 `CheckpointManager` 负责，因此 018 只能输出 `ResumeSeed`，不能越权生成 `ResumePlan`。

## 设计结论

1. `SessionManager` 作为 runtime 私有控制器落在：
   - `runtime/src/session/SessionManager.h`
   - `runtime/src/session/SessionManager.cpp`
2. 控制器内部只维护一个最小 in-memory session store：`std::optional<SessionSnapshot> stored_snapshot_`，并由 `mutable std::mutex session_mutex_` 保护，满足 6.14.2 的 L3 锁序约束。
3. `load_session(...)` 的固定语义为：
   - `session_id` / `request_id` 缺失直接拒绝
   - 若已有 snapshot，则要求 `checkpoint_ref` 与 active anchor 一致
   - 若无 snapshot 且 `allow_session_create=true`，返回新会话快照，但不宣称真实外部持久化已接线
4. `prepare_turn(...)` 的固定语义为：
   - `expected_checkpoint_ref` 必须与 session anchor 一致
   - `pending_interaction.active()` 只能出现在 waiting-state session 上
5. `persist_turn(...)` / `bind_checkpoint_ref(...)` 的固定语义为：
   - `persist_turn(...)` 只接受 non-idle terminal state
   - `bind_checkpoint_ref(...)` 负责更新 active checkpoint anchor 与 waiting interaction
6. `build_resume_seed(...)` 的固定语义为：
   - 只从 session snapshot 提取 runtime-owned 恢复事实
   - active checkpoint anchor 与 request.checkpoint_ref 不一致时拒绝
   - 不直接生成 `ResumePlan`
7. 018 不负责：
   - memory 真持久化 backend 接线
   - checkpoint compatibility/version gate
   - recovery admission 或 safe-mode 裁定

## 边界与职责表

| 对象 | 本轮职责 | 明确不负责 |
|---|---|---|
| `SessionManager::load_session` | 输出 session truth snapshot | 不宣称真端口 ready |
| `SessionManager::prepare_turn` | 校验 waiting state / checkpoint anchor | 不修改持久化存储 |
| `SessionManager::persist_turn` | 写回 turn terminal state 与 checkpoint anchor | 不构建 checkpoint |
| `SessionManager::build_resume_seed` | 构建最小恢复种子 | 不越权生成 `ResumePlan` |

## 文件落点

| 设计项 | 文件 |
|---|---|
| 私有类声明 | `runtime/src/session/SessionManager.h` |
| 私有类实现 | `runtime/src/session/SessionManager.cpp` |
| 行为测试 | `tests/unit/runtime/SessionManagerTest.cpp` |
| CMake 接线 | `runtime/CMakeLists.txt`、`tests/unit/runtime/CMakeLists.txt` |

## Design -> Build 映射

1. 代码目标：`runtime/src/session/SessionManager.h`、`runtime/src/session/SessionManager.cpp`
2. 测试目标：`tests/unit/runtime/SessionManagerTest.cpp`
3. 验收命令：`cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_runtime_session_manager_unit_test && ctest --test-dir build-ci -R "^SessionManagerTest$" --output-on-failure`

## D 原子项

| ID | 设计目标 | 完成判定 | 结果 |
|---|---|---|---|
| D1 | 锁定 in-memory session store 语义 | runtime-local 可验证，不冒充真端口 ready | PASS |
| D2 | 锁定 waiting state / checkpoint anchor 规则 | waiting interaction 与 anchor mismatch 可拒绝 | PASS |
| D3 | 锁定 resume seed 边界 | SessionManager 只产出 `ResumeSeed` | PASS |
| D4 | 锁定 Build 三件套 | 代码目标、测试目标、验收命令齐备 | PASS |

## D Gate

1. 设计交付物已落盘。
2. RT-BLK-01 对 true integration 仍成立，但不阻断 runtime-local stub 路径。
3. Build 三件套已锁定。
4. 范围未越出 018 控制器边界。

结论：D Gate = PASS，可进入 RT-TODO-018 的 Build 阶段。