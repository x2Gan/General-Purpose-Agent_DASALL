# PLAT-LNX-TODO-004 IThread 设计收敛

日期：2026-03-27  
任务：PLAT-LNX-TODO-004  
状态：D Gate PASS

## 1. 本地证据

1. docs/architecture/platform_linux_detailed_design.md 6.5 明确 ThreadOptions 关键字段为 name、stack_size_kb、detach_policy、affinity_hint。
2. docs/architecture/platform_linux_detailed_design.md 6.6 明确 IThread 需要冻结 create_thread、join_thread、request_stop 三个接口。
3. docs/architecture/platform_linux_detailed_design.md 6.6 明确 IThread 错误语义包含 InvalidArgument、Timeout、ResourceExhausted、InternalFailure。
4. docs/architecture/platform_linux_detailed_design.md 7 映射表要求在 platform/include/IThread.h 落盘接口面，并通过 InterfaceSurfaceTest 做 surface 校验。

## 2. 外部参考

1. C++ Core Guidelines（抽象接口规则）建议接口基类只暴露稳定契约，错误通道与生命周期语义通过显式返回类型固定。本任务据此将 IThread 设计为纯抽象接口，并通过 PlatformResult 显式承载成功/失败，不引入业务语义。

## 3. Design 结论

1. 冻结 IThread 调用面：create_thread、join_thread、request_stop 三方法保持最小可实现形态。
2. 冻结 ThreadOptions 输入对象：name、stack_size_kb、detach_policy、affinity_hint，并提供一致性检查入口。
3. 冻结 ThreadHandle 最小句柄语义：native_id + detach_policy，禁止零值句柄作为有效输入。
4. 复用 PlatformResult/PlatformError 作为统一错误语义载体，确保后续 PosixThreadProvider 直接复用。

## 4. Design -> Build 映射

| Design 项 | Build 落点 |
|---|---|
| 冻结 IThread 接口面 | platform/include/IThread.h |
| 冻结 ThreadOptions/ThreadHandle 输入输出对象 | platform/include/IThread.h |
| 校验接口签名与对象一致性 | tests/unit/platform/linux/InterfaceSurfaceTest.cpp |
| 接入测试发现与执行入口 | tests/unit/platform/linux/CMakeLists.txt |

## 5. Build 三件套

1. 代码目标：新增 platform/include/IThread.h。
2. 测试目标：新增 tests/unit/platform/linux/InterfaceSurfaceTest.cpp，覆盖 IThread 签名稳定性、ThreadOptions/ThreadHandle 正负例。
3. 验收命令：
   - cmake -S . -B build-ci -G Ninja
   - cmake --build build-ci --target dasall_platform dasall_platform_interface_surface_unit_test
   - ctest --test-dir build-ci -N -R InterfaceSurfaceTest
   - ctest --test-dir build-ci -R InterfaceSurfaceTest --output-on-failure

## 6. 风险与回退

1. 本轮只冻结 IThread，不包含 ITimer/IQueue 等其余接口；后续扩展需保持当前 IThread 签名稳定，避免隐式 breaking change。
2. join_thread 当前仅冻结 timeout 参数与返回通道，不定义 provider 内部调度策略；后续实现阶段若需扩展返回信息，应先走接口评审。