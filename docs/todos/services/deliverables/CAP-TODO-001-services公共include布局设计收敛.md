# CAP-TODO-001 services 公共 include 布局设计收敛

日期：2026-04-09  
任务：CAP-TODO-001  
状态：D Gate PASS

## 1. 本地证据

1. docs/architecture/DASALL_capability_services子系统详细设计.md 6.5 明确 V1 公共 supporting objects 必须统一落在 ServiceTypes.h，且当前阶段只允许依赖 STL 与既有冻结 contracts 类型。
2. docs/architecture/DASALL_capability_services子系统详细设计.md 6.6 明确 IExecutionService / IDataService 先作为 services 模块内公共接口落盘，不直接进入 contracts。
3. docs/architecture/DASALL_capability_services子系统详细设计.md 8.1 直接给出 services/include/ServiceTypes.h、services/include/IExecutionService.h、services/include/IDataService.h 的推荐目录布局，未要求额外模块前缀目录。
4. services/CMakeLists.txt 当前只有 src/placeholder.cpp 与 include 根路径声明，缺少可追踪的 public header 入口；CAP-TODO-001 的代码目标因此必须同时补头文件落盘与 CMake 公开头文件接线。
5. contracts/include/boundary/InterfaceCatalog.h 与 tests/contract/smoke/InterfaceCatalogContractTest.cpp 已冻结 IExecutionService / IDataService 的 owner/readiness，说明本轮只能建立模块内 public include 槽位，不能改写 admission 状态。

## 2. 外部参考

1. CMake 官方文档 target_sources FILE_SET HEADERS 说明：HEADERS file set 用于通过 include 机制消费的头文件；PUBLIC file set 会把 BASE_DIRS 追加到 target 的 include properties，并为 IDE/安装导出提供稳定入口。本任务据此在 dasall_services 上显式注册 public header file set，而不是只依赖 include 目录存在。

## 3. Design 结论

1. services/include 直接作为模块公共 include 根，首批只放 ServiceTypes.h、IExecutionService.h、IDataService.h 三个稳定入口文件，不新增 dasall/services 之类的重复嵌套层级。
2. CAP-TODO-001 只负责 ABI 槽位与 CMake 暴露，不提前冻结对象字段或接口方法签名；具体 public object/interface 内容分别留给 CAP-TODO-002~007。
3. 为了让 build graph 和后续评审都能直接感知 public header 面，dasall_services 通过 PUBLIC HEADERS file set 显式登记三份头文件，保持 include 根路径与详细设计 8.1 一致。
4. 现有 InterfaceCatalog contract gate 保持不变，本轮只验证 services 公共头文件落盘后不会导致 services admission readiness 回退或漂移。

## 4. Design -> Build 映射

| Design 项 | Build 落点 |
|---|---|
| 建立 services 公共 include 根 | services/include/ |
| 预留 ServiceTypes 公开头文件槽位 | services/include/ServiceTypes.h |
| 预留 execution/data 接口头文件槽位 | services/include/IExecutionService.h、services/include/IDataService.h |
| 建立 CMake 公开头文件入口 | services/CMakeLists.txt 的 PUBLIC HEADERS file set |

## 5. Build 三件套

1. 代码目标：新增 services/include/ServiceTypes.h、services/include/IExecutionService.h、services/include/IDataService.h，并更新 services/CMakeLists.txt。
2. 测试目标：复用 InterfaceCatalogContractTest，确认 IExecutionService / IDataService 的 catalog owner/readiness 不回退，同时为后续 ServiceHeaderLayout smoke 保留可 discover 的头文件面。
3. 验收命令：
   - cmake -S . -B build-ci -G "Unix Makefiles"
   - cmake --build build-ci --target dasall_services dasall_contract_tests
   - ctest --test-dir build-ci --output-on-failure -R InterfaceCatalogContractTest

## 6. 风险与回退

1. 本轮不能提前在头文件中写入对象字段或接口方法签名，否则会与 CAP-TODO-002~007 的粒度边界重叠；若评审认为头文件壳过薄，应在后续原子任务中补齐，而不是回到 001 扩张范围。
2. 若后续仓库统一改成 install/export 驱动的头文件导出口径，可在不改变 include 根布局的前提下扩展当前 file set；本轮不提前引入 install 规则。