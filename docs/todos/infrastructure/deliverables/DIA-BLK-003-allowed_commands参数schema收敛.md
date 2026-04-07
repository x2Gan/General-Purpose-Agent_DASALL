# DIA-BLK-003 allowed_commands 参数 schema 收敛

日期：2026-04-07  
任务：DIA-BLK-003  
状态：解阻 PASS

## 1. 本地证据

1. [docs/todos/infrastructure/DASALL_infrastructure_diagnostics组件专项TODO.md](docs/todos/infrastructure/DASALL_infrastructure_diagnostics组件专项TODO.md) 将 `DIA-TODO-013` 标记为 `Blocked`，根因明确为“allowed_commands 参数 schema 未冻结，无法安全完成 validate 语义”。
2. [docs/architecture/DASALL_infra_diagnostics模块详细设计.md](docs/architecture/DASALL_infra_diagnostics模块详细设计.md) 在本轮前已冻结 `CommandCatalog` / `ValidationResult` 的 ref+summary 边界，但尚未把 `health.snapshot`、`queue.stats`、`thread.dump` 的真实参数约束写成权威 schema。
3. [infra/include/diagnostics/DiagnosticsTypes.h](infra/include/diagnostics/DiagnosticsTypes.h) 已固定 `DiagnosticsCommand.args` 为 `std::vector<std::string>`，说明 v1 validate 不能假定 JSON object payload；它必须基于 token grammar 做静态校验与规范化。
4. [tests/unit/infra/DiagnosticsCommandRegistryTest.cpp](tests/unit/infra/DiagnosticsCommandRegistryTest.cpp) 当前只冻结了 catalog discoverability 和成功/失败返回边界，还没有覆盖非法 token、越界 `timeout_ms` 或 `request_scope` 不匹配的负例；这正是 013 在本轮前无法安全进入实现的原因。
5. [infra/src/diagnostics/DiagnosticsServiceFacade.cpp](infra/src/diagnostics/DiagnosticsServiceFacade.cpp) 的 safe_mode skeleton 已把 `health.snapshot` 固定为最后保留的低风险命令，因此 schema 冻结必须保证 `health.snapshot` 的参数面比其他命令更窄、更稳定。

## 2. 外部参考

1. JSON Schema Validation Draft 2020-12 将 `type`、`enum`、`minimum`、`maximum`、`minItems`、`maxItems`、`uniqueItems` 等视为 assertion 关键词，同时把 `default`、`readOnly`、`examples` 视为 annotation；本轮据此把 diagnostics 的参数规则拆成“权威 schema 约束 + `arg_schema_summary` 摘要注解”，而不是把完整 schema 直接塞进目录返回对象。
   - https://json-schema.org/draft/2020-12/json-schema-validation
2. RFC 6901 定义了 JSON Pointer 的稳定定位方式，适合在 schema 资产内部定位子字段；本轮据此约束：若未来需要定位 schema 内部字段，可在 `arg_schema_ref` 中使用 JSON Pointer fragment，但公开的 `ValidationResult.field_paths` 继续保持已冻结的稳定简写（如 `command_name`、`args[0]`），避免与现有接口边界冲突。
   - https://datatracker.ietf.org/doc/html/rfc6901

## 3. 阻塞修复与设计结论

阻塞分类：

1. `DIA-BLK-003` 属于 context blocker：公开接口已经可编译，但 validate 的真实参数边界没有 source of truth，继续实现只会把 schema 猜测写进代码。

最小 blocker-fix：

1. 在 diagnostics 详细设计中新增专门章节，冻结三条只读命令的 schema ref、`request_scope`、args token grammar、默认值与负例锚点。
2. 约束 `infra.diagnostics.allowed_commands` 在 v1 只承担 capability gate，禁止 profile/部署层改写参数 schema。
3. 把“schema 权威正文”和“公开目录只暴露 ref+summary”同时固定下来，既解除 013，又不回退 008 的边界纪律。

设计结论：

1. diagnostics v1 只允许三条只读命令：`health.snapshot`、`queue.stats`、`thread.dump`；任何变更型命令继续被排除在白名单外。
2. 三条命令的 `request_scope` 在 v1 固定为 `runtime`；`CommandRegistry` 只做静态 scope 匹配，不引入实例级或模块级扩张。
3. `args` 采用受限 CLI token 形式：
   - 空数组表示使用 schema 默认值；
   - 布尔 flag 为 `--flag`；
   - 带值参数为 `--key=value`；
   - 禁止 positional args、短参数、未知 key 与重复 token。
4. `timeout_ms` 继续由请求对象携带，但 validate 必须拒绝 `timeout_ms <= 0` 或超过 `infra.diagnostics.command.timeout_ms` cap 的请求。
5. `queue.stats` 只冻结 `queue_id` 的语法，不在 registry 层校验队列实例是否存在；队列不存在属于执行期失败，而不是 schema 失败。
6. `thread.dump` 的默认 `--limit=5` 与范围 `1..32` 属于输出体积治理；若未来需要更大范围，必须引入新 schema version，而不是原位改写 v1。

