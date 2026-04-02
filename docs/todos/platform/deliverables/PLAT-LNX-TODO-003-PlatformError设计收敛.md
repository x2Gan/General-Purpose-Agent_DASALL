# PLAT-LNX-TODO-003 PlatformError 设计收敛

日期：2026-03-27  
任务：PLAT-LNX-TODO-003  
状态：D Gate PASS

## 1. 本地证据

1. docs/architecture/platform_linux_detailed_design.md 6.5 明确 PlatformError 字段为 code、category、retryable_hint、syscall_name、errno_value、detail。
2. docs/architecture/platform_linux_detailed_design.md 6.7/6.8 明确平台层失败必须返回可判定错误事实，不承担上层恢复决策。
3. docs/architecture/platform_linux_detailed_design.md 7 映射表要求新增 PlatformError.h、PlatformResult.h 与 PlatformErrorMappingTest。
4. docs/architecture/platform_linux_detailed_design.md 11.1 指出上层 ErrorInfo 映射规范未完全评审；允许先冻结 PlatformError 码集和最小映射锚点，再推进更细分映射。

## 2. 外部参考

1. OpenTelemetry Common Specification 强调跨组件错误/状态字段应保持 machine-readable 且可判定。为避免未评审阶段扩张共享契约，本任务采用“平台私有错误类别 -> contracts 一级失败域”的最小映射锚点，保证测试可断言且不改写 contracts 对象。

## 3. Design 结论

1. PlatformError 保持平台私有结构，只承载平台事实字段，不直接复用或写入 contracts ErrorInfo。
2. PlatformErrorCategory 冻结为 Validation、Resource、IO、Network、IPC、Internal 六类；映射到 contracts 一级失败域只使用 Validation/Provider/Runtime。
3. PlatformResult 作为值/错互斥容器，强制 success 与 failure 二值互斥，防止“同时有值和错误”的不一致状态。
4. BLK-04 解阻策略：先冻结 category 到 contracts 一级域的映射函数并用单测固化，细粒度 code->ErrorInfo 评审仍可在后续扩展，不阻断当前 Build-ready 任务。

## 4. Design -> Build 映射

| Design 项 | Build 落点 |
|---|---|
| 冻结 PlatformError 字段集合 | platform/include/PlatformError.h |
| 冻结 PlatformResult 二值容器 | platform/include/PlatformResult.h |
| 冻结最小 contracts 映射锚点 | map_platform_error_category_to_contracts |
| 固化正负例与互斥约束 | tests/unit/platform/linux/PlatformErrorMappingTest.cpp |

## 5. Build 三件套

1. 代码目标：新增 platform/include/PlatformError.h、platform/include/PlatformResult.h。
2. 测试目标：新增 tests/unit/platform/linux/PlatformErrorMappingTest.cpp，覆盖 category 映射正例、字段一致性负例和 PlatformResult 互斥性负例。
3. 验收命令：
   - cmake -S . -B build-ci -G Ninja
   - cmake --build build-ci --target dasall_platform dasall_platform_error_mapping_unit_test
   - ctest --test-dir build-ci -N -R PlatformErrorMappingTest
   - ctest --test-dir build-ci -R PlatformErrorMappingTest --output-on-failure

## 6. 风险与回退

1. 当前映射仅冻结到 contracts 一级失败域，未承诺细粒度 ErrorInfo 字段映射；如后续评审要求细化，应在不破坏当前映射测试的前提下增量扩展。
2. 若后续架构评审要求补充更多 PlatformErrorCode，需同步更新映射单测，防止 category 漂移。