# ACC-TODO-006 access include、CMake 与测试拓扑骨架收敛

日期：2026-04-23  
任务：ACC-TODO-006  
状态：Build PASS

## 1. 本地证据

1. [docs/architecture/DASALL_access子系统详细设计.md](/home/gangan/DASALL/docs/architecture/DASALL_access子系统详细设计.md) 的 8.1 已把 `access/include/AccessErrors.h`、`IAdmissionController.h` 以及 `tests/unit/access`、`tests/integration/access` 列为 Access 骨架阶段的标准落点，但当前仓库只存在 `AccessTypes.h`、`IAccessGateway.h`、`IAccessRuntimeBridge.h`、`IProtocolAdapter.h` 四个头文件。
2. [docs/architecture/DASALL_access子系统详细设计.md](/home/gangan/DASALL/docs/architecture/DASALL_access子系统详细设计.md) 的 9.1/9.2 明确要求 Access 至少有可被 `ctest -N` 发现的 unit / integration 入口；当前仓库尚无 `tests/unit/access`、`tests/integration/access`，因此 Access discoverability 为空洞状态。
3. [docs/todos/access/DASALL_access子系统专项TODO.md](/home/gangan/DASALL/docs/todos/access/DASALL_access子系统专项TODO.md) 已把 ACC-TODO-006 定义为“新增 access include、CMake 与测试拓扑骨架”，且完成判定直接绑定到 `tests/unit/access`、`tests/integration/access` 顶层接入和 `ctest -N` 可发现。

## 2. 设计结论

1. ACC-TODO-006 的职责只收敛到“文件拓扑、构建 discoverability、测试 discoverability”，不提前冻结 `AccessErrorCode`、`AccessGatewayState`、`IAdmissionController`、`dispatch/cancel` 等后续子任务语义。
2. `dasall_access` 必须显式维护 public header inventory，并在 CMake 层对 header 存在性做检查，避免后续任务在没有稳定文件锚点的情况下继续扩实现。
3. `tests/unit/access` 本轮只注册 `AccessInterfaceSurfaceTest`，用途是证明 public surface 可 include、可链接、可被 unit 门发现；它不是行为门，也不替代 ACC-TODO-031 的生命周期 / registry gate。
4. `tests/integration/access` 本轮只注册 `AccessGatewaySmokeIntegrationTest` 空骨架，职责是为 Access 主链留出独立 integration 入口，满足 SSOT 级 discoverability 要求；真正的 CLI/daemon unary 与 async receipt 集成链路仍由 ACC-TODO-034 等后续任务补齐。
5. `tests/unit/CMakeLists.txt` 与 `tests/integration/CMakeLists.txt` 必须把 Access 子目录和 Access test target list 纳入顶层聚合，否则即使局部目录存在，`dasall_unit_tests` / `dasall_integration_tests` 与 `ctest -N` 仍无法把 Access 当作正式测试拓扑的一部分。

## 3. 边界 / 职责

| 对象 | 边界与职责 | 本轮不做的事 |
|---|---|---|
| `access/include` | 冻结 public 头文件落点和所有权，保证后续接口任务在稳定文件上增量演进 | 不提前定义 `AccessErrorCode`、`AccessAdmissionResult`、`AccessGatewayState` 的具体字段与枚举值 |
| `access/CMakeLists.txt` | 显式导出 Access public headers，并保证 `dasall_access` 保持可独立编译 | 不引入新的业务实现源文件，不修改依赖方向 |
| `tests/unit/access` | 暴露 Access unit discoverability，占住 public surface 单测入口 | 不承担 lifecycle / registry / validator 等行为验证 |
| `tests/integration/access` | 暴露 Access integration discoverability，占住 smoke 级集成入口 | 不伪造 CLI/daemon/HTTP 真正端到端链路 |
| 顶层 tests 聚合 CMake | 把 Access test targets 纳入总测试拓扑，保证 `ctest -N` 可发现 | 不新增模块级专项 custom target 或额外 gate 逻辑 |

## 4. 数据 / 接口说明

1. Access public header inventory 在本轮收敛为六个文件：
   - `AccessErrors.h`
   - `AccessTypes.h`
   - `IAccessGateway.h`
   - `IAccessRuntimeBridge.h`
   - `IAdmissionController.h`
   - `IProtocolAdapter.h`
2. `AccessErrors.h`、`IAdmissionController.h` 本轮只作为 skeleton header 落盘，用于固定 include 拓扑；具体 ABI / type surface 分别留给 ACC-TODO-007、ACC-TODO-010。
3. unit skeleton target / test 约定：
   - target：`dasall_access_interface_surface_unit_test`
   - CTest：`AccessInterfaceSurfaceTest`
   - labels：`unit;access`
4. integration skeleton target / test 约定：
   - target：`dasall_access_gateway_smoke_integration_test`
   - CTest：`AccessGatewaySmokeIntegrationTest`
   - labels：`integration;access`
5. 顶层聚合变量约定：
   - `DASALL_ACCESS_UNIT_TEST_EXECUTABLE_TARGETS`
   - `DASALL_ACCESS_INTEGRATION_TEST_EXECUTABLE_TARGETS`

