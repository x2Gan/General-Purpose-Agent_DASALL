# INF-LOG-FIX-002 StructuredFormatter 与 RedactionFilter 主链收口

- 日期：2026-05-27
- 任务：INF-LOG-FIX-002
- 状态：已完成（L2 focused structured/redaction main chain 已闭合；本轮不使用 qemu / kvm）

## 1. 执行前提

1. `INF-LOG-FIX-001` 已冻结 logging production acceptance matrix，因此本轮可以在不改变 owner boundary 的前提下进入主链实现。
2. `BLK-INF-LOG-002` 的真实缺口不是 sink/path，而是 redaction schema、structured field schema 与 golden fixture 尚未冻结；如果继续跳过这一步，后续 `INF-LOG-FIX-003~011` 会把未脱敏 payload 带入真实 sink。
3. 用户已明确删除 qemu / kvm 作为当前 owner 验收口径，因此本轮只声明 L2 focused evidence，不把任何结果外推为 installed/package ready。

## 2. 冻结结论

### 2.1 Structured schema

1. `StructuredFormatter` 首版 schema version 固定为 `dasall.logging.event.v1`。
2. formatter 保持 `LogEvent` 外形不变，只把结构化 JSON envelope 写入 `LogEvent.message`。
3. formatter 必补 attrs：`schema_version`、`correlation_id`、`idempotency_key`。
4. `correlation_id` 优先级固定为 `trace_id -> request_id -> session_id -> task_id -> unknown`。
5. `idempotency_key` frozen tuple 固定为 `correlation_id|task_id|module|ts_ms`。

### 2.2 Redaction schema

1. sensitive key fragments 固定为 `token`、`secret`、`password`、`authorization`、`api_key`、`apikey`。
2. message/exception 文本最低必拦截模式固定为 `bearer `、`token=`、`token:`、`secret=`、`secret:`、`password=`、`password:`、`authorization=`、`authorization:`、`api_key=`、`apikey=`。
3. owner-safe allowlist 当前固定为 `request_id`、`session_id`、`trace_id`、`task_id`、`parent_task_id`、`lease_id`、`event_name`、`event_kind`、`evidence_ref`、`audit_ref_pending`、`schema_version`、`correlation_id`、`idempotency_key`。
4. allowlist 只允许保留字段语义，不豁免 value redaction；value 一旦命中 message pattern，仍必须替换为 `<redacted>`。

## 3. Design -> Build 映射

| 设计冻结项 | 本轮 Build 目标 |
|---|---|
| `dasall.logging.event.v1` structured schema | `infra/include/logging/StructuredFormatter.h`、`infra/src/logging/StructuredFormatter.cpp` |
| deny-by-default key/message pattern | `infra/include/logging/RedactionFilter.h`、`infra/src/logging/RedactionFilter.cpp` |
| 默认不可绕过主链 | `infra/src/logging/LoggingFacade.cpp` |
| focused golden fixture | `LoggingStructuredFormatterTest`、`LoggingRedactionFilterTest`、`LoggingFacadeRedactionIntegrationTest` |

## 4. Golden fixture 与验证

1. `Build_CMakeTools(buildTargets=["dasall_logging_structured_formatter_unit_test","dasall_logging_redaction_filter_unit_test","dasall_logging_facade_redaction_integration_test"])`
   - 结果：通过。
2. `RunCtest_CMakeTools(tests=["LoggingStructuredFormatterTest","LoggingRedactionFilterTest","LoggingFacadeRedactionIntegrationTest"])`
   - 结果：命中仓库既有泛化错误 `生成失败`。
3. fallback 直接执行：
   - `./build/vscode-linux-ninja/tests/unit/infra/dasall_logging_structured_formatter_unit_test`
   - `./build/vscode-linux-ninja/tests/unit/infra/dasall_logging_redaction_filter_unit_test`
   - `./build/vscode-linux-ninja/tests/integration/infra/logging/dasall_logging_facade_redaction_integration_test`
   - 结果：3/3 通过。
4. 邻近回归补充：
   - `./build/vscode-linux-ninja/tests/unit/infra/dasall_logging_facade_unit_test`
   - `./build/vscode-linux-ninja/tests/integration/infra/logging/dasall_logging_pipeline_integration_test`
   - `./build/vscode-linux-ninja/tests/integration/infra/logging/dasall_logging_audit_link_integration_test`
   - 结果：3/3 通过。

## 5. 完成判定

1. `LoggingFacade` 默认主链已固定为 `enrich -> redact -> format -> dispatch`，调用方无法通过正常入口绕过 redaction/formatter。
2. focused golden 输出已证明不再泄漏 `secret/token/password/auth` value。
3. `BLK-INF-LOG-002` 已解阻：schema docs、golden fixture 与 deny-by-default 规则都已落盘。
4. 本轮结论只到 L2；真实 sink/rotation/path/installed artifact 继续留给 `INF-LOG-FIX-003~011`。