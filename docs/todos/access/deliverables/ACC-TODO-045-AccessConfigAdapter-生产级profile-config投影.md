# ACC-TODO-045 设计收敛文档

## 1. 任务定义

实现 `AccessConfigAdapter` 的生产级 profile/config 投影：把启动期 `AccessBootstrapConfig` 与运行期 `RuntimePolicySnapshot` 收敛为 Access 主链可消费的 immutable `AccessAuthView`、`AccessAdmissionView`、`AccessPublishView` 与 `AccessRuntimeGovernanceView`，并提供 fingerprint、last-known-good、hot update invalidation 与非法 schema fail-closed 语义。

## 2. 本地证据

1. 专项 TODO 将 `ACC-TODO-045` 定义为 P1-1 / R3 主链任务，要求 bootstrap config 与 profile/policy snapshot 统一投影为 immutable governance views，并明确 `fingerprint`、`last-known-good`、`hot update invalidation` 与 `非法 schema fail init` 是交付必选项。
2. `docs/architecture/DASALL_access子系统详细设计.md` 的 `AccessConfigAdapter` 章节明确要求：投影视图只消费 typed bootstrap carrier 与 `RuntimePolicySnapshot`，fingerprint 以 `bootstrap_revision + effective_profile_id + runtime_policy_generation` 为主锚，热更新只影响下一次请求，任何缺省都必须偏向收紧。
3. `access/src/AccessConfigAdapter.h/.cpp` 现在提供 `project()`、`last_known_good_projection()`、`snapshot_fingerprint()`、`is_snapshot_current()` 与内部 `project_uncached()`，并在投影阶段统一做 bootstrap/runtime snapshot 一致性检查、收紧型默认值与缓存失效。
4. `access/include/AccessGatewayFactory.h` 与 `access/src/AccessGatewayFactory.cpp` 为 daemon/gateway submit pipeline 增加 `derive_views_from_runtime_policy` 与 `runtime_policy_snapshot` seam；当投影失败时，pipeline 构建直接 fail-closed，不让不一致配置进入 Ready。
5. `apps/daemon/src/main.cpp` 与 `apps/gateway/src/main.cpp` 的生产组合根都显式设置了 `derive_views_from_runtime_policy = true` 并传入 runtime snapshot；`tests/integration/access/CMakeLists.txt` 将 `DaemonProfileCompatibilityTest` 以 `AccessProfileCompatibilityTest` 别名纳入 `gate-int-08`，所以 045 的验收已经覆盖真实生产 caller。

## 3. 外部参考

1. Envoy runtime layered configuration 强调运行时配置以分层 snapshot 生效，并把更新影响限制在后续请求，而不是让进行中的请求看到半新半旧状态：https://www.envoyproxy.io/docs/envoy/latest/configuration/operations/runtime
2. OWASP Authorization Cheat Sheet 将 deny by default 作为基础原则；对本任务的对应要求是 runtime/profile schema 缺失或不一致时必须 fail-closed，而不是回退到放宽入口治理的散落默认值：https://cheatsheetseries.owasp.org/cheatsheets/Authorization_Cheat_Sheet.html

## 4. 边界与职责

### 4.1 边界

1. 本任务只收敛 Access 内部的配置/治理投影，不新增第二套 policy/config source，也不让 Access 直接解析 profile 原始 YAML。
2. 本任务不改变 Runtime、Recovery、Context 三条 ADR owner 边界；`AccessConfigAdapter` 只消费 typed snapshot，不接管 runtime owner。
3. 本任务不把 Access 内部治理视图提升为 shared contracts，也不在投影器内部做授权裁定或 transport 绑定决策。

### 4.2 职责

| 对象 | 职责 | 非职责 |
|---|---|---|
| `AccessConfigAdapter` | 将 bootstrap + runtime snapshot 投影为 immutable access views，并维护 fingerprint / cache / last-known-good | 不解析原始 profile 文件，不拥有第二套策略系统 |
| `AccessGatewayFactory` projection seam | 在 daemon/gateway submit pipeline 构建前统一拉起投影，并在失败时 fail-closed | 不替代 app main 的 snapshot 装配来源 |
| daemon/gateway app roots | 提供 entry-specific bootstrap facts 与 `RuntimePolicySnapshot` 指针 | 不在 app root 中复制投影规则 |

