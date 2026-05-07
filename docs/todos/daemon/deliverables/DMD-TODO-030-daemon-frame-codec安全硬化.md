# DMD-TODO-030 daemon frame codec 安全硬化

状态：Done
日期：2026-04-28
来源 TODO：docs/todos/daemon/DASALL-daemon本地控制面专项TODO.md

## 1. 任务边界

1. 本任务只收敛 daemon 本地控制面 UDS frame 的编解码、schema_version 校验与协议错误映射。
2. 本轮不扩张到 CLI 输出格式、Runtime 业务语义或 diagnostics/status/cancel 命令路由实现；只为 DMD-TODO-011、019、031 提供安全可复用的 codec 基线。
3. 若仓库未接入现成 JSON 依赖，则允许在 access 内实现最小 JSON codec，但必须把转义、UTF-8、未知字段和 malformed 输入处理集中到单一组件，禁止继续在 adapter 内散落字符串扫描。

## 2. 研究结论

### 2.1 本地证据

1. DMD-TODO-030 明确指出当前 `DaemonProtocolAdapter` 仍通过字符串扫描解析 JSON，这直接违背“安全敏感字段不再由散落字符串扫描解析”的完成判定。
2. `access/src/daemon/DaemonProtocolAdapter.cpp` 当前 `extract_json_string()` 只找首个双引号结束，无法处理 escaped quotes、截断 payload、未知字段和 schema_version 校验。
3. `access/include/daemon/DaemonProtocolTypes.h` 已冻结 `UdsRequestFrame`、`UdsResponseFrame`、`DaemonFrameDecodeError` 与 `kDaemonProtocolSchemaVersion`，说明 030 的正确落点是“补 codec 并复用既有类型”，不是重新发明新 frame 类型。
4. DMD-TODO-011 已声明后续 adapter 必须优先委托 `DaemonFrameCodec`，因此 030 的实现需要保持 public include 可被 adapter 直接复用。

### 2.2 外部参考

1. RFC 8259 要求 JSON generator 必须生成严格符合 JSON grammar 的文本，string 中的双引号、反斜杠和控制字符必须 escape；JSON text 交换时应使用 UTF-8；parser 可以设置大小与内容限制，但不能把非 JSON 或截断文本当作成功解析。
2. RFC 8259 同时指出 object member name 应保持唯一，重复或非法 Unicode/截断 surrogate 会导致实现间行为不可预测。因此 daemon v1 frame codec 应保持字段白名单、拒绝未知顶层字段和非 UTF-8/截断输入，而不是宽松吞掉异常值。

参考来源：RFC 8259 The JavaScript Object Notation (JSON) Data Interchange Format，Section 4、7、8、9、10。

## 3. 设计结论

1. 新增 `DaemonFrameCodec` 作为 daemon UDS frame 的唯一编解码入口，对 request decode 和 response encode 做集中治理。
2. request decode 只接受顶层 object，并限制白名单字段：`schema_version`、`request_id`、`trace_id`、`session_hint`、`idempotency_key`、`command`、`args`、`payload`、`async_preference`。
3. `schema_version` 缺失或不等于 `kDaemonProtocolSchemaVersion` 时必须返回明确 `DaemonFrameDecodeError`，不得退化为“空 packet”。
4. `command` 必须映射到已冻结 taxonomy；未知 command 直接拒绝，不进入 Access/Runtime。
5. codec 必须验证 UTF-8、字符串转义和 payload 上限；截断 JSON、非法 escape、控制字符或非 UTF-8 一律映射为 malformed / payload-too-large 错误。
6. response encode 必须统一 escape 用户可控字段，并稳定输出 `schema_version`、`request_id`、`trace_id`、`disposition`、可选 `session_id`、`receipt_ref`、`error_ref` 与最小 `agent_result` 投影。
7. 错误映射先收敛为 publish envelope 级别：decode 失败时生成 `PublishEnvelope`，以 `protocol_status_hint` 和 `payload` 承载错误类别，保证后续 adapter/CLI 能统一消费。

## 4. Design -> Build 映射

| 设计结论 | Build 落点 | 验收信号 |
|---|---|---|
| 编解码逻辑集中到单一组件 | `access/include/daemon/DaemonFrameCodec.h`、`access/src/daemon/DaemonFrameCodec.cpp` | adapter 不再声明临时 JSON 扫描 helper |
| schema_version 与 command taxonomy 显式校验 | `decode_request_frame()` | 缺 schema、错版本、未知 command 都能返回稳定 decode error |
| response 输出严格 JSON escaping | `encode_response_frame()` | 含引号、反斜杠、换行的 payload/错误字段可正确 roundtrip |
| malformed 输入尽早拒绝并投影为 publish envelope | `map_frame_error_to_publish_envelope()` | adapter 测试可断言 malformed frame 不进入 Runtime |
| focused tests 可发现 | `tests/unit/access/CMakeLists.txt` + 新增 codec tests | `ctest -R "DaemonFrameCodec(Test|MalformedTest)|DaemonProtocolAdapterTest"` 可发现并通过 |

