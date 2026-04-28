# DMD-TODO-010 daemon composition root include 边界收敛

状态：Done
日期：2026-04-28
来源 TODO：docs/todos/daemon/DASALL_daemon本地控制面专项TODO.md

## 1. 任务边界

1. 本任务只收敛 `apps/daemon` 组合根对 `access/` 的 include 边界，不提前展开 submit pipeline、RuntimeBridge、health router 或 gateway 入口的连带重构。
2. 本轮的判别点不是“daemon 是否还能 new 出 AccessGateway concrete”，而是“apps/daemon 是否还能通过 `access/src` 直接拿内部实现头”。
3. 只允许在 `access/include` 增加受控组合根接缝；不得把 `AccessGateway` concrete 头升级成 module public interface，也不得把 daemon 进一步扩张到 `cognition/`、`llm/`、`tools/` 实现依赖。

## 2. 研究与设计结论

### 2.1 本地证据

1. `apps/daemon/src/main.cpp` 当前直接 `#include "AccessGateway.h"`，并通过 `std::make_shared<dasall::access::AccessGateway>()` 构造 gateway concrete；`apps/daemon/CMakeLists.txt` 也显式把 `access/src` 加进 PRIVATE include dirs。
2. 蓝图 `docs/architecture/DASALL_Engineering_Blueprint.md` 第 3.2 节将 `access/include/` 定义为 Access module public interface，将 `access/src/` 定义为共享 access core 实现；同文第 4.3 节要求跨模块优先走冻结接口。
3. 编码规范 `docs/development/DASALL_工程协作与编码规范.md` 第 3.2、3.3 节明确要求对外接口放在模块 `include/` 下，跨模块依赖不得直接 include 其他模块实现细节。
4. `access/src/AccessGateway.cpp` 证明当前 daemon 只依赖 `AccessGateway` 的默认构造 + `init()` 生命周期入口，没有额外 concrete-only 前置；这使得“受控 public factory 替代 concrete include”成为最小改动方案。

### 2.2 外部参考

1. Mark Seemann 在 Composition Root 设计说明中强调：对象图组装应尽量靠近应用入口，但 library/framework 不应要求上层直接引用内部实现；对外应提供应用可消费的受控组合接缝，而不是让调用方跨层触达实现细节。
2. 这与 DASALL 蓝图的“apps 作为 entry-specific bootstrap，access 作为共享 access core owner”完全一致：daemon 可以保留 composition root 职责，但它消费的应该是 `access/include` 暴露的稳定入口，而不是 `access/src` 内部头。

### 2.3 设计结论

1. 采用“新增 `create_access_gateway()` public factory”方案，明确否决“继续允许 apps/daemon include `access/src/AccessGateway.h`”的保留现状路径。
2. `create_access_gateway()` 应放在 `access/include/`，返回 `std::shared_ptr<IAccessGateway>`，并以 public supporting types 承载可选的 submit/publish 回调，避免泄漏 concrete class 名称和内部头路径。
3. `apps/daemon/src/main.cpp` 改为只 include public factory + `IAccessGateway.h`，通过 factory 组装默认 gateway concrete；`apps/daemon/CMakeLists.txt` 移除 `access/src` include dir。
4. `AccessInterfaceSurfaceTest` 扩展 factory 可发现性断言，用 public surface 验证 factory 返回 `IAccessGateway`、默认构造可初始化并进入 ready。

## 3. Design -> Build 映射

| 设计结论 | Build 落点 | 验收信号 |
|---|---|---|
| access 对 apps 暴露受控组合接缝 | `access/include/AccessGatewayFactory.h`、`access/src/AccessGatewayFactory.cpp`、`access/CMakeLists.txt` | public header 可被 `dasall_access` 导出，`AccessInterfaceSurfaceTest` 可直接 include |
| daemon 不再 include access internal header | `apps/daemon/src/main.cpp`、`apps/daemon/CMakeLists.txt` | `dasall_daemon` 在不依赖 `access/src` include dir 的情况下编译通过 |
| factory 属于 public surface 而非 concrete 泄漏 | `tests/unit/access/AccessInterfaceSurfaceTest.cpp` | 测试通过 public header 创建 gateway 并验证生命周期入口 |

## 4. D Gate

结论：PASS。

依据：

1. 本轮已经形成单一、可执行的 Build 方案：新增 Access public factory，迁移 daemon consumer，补 public surface test。
2. Build 三件套已锁定：
   - 代码目标：`access/include` + `access/src` 新增 factory，移除 daemon 对 `access/src` 的 include 依赖；
   - 测试目标：扩展 `AccessInterfaceSurfaceTest`；
   - 验收命令：构建 `dasall_daemon` 与运行 `AccessInterfaceSurfaceTest`。
3. 范围保持在 DMD-TODO-010 内，没有顺手扩张到 daemon pipeline、gateway main、runtime bridge 或其它相邻原子任务。

## 5. 落盘结果

1. 新增 `access/include/AccessGatewayFactory.h`：
   - 定义 `AccessGatewayFactoryOptions`；
   - 暴露 `create_access_gateway()` public seam，返回 `std::shared_ptr<IAccessGateway>`。
2. 新增 `access/src/AccessGatewayFactory.cpp`：
   - 在模块内部桥接到 `AccessGateway` concrete；
   - 保持 apps 侧不感知 `access/src/AccessGateway.h`。
3. 更新 `access/CMakeLists.txt`：
   - 将 factory public header 纳入 `DASALL_ACCESS_PUBLIC_HEADERS`；
   - 将 `AccessGatewayFactory.cpp` 接入 `dasall_access` 构建图。
4. 更新 `apps/daemon/src/main.cpp`：
   - 移除 `AccessGateway.h` include；
   - 切换到 `create_access_gateway()` 组装默认 gateway。
5. 更新 `apps/daemon/CMakeLists.txt`：
   - 删除 `access/src` PRIVATE include dir，完成 daemon 侧跨模块 include 收口。
6. 更新 `tests/unit/access/AccessInterfaceSurfaceTest.cpp`：
   - 补 factory 可发现性断言；
   - 通过 public surface 验证 factory-created gateway 的生命周期初态。
7. 回写 `docs/todos/daemon/DASALL_daemon本地控制面专项TODO.md`：
   - DMD-TODO-010 改为 Done；
   - DMD-BLK-003 标记清除。

## 6. Validation

1. `Build_CMakeTools(buildTargets=["dasall_access_interface_surface_unit_test"])`
   - 结果：通过。
2. `RunCtest_CMakeTools(tests=["AccessInterfaceSurfaceTest"])`
   - 结果：通过，1/1 passed；CTest 仍打印仓库既有 `DartConfiguration.tcl` 缺失提示，但返回码为 0。
3. `Build_CMakeTools(buildTargets=["dasall_daemon"])`
   - 结果：通过，证明 daemon 在不依赖 `access/src` include dir 的情况下仍可构建。

## 7. 完成判定

DMD-TODO-010 已完成。判定依据：

1. `apps/daemon` 已不再 include 或依赖 `access/src/AccessGateway.h`。
2. `access/include` 已提供稳定的 `create_access_gateway()` public seam，daemon 仅消费 `IAccessGateway` + public factory。
3. `AccessInterfaceSurfaceTest` 与 `dasall_daemon` 构建同时通过，证明 include 边界收敛没有破坏默认生命周期入口。