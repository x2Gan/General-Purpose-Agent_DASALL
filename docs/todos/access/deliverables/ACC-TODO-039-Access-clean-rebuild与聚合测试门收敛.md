# ACC-TODO-039 设计收敛文档

## 1. 任务定义

闭合 Access clean rebuild 与聚合测试门，使 `dasall_access_tests` 成为后续 Access Gate 的统一入口，并消除 `AccessGatewaySmokeIntegrationTest.cpp` 因 `PublishEnvelope` designated initializer 顺序导致的 clean rebuild 失败。

## 2. 本地证据

1. 专项 TODO 将 ACC-TODO-039 定义为 P0 R0 前置任务，要求新增 `dasall_access_tests`、修复 smoke clean compile、统一 `access` 标签，并以 `ctest -L access` 作为可重复证据入口。
2. 交付评审报告 P0-2 指出历史 `ctest` 通过依赖 stale binary，clean rebuild 在 `AccessGatewaySmokeIntegrationTest.cpp` 的 `PublishEnvelope` 初始化顺序处失败。
3. `tests/unit/access/CMakeLists.txt` 与 `tests/integration/access/CMakeLists.txt` 已为 Access 用例设置 `unit;access` / `integration;access` 标签，但顶层缺少只构建 Access 测试目标并运行 `ctest -L access` 的聚合 target。
4. `AccessTypes.h` 中 `PublishEnvelope` 的声明顺序为 `request_id/result_id/session_id/trace_id/channel_ref/protocol_kind/.../payload`，smoke test 旧写法先初始化 `payload` 再初始化 `protocol_kind`，不满足 C++20 designated initializer 顺序要求。

## 3. 外部参考

1. CMake `add_custom_target` 官方文档说明 custom target 可用于没有输出文件的构建入口，并可通过 `DEPENDS` 声明依赖目标：https://cmake.org/cmake/help/latest/command/add_custom_target.html
2. CMake `LABELS` 测试属性官方文档说明 label 可用于 `ctest -L` 过滤测试集合：https://cmake.org/cmake/help/latest/prop_test/LABELS.html
3. CMake `add_test` 官方文档说明由 `add_test(NAME ...)` 注册的测试会被 `ctest` 发现并按进程退出码判定成功或失败：https://cmake.org/cmake/help/latest/command/add_test.html

## 4. 边界与职责

### 4.1 边界

1. 本任务只收敛 Access 测试门禁入口和 clean rebuild 编译问题，不扩展 AccessGateway production pipeline。
2. 本任务不把 `AccessGatewaySmokeIntegrationTest` 改写为真实端到端主链；真实主链矩阵保留给 ACC-TODO-041、042、049。
3. 本任务不处理非 Access 测试目标缺可执行文件的历史 discoverability 噪声；039 的判定范围是 `access` label 集合。

### 4.2 职责

| 对象 | 职责 | 非职责 |
|---|---|---|
| `dasall_access_unit_tests` | 构建 Access unit test 可执行文件集合 | 不直接运行测试 |
| `dasall_access_integration_tests` | 构建 Access integration test 可执行文件集合 | 不扩大 integration 覆盖矩阵 |
| `dasall_access_tests` | 构建 Access unit/integration 子聚合并运行 `ctest -L access` | 不运行全仓非 Access 测试 |
| `AccessGatewaySmokeIntegrationTest` | 保持 smoke skeleton 可 clean compile、可发现、可运行 | 不宣称 Access v1 release ready |

## 5. 数据与接口说明

1. CMake target：
   - `dasall_access_unit_tests`：依赖 `${DASALL_ACCESS_UNIT_TEST_EXECUTABLE_TARGETS}`。
   - `dasall_access_integration_tests`：依赖 `${DASALL_ACCESS_INTEGRATION_TEST_EXECUTABLE_TARGETS}`。
   - `dasall_access_tests`：依赖两个子聚合目标，并执行 `${CMAKE_CTEST_COMMAND} --output-on-failure -L access`。
2. CTest label：
   - Access unit tests 继续使用 `unit;access`。
   - Access integration tests 继续使用 `integration;access`。
   - `ctest -L access` 是 Access 本轮统一测试集合入口。
