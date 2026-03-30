# PLAT-LNX-TODO-002 LinuxPlatformCapabilities 设计收敛

日期：2026-03-27  
任务：PLAT-LNX-TODO-002  
状态：D Gate PASS

## 1. 本地证据

1. docs/architecture/platform_linux_detailed_design.md 6.5 明确 PlatformCapabilitySet 包含 thread、timer、queue、filesystem、network、ipc、hal 七类能力，每项记录 enabled、disabled、degraded 与 reason。
2. docs/architecture/platform_linux_detailed_design.md 6.7 明确 CapabilityRegistry 在平台初始化探测后登记能力状态，上层据此决定降级路径。
3. docs/architecture/platform_linux_detailed_design.md 6.8 明确 HAL 缺失、定时漂移扩大等异常会被标记为 disabled 或 degraded，而不是在平台层隐式恢复。
4. docs/architecture/platform_linux_detailed_design.md 11.1 仍未冻结 reason_code 规范化表，因此本轮只冻结状态三态和 reason 文本承载，不提前扩张独立 reason code 域。

## 2. 外部参考

1. Kubernetes Pod Conditions 将 status、reason、message 分离，其中 reason 保持机器可判定的稳定文本。本任务据此采用显式状态枚举加 reason 字段的形状，让 CapabilityRegistry 和后续测试既能做二值断言，也不必在本轮提前冻结更细粒度错误码表。

## 3. Design 结论

1. 能力状态冻结为 PlatformCapabilityState 三态枚举：Enabled、Disabled、Degraded，不引入第四种 Unknown，避免和详细设计冲突。
2. 单个能力项抽象为 PlatformCapability，保持 state + reason 两字段；Enabled 不允许携带 reason，Disabled/Degraded 必须携带非空 reason。
3. PlatformCapabilitySet 只承载七个能力项与最小聚合 helper，不提前承担 CapabilityRegistry 的查询、快照导出或 profile 决策逻辑。
4. 默认对象以 Disabled + NotProbed 初始化，表达“探测前不可用但原因可判定”的安全基线，避免空 reason 进入工厂和测试链路。

## 4. Design -> Build 映射

| Design 项 | Build 落点 |
|---|---|
| 冻结能力三态枚举 | platform/include/linux/LinuxPlatformCapabilities.h |
| 冻结单项能力的 state + reason 形状 | PlatformCapability |
| 冻结七项能力聚合对象 | PlatformCapabilitySet |
| 增加 reason 与状态一致性断言出口 | has_consistent_values() + LinuxPlatformCapabilitiesTest |

## 5. Build 三件套

1. 代码目标：新增 platform/include/linux/LinuxPlatformCapabilities.h。
2. 测试目标：新增 tests/unit/platform/linux/LinuxPlatformCapabilitiesTest.cpp，覆盖默认 disabled/not-probed 正例、enabled/disabled/degraded 混合正例，以及 reason 缺失或越界负例。
3. 验收命令：
   - cmake -S . -B build-ci -G Ninja
   - cmake --build build-ci --target dasall_platform dasall_linux_platform_capabilities_unit_test
   - ctest --test-dir build-ci -N -R LinuxPlatformCapabilitiesTest
   - ctest --test-dir build-ci -R LinuxPlatformCapabilitiesTest --output-on-failure

## 6. 风险与回退

1. 独立 reason_code 域仍属于后续 CapabilityRegistry 或补设计任务范围，本轮不提前把 reason 拆成 enum/code/message 组合。
2. 如果后续评审否定 Disabled + NotProbed 的默认初始化策略，应在不破坏三态和 reason 约束的前提下局部调整默认值，而不是扩张新的状态枚举。