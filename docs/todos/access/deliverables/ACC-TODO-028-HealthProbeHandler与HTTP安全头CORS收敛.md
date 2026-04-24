# ACC-TODO-028 交付物：HealthProbeHandler 与 HTTP 安全头/CORS Gate 收敛

最近更新时间：2026-04-23
状态：Done

## 任务来源

- TODO：`docs/todos/access/DASALL_access子系统专项TODO.md` ACC-TODO-028
- 前置：ACC-TODO-004（gateway transport 冻结）、ACC-TODO-026（HttpProtocolAdapter + gateway 组合根）

## 代码目标

| 文件 | 说明 |
|------|------|
| `apps/gateway/src/HealthProbeHandler.h` | HealthProbeHandler 公共头：`ProbeResult`、`SecurityConfig`、`HealthProbeHandler` 类、`apply_security_headers()` 自由函数声明 |
| `apps/gateway/src/HealthProbeHandler.cpp` | HealthProbeHandler 实现：handle_live/ready/startup 三探针逻辑；apply_security_headers 写入固定安全头 + CORS 白名单过滤 |
| `apps/gateway/src/main.cpp` | 集成 HealthProbeHandler：GET /health/live、/health/ready、/health/startup 路由；OPTIONS preflight 204；所有响应写入安全头 |
| `apps/gateway/CMakeLists.txt` | 增加 `HealthProbeHandler.cpp` 编译 |

## 测试目标

| 测试文件 | 测试目标名称 | 覆盖点 |
|---------|-------------|--------|
| `tests/unit/access/AccessHealthProbeHandlerTest.cpp` | `dasall_access_health_probe_handler_unit_test` | live 始终 200、ready 503/200、startup 503/200、ready 需要 started+ready 均为 true |
| `tests/unit/access/AccessHttpSecurityHeadersTest.cpp` | `dasall_access_http_security_headers_unit_test` | 固定安全头写入、CORS 默认禁用、白名单命中/拒绝、通配符 fail-closed、X-Request-Id 可选写入 |

## 验收命令与结果

```bash
# 构建测试目标
cmake --build /home/gangan/DASALL/build-ci \
  --target dasall_access_health_probe_handler_unit_test \
           dasall_access_http_security_headers_unit_test \
           dasall_gateway
# → 全部 Linking 成功，零 error

# 运行测试
ctest --test-dir /home/gangan/DASALL/build-ci \
  -R "AccessHealthProbeHandlerTest|AccessHttpSecurityHeadersTest" \
  --output-on-failure
# → 2/2 passed (AccessHealthProbeHandlerTest, AccessHttpSecurityHeadersTest)
```

## 架构约束记录

1. health probe 不经过 Admission pipeline（在 main.cpp 直接路由）。
2. liveness probe 不依赖外部服务（进程存活即 200 OK）。
3. readiness 要求 `set_started(true)` 且 `set_ready(true)` 均已调用。
4. probe 不暴露内部状态细节（无 adapter 列表、registry 大小、queue 深度）。
5. CORS 默认禁用（`cors_allowed_origins` 为空）；通配符 `*` 被 fail-closed 拒绝（不写入 CORS 头）。
6. OPTIONS preflight 独立路由，返回 204，不经过 Admission。
7. 安全头（X-Content-Type-Options/X-Frame-Options/Cache-Control/CSP）对所有响应写入，包括健康探针和拒绝响应。
8. ADR-007/008 边界不变：gateway 不持有 runtime 内部控制权。
