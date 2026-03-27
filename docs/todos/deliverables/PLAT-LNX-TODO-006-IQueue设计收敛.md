# PLAT-LNX-TODO-006 IQueue 设计收敛

日期：2026-03-27  
任务：PLAT-LNX-TODO-006  
状态：D Gate PASS

## 1. 本地证据

1. docs/architecture/platform_linux_detailed_design.md 6.5 明确 QueueOptions 关键字段为 capacity、overflow_policy、shutdown_policy。
2. docs/architecture/platform_linux_detailed_design.md 6.6 明确 IQueue 需要冻结 create_queue、push、pop、close 四个接口。
3. docs/architecture/platform_linux_detailed_design.md 6.6 明确 IQueue 错误语义应对齐 Timeout、QueueClosed、ResourceExhausted。
4. docs/architecture/platform_linux_detailed_design.md 6.2/6.5 明确 BlockingQueueProvider 输入为 QueueOptions、push/pop 请求，输出 QueueHandle、QueueStats，且超时/关闭/容量耗尽需可判定。
5. docs/architecture/platform_linux_detailed_design.md 7 映射表要求在 platform/include/IQueue.h 落盘接口面，并通过 InterfaceSurfaceTest 固化 surface。

## 2. 外部参考

1. Linux POSIX message queue 文档显示消息队列由显式 descriptor 管理，发送/接收与关闭语义分离，这支持本轮固定 QueueHandle + create/push/pop/close 的接口分层。
2. Linux POSIX message queue 文档强调队列容量与系统资源上限直接相关（msg_max/msgsize_max），这支持在 QueueOptions 中固定 capacity 与 overflow_policy，并在错误语义中保留 ResourceExhausted 路径。

## 3. Design 结论

1. 冻结 IQueue 调用面：create_queue、push、pop、close 四方法保持最小可实现形态。
2. 冻结 QueueOptions 输入对象：capacity、overflow_policy、shutdown_policy，并通过 has_consistent_values() 固定 capacity 必须为正值。
3. 冻结 QueueHandle 最小句柄语义：native_id 非零才视为有效队列句柄。
4. 冻结 QueuePushResult/QueuePopResult/QueueCloseResult 作为最小事实输出对象，确保后续 BlockingQueueProvider 可暴露入队、出队与关闭结果，而不引入上层恢复裁定。

## 4. Design -> Build 映射

| Design 项 | Build 落点 |
|---|---|
| 冻结 IQueue 接口面 | platform/include/IQueue.h |
| 冻结 QueueOptions/QueueHandle/QueuePushResult/QueuePopResult/QueueCloseResult | platform/include/IQueue.h |
| 校验接口签名与对象一致性 | tests/unit/platform/linux/InterfaceSurfaceTest.cpp |
| 复用测试发现与执行入口 | tests/unit/platform/linux/CMakeLists.txt |

## 5. Build 三件套

1. 代码目标：新增 platform/include/IQueue.h。
2. 测试目标：扩展 tests/unit/platform/linux/InterfaceSurfaceTest.cpp，覆盖 IQueue 签名稳定性、QueueOptions 默认值正例、零容量与不一致 pop 结果负例。
3. 验收命令：
   - cmake -S . -B build-ci -G Ninja
   - cmake --build build-ci --target dasall_platform dasall_platform_interface_surface_unit_test
   - ctest --test-dir build-ci -N -R InterfaceSurfaceTest
   - ctest --test-dir build-ci -R InterfaceSurfaceTest --output-on-failure

## 6. 风险与回退

1. 本轮只冻结 IQueue 接口层，不引入 provider 实现；若后续 BlockingQueueProvider 需要补充统计字段，应先走接口评审，避免隐式 breaking change。
2. 设计文档中关于 IQueue item 抽象（模板化或统一字节缓冲）尚属后续决策点，本轮先采用 QueueItem 字节缓冲作为稳定最小面，具体实现策略留在 PLAT-LNX-TODO-014 落地阶段评审。