# INF-LOG-FIX-003 file / rotating sink adapter 收口

关联任务：INF-LOG-FIX-003  
日期：2026-05-27  
结论级别：L2/L3 build-tree focused evidence

## 1. 本轮目标

在不把具体 backend 类型泄漏到调用方的前提下，先在当前 repo 依赖集里闭合 logging file sink 的最小 owner 行为链：

1. `ILogSink` / `FileLogSink` public seam。
2. build-tree 与 state_root authoritative 路径解析。
3. `runtime.log.<n>` rotation family。
4. 显式不可写路径的 fail-closed sink IO failure。
5. `SinkDispatcher` 的 route -> sink 注入面。

## 2. 改动范围

1. `infra/include/logging/ILogSink.h`
2. `infra/include/logging/FileLogSink.h`
3. `infra/src/logging/FileLogSink.cpp`
4. `infra/src/logging/SinkDispatcher.h`
5. `infra/src/logging/SinkDispatcher.cpp`
6. `infra/CMakeLists.txt`
7. `tests/unit/infra/logging/FileLogSinkTest.cpp`
8. `tests/integration/infra/logging/SinkDispatcherRouteIntegrationTest.cpp`
9. `tests/integration/infra/logging/LoggingSinkFailureInjectionTest.cpp`
10. `tests/unit/CMakeLists.txt`
11. `tests/unit/infra/CMakeLists.txt`
12. `tests/integration/CMakeLists.txt`
13. `tests/integration/infra/logging/CMakeLists.txt`

## 3. 关键结论

1. `ILogSink` 现为调用方唯一 sink seam；后续若替换 internal backend，不需要再改 `LoggingFacade` / `SinkDispatcher` public surface。
2. `FileLogSink` 已支持 build-tree 默认相对路径与 installed/state_root authoritative 路径解析，并保持 `DASALL_STATE_ROOT` 为唯一 state_root override。
3. size budget 超限时，`FileLogSink` 会把 active file 轮转到同目录 `runtime.log.<n>` family。
4. 显式不可写或父目录不可创建路径现在会 fail-closed 返回 sink IO failure，不再 silently fallback 到 repo 根、`/tmp` 或 qemu guest-side 路径。
5. `SinkDispatcher` 已支持 runtime/basic 与 audit route 的独立 sink 注入；未注入 sink 时仍保持 skeleton queue bookkeeping，不把默认行为误写成 live composition ready。

## 4. focused 验证

1. `Build_CMakeTools(buildTargets=["dasall_file_log_sink_unit_test","dasall_sink_dispatcher_unit_test","dasall_sink_dispatcher_route_integration_test","dasall_logging_sink_failure_injection_test"])`
   - 结果：通过。
2. `RunCtest_CMakeTools(tests=["FileLogSinkTest","SinkDispatcherTest","SinkDispatcherRouteIntegrationTest","LoggingSinkFailureInjectionTest"])`
   - 结果：命中仓库既有泛化 `生成失败`。
3. fallback 直接执行：
   - `./build/vscode-linux-ninja/tests/unit/infra/dasall_file_log_sink_unit_test`
   - `./build/vscode-linux-ninja/tests/unit/infra/dasall_sink_dispatcher_unit_test`
   - `./build/vscode-linux-ninja/tests/integration/infra/logging/dasall_sink_dispatcher_route_integration_test`
   - `./build/vscode-linux-ninja/tests/integration/infra/logging/dasall_logging_sink_failure_injection_test`
   - 结果：4/4 通过。

## 5. 非外推说明

1. 本轮只到 L2/L3 build-tree evidence；尚未宣称 async worker、flush deadline、live composition config 或 installed package proof ready。
2. `spdlog-backed file / rotating sink` 仍是 primary backend policy，但当前 public surface 刻意保持 backend-neutral，避免把依赖治理扩成这轮原子任务范围。
3. installed authoritative evidence 与 package smoke artifact 继续留给 `INF-LOG-FIX-011`。