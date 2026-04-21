# KNO-TODO-025 KnowledgeTelemetry 观测桥设计收敛

- 日期：2026-04-21
- 任务：KNO-TODO-025
- 状态：已收敛
- 对应 Blocker：无

## 1. 输入与约束

1. 详细设计 6.11 与 6.13.4 已冻结 Knowledge 的可观测目标：retrieve / ingest / health / snapshot_swap 四类事件必须共享统一字段、统一 reason code 语义，并且 sink 失败不得阻断主链路。
2. 006 已冻结 `KnowledgeErrorCode -> ErrorInfo` 映射，007 已冻结 `KnowledgeConfigProjector`；025 必须消费这些稳定边界，而不是再定义一套 error/config 投影字段。
3. 本轮 owner 只到“统一事件桥 + failure accounting + unit gates”为止，不提前实现 `KnowledgeHealthProbe`、不把具体日志/指标/追踪/审计后端硬编码进 knowledge。
4. 审计必需字段缺失时，事件不能静默丢弃；必须改写为 `result=invalid_telemetry_payload` 并进入最小 fallback log 路径。
5. 参考仓库既有 observability bridge 基线：trace/metrics/audit 失败一律 fail-open，仅通过 degraded status / failure counter / fallback evidence 反映健康度下降，不改变业务返回值。

## 2. 本地证据

| 证据 | 观察结果 | 结论 |
|---|---|---|
| 详细设计 6.13.4 `KnowledgeTelemetry` 组件卡片 | 已定义 `KnowledgeTelemetryEvent` 与四个 `emit_*` 接口，并明确 sink failure fail-open | 025 可以直接把事件桥与 failure accounting 落到代码 |
| 详细设计 6.11 必带字段/指标/审计表 | 已给出 `request_id`、`query_kind`、`retrieval_mode`、`corpus_count`、`result_count`、`degraded`、`profile_id`、`error_category` | 025 的事件结构必须覆盖这些统一字段 |
| repo memory `tools-observability-bridges.md` | tools trace/health 桥已固定 fail-open 与 degraded counter 语义 | KnowledgeTelemetry 应保持同样的观测故障边界，而不是把 sink 故障升级成业务错误 |

## 3. 设计结论

### 3.1 事件桥边界

1. `KnowledgeTelemetry` 当前实现为统一事件桥，不直接绑定 конкрет logging/metrics/audit/tracing 类型；输入通过 `TelemetrySinks` 回调聚合，以便后续 adapter/bridge 任务增量接入真实 infra sink。
2. `KnowledgeTelemetryEvent` 使用统一 superset 字段：
   - 基础字段：`event_name`、`request_id`、`component`、`snapshot_id`、`result`、`degraded`、`latency_ms`
   - 诊断字段：`reason_codes`、`corpus_ids`、`profile_id`、`query_kind`、`retrieval_mode`、`corpus_count`、`result_count`、`error_category`
3. 四个公共入口保持稳定：
   - `emit_retrieve_event(...)`
   - `emit_ingest_event(...)`
   - `emit_health_event(...)`
   - `emit_snapshot_swap_event(...)`

### 3.2 Fail-open 与 fallback 规则

1. 任一 primary sink 写入异常时：
   - 主链路继续返回；
   - `sink_failure_total` 与 `dropped_event_total` 递增；
   - telemetry 状态进入 `degraded=true`；
   - 如存在 `fallback_log_sink`，则把带 `*_sink_failure` reason code 的最小事件写入 fallback log。
2. 对 retrieve 事件，若缺失 `request_id` / `profile_id` / `query_kind` / `retrieval_mode` 等必需字段：
   - 事件被改写为 `result=invalid_telemetry_payload`；
   - `invalid_payload_total` 与 `dropped_event_total` 递增；
   - 仅走 fallback log，不再继续投递 primary sinks。
