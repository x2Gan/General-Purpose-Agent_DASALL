# PLG-TODO-007 PluginErrorCode 设计收敛

日期：2026-04-01  
任务：PLG-TODO-007  
状态：D Gate PASS

## 1. 本地证据

1. docs/todos/infrastructure/DASALL_infrastructure_plugin组件专项TODO.md 的 PLG-TODO-007 要求冻结“六个 INF_E_PLUGIN_* 错误码”，但原始锚点只明确列出 validate/load 两个名字，其余四个名字未在对象级文档中显式收敛。
2. docs/architecture/DASALL_infra_plugin模块详细设计.md 6.6 明确 IPluginManager 对外错误语义至少包含 INF_E_PLUGIN_VALIDATE_FAIL 与 INF_E_PLUGIN_LOAD_FAIL。
3. docs/architecture/DASALL_infra_plugin模块详细设计.md 6.8 明确准入类失败包含策略拒绝、签名失败、兼容失败，运行类失败包含卸载失败；9.1 failure injection 又明确覆盖签名伪造、ABI 冲突、load 超时、卸载失败。
4. infra/include/audit/AuditErrors.h、infra/include/diagnostics/DiagnosticsErrors.h 已提供 infra 私有错误码的冻结模式：子域私有 enum + `*_error_code_name()` + `map_*_error_code()`，并限制映射只落在 contracts 已冻结 ResultCode 集合中。

## 2. 外部参考

1. Microsoft MSDN Magazine《Writing, Loading, and Accessing Plug-Ins》强调插件治理应把发现/校验/装载失败显式建模，而不是把不同阶段的失败混成单一不可追踪错误。本轮据此把 plugin 失败路径收敛为六个阶段明确、边界可测的私有错误码。

## 3. Blocker 修复与 Design 结论

阻塞结论：

1. 原始 TODO 声称“六个 INF_E_PLUGIN_*”已具备，但详细设计只显式列出 2 个名字；这会导致 007 无法满足“六个私有错误码均可追溯到详设”的完成判定，属于可在本轮内修复的 context blocker。

最小 blocker-fix：

1. 基于 6.6 的 validate/load 锚点与 6.8/9.1 的失败类别，把剩余四个名字冻结为 `INF_E_PLUGIN_POLICY_DENIED`、`INF_E_PLUGIN_SIGNATURE_FAIL`、`INF_E_PLUGIN_COMPATIBILITY_FAIL`、`INF_E_PLUGIN_UNLOAD_FAIL`。

设计结论：

1. PluginErrorCode 采用 plugin 私有 header-only 错误码定义，避免在对象冻结阶段扩张到具体实现或 facade 逻辑。
2. 六个冻结错误码固定为：`INF_E_PLUGIN_VALIDATE_FAIL`、`INF_E_PLUGIN_POLICY_DENIED`、`INF_E_PLUGIN_SIGNATURE_FAIL`、`INF_E_PLUGIN_COMPATIBILITY_FAIL`、`INF_E_PLUGIN_LOAD_FAIL`、`INF_E_PLUGIN_UNLOAD_FAIL`。
3. 映射规则只允许落入 contracts 已冻结的三级失败域：Validation、Policy、Runtime；不新增共享 ResultCode。
4. 由于 ABI/signature 细粒度规则仍受 INF-BLK-09 约束，`COMPATIBILITY_FAIL` 与 `SIGNATURE_FAIL` 当前只冻结一级映射，不提前扩张到更细粒度 contracts 错误语义。

## 4. Design -> Build 映射

| Design 项 | Build 落点 |
|---|---|
| 冻结六个 plugin 私有错误码 | infra/include/plugin/PluginErrorCode.h |
| 冻结错误码名到字符串的稳定映射 | plugin_error_code_name() |
| 冻结 plugin -> contracts 的一级映射 | map_plugin_error_code() |
| 验证名字与映射稳定性 | PluginErrorCodeTest |
| 阻断共享 ResultCode 越权扩张 | PluginErrorCodeBoundaryContractTest |

## 5. Build 三件套

1. 代码目标：新增 infra/include/plugin/PluginErrorCode.h，并在 infra/CMakeLists.txt 中注册 public header。
2. 测试目标：新增 tests/unit/infra/plugin/PluginErrorCodeTest.cpp，覆盖六个名字稳定性与 Validation/Policy/Runtime 映射；新增 tests/contract/smoke/PluginErrorCodeBoundaryContractTest.cpp，校验仅映射到 contracts 既有 ResultCode，且名字保持在 `INF_E_PLUGIN_*` 私有命名空间。
3. 验收命令：
   - cmake -S . -B build-ci -G Ninja
   - cmake --build build-ci --target dasall_infra dasall_plugin_error_code_unit_test dasall_contract_plugin_error_code_boundary_test
   - ctest --test-dir build-ci -N -R "PluginErrorCodeTest|PluginErrorCodeBoundaryContractTest"
   - ctest --test-dir build-ci --output-on-failure -R "PluginErrorCodeTest|PluginErrorCodeBoundaryContractTest"

## 6. 风险与回退

1. `SIGNATURE_FAIL` 与 `COMPATIBILITY_FAIL` 当前只冻结到一级 contracts 映射；若 INF-BLK-09 解阻后要求更细粒度分类，应以新增 reason 细节或报告对象承接，而不是替换现有码名。
2. 本轮不把 plugin 错误码并入 infra 顶层 `InfraErrorCode` 枚举，避免在 central enum 中引入过早耦合；后续如需 facade 聚合，可在不破坏现有 plugin 头文件的前提下增加桥接入口。
3. `POLICY_DENIED` 采用 `INF_E_PLUGIN_*` 前缀是本轮为满足六个 plugin 私有码一致命名所做的收敛；后续若与 `INF_E_POLICY_INVALID` 产生语义重叠，需走评审而不是隐式改名。