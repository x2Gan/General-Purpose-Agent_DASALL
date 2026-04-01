# PLG-TODO-002 PluginCatalog 设计收敛

日期：2026-04-01  
任务：PLG-TODO-002  
状态：D Gate PASS

## 1. 本地证据

1. docs/architecture/DASALL_infra_plugin模块详细设计.md 6.5 明确 PluginCatalog 固定为 discovered_plugins[] 与 rejected_plugins[]，并要求“发现与拒绝原因必须可解释”。
2. docs/architecture/DASALL_infra_plugin模块详细设计.md 6.3/6.7 明确 PluginRegistry 扫描目录与 Manifest 后输出 PluginCatalog，且 catalog 只表达发现结果，不表达激活结论。
3. docs/todos/infrastructure/DASALL_infrastructure_plugin组件专项TODO.md 中 PLG-TODO-002 要求拒绝原因可追溯，并与 Observation evidence_ref 引用语义对齐。
4. docs/architecture/DASALL_infrastructure子系统详细设计.md 6.5、infra/include/policy/PolicyDecisionRef.h、infra/include/diagnostics/DiagnosticsTypes.h 已给出 evidence_ref 与 reason_code 的既有引用模式，可直接复用为 plugin catalog 的拒绝记录锚点。

## 2. 外部参考

1. Microsoft MSDN Magazine《Writing, Loading, and Accessing Plug-Ins》把插件管理器描述为“扫描目录、识别候选插件并把发现结果保存在列表中”的独立步骤。本任务据此把 PluginCatalog 收敛为 discovery result 对象，不提前混入 load handle、执行结果或 runtime 调度语义。

## 3. Design 结论

1. PluginCatalog 采用 header-only 数据结构，承载 discovered_plugins 与 rejected_plugins 两个结果集合。
2. rejected_plugins 使用 RejectedPluginRecord 私有结构，最小字段固定为 descriptor、reason_code、evidence_ref，满足“拒绝原因可解释”和“证据可追溯”两个约束。
3. Catalog 提供 empty()、has_traceable_rejections() 与 has_consistent_entries() 三个二值出口，用于分别判断空结果、拒绝证据完整性与全量结果一致性。
4. 一致性规则固定为：发现项必须 governance-ready、拒绝项必须带 reason_code/evidence_ref、同一 plugin_id 不允许在 discovered/rejected 两侧重复出现。

## 4. Design -> Build 映射

| Design 项 | Build 落点 |
|---|---|
| 冻结 PluginCatalog 两个结果集合 | infra/include/plugin/PluginCatalog.h |
| 冻结拒绝记录的最小可追溯字段 | RejectedPluginRecord |
| 冻结空 catalog / 全发现 / 全拒绝判定出口 | empty() + has_traceable_rejections() + has_consistent_entries() |
| 验证发现/拒绝集合一致性 | PluginCatalogTest |
| 阻断 Observation/ErrorInfo ownership 越权 | PluginCatalogBoundaryContractTest |

## 5. Build 三件套

1. 代码目标：新增 infra/include/plugin/PluginCatalog.h，并在 infra/CMakeLists.txt 中注册 public header。
2. 测试目标：新增 tests/unit/infra/plugin/PluginCatalogTest.cpp，覆盖空 catalog、全发现、全拒绝和不带 evidence_ref/重复 plugin_id 的负例；新增 tests/contract/smoke/PluginCatalogBoundaryContractTest.cpp，校验 rejected_plugins 只复用 reason_code/evidence_ref，不直接拥有 Observation/ErrorInfo。
3. 验收命令：
   - cmake -S . -B build-ci -G Ninja
   - cmake --build build-ci --target dasall_infra dasall_plugin_catalog_unit_test dasall_contract_plugin_catalog_boundary_test
   - ctest --test-dir build-ci -N -R "PluginCatalogTest|PluginCatalogBoundaryContractTest"
   - ctest --test-dir build-ci --output-on-failure -R "PluginCatalogTest|PluginCatalogBoundaryContractTest"

## 6. 风险与回退

1. rejected_plugins 当前仅冻结 reason_code 与 evidence_ref，不提前引入签名报告、兼容报告或 ErrorInfo 复合对象；这些 richer reports 由后续 Plugin*Report 任务承接。
2. 一致性规则当前只冻结 plugin_id 唯一性与 discovered/rejected 不重叠；若后续对 catalog 排序或 size limit 有额外要求，应以增量规则追加而非破坏现有字段结构。
3. 本轮不把 catalog 扩张为 active set、load result 或 policy decision 聚合器，避免越过 discovery object 的边界。