### 3.1 v1 只读命令 schema 表

| command_name | schema_ref | request_scope | args schema | normalized default | validate 负例锚点 |
|---|---|---|---|---|---|
| `health.snapshot` | `schema://diagnostics/health.snapshot/v1` | `runtime` | 允许 `[]` 或 `["--summary"]`；`minItems=0`、`maxItems=1`、`uniqueItems=true` | `[] -> ["--summary"]` | `request_scope`、`args[0]`、`args[1]` |
| `queue.stats` | `schema://diagnostics/queue.stats/v1` | `runtime` | 允许 `[]` 或 `["--queue=<queue_id>"]`；`queue_id` 语法为 `[a-z0-9._-]{1,32}`；`minItems=0`、`maxItems=1`、`uniqueItems=true` | `[] -> ["--queue=main"]` | `request_scope`、`args[0]`、`args[1]` |
| `thread.dump` | `schema://diagnostics/thread.dump/v1` | `runtime` | 允许 `[]` 或 `["--limit=<n>"]`；`n` 为十进制整数且满足 `1 <= n <= 32`；`minItems=0`、`maxItems=1`、`uniqueItems=true` | `[] -> ["--limit=5"]` | `request_scope`、`args[0]` |

补充约束：

1. `health.snapshot` 在 v1 不允许 `--full`、`--component=<id>`、`--include-secret=*` 等扩张参数；safe_mode 回退链必须保持摘要级最小证据面。
2. `queue.stats` 若携带 `--queue=<queue_id>`，`queue_id` 不能为空，且只能由小写字母、数字、点、下划线、短横线组成。
3. `thread.dump` 禁止裸数字、负数、前缀零混淆或多个 limit token；超界值统一在 registry 阶段拒绝。

## 4. Design -> Build 映射

| Design 项 | Build 落点 |
|---|---|
| 冻结三条只读命令的权威 schema ref 与默认值 | `infra/src/diagnostics/CommandRegistry.cpp` 直接按 `schema://diagnostics/<command>/v1` 实现 validate 和 normalize |
| 冻结 `request_scope=runtime` 与 timeout cap 约束 | `DiagnosticsCommandRegistryTest` / `DiagnosticsCommandPolicyTest` 新增 scope/time 负例 |
| 冻结受限 CLI token grammar | `CommandRegistry` 只接受 `--flag` / `--key=value` 两种 token，不再猜测 object payload |
| 冻结 Profile 只能裁剪命令集合、不能改 schema | 实现轮只把 `infra.diagnostics.allowed_commands` 当作名称白名单消费，不新增 profile 自定义 schema 通道 |
| 冻结 `field_paths` 的稳定简写 | 单测直接断言 `command_name`、`request_scope`、`timeout_ms`、`args[0]`，不把 JSON Pointer 文本暴露到公开结果对象 |

## 5. 对 DIA-TODO-013 的直接交接

1. `DIA-TODO-013` 可以从 `Blocked` 转为 `Not Started`，并按本交付物与 diagnostics 详细设计 6.5.2 直接实现 `CommandRegistry.cpp` 骨架。
2. 013 的最小完成边界应包括：
   - 非白名单命令拒绝；
   - `request_scope!=runtime` 拒绝；
   - `timeout_ms` 越界拒绝；
   - 三条命令的 token schema 校验；
   - 空 `args` 按 schema 规范化到默认 token。
3. 013 不得顺手把完整 schema 重新内联进 `CommandCatalog` / `ValidationResult`，也不得扩张到变更型命令或 profile 自定义 schema。

## 6. Build 三件套

1. 代码目标：更新 diagnostics 详细设计、diagnostics 专项 TODO、infrastructure 总 TODO 和 worklog，并新增 blocker deliverable。
2. 测试目标：执行 process validation，确认 6.5.2 与三条 schema 锚点已落盘，且 `DIA-BLK-003` / `DIA-TODO-013` 的台账状态一致。
3. 验收命令：
   - `rg -n "### 6.5.2|schema://diagnostics/health.snapshot/v1|schema://diagnostics/queue.stats/v1|schema://diagnostics/thread.dump/v1" docs/architecture/DASALL_infra_diagnostics模块详细设计.md`
   - `rg -n "DIA-BLK-003|DIA-TODO-013|已解阻|Not Started" docs/todos/infrastructure/DASALL_infrastructure_diagnostics组件专项TODO.md docs/todos/infrastructure/DASALL_infrastructure子系统专项TODO.md docs/worklog/DASALL_开发执行记录.md`

## 7. 风险与回退

1. 若后续实现把 `infra.diagnostics.allowed_commands` 扩成“按 profile 注入对象 schema”，将直接回退本轮 blocker fix，并放大 profile/config breaking 风险。
2. 若下一轮把公开 `field_paths` 从稳定简写切换到 JSON Pointer 文本，会把 schema 收敛和接口兼容性风险混在同一轮，应该拆成单独评审。
3. 若 `thread.dump` 或 `queue.stats` 需要扩张参数面，必须引入新的 schema version 或新的命令名，不能在 v1 上做 in-place 修改。