# ACC-TODO-026 交付物：HttpProtocolAdapter 与 gateway 组合根收敛

最近更新时间：2026-04-23
状态：Done

## 任务来源

- TODO：`docs/todos/access/DASALL_access子系统专项TODO.md` ACC-TODO-026
- 前置：ACC-TODO-004（gateway transport 选型冻结）、ACC-TODO-013/018/021/024

## 代码目标

| 文件 | 说明 |
|------|------|
| `apps/gateway/src/HttpProtocolAdapter.h` | HttpProtocolAdapter 公共头：定义 `HttpRequestContext`、`HttpResponseContext`；继承 `IProtocolAdapter` |
| `apps/gateway/src/HttpProtocolAdapter.cpp` | HttpProtocolAdapter 实现：`decode()`（JSON body 解析）、`encode()`（JSON response 序列化）、`hint_to_status_code()` |
| `apps/gateway/src/main.cpp` | Gateway HTTP 组合根：cpp-httplib + AccessGateway + HttpProtocolAdapter；POST `/v1/submit` 路由 |
| `apps/gateway/CMakeLists.txt` | 增加 `HttpProtocolAdapter.cpp` 编译；链接 `cpp-httplib`；PRIVATE include `apps/gateway/src`、`access/src` |
| `cmake/DASALLThirdParty.cmake` | 新增 cpp-httplib `dasall_resolve_dependency()` 调用（local-cache 优先） |
| `third_party/.cache/cpp-httplib/CMakeLists.txt` | cpp-httplib v0.15.3 INTERFACE 目标定义 |
| `third_party/.cache/cpp-httplib/include/httplib.h` | cpp-httplib v0.15.3 单头文件（9441 行） |

## 测试目标

| 测试文件 | 测试目标名称 | 覆盖点 |
|---------|-------------|--------|
| `tests/unit/access/HttpProtocolAdapterTest.cpp` | `dasall_access_http_protocol_adapter_unit_test` | can_handle 匹配/拒绝、decode 空请求/entry_type 提取/默认 peer_ref、encode 填充 HttpResponseContext |
| `tests/unit/access/HttpProtocolAdapterErrorMappingTest.cpp` | `dasall_access_http_protocol_adapter_error_mapping_unit_test` | hint_to_status_code 正常/空/无效、encode 400 路径、decode 不感知 HTTP method |

## 验收命令与结果

```bash
# 构建测试目标
cmake --build /home/gangan/DASALL/build-ci \
  --target dasall_access_http_protocol_adapter_unit_test \
           dasall_access_http_protocol_adapter_error_mapping_unit_test \
           dasall_gateway
# → 全部 Linking 成功，零 error

# 运行测试
ctest --test-dir /home/gangan/DASALL/build-ci \
  -R "HttpProtocolAdapterTest|HttpProtocolAdapterErrorMappingTest" \
  --output-on-failure
# → 2/2 passed (HttpProtocolAdapterTest, HttpProtocolAdapterErrorMappingTest)
```

## 架构约束记录

1. `HttpProtocolAdapter` 属于 `apps/gateway/src` 私有作用域，不暴露为 access 公共头。
2. `main.cpp` 通过 `access/src` PRIVATE include 直接使用 `AccessGateway` 具体类，其余 apps 只见 `IAccessGateway` 接口。
3. cpp-httplib 引入警告（`-Wformat-nonliteral`）来自第三方库内部，不影响 `-Werror` 边界（通过 `SYSTEM` 头路径隔离）。
4. gateway v1 仅支持 HTTP unary POST + accepted async receipt；WebSocket/MQTT 在 Phase A5 延后。
5. 健康探针路径 `/health/*` 在 ACC-TODO-028 中集成。
6. ADR-007/008 边界：gateway 不持有 runtime 内部控制权，submit 链路仅调用 `IAccessGateway::submit()`。