3. `KnowledgeTelemetryStatus` 暴露 `retrieve/ingest/health/snapshot_swap` 四类计数、drop/invalid/sink failure/fallback 计数和 degraded 位，为后续 health probe 提供只读观测事实。

### 3.3 当前范围纪律

1. 025 没有接入真实 infra sink 类型，也没有规定 trace span 父子链；这些属于后续桥接/集成任务。
2. 025 不承担健康聚合、告警策略或重试治理；它只负责“把事件以 fail-open 方式交给 sinks，并留下可验证的丢弃/退化证据”。
3. `KnowledgeTelemetryEvent` 的 `query_kind` / `retrieval_mode` 直接复用 006 的 Knowledge ABI 枚举，不重写字符串枚举体系。

## 4. Design -> Build 映射

| Design 项 | Build / 文档落点 |
|---|---|
| `KnowledgeTelemetryEvent`、`TelemetrySinks`、`KnowledgeTelemetryStatus`、四个 emit 接口 | `knowledge/include/health/KnowledgeTelemetry.h` |
| fail-open sink fanout、invalid payload fallback、drop/failure accounting | `knowledge/src/observability/KnowledgeTelemetry.cpp` |
| telemetry success / field set / degrade 单测 | `tests/unit/knowledge/KnowledgeTelemetryTest.cpp`、`tests/unit/knowledge/KnowledgeTelemetryFieldSetTest.cpp`、`tests/unit/knowledge/KnowledgeTelemetryDegradeEventTest.cpp` |
| unit target 注册 | `tests/unit/knowledge/CMakeLists.txt` |
| TODO / worklog 回写 | `docs/todos/knowledge/DASALL_knowledge子系统专项TODO.md`、`docs/worklog/DASALL_开发执行记录.md` |

## 5. 本任务三件套

- 代码目标：实现 `KnowledgeTelemetry` 统一事件桥、字段校验、sink failure accounting 与 fallback log。
- 测试目标：`KnowledgeTelemetryTest`、`KnowledgeTelemetryFieldSetTest`、`KnowledgeTelemetryDegradeEventTest` 覆盖成功 fanout、invalid payload 与 sink failure fail-open 三类语义。
- 验收命令：

```bash
cmake -S . -B build-ci -G "Unix Makefiles" && \
cmake --build build-ci --target dasall_knowledge dasall_knowledge_telemetry_unit_test dasall_knowledge_observability_fields_unit_test dasall_knowledge_telemetry_degrade_event_unit_test && \
ctest --test-dir build-ci -R "KnowledgeTelemetry(Test|FieldSetTest|DegradeEventTest)" --output-on-failure
```

## 6. 风险与回退

1. 风险：若后续组件直接向具体 logger/meter/audit sink 写事件，而绕过 `KnowledgeTelemetry`，reason code 与 drop accounting 会漂移。
   - 处置：025 已固定统一事件桥与 `KnowledgeTelemetryStatus`，后续实现应只在 bridge 内做 sink fanout。
2. 风险：若 invalid payload 被静默吞掉，QG-K08 对 observability field set 的门禁会失效。
   - 处置：025 强制把 invalid retrieve payload 改写为 `invalid_telemetry_payload` 并走 fallback log。
3. 风险：若 sink failure 被升级为业务错误，会把观测故障传播回 retrieval/ingest 主链，破坏 fail-open 约束。
   - 处置：025 只增加 degraded / drop / failure accounting，不改变 emit 调用方的控制流。

## 7. 收敛结论

1. Knowledge 现已具备最小可验证的观测桥，retrieve/ingest/health/snapshot_swap 四类事件拥有统一字段集和统一 fail-open 语义。
2. invalid payload 与 sink failure 都会留下结构化证据，不再是静默观测盲区。
3. 025 为后续 `KnowledgeHealthProbe` 和 profile/integration observability gate 提供了稳定的 status / event owner，但没有提前绑定具体 infra exporter，实现边界保持干净。