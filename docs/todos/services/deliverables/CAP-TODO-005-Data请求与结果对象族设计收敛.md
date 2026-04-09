# CAP-TODO-005 Data 请求与结果对象族设计收敛

日期：2026-04-09  
任务：CAP-TODO-005  
状态：D Gate PASS

## 1. 本地证据

1. docs/architecture/DASALL_capability_services子系统详细设计.md 6.5 已将 DataQueryRequest、DataCatalogRequest、DataQueryResult、DataCatalogResult 纳入 V1 公共 supporting object 清单。
2. docs/architecture/DASALL_capability_services子系统详细设计.md 6.5 约束 DataQueryRequest 只表达业务/状态数据查询，可带 projection/filter，但 V1 只定义读语义，不扩张到业务写操作。
3. docs/architecture/DASALL_capability_services子系统详细设计.md 6.5 要求 DataCatalogRequest 只做 capability discoverability，不承载运行控制；DataCatalogResult 只返回目录查询结果，不承载健康裁定。
4. docs/architecture/DASALL_capability_services子系统详细设计.md 6.6 代码草图已冻结四个 data 对象的字段清单，包括 `filters_json`、`rows_json`、`catalog_json` 的 SerializedJson 承载方式，以及 DataQueryResult 的 `from_cache` 字段。
5. docs/architecture/DASALL_capability_services子系统详细设计.md 6.6 接口语义约束明确 `query_state` 与 `query` 必须保持只读语义，允许缓存或 stale-read 策略，但不得触发隐式写入；这要求 Data 请求/结果对象在公共 ABI 层就保持 query-only 口径。

## 2. 外部参考

1. Protobuf Best Practices 强调不同职责的 API 消息应拆分独立类型，并避免用一个对象同时承载多类演进方向不同的语义。本任务据此把 data 子域拆成 query 与 catalog 两类 request/result 对象，保持 discoverability 与数据读取分离，同时继续用 SerializedJson 字符串承载结构化 payload，避免把内部数据模型直接冻结进公共 ABI。

## 3. Design 结论

1. DataQueryRequest 聚焦只读数据查询，最小字段集合为 context、dataset、filters_json、projection、freshness。
2. DataCatalogRequest 只保留 context 与 target_class，用于能力/数据目录 discoverability，不承载执行、审批或路由字段。
3. DataQueryResult 只输出 rows_json、from_cache 与可选错误对象，显式保留缓存命中事实，不掩盖 stale-read 语义。
4. DataCatalogResult 只输出 catalog_json 与可选错误对象，保持目录查询结果最小化，不扩展健康或控制语义。
5. Data 对象族统一复用 contracts::ResultCode 与 contracts::ErrorInfo，继续维持“services 只填值、不重定义 contracts 错误语义”的边界。

## 4. Design -> Build 映射

| Design 项 | Build 落点 |
|---|---|
| 冻结只读数据查询请求对象 | services/include/ServiceTypes.h 中的 DataQueryRequest |
| 冻结目录 discoverability 请求对象 | services/include/ServiceTypes.h 中的 DataCatalogRequest |
| 冻结只读数据查询结果对象 | services/include/ServiceTypes.h 中的 DataQueryResult |
| 冻结目录查询结果对象 | services/include/ServiceTypes.h 中的 DataCatalogResult |

## 5. Build 三件套

1. 代码目标：更新 services/include/ServiceTypes.h，新增四个 Data 请求/结果对象。
2. 测试目标：保持 InterfaceCatalogContractTest 不回退，并补一条 ServiceTypes.h 语法编译检查，显式实例化 data request/result 类型。
3. 验收命令：
   - cmake -S . -B build-ci -G "Unix Makefiles"
   - cmake --build build-ci --target dasall_services dasall_contract_tests
   - ctest --test-dir build-ci --output-on-failure -R "^InterfaceCatalogContractTest$"
   - printf '#include "ServiceTypes.h"\nusing namespace dasall::services;\nint main() { DataQueryRequest a{}; DataCatalogRequest b{}; DataQueryResult c{}; DataCatalogResult d{}; return static_cast<int>(a.dataset.size() + a.projection.size() + b.target_class.size() + c.from_cache + d.catalog_json.size()); }\n' | c++ -std=c++20 -Iservices/include -Icontracts/include -xc++ -fsyntax-only -

## 6. 风险与回退

1. IDataService 接口方法签名属于 CAP-TODO-007，本轮不得提前在接口头文件中落 query/list_capabilities 方法。
2. DataQueryLane、DataProjectionCache 与 stale-read 运行时策略属于后续组件任务；本轮只冻结公共对象字段，不在头文件中加入缓存/投影实现细节。