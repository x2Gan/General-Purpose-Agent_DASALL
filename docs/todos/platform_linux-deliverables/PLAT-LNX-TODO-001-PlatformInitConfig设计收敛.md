# PLAT-LNX-TODO-001 PlatformInitConfig 设计收敛

日期：2026-03-26  
任务：PLAT-LNX-TODO-001  
状态：D Gate PASS

## 1. 本地证据

1. docs/architecture/platform_linux_detailed_design.md 6.5 明确 PlatformInitConfig 固定字段为 target_platform、profile_name、enable_hal、queue_defaults、io_timeouts。
2. docs/architecture/platform_linux_detailed_design.md 6.7 明确该对象由 Boot 阶段在 Profile Bind 后注入 LinuxPlatformFactory，platform 自身不读取配置文件。
3. docs/architecture/platform_linux_detailed_design.md 6.9 给出首版默认策略：desktop_full/cloud_full 默认关闭 HAL，queue 默认容量 1024、overflow_policy 为 reject，network connect/io timeout 默认分别为 3000/5000 ms。
4. docs/todos/DASALL_platform_linux组件专项TODO.md 将本任务收敛为单一数据结构头文件，不要求提前扩张到工厂、provider 或 profile 注入键统一。

## 2. 外部参考

1. OpenTelemetry Common Specification 强调跨组件通用属性集合必须保持键唯一、空值具有语义、默认约束需清晰可判定。本任务据此将 PlatformInitConfig 设计为最小显式配置聚合对象，并提供一致性检查入口，避免后续工厂装配期对空字符串、负超时和非法容量进行隐式猜测。

## 3. Design 结论

1. PlatformInitConfig 保持 header-only 数据结构，避免在对象冻结阶段引入额外实现耦合。
2. queue_defaults 与 io_timeouts 采用内嵌子结构，直接承载详细设计 6.9 已冻结的默认值，避免把容量、溢出策略和 I/O 超时散落成无语义裸字段。
3. 默认 target_platform 固定为 linux，默认 profile_name 固定为 desktop_full，和 6.9 的 desktop_full 基线保持一致。
4. 对象提供 has_consistent_values()，把本任务范围内已经冻结的最小负例边界前置为可判定规则：target_platform/profile_name 非空、queue capacity 大于 0、overflow_policy 只允许 reject 或 block、timeout 不为负值。

## 4. Design -> Build 映射

| Design 项 | Build 落点 |
|---|---|
| 冻结 PlatformInitConfig 五个核心字段 | platform/include/linux/PlatformInitConfig.h |
| 冻结 queue 默认容量与溢出策略 | QueueDefaults 子结构与默认值 |
| 冻结 I/O 默认超时 | IoTimeouts 子结构与默认值 |
| 增加正负例可判定出口 | has_consistent_values() + PlatformInitConfigTest |

## 5. Build 三件套

1. 代码目标：新增 platform/include/linux/PlatformInitConfig.h。
2. 测试目标：新增 tests/unit/platform/linux/PlatformInitConfigTest.cpp，覆盖默认值正例和空 profile/零容量/负超时负例。
3. 验收命令：
   - cmake -S . -B build-ci -G Ninja
   - cmake --build build-ci --target dasall_platform dasall_platform_init_config_unit_test
   - ctest --test-dir build-ci -R PlatformInitConfigTest --output-on-failure

## 6. 风险与回退

1. profile 注入键统一仍属于 PLAT-LNX-TODO-025 范围，本轮不提前把键名或覆盖层级写入该对象。
2. 若后续评审要求 target_platform/profile_name 改为 enum 或强类型包装，应在不破坏字段语义的前提下单独走 breaking review，而不是在本轮内联扩张。