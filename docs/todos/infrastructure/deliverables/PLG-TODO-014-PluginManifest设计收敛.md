# PLG-TODO-014 PluginManifest 设计收敛

日期：2026-04-07  
任务：PLG-TODO-014  
状态：D Gate PASS / Build PASS

## 1. 本地证据

1. docs/architecture/DASALL_infra_plugin模块详细设计.md 6.5.1 已冻结 PluginManifest v1.0 的字段集合、`required_abi` 编码规则与扩展命名空间。
2. docs/todos/infrastructure/DASALL_infrastructure_plugin组件专项TODO.md 中 PLG-TODO-014 已由 INF-BLK-09 shared blocker recovery 恢复为可执行任务，且本轮目标限定为“对象头文件 + unit/contract 守卫”。
3. 现有 PluginDescriptor.h、PluginCatalog.h 与 OTATypes.h 已给出仓库当前的数据结构冻结风格：header-only、显式一致性检查入口、避免在对象轮次提前掺入 facade/manager 行为。
4. plugin 当前 public boundary 尚未把 manifest 接入 manager/pipeline，因此本轮必须避免把 014 扩张为 parser、registry 或 validation pipeline 改签名任务。

## 2. 外部参考

1. Semantic Versioning 2.0.0 要求公开版本规则明确、已发布版本不可原地改写，并用 `MAJOR.MINOR.PATCH` 表达兼容性含义。本轮据此把 `schema_version` 与 `version` 都冻结为可校验的 SemVer 语义，而不是任意字符串。

## 3. Design 结论

1. PluginManifest 采用 header-only 数据结构，不在本轮引入 manifest parser、registry 或 manager 接线，保持 014 的原子粒度。
2. `schema_version` 首版固定为 `1.0.0`，`version` 采用 SemVer；`required_abi` 冻结为 `<platform_tag>@<semver>` 编码，供后续 016 直接消费。
3. `capabilities` 冻结为唯一且非空的小写点分 token 集合；`signature_ref` 必须非空；`entry` 必须非空且不允许空白字符。
4. 可选扩展字段冻结为 `PluginManifestExtension` 列表，key 必须使用 `x.<owner>.<name>` 命名空间，并显式拒绝 `infra`、`contracts`、`profile`、`runtime`、`tool`、`skill`、`plugin` 等保留 owner，阻断 contracts 或运行时语义越权。
5. 对象提供 `uses_unknown_defaults()`、`is_schema_frozen_v1()`、`has_valid_capabilities()`、`has_valid_extensions()` 与 `is_valid()` 五个二值出口，供 unit/contract 与后续 verifier/compatibility engine 直接复用。

## 4. Design -> Build 映射

| Design 项 | Build 落点 |
|---|---|
| 冻结 PluginManifest v1.0 字段集合 | infra/include/plugin/PluginManifest.h |
| 冻结 `required_abi` 编码与 SemVer 校验 | `is_plugin_manifest_required_abi()` + `is_plugin_manifest_semver()` |
| 冻结扩展命名空间策略 | `PluginManifestExtension` + `uses_allowed_plugin_manifest_extension_namespace()` |
| 增加正负例判定出口 | PluginManifestSchemaTest |
| 阻断 contracts/tool/skill/runtime 语义越权 | PluginManifestBoundaryContractTest |

## 5. Build 三件套

1. 代码目标：新增 infra/include/plugin/PluginManifest.h，并在 infra/CMakeLists.txt 中注册 public header。
2. 测试目标：新增 tests/unit/infra/plugin/PluginManifestTest.cpp，覆盖默认 unknown、有效 schema、保留扩展命名空间与 `required_abi` 负例；新增 tests/contract/smoke/PluginManifestBoundaryContractTest.cpp，校验对象不吸收 request/trace/task/tool/skill 等外域语义。
3. 验收命令：
   - cmake -S . -B build-ci -G "Unix Makefiles"
   - cmake --build build-ci --target dasall_infra dasall_plugin_manifest_unit_test dasall_contract_plugin_manifest_boundary_test
   - ctest --test-dir build-ci -N -R "PluginManifestSchemaTest|PluginManifestBoundaryContractTest"
   - ctest --test-dir build-ci --output-on-failure -R "PluginManifestSchemaTest|PluginManifestBoundaryContractTest"

## 6. 验收结果

1. `dasall_infra`、`dasall_plugin_manifest_unit_test` 与 `dasall_contract_plugin_manifest_boundary_test` 全部构建通过。
2. `PluginManifestSchemaTest` 与 `PluginManifestBoundaryContractTest` 均已进入 CTest 图。
3. 两个测试 2/2 通过。

## 7. 风险与回退

1. 本轮不把 PluginManifest 接入 IPluginManager / PluginValidationPipeline public signature，避免在 014 中过早触发 breaking review gate。
2. 扩展字段当前只冻结 key/value 列表与命名空间规则，不引入 parser/serialization 细节；若后续需要 JSON/YAML 编解码，应另起原子任务完成。
3. `required_abi` 只冻结编码格式，不在本轮提前实现 strict/non-strict 判定；这属于 016 的职责。