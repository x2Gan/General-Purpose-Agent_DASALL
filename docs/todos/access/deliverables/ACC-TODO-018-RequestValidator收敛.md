# ACC-TODO-018 设计收敛文档

## 1. 任务定义

实现 RequestValidator，在请求进入 RuntimeBridge 前完成输入校验与 fail-closed 拒绝，覆盖 packet 元数据、payload 限制和 request_context 头字段注入防护。

本任务范围：

1. 落盘 access/src/RequestValidator.h 与 access/src/RequestValidator.cpp。
2. 将 RequestValidator 接入 dasall_access 静态库。
3. 新增 RequestValidatorTest.cpp、RequestValidatorPayloadLimitTest.cpp、RequestValidatorInjectionTest.cpp。
4. 回写 TODO 状态与证据，并完成提交推送。

## 2. 边界与职责

### 2.1 职责

1. 在 admission 之后、runtime bridge 之前执行输入结构校验。
2. 对 payload 与 user_input 尺寸执行硬上限拒绝。
3. 对 request_context 执行 CRLF 注入与非法键名字符拦截。
4. 将拒绝原因映射为 AccessErrorCode（ValidationRejected/UnsupportedProtocol/PayloadTooLarge/MalformedInput）。

### 2.2 非职责

1. 不负责主体解析、认证、授权与 admission 判定。
2. 不负责 AgentRequest 投影与结果发布。
3. 不负责 runtime dispatch、cancel 或 receipt 查询。
4. 不对外扩展 public ABI，仅提供 access internal 组件实现。

## 3. 本地证据与外部参考

### 3.1 本地证据

1. Access 详设 6.16 要求输入规则在 RuntimeBridge 前落地，异常输入不得进入 runtime。
2. Access 详设 6.17 要求错误域映射稳定，验证失败必须落入 validation taxonomy。
3. 专项 TODO 对 018 已冻结目标函数：validate_packet()/validate_payload_limits()/validate_headers() 与三个单测出口。

### 3.2 外部参考

1. OWASP 输入验证实践强调统一入口校验与注入字符阻断；本任务将 CR/LF 视为 header injection 风险并 fail-closed。

## 4. 数据与接口说明

### 4.1 内部数据模型

1. RequestValidationResult
   - 字段：accepted、error
   - 用途：统一表达校验通过或拒绝。

2. RequestValidator
   - 构造参数：AccessPublishView（payload/user_input 限制）、allowed_protocols（协议白名单）
   - 核心方法：validate_packet()/validate_payload_limits()/validate_headers()

### 4.2 关键校验规则

1. packet_id/entry_type/protocol_kind 与 subject_identity.actor_ref 必填。
2. protocol_kind 必须属于 allowed_protocols（若 allowlist 非空）。
3. payload 长度不得超过 max_payload_bytes。
4. request_context["user_input"] 长度不得超过 max_user_input_bytes。
5. request_context 键和值不能包含 CR/LF；键名只能包含 a-zA-Z0-9-_.。

## 5. 流程

1. validate_packet 先做必填字段与协议白名单校验。
2. 通过后进入 validate_payload_limits 校验 payload/user_input 尺寸。
3. 再进入 validate_headers 校验 request_context 键值合法性与注入特征。
4. 任一步失败立即返回 accepted=false + AccessError，终止后续处理。

## 6. 决策规则

1. 结构字段缺失 -> ValidationRejected。
2. 协议不在 allowlist -> UnsupportedProtocol。
3. payload 或 user_input 超限 -> PayloadTooLarge。
4. request_context 含 CRLF 或非法 key 字符 -> MalformedInput。
5. fail-closed：拒绝路径不降级为 warning，不向 RuntimeBridge 透传。

## 7. Design -> Build 映射

| 设计项 | Build 落点 |
|---|---|
| validator internal types 与实现 | access/src/RequestValidator.h / .cpp |
| access 库接线 | access/CMakeLists.txt |
| 基本通过 + 协议拒绝路径 | tests/unit/access/RequestValidatorTest.cpp |
| payload 限制拒绝路径 | tests/unit/access/RequestValidatorPayloadLimitTest.cpp |
| header 注入拒绝路径 | tests/unit/access/RequestValidatorInjectionTest.cpp |
| 测试注册 | tests/unit/access/CMakeLists.txt |

## 8. 文件范围

1. access/src/RequestValidator.h
2. access/src/RequestValidator.cpp
3. access/CMakeLists.txt
4. tests/unit/access/RequestValidatorTest.cpp
5. tests/unit/access/RequestValidatorPayloadLimitTest.cpp
6. tests/unit/access/RequestValidatorInjectionTest.cpp
7. tests/unit/access/CMakeLists.txt
8. docs/todos/access/DASALL_access子系统专项TODO.md
9. 本文档

## 9. 验收三件套

### 9.1 代码目标

1. 实现 RequestValidator concrete internal 组件。
2. 收敛 validate_packet/validate_payload_limits/validate_headers 三段校验链路。

### 9.2 测试目标

1. RequestValidatorTest：合法输入通过 + 协议白名单拒绝。
2. RequestValidatorPayloadLimitTest：payload 超限拒绝。
3. RequestValidatorInjectionTest：CRLF 注入拒绝。

### 9.3 验收命令

```bash
cmake --build build-ci --target \
  dasall_access_request_validator_unit_test \
  dasall_access_request_validator_payload_limit_unit_test \
  dasall_access_request_validator_injection_unit_test && \
ctest --test-dir build/vscode-linux-ninja -R "RequestValidator(Test|PayloadLimitTest|InjectionTest)" --output-on-failure
```

说明：当前仓库全量 dasall_unit_tests 仍受历史 knowledge 侧问题影响，本任务沿用定向构建与定向 ctest 验收。

## 10. 风险与回退

1. 当前 header 校验对 key 字符采取白名单策略；后续若新增协议特例需显式扩展规则，不可静默放宽。
2. payload 限制基于 AccessPublishView，后续若 profile 投影变动，应保持错误码与拒绝语义稳定。
3. 如需接入更复杂 payload 语义校验，应新增独立规则模块，避免污染 validator 的首层判定职责。
