# CAP-TODO-007 IDataService 公共接口设计收敛

日期：2026-04-09  
任务：CAP-TODO-007  
状态：D Gate PASS

## 1. 本地证据

1. docs/architecture/DASALL_capability_services子系统详细设计.md 6.6 已给出 IDataService 的 V1 接口草图，固定为 `query` 与 `list_capabilities` 两个方法。
2. docs/architecture/DASALL_capability_services子系统详细设计.md 6.6 要求 IDataService 继续复用已冻结的 Data request/result 对象，不新增执行类或审批类参数。
3. docs/architecture/DASALL_capability_services子系统详细设计.md 6.6 的接口语义约束明确 `query` 必须保持只读语义，允许 cache、snapshot 或 stale-read 策略，但不得触发隐式写入。
4. docs/architecture/DASALL_capability_services子系统详细设计.md 6.6 的接口语义约束明确 `list_capabilities` 只提供 discoverability 目录查询，不承载健康裁定、执行授权或路由强制。
5. docs/architecture/DASALL_capability_services子系统详细设计.md 10.2 与 12.2 明确当前建议保持 IDataService 为 query-only，不让其承接幂等写操作或 system snapshot 等 internal-only 能力。

## 2. 外部参考

1. C++ Core Guidelines C.121、C.127 与 I.4 强调接口基类应保持纯抽象、具备公共虚析构，并通过强类型签名直接表达语义。本任务据此将 IDataService 固定为只包含两个纯虚方法的稳定接口头，分别绑定 DataQueryRequest/DataCatalogRequest 与对应结果对象，避免用弱类型入口把查询、目录发现与执行控制语义混杂在一起。

## 3. Design 结论

1. IDataService 作为 data 子域唯一公共接口头，只暴露 `query` 与 `list_capabilities` 两个纯虚方法，不携带任何数据成员。
2. `query` 只服务于数据读取语义，返回 `DataQueryResult`，明确承接 rows/caching 事实，但不承接业务写、审批或恢复裁定。
3. `list_capabilities` 只服务于 capability catalog discoverability，返回 `DataCatalogResult`，不扩张为执行授权或 adapter route 控制接口。
4. V1 不新增 `upsert`、`mutate`、`system_snapshot` 等额外顶层方法，继续把 internal snapshot 与 health probe 留在内部实现层。
5. 本轮只冻结头文件签名，不提前落 DataQueryLane、DataProjectionCache、ServiceFacade 或测试 mock 的实现细节，保持 007 的 L3 粒度边界。

## 4. Design -> Build 映射

| Design 项 | Build 落点 |
|---|---|
| 冻结 data 公共接口基类 | services/include/IDataService.h 中的 IDataService |
| 暴露只读数据查询入口 | services/include/IDataService.h 中的 query() |
| 暴露 capability discoverability 入口 | services/include/IDataService.h 中的 list_capabilities() |

## 5. Build 三件套

1. 代码目标：更新 services/include/IDataService.h，定义纯抽象接口与两个稳定方法签名。
2. 测试目标：保持 InterfaceCatalogContractTest 不回退，并补一条 IDataService.h 语法编译检查，显式定义最小 stub 实现以验证签名可覆写。
3. 验收命令：
   - cmake -S . -B build-ci -G "Unix Makefiles"
   - cmake --build build-ci --target dasall_services dasall_contract_tests
   - ctest --test-dir build-ci --output-on-failure -R "^InterfaceCatalogContractTest$"
   - printf '#include "IDataService.h"\nusing namespace dasall::services;\nstruct Demo final : IDataService {\n  DataQueryResult query(const DataQueryRequest&) override { return {}; }\n  DataCatalogResult list_capabilities(const DataCatalogRequest&) override { return {}; }\n};\nint main() { Demo demo{}; IDataService* service = &demo; return static_cast<int>(service == nullptr); }\n' | c++ -std=c++20 -Iservices/include -Icontracts/include -xc++ -fsyntax-only -

## 6. 风险与回退

1. 当前 data 公共接口仍严格保持 query-only；若后续出现写语义诉求，必须通过新的 action taxonomy / admission 评审，不应就地扩张 IDataService。
2. DataProjectionCache、DataQueryLane 与 mock 对齐属于后续实现/测试任务；本轮若提前引入具体类依赖，会把纯 ABI 冻结任务扩展成实现任务。