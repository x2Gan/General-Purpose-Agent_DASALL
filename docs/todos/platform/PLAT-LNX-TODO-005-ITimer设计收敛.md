# PLAT-LNX-TODO-005 ITimer 设计收敛

日期：2026-03-27  
任务：PLAT-LNX-TODO-005  
状态：D Gate PASS

## 1. 本地证据

1. docs/architecture/platform_linux_detailed_design.md 6.5 明确 TimerSpec 关键字段为 mode、interval_ms、initial_delay_ms、clock_kind。
2. docs/architecture/platform_linux_detailed_design.md 6.6 明确 ITimer 需要冻结 start_once、start_periodic、cancel 三个接口。
3. docs/architecture/platform_linux_detailed_design.md 6.6 明确 ITimer 错误语义应对齐 InvalidArgument、Timeout、Cancelled、InternalFailure。
4. docs/architecture/platform_linux_detailed_design.md 6.2/6.5 明确 PosixTimerProvider 输入为 TimerSpec、回调、取消请求，输出为 TimerHandle、DriftStats，周期误差只作为平台事实暴露。
5. docs/architecture/platform_linux_detailed_design.md 7 映射表要求在 platform/include/ITimer.h 落盘接口面，并通过 InterfaceSurfaceTest 固化 surface。

## 2. 外部参考

1. libuv timer 文档明确区分 single-shot 与 repeating timer，且允许 timeout=0 表示在下一次事件循环触发；这支持本轮保留 start_once/start_periodic 双入口，并允许 initial_delay_ms=0 的基线语义。
2. Linux timerfd 文档明确 interval=0 表示一次性定时器、非零 interval 表示周期定时器，同时要求调用方显式选择时钟源；这支持在 TimerSpec 中固定 mode、interval_ms、clock_kind，并把漂移/取消影响留给 provider 返回事实处理。

## 3. Design 结论

1. 冻结 ITimer 调用面：start_once、start_periodic、cancel 三方法保持最小可实现形态。
2. 冻结 TimerSpec 输入对象：mode、interval_ms、initial_delay_ms、clock_kind，并通过 has_consistent_values() 固定周期定时器必须显式提供正 interval。
3. 冻结 TimerHandle 最小句柄语义：native_id 非零才视为有效定时器句柄。
4. 冻结 TimerDriftStats/TimerCancelResult 作为最小事实输出对象，确保后续 PosixTimerProvider 可暴露漂移与取消结果，而不把恢复决策塞进 platform。

## 4. Design -> Build 映射

| Design 项 | Build 落点 |
|---|---|
| 冻结 ITimer 接口面 | platform/include/ITimer.h |
| 冻结 TimerSpec/TimerHandle/TimerDriftStats/TimerCancelResult | platform/include/ITimer.h |
| 校验接口签名与对象一致性 | tests/unit/platform/linux/InterfaceSurfaceTest.cpp |
| 复用测试发现与执行入口 | tests/unit/platform/linux/CMakeLists.txt |

## 5. Build 三件套

1. 代码目标：新增 platform/include/ITimer.h。
2. 测试目标：扩展 tests/unit/platform/linux/InterfaceSurfaceTest.cpp，覆盖 ITimer 签名稳定性、TimerSpec 默认值正例、周期零 interval 与非法 drift 负例。
3. 验收命令：
   - cmake -S . -B build-ci -G Ninja
   - cmake --build build-ci --target dasall_platform dasall_platform_interface_surface_unit_test
   - ctest --test-dir build-ci -N -R InterfaceSurfaceTest
   - ctest --test-dir build-ci -R InterfaceSurfaceTest --output-on-failure

## 6. 风险与回退

1. 本轮只冻结 ITimer 接口层，不引入 provider 实现；若后续 PosixTimerProvider 需要补充更多统计字段，应先走接口评审，避免隐式 breaking change。
2. PlatformErrorCode 当前未细化到 timer 专属 Cancelled 断言路径，本轮先冻结接口与对象边界；若后续 provider 验证需要单独错误码，应在 PLAT-LNX-TODO-013 前回到错误模型评审补齐。