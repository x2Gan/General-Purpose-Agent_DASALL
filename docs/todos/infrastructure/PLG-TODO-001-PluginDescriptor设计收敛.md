# PLG-TODO-001 PluginDescriptor 设计收敛

日期：2026-04-01  
任务：PLG-TODO-001  
状态：D Gate PASS

## 1. 本地证据

1. docs/architecture/DASALL_infra_plugin模块详细设计.md 6.5 明确 PluginDescriptor 固定字段为 plugin_id、version、abi、trust_level、status、source，且必填字段不可为空。
2. docs/architecture/DASALL_infrastructure子系统详细设计.md 6.5 明确 PluginDescriptor 仅承载治理字段，不反向扩写 Tool/Skill 契约。
3. docs/todos/infrastructure/DASALL_infrastructure_plugin组件专项TODO.md 中 PLG-C009、PLG-C010、PLG-TODO-001 明确本轮只冻结数据结构与 unknown 兜底，不进入 manifest、签名或生命周期实现。
4. infra 现有对象如 InfraContext、PolicyDecisionRef、OTATypes 均采用 header-only 数据结构加显式一致性检查入口，适合作为本轮对象冻结风格参考。

## 2. 外部参考

1. Microsoft MSDN Magazine《Writing, Loading, and Accessing Plug-Ins》指出插件模型的第一步是先冻结最小稳定接口/元数据面，再把加载与隔离策略留给后续治理层。本任务据此把 PluginDescriptor 收敛为纯治理元数据对象，不提前混入装载句柄、入口函数或业务能力描述。

## 3. Design 结论

1. PluginDescriptor 采用 header-only 数据结构，避免在对象冻结阶段引入不必要的实现依赖。
2. trust_level 与 status 使用 plugin 私有枚举，阻断把 contracts 标识语义或 Tool/Skill 语义混入该对象。
3. plugin_id、version、abi、source 默认值统一为 unknown，并提供 normalize() 入口把空字符串收敛到 unknown，避免后续 discover/validate 流程对空值做隐式猜测。
4. 对象提供 uses_unknown_defaults() 与 is_governance_ready() 两个二值出口，分别用于默认态与冻结字段完整态的判断。

## 4. Design -> Build 映射

| Design 项 | Build 落点 |
|---|---|
| 冻结 PluginDescriptor 六个治理字段 | infra/include/plugin/PluginDescriptor.h |
| 冻结 trust_level/status 的 plugin 私有枚举 | PluginTrustLevel / PluginStatus |
| 冻结 unknown 默认值与空值归一化规则 | kPluginUnknownValue + plugin_value_or_unknown() + PluginDescriptor::normalize() |
| 增加正负例判定出口 | uses_unknown_defaults() + is_governance_ready() + PluginDescriptorFieldTest |
| 阻断 contracts 标识语义越权 | PluginDescriptorBoundaryContractTest |

## 5. Build 三件套

1. 代码目标：新增 infra/include/plugin/PluginDescriptor.h，并在 infra/CMakeLists.txt 中注册 public header。
2. 测试目标：新增 tests/unit/infra/plugin/PluginDescriptorTest.cpp，覆盖默认 unknown、空值归一化负例和完整字段正例；新增 tests/contract/smoke/PluginDescriptorBoundaryContractTest.cpp，校验对象未扩写 request_id/trace_id/task_id 等 contracts 标识语义。
3. 验收命令：
   - cmake -S . -B build-ci -G Ninja
   - cmake --build build-ci --target dasall_infra dasall_plugin_descriptor_unit_test dasall_contract_plugin_descriptor_boundary_test
   - ctest --test-dir build-ci -N -R "PluginDescriptorFieldTest|PluginDescriptorBoundaryContractTest"
   - ctest --test-dir build-ci --output-on-failure -R "PluginDescriptorFieldTest|PluginDescriptorBoundaryContractTest"

## 6. 风险与回退

1. trust_level 词典当前只冻结 Unknown/Untrusted/External/Vendor/Internal 五档；若后续签名规范评审要求新增等级，应走单独评审，不在本轮隐式扩张。
2. status 当前只冻结治理状态机所需的最小枚举，不提前绑定 lifecycle manager 具体转移规则；若后续状态图收敛，需要保持既有 Unknown/Discovered/Validated/Rejected/Loaded/Active/Disabled/Unloaded 的兼容性。
3. 本轮不引入 manifest、capabilities、handle_ref 或 evidence_ref，避免越过 PLG-TODO-001 的对象冻结边界。