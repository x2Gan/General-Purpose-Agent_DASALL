# ACC-FIX-004 streaming 延后与 feature gate 固化

来源任务：ACC-FIX-004
完成日期：2026-05-27

## 1. 任务边界

1. 本轮只固化 Access v1 的 streaming 延后口径，不实现 `StreamGateway`、WS/MQTT listener、attach/reconnect/replay cursor 或 shared streaming lifecycle。
2. authoritative 问题定义固定为：`Gate-INT-08` 是否已被明确冻结为 Access v1 unary focused ingress，且 `StreamGateway / WS / MQTT` 是否继续受 `ACC-GATE-11` 持有、保持 feature flag default-off 与 async receipt + poll fallback。
3. 本轮不使用 qemu / kvm；证据仅来自 build-tree wording guard、已有 unary transport regression 与相关 SSOT / TODO 回写。

## 2. 设计回链

1. `docs/architecture/DASALL_access子系统详细设计.md` 已冻结 `ACC-GATE-11`：shared streaming lifecycle 未冻结前，`StreamGateway / WS / MQTT` 只能保持 feature flag default-off、disabled/not ready，并统一回退到 async receipt + poll。
2. `docs/ssot/BinaryEntrypointReadinessV1.md` 已明确 `Gate-INT-08` 不覆盖 streaming / async receipt / multi-agent readiness；因此若系统 Gate 缺少 unary-only 明文，review 很容易把 focused ingress 误读成 stream-ready。
3. `apps/gateway/src/HttpProtocolAdapter.h` 与 `apps/gateway/src/main.cpp` 已把 gateway v1 固定为 unary POST + accepted async receipt（无 WebSocket/MQTT）；本轮只把这些已有代码信号与 SSOT / TODO 做一致化锁定。
4. `HttpProtocolAdapterTest` 已有 websocket reject regression，因此本轮新增的自动化重点应是 wording guard，而不是重复引入一套 streaming runtime 实现。

## 3. 实现摘要

1. 新增 `tests/integration/access/AccessStreamingDeferralWordingGuardIntegrationTest.cpp`。
   - 扫描 `docs/ssot/SystemIntegrationGateMatrix.md`，要求 `Gate-INT-08` 同时出现 `Access v1 unary production ingress gate`、`ACC-GATE-11`、`feature flag default-off` 与 `StreamGateway / WS / MQTT`。
   - 扫描 `docs/todos/access/DASALL_access子系统专项TODO.md`，要求 unary/not-stream-ready 与 `ACC-GATE-11` 的边界继续存在。
   - 扫描 `apps/gateway/src/HttpProtocolAdapter.h` 与 `apps/gateway/src/main.cpp`，要求 gateway source 仍保留 unary-only / 无 WebSocket/MQTT 注释锚点。
2. 更新 `tests/integration/access/CMakeLists.txt`。
   - 注册 `dasall_access_streaming_deferral_wording_guard_integration_test` 与 `AccessStreamingDeferralWordingGuardIntegrationTest`，把这条边界接入 focused integration discoverability。
3. 更新 `docs/ssot/SystemIntegrationGateMatrix.md`。
   - 将 `Gate-INT-08` 明确写成 `Access v1 unary production ingress gate`。
   - 明确 `StreamGateway / WS / MQTT` 继续由 `ACC-GATE-11` 持有，feature flag default-off，`Gate-INT-08` 不得被误读为 streaming-ready。
4. 更新 `docs/todos/access/DASALL_access子系统专项TODO.md` 与顶层总账。
   - 当前结论与 `FULLINT-TODO-006` 回链段落统一改写为：`Gate-INT-08` 只代表 unary focused ingress；streaming 继续留在 `ACC-GATE-11`。
   - 将 `ACC-GAP-004 / ACC-FIX-004` 从开放缺口改为已收口的边界冻结项。

## 4. Design -> Build 映射

| Design 目标 | Build / Validation 落点 |
|---|---|
| `Gate-INT-08` 只代表 Access v1 unary production ingress | `AccessStreamingDeferralWordingGuardIntegrationTest` 扫描 `SystemIntegrationGateMatrix.md` 的 Gate 行 |
| `StreamGateway / WS / MQTT` 继续受 `ACC-GATE-11` 管理，feature flag default-off | `AccessStreamingDeferralWordingGuardIntegrationTest` 扫描 Access TODO 与 gateway source 注释锚点 |
| gateway v1 继续拒绝 stream transport，不把 unary adapter 扩写成 websocket-ready | `HttpProtocolAdapterTest` 保持 `can_handle("gateway", "websocket") == false` |
| 顶层缺口总账不再把 unary Gate 写成 streaming ready | 顶层 `ACC-GAP-004` / `ACC-FIX-004` 回写与 deliverable closeout |

## 5. D Gate

1. 不新增 public ABI、contracts 字段、HTTP route、WS/MQTT listener 或 runtime stream handle。
2. 不宣称 streaming 已实现；如果未来要进入 stream-ready 结论，必须先由 runtime / llm / contracts 共同冻结 shared lifecycle，再单开 listener / replay / auth / reconnect 测试任务。
3. 本轮结论仅说明 unary-not-stream-ready 边界已经冻结，不外推为 app-binary、installed-package、release security hardening 或更高层 release-ready。
4. 不把 qemu / kvm 混入本轮验收。

## 6. 验证结果

1. `Build_CMakeTools(buildTargets=["dasall_access_streaming_deferral_wording_guard_integration_test"])`
   - 结果：通过。
2. 首次执行 `./build/vscode-linux-ninja/tests/integration/access/dasall_access_streaming_deferral_wording_guard_integration_test`
   - 结果：失败；直接暴露 `SystemIntegrationGateMatrix.md` 缺少 `Access v1 unary production ingress gate` 明文，证实本轮缺口确实在 SSOT wording。
3. 修正 `Gate-INT-08` wording 后，再次执行 `./build/vscode-linux-ninja/tests/integration/access/dasall_access_streaming_deferral_wording_guard_integration_test`
   - 结果：通过。
4. `Build_CMakeTools(buildTargets=["dasall_access_http_protocol_adapter_unit_test"])`
   - 结果：通过。
5. `RunCtest_CMakeTools(tests=["AccessStreamingDeferralWordingGuardIntegrationTest","HttpProtocolAdapterTest"])`
   - 结果：命中仓库既有泛化错误 `生成失败`。
6. fallback：
   - `./build/vscode-linux-ninja/tests/integration/access/dasall_access_streaming_deferral_wording_guard_integration_test`
   - `./build/vscode-linux-ninja/tests/unit/access/dasall_access_http_protocol_adapter_unit_test`
   - 结果：2/2 通过。

## 7. 完成判定

1. `ACC-FIX-004` 已完成。
2. `Gate-INT-08` 不再被误读为 streaming-ready；Access v1 对外只有 unary focused ingress 结论。
3. `StreamGateway / WS / MQTT` 继续受 `ACC-GATE-11` 管理，保持 feature flag default-off、disabled/not ready 与 async receipt + poll fallback。
4. 如果后续要宣称 stream-ready，必须单开跨模块 shared lifecycle 冻结与 listener/replay/attach 测试任务；当前结论不外推。