## 5. Build 三件套

### 5.1 代码目标

1. 新增 `DaemonFrameCodec` public header 与实现。
2. 更新 `access/CMakeLists.txt` 暴露 codec。
3. 改造 `DaemonProtocolAdapter` 复用 codec，移除本地字符串扫描 helper。

### 5.2 测试目标

1. 新增 `DaemonFrameCodecTest` 覆盖正常 decode/encode、escaped quotes、args、response escaping。
2. 新增 `DaemonFrameCodecMalformedTest` 覆盖 schema 缺失、版本不兼容、未知 command、payload 过大、非 UTF-8、截断/非法 JSON。
3. 扩展 `DaemonProtocolAdapterTest`，验证 codec 接线后 malformed frame 不被当作成功 packet。

### 5.3 验收命令

1. `cmake -S . -B build-ci -G "Unix Makefiles"`
2. `cmake --build build-ci --target dasall_unit_tests`
3. `ctest --test-dir build-ci -R "DaemonFrameCodec(Test|MalformedTest)|DaemonProtocolAdapterTest" --output-on-failure`

## 6. 落盘结果

1. 新增 `access/include/daemon/DaemonFrameCodec.h` 与 `access/src/daemon/DaemonFrameCodec.cpp`，集中实现：
	- request frame decode
	- response frame encode
	- UTF-8 校验
	- 最小 JSON object/string/bool 解析
	- 顶层字段白名单与错误映射
2. 更新 `access/CMakeLists.txt`，将 codec public header/source 纳入 `dasall_access`。
3. 更新 `access/src/daemon/DaemonProtocolAdapter.cpp`，删除本地 `extract_json_string()` / `extract_json_bool()` / `envelope_to_json()`，改为统一委托 `DaemonFrameCodec`。
4. 新增 `tests/unit/access/DaemonFrameCodecTest.cpp`，覆盖：
	- request frame 正常 decode
	- escaped quotes 解码
	- args 对象投影
	- response encode escaping
5. 新增 `tests/unit/access/DaemonFrameCodecMalformedTest.cpp`，覆盖：
	- schema 缺失
	- 版本不兼容
	- 未知 command
	- payload 过大
	- 非 UTF-8
	- 截断 payload
6. 更新 `tests/unit/access/DaemonProtocolAdapterTest.cpp`，切换到 v1 frame schema，并新增 malformed frame 不生成有效 `InboundPacket` 的回归断言。
7. 更新 `tests/unit/access/CMakeLists.txt`，注册 `DaemonFrameCodecTest` 与 `DaemonFrameCodecMalformedTest`。

## 7. Validation

1. `Build_CMakeTools(buildTargets=["dasall_access_daemon_frame_codec_unit_test","dasall_access_daemon_frame_codec_malformed_unit_test","dasall_access_daemon_protocol_adapter_unit_test"])`
	- 结果：通过。
2. `RunCtest_CMakeTools(tests=["DaemonFrameCodecTest","DaemonFrameCodecMalformedTest","DaemonProtocolAdapterTest"])`
	- 结果：三条测试均通过。
	- 备注：工具 stderr 继续打印仓库既有 `DartConfiguration.tcl` 缺失提示，但返回码为 0，按当前仓库基线计为有效证据。

## 8. D Gate

结论：PASS。

依据：

1. 本地证据、RFC 8259 外部参考、Design -> Build 映射和 Build 三件套已锁定。
2. 本轮边界仍局限于 codec 与 adapter 接线，没有扩张到后续命令路由任务。
3. 可进入 DMD-TODO-030-B 实施阶段。

## 9. 完成判定

1. daemon frame 的安全敏感字段已从 `DaemonProtocolAdapter` 的散落字符串扫描迁移到集中 codec。
2. malformed frame 在 adapter 层已回落为空 packet，不再被当作有效 `InboundPacket` 进入 Access/Runtime。
3. response encode 已统一 escape 用户可控字符串字段，覆盖双引号、反斜杠与换行。
4. `DaemonFrameCodecTest`、`DaemonFrameCodecMalformedTest` 与 `DaemonProtocolAdapterTest` 已提供正负例验证，满足 DMD-TODO-030 完成条件。