## 5. 数据与接口说明

1. `access/src/AccessConfigAdapter.h`
   - `AccessConfigProjection`：聚合 `fingerprint`、`auth_view`、`admission_view`、`publish_view`、`runtime_governance_view`。
   - `AccessConfigProjectionResult`：统一封装 `projection` 与 `error`，确保 fail-closed 结果可被 factory 直接消费。
   - `AccessConfigAdapter`：公开 `project()`、`last_known_good_projection()`、`snapshot_fingerprint()`、`is_snapshot_current()`，内部保留 cache / last-known-good。
2. `access/include/AccessGatewayFactory.h`
   - `DaemonAccessPipelineOptions` / `GatewayAccessPipelineOptions` 新增 `derive_views_from_runtime_policy` 与 `runtime_policy_snapshot`，用来声明是否从 runtime snapshot 派生治理视图。
3. `access/src/AccessGatewayFactory.cpp`
   - `project_access_views_from_runtime_policy()` 统一负责 fingerprint 投影与 fail-closed；daemon/gateway submit pipeline 若 projection 失败则直接返回空 pipeline。
4. `tests/unit/access/AccessConfigAdapterProjectionTest.cpp`
   - 验证 tightened projection、fingerprint 与 hot update invalidation。
5. `tests/unit/access/AccessConfigAdapterInvalidSchemaTest.cpp`
   - 验证 invalid bootstrap / invalid runtime snapshot 不污染 last-known-good，且 `create_daemon_access_gateway()->init()` 会 fail-closed。

## 6. 流程与时序

1. app root 先通过 deployment/profile loader 获得 entry-specific bootstrap facts 与 `RuntimePolicySnapshot`。
2. daemon/gateway main 把 `derive_views_from_runtime_policy = true` 与 snapshot 指针写入 pipeline options。
3. `AccessGatewayFactory` 在构建 submit pipeline 前调用 `project_access_views_from_runtime_policy()`。
4. `AccessConfigAdapter::project()` 先做 schema consistency 校验，再计算 fingerprint，命中缓存则返回 immutable snapshot，未命中则走 `project_uncached()` 生成 tightened views 并更新 last-known-good。
5. pipeline 成功构建后，`RequestValidator`、`AdmissionController`、`RequestNormalizer` 等组件在同一请求生命周期内共享同一版治理视图。
6. 当 runtime generation / profile id / bootstrap revision 发生变化时，fingerprint 改变，下一次请求或下一次初始化会拿到新视图；若 bootstrap/snapshot 非法，则 pipeline 构建失败，gateway init fail-closed。

## 7. Design -> Build 映射

| 设计项 | Build 落点 | 完成判定 |
|---|---|---|
| immutable 投影视图与 fingerprint | `access/src/AccessConfigAdapter.h/.cpp` | `project()` 输出 `AccessConfigProjection`，fingerprint 可复算并用于 currentness 判断 |
| last-known-good 与 hot update invalidation | `access/src/AccessConfigAdapter.cpp`、`tests/unit/access/AccessConfigAdapterProjectionTest.cpp` | generation/profile/revision 变更后缓存失效，旧 LKG 不被非法输入污染 |
| 生产工厂 fail-closed seam | `access/include/AccessGatewayFactory.h`、`access/src/AccessGatewayFactory.cpp` | daemon/gateway submit pipeline 构建前统一投影，失败直接返回空 pipeline |
| 生产 caller 接线 | `apps/daemon/src/main.cpp`、`apps/gateway/src/main.cpp` | app roots 显式传入 `derive_views_from_runtime_policy` 与 `runtime_policy_snapshot` |
| profile compatibility 与非法 schema init 证据 | `tests/integration/access/DaemonProfileCompatibilityTest.cpp`、`tests/unit/access/AccessConfigAdapterInvalidSchemaTest.cpp` | baseline profile 兼容通过，invalid schema 不进入 Ready |