3. `PublishEnvelope` 初始化：
   - smoke test 按 `AccessTypes.h` 声明顺序显式初始化字段，避免 C++20 designated initializer 顺序错误和缺字段 warning。

## 6. 流程与时序

1. 开发者执行 `cmake --build build-ci --target dasall_access_tests`。
2. Ninja/CMake 先重配置并构建 Access unit 与 integration 可执行文件。
3. 顶层 `dasall_access_tests` 在 build tree 中运行 `ctest -L access --output-on-failure`。
4. `ctest -N -L access` 可列出 62 个 Access 测试，作为 discoverability 证据。
5. 后续 040~049 只需继续给新增测试打 `access` label，即可自动进入统一 Gate。

## 7. Design -> Build 映射

| 设计项 | Build 落点 | 完成判定 |
|---|---|---|
| Access unit 子聚合目标 | `tests/unit/access/CMakeLists.txt` | `dasall_access_unit_tests` 可被顶层依赖 |
| Access integration 子聚合目标 | `tests/integration/access/CMakeLists.txt` | `dasall_access_integration_tests` 可被顶层依赖 |
| 顶层 Access Gate | `tests/CMakeLists.txt` | `dasall_access_tests` 可构建并运行 `ctest -L access` |
| smoke clean compile | `tests/integration/access/AccessGatewaySmokeIntegrationTest.cpp` | clean rebuild 不再触发 designated initializer 顺序错误 |

## 8. 文件范围

1. `tests/CMakeLists.txt`
2. `tests/unit/access/CMakeLists.txt`
3. `tests/integration/access/CMakeLists.txt`
4. `tests/integration/access/AccessGatewaySmokeIntegrationTest.cpp`
5. `docs/todos/access/DASALL_access子系统专项TODO.md`
6. `docs/todos/access/deliverables/ACC-TODO-039-Access-clean-rebuild与聚合测试门收敛.md`
7. `docs/worklog/DASALL_开发执行记录.md`

## 9. Build 原子清单

| 原子项 | 代码目标 | 测试目标 | 验收命令 |
|---|---|---|---|
| B1 | 新增 Access unit/integration 子聚合目标 | Access test 可执行文件可被统一构建 | `cmake --build build-ci --target dasall_access_tests` |
| B2 | 新增顶层 `dasall_access_tests` | `ctest -L access` 作为统一 Gate 运行 62 个 Access 测试 | `ctest --test-dir build-ci -L access --output-on-failure` |
| B3 | 修复 `PublishEnvelope` 初始化顺序 | `AccessGatewaySmokeIntegrationTest` clean compile + pass | `ctest --test-dir build-ci -R "AccessGatewaySmokeIntegrationTest" --output-on-failure` |
| B4 | 验证 Access discoverability | Access unit/integration 被 `ctest -N -L access` 发现 | `ctest --test-dir build-ci -N -L access` |

## 10. 验收结果

1. `cmake --build build-ci --target dasall_access_tests --clean-first`
   - 结果：通过；清理 837 个既有构建产物后重新构建 Access 测试目标并运行 `ctest -L access`，62/62 通过。
2. `ctest --test-dir build-ci -L access --output-on-failure`
   - 结果：通过；62/62 通过。
3. `ctest --test-dir build-ci -N`
   - 结果：命令退出码为 0，可发现全仓 691 个测试；其中 Access 测试位于 #405~#464、#690~#691。该命令仍会输出非 Access 历史测试缺可执行文件提示，不属于 039 范围。
4. `ctest --test-dir build-ci -N -L access`
   - 结果：通过；列出 62 个 Access tests。

## 11. D Gate 结果

Gate = PASS。

1. 设计边界已固定为 Access clean rebuild 与聚合测试入口。
2. Build 三件套已锁定并执行。
3. `ACC-BLK-009` 中 clean rebuild / stale binary blocker 的 Access 侧最小解阻条件已满足。
4. Access v1 release gate 仍不得标记 Ready；production pipeline、`AgentRequest` handoff 与端到端集成矩阵仍由 ACC-TODO-040、041、049 继续闭合。