## 5. 流程 / 时序

1. 在 `access/include` 增补缺失 skeleton headers，并在 `access/CMakeLists.txt` 把 Access public header set 明确化。
2. 建立 `tests/unit/access` 与 `tests/integration/access` 子目录，分别注册最小 unit / integration skeleton。
3. 在 `tests/unit/CMakeLists.txt`、`tests/integration/CMakeLists.txt` 接入 Access 子目录和对应 target 聚合变量。
4. 触发 CMake 重新配置后，`dasall_access` 继续可编译；`ctest -N` 能列出 Access unit / integration 入口。

## 6. 文件范围

1. 代码 / 构建：
   - [access/CMakeLists.txt](/home/gangan/DASALL/access/CMakeLists.txt)
   - [access/include/AccessErrors.h](/home/gangan/DASALL/access/include/AccessErrors.h)
   - [access/include/IAdmissionController.h](/home/gangan/DASALL/access/include/IAdmissionController.h)
2. 单测拓扑：
   - [tests/unit/CMakeLists.txt](/home/gangan/DASALL/tests/unit/CMakeLists.txt)
   - [tests/unit/access/CMakeLists.txt](/home/gangan/DASALL/tests/unit/access/CMakeLists.txt)
   - [tests/unit/access/AccessInterfaceSurfaceTest.cpp](/home/gangan/DASALL/tests/unit/access/AccessInterfaceSurfaceTest.cpp)
3. 集成拓扑：
   - [tests/integration/CMakeLists.txt](/home/gangan/DASALL/tests/integration/CMakeLists.txt)
   - [tests/integration/access/CMakeLists.txt](/home/gangan/DASALL/tests/integration/access/CMakeLists.txt)
   - [tests/integration/access/AccessGatewaySmokeIntegrationTest.cpp](/home/gangan/DASALL/tests/integration/access/AccessGatewaySmokeIntegrationTest.cpp)
4. 追踪文档：
   - [docs/todos/access/DASALL_access子系统专项TODO.md](/home/gangan/DASALL/docs/todos/access/DASALL_access子系统专项TODO.md)
   - [docs/todos/access/deliverables/ACC-TODO-006-access-include-cmake-测试拓扑骨架收敛.md](/home/gangan/DASALL/docs/todos/access/deliverables/ACC-TODO-006-access-include-cmake-测试拓扑骨架收敛.md)

## 7. Design -> Build 映射

| Design 项 | Build 落点 |
|---|---|
| Access public include inventory | `access/CMakeLists.txt` + `access/include/*.h` skeleton |
| unit discoverability | `tests/unit/access/CMakeLists.txt` + `AccessInterfaceSurfaceTest.cpp` |
| integration discoverability | `tests/integration/access/CMakeLists.txt` + `AccessGatewaySmokeIntegrationTest.cpp` |
| 顶层聚合发现 | `tests/unit/CMakeLists.txt`、`tests/integration/CMakeLists.txt` |

## 8. Build 三件套

1. 代码目标：补齐 Access public header inventory 的骨架文件，并让 `dasall_access` 在 CMake 层显式导出 public headers。
2. 测试目标：新增 Access unit / integration skeleton test，让 `AccessInterfaceSurfaceTest` 和 `AccessGatewaySmokeIntegrationTest` 被总测试拓扑发现。
3. 验收命令：
   - `cmake --build build-ci --target dasall_access`
   - `cmake --build build-ci --target dasall_access_interface_surface_unit_test dasall_access_gateway_smoke_integration_test`
   - `ctest --test-dir build-ci -N`

## 9. 后续任务接口

1. ACC-TODO-007 在 `AccessErrors.h` 上补全 `AccessErrorCode` / `AccessError` 语义，不需要再调整 include 拓扑。
2. ACC-TODO-008 / 009 / 010 / 011 / 012 可以直接在本轮固定的 public headers 上继续收敛生命周期、supporting types、Admission 与 runtime bridge surface。
3. ACC-TODO-031 / 034 直接复用本轮接好的 `tests/unit/access`、`tests/integration/access` CMake 拓扑扩展行为门和集成门，无需再重复铺目录。

## 10. 验证结果

1. `cmake --build build-ci --target dasall_access` 已通过，说明 `access/CMakeLists.txt` 的 public header inventory 与库目标仍可独立构建。
2. `cmake --build build-ci --target dasall_access_interface_surface_unit_test dasall_access_gateway_smoke_integration_test` 已通过，说明新增 unit / integration skeleton 本身可编译可链接。
3. `ctest --test-dir build-ci -R "AccessInterfaceSurfaceTest|AccessGatewaySmokeIntegrationTest" --output-on-failure` 已通过，两条 Access skeleton 入口都可执行。
4. `ctest --test-dir build-ci -N` 已列出 `AccessInterfaceSurfaceTest` 与 `AccessGatewaySmokeIntegrationTest`。当前 `build-ci` 仍会对仓库中大量未构建的历史测试目标打印 `Could not find executable` 提示，但不影响本轮对 Access discoverability 的完成判定。