## 8. 文件范围

1. `access/include/AccessGatewayFactory.h`
2. `access/src/AccessConfigAdapter.h`
3. `access/src/AccessConfigAdapter.cpp`
4. `access/src/AccessGatewayFactory.cpp`
5. `tests/unit/access/AccessConfigAdapterProjectionTest.cpp`
6. `tests/unit/access/AccessConfigAdapterInvalidSchemaTest.cpp`
7. `tests/unit/access/CMakeLists.txt`
8. `tests/integration/access/DaemonProfileCompatibilityTest.cpp`（只读复核验收覆盖）
9. `tests/integration/access/CMakeLists.txt`（只读复核 `AccessProfileCompatibilityTest` 别名）
10. `apps/daemon/src/main.cpp`（只读复核生产 caller）
11. `apps/gateway/src/main.cpp`（只读复核生产 caller）
12. `docs/todos/access/DASALL_access子系统专项TODO.md`
13. `docs/todos/access/deliverables/ACC-TODO-045-AccessConfigAdapter-生产级profile-config投影.md`
14. `docs/worklog/DASALL_开发执行记录.md`

## 9. Build 原子清单

| 原子项 | 代码目标 | 测试目标 | 验收命令 |
|---|---|---|---|
| B1 | 实现 `AccessConfigAdapter` 的 immutable projection、fingerprint 与 cache/LKG | `AccessConfigAdapterProjectionTest` | `ctest --test-dir build/vscode-linux-ninja -R "AccessConfigAdapterProjectionTest" --output-on-failure` |
| B2 | 将 daemon/gateway submit pipeline 接入 runtime policy projection seam | `AccessProfileCompatibilityTest` | `ctest --test-dir build/vscode-linux-ninja -R "AccessProfileCompatibilityTest" --output-on-failure` |
| B3 | 对 invalid bootstrap / invalid snapshot 增加 fail-closed 与 init 失败断言 | `AccessConfigAdapterInvalidSchemaTest` | `ctest --test-dir build/vscode-linux-ninja -R "AccessConfigAdapterInvalidSchemaTest" --output-on-failure` |

## 10. 验收结果

1. `Build_CMakeTools()`
   - 结果：通过。
   - 说明：增量构建成功重编 `dasall_access_config_adapter_invalid_schema_unit_test` 及相关 access 目标。
2. `RunCtest_CMakeTools(tests=["AccessConfigAdapterProjectionTest","AccessConfigAdapterInvalidSchemaTest","AccessProfileCompatibilityTest"])`
   - 结果：通过，3/3 passed。
   - 明细：`AccessConfigAdapterProjectionTest`、`AccessConfigAdapterInvalidSchemaTest`、`AccessProfileCompatibilityTest` 全部通过。
3. 生产 caller 复核
   - `apps/daemon/src/main.cpp` 与 `apps/gateway/src/main.cpp` 均已显式设置 `derive_views_from_runtime_policy = true` 并传入 `runtime_policy_snapshot`。
   - `AccessProfileCompatibilityTest` 通过 `DaemonProfileCompatibilityTest` 别名执行，覆盖五档 baseline profile 的 daemon unary 主链。

## 11. D Gate 结果

Gate = PASS。

1. bootstrap config 与 runtime policy snapshot 现在由同一个 `AccessConfigAdapter` 统一投影为 immutable access views，不再依赖散落默认值和局部拼装。
2. fingerprint、last-known-good 与 hot update invalidation 已被 focused unit tests 锁定；profile 差异会影响下一次请求，不回溯污染进行中请求。
3. 非法 bootstrap 或 runtime snapshot 会让 production gateway init fail-closed，不再让不一致配置进入 Ready。
4. 当前 `AccessProfileCompatibilityTest` 以 daemon baseline profiles 为主入口；gateway 生产 caller 已接入同一 projection seam，但尚无独立 gateway profile matrix。该点不阻断 045 完成，因为 daemon/gateway 在本轮共享同一个 factory projection path。