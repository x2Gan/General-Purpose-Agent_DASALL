# TOOL-TODO-004 ITool 与 IToolManager 接口设计收敛

日期：2026-04-15  
任务：TOOL-TODO-004  
状态：D Gate PASS

## 1. 本地证据

1. docs/architecture/DASALL_tools子系统详细设计.md 6.6 已给出 ITool 与 IToolManager 的建议签名：`descriptor()`、`execute()`、`invoke()`、`invoke_batch()`、`compensate()`，并明确这些接口属于 tools/include 下的模块公共 ABI。
2. 同一设计文档 6.5.1 已完成 ToolInvocationContext 与 ToolInvocationEnvelope 的 module-public 冻结，因此 004 的接口可以直接消费这两个对象，而无需再回写 shared contracts。
3. docs/todos/tools/DASALL_tools子系统专项TODO.md 中 TOOL-TODO-004 的完成判定要求接口签名与 6.6 一致，且 `invoke_batch` 保持 request 级隔离语义，不越过 runtime 边界。
4. 同一专项 TODO 中 TOOL-TC003、TOOL-TC004、TOOL-TC006 明确 tools 不拥有 prompt/recovery 主控权，且 supporting object 默认 module-local；因此 IToolManager 只暴露调用与补偿入口，不暴露 replan/retry/abort 裁定接口。
5. TOOL-TODO-003 已冻结 Envelope 的统一返回面，当前实现 004 的最小正确路径就是：ITool 专注 descriptor + execute SPI，IToolManager 专注 invoke / batch / compensate orchestration API，不提前引入 ToolManager 内部责任链细节。

## 2. 外部参考

1. cppreference 对 `std::span` 的说明指出，`span` 是对连续对象序列的 non-owning view，并满足 borrowed_range / view 语义。这支持本任务把 `invoke_batch()` 设计为 `std::span<const ToolRequest>`：调用方保留请求数组所有权，接口只消费一段连续、只读、非 owning 的批次视图。

## 3. Design 结论

1. ITool 首版冻结为最小 SPI：`descriptor()` 暴露稳定目录面，`execute()` 消费 `ToolIR` 与 `ToolExecutionContext` 并返回 shared `ToolResult`；不在该接口中引入 policy、route 或 projection 细节。
2. IToolManager 首版冻结为三条 runtime-facing API：`invoke()`、`invoke_batch()`、`compensate()`；返回值统一为已冻结的 `ToolInvocationEnvelope`。
3. `invoke_batch()` 使用 `std::span<const ToolRequest>` 表达 non-owning、只读、单轮批次视图，避免复制请求集合，同时保持 request 级隔离由 ToolManager 内部实现负责，而不是在接口层暴露共享可变状态。
4. `CompensationRequest` 与 `ToolExecutionContext` 在 004 中继续以前向声明方式保留，因为本轮只冻结接口签名，不提前推进 supporting object 细节。
5. 这些接口不承载 runtime 继续推理、memory 写回、恢复裁定或 worker orchestration 主控权，保持 ADR-006/007/008 的边界不变。

## 4. Design -> Build 映射

| Design 项 | Build 落点 |
|---|---|
| 冻结 ITool SPI | tools/include/ITool.h |
| 冻结 IToolManager runtime-facing API | tools/include/IToolManager.h |
| 增加接口 surface 证据 | tests/unit/tools/ToolInterfaceSurfaceTest.cpp |

## 5. Build 三件套

1. 代码目标：把 tools/include/ITool.h、tools/include/IToolManager.h 从壳文件替换为真实接口签名。
2. 测试目标：新增未接线的 compile-only surface 源文件 tests/unit/tools/ToolInterfaceSurfaceTest.cpp，通过方法指针类型断言与 abstractness 断言锁定 SPI/API 形状，同时不提前侵入 TOOL-TODO-008 的 unit 拓扑接线 owner。
3. 验收命令：
   - `c++ -std=c++20 -I/home/gangan/DASALL/tools/include -I/home/gangan/DASALL/contracts/include -x c++ -fsyntax-only /home/gangan/DASALL/tests/unit/tools/ToolInterfaceSurfaceTest.cpp`
   - Build_CMakeTools 构建 `dasall_tools`、`dasall_unit_tests`
   - `dasall_unit_tests` 目标构建期间自动执行当前 unit 集合，并以构建期 gate 结果作为本轮验收依据

## 6. 风险与回退

1. 若后续需要更细的批次策略（例如并发窗口、优先级、取消令牌），应在 ToolManager internal policy 中演进，而不是在 004 中扩大 `invoke_batch()` 接口面。
2. 若后续 ToolExecutionContext 或 CompensationRequest 需要成为 module-public supporting object，应在各自任务中单独冻结，不应在 004 中顺手把未定稿对象塞进接口头实现。
3. 若后续想把 SPI 与 orchestration API 拆为更多子接口，应在保持当前 public ABI 兼容的前提下新增扩展接口，而不是直接破坏 004 已冻结的方法签名。