# AUD-TODO-010 AuditFallbackPipeline 骨架收敛

日期：2026-04-03  
任务：AUD-TODO-010  
状态：已完成

## 1. 输入依据

1. [docs/todos/infrastructure/DASALL_infrastructure_audit组件专项TODO.md](docs/todos/infrastructure/DASALL_infrastructure_audit组件专项TODO.md) 已将 `AUD-TODO-010` 定义为“实现 AuditFallbackPipeline 降级骨架”，完成标准是主写失败可触发 `fallback_used=true`，且 fallback 失败返回 `INF_E_AUDIT_FALLBACK_FAIL`。
2. [docs/architecture/DASALL_infra_audit模块详细设计.md](docs/architecture/DASALL_infra_audit模块详细设计.md) 6.2/6.3/6.8 已冻结 `AuditFallbackPipeline` 的职责：只承接主写失败后的降级写入，不可静默丢失，失败时要保持 degraded/fatal 可观测。
3. `AUD-TODO-009` 已完成 `AuditPipeline` 主写骨架，当前 service 已具备“主写失败后转入下一环”的稳定落点，可以继续拆出 fallback pipeline。

## 2. 研究学习结果

### 2.1 本地证据

1. audit 设计把 fallback 明确为主写失败后的独立降级链路，而不是 `AuditService` 内的临时分支。
2. 现有 `AuditServiceFallbackTest` 已覆盖 `fallback_used=true` 和 fallback exhaustion 映射到 runtime contracts code，因此 010 的拆分必须保持这些行为不回退。
3. `AuditFallbackPipeline` 暂不需要 public interface；internal helper 足以支撑当前 round 的最小实现。

### 2.2 外部参考

1. OWASP Logging Cheat Sheet 要求对日志写入故障、容量耗尽和存储异常做显式测试，同时强调日志失败不应被静默吞掉；这直接支持将 fallback 容量耗尽保留为结构化失败，并让降级链路本身也具备 append 顺序与容量边界。

### 2.3 可落地启发

1. fallback pipeline 首版只需要建模 degraded record store 与容量耗尽，不要提前塞入 metrics/health/facade 协调逻辑。
2. fallback pipeline 的失败结果应固定收敛到 `AuditErrorCode::FallbackFail`，便于后续 facade 统一错误映射。
3. 现阶段最小实现仍应直接操作现有 fallback record store，避免为了 010 提前重写 public `AuditService` 头文件。

## 3. Design 原子清单

| D 子项 | 设计目标 | 输入依据 | 产出 | 完成判定 |
|---|---|---|---|---|
| D1 | 冻结降级写入的 internal 输入/输出 | audit 设计 6.2/6.3/6.8 | `AuditFallbackWriteResult`、`AuditFallbackPipeline` | fallback pipeline 只负责降级 append |
| D2 | 收敛 fallback 容量耗尽与未绑定存储失败 | audit 设计 6.8；AuditErrors | internal fallback 失败分类 | fallback fail 可被上层映射为 `INF_E_AUDIT_FALLBACK_FAIL` |
| D3 | 锁定 Build 三件套 | 010 任务行；现有 unit 测试面 | 代码目标、测试目标、验收命令 | Build 前已有明确验证出口 |

## 4. D Gate 结论

### 4.1 Design -> Build 映射

| Design 结论 | Build 落地 |
|---|---|
| 降级写入独立成 internal helper | 新增 [infra/src/audit/AuditFallbackPipeline.h](infra/src/audit/AuditFallbackPipeline.h) 与 [infra/src/audit/AuditFallbackPipeline.cpp](infra/src/audit/AuditFallbackPipeline.cpp) |
| `AuditService` 在主写失败后只委托 fallback pipeline 做降级 append | 更新 [infra/src/audit/AuditService.cpp](infra/src/audit/AuditService.cpp)，移除 fallback vector/capacity 直接判断 |
| 010 的单测证据继续复用 `AuditServiceFallbackTest` | 更新 [tests/unit/infra/AuditServiceFallbackTest.cpp](tests/unit/infra/AuditServiceFallbackTest.cpp)，新增 fallback append 顺序正例 |

### 4.2 Build 三件套

1. 代码目标：新增 `infra/src/audit/AuditFallbackPipeline.h/.cpp`，并把 `AuditService` 的降级 append 切到 fallback pipeline。
2. 测试目标：扩展 `AuditServiceFallbackTest`，覆盖 fallback append 顺序正例，同时保留 fallback exhaustion 负例。
3. 验收命令：
   - `cmake --build build-ci --target dasall_infra dasall_audit_service_fallback_unit_test`
   - `ctest --test-dir build-ci -N -R "AuditServiceFallbackTest"`
   - `ctest --test-dir build-ci -R "AuditServiceFallbackTest" --output-on-failure`

### 4.3 D Gate

结论：PASS。

理由：

1. fallback pipeline 的 internal 边界、失败分类和测试出口都已明确，不依赖额外 blocker。
2. 本轮仍然只处理降级写入骨架，没有跨到 facade 统一入口或 exporter/metrics/health。
3. public `AuditService` 与 `IAuditLogger` 对外契约没有变化。

## 5. Build 合规提醒

1. `AuditFallbackPipeline` 只允许执行降级 append，不得提前实现 metrics、health 或 facade 编排。
2. 测试至少覆盖 1 个正例和 1 个负例；本轮正例为 fallback append 顺序，负例继续复用 fallback exhaustion 回归。
3. `infra/CMakeLists.txt` 只追加 `AuditFallbackPipeline.cpp` 的最小 source 接线，不在本轮关闭 `AUD-TODO-016`。

## 6. Build 落地结果

1. 新增 [infra/src/audit/AuditFallbackPipeline.h](infra/src/audit/AuditFallbackPipeline.h) 与 [infra/src/audit/AuditFallbackPipeline.cpp](infra/src/audit/AuditFallbackPipeline.cpp)，定义 internal `AuditFallbackWriteResult` 和 `AuditFallbackPipeline`。
2. 更新 [infra/src/audit/AuditService.cpp](infra/src/audit/AuditService.cpp)，在主写失败后改为实例化 `AuditFallbackPipeline` 执行降级 append。
3. 更新 [infra/CMakeLists.txt](infra/CMakeLists.txt)，将 `AuditFallbackPipeline.cpp` 纳入 `dasall_infra` 构建图。
4. 更新 [tests/unit/infra/AuditServiceFallbackTest.cpp](tests/unit/infra/AuditServiceFallbackTest.cpp)，补充 fallback append 顺序正例，并保持 fallback exhaustion 回归断言不变。

## 7. 验证结果

1. `ctest --test-dir build-ci -N -R "AuditServiceFallbackTest"`：发现 1 个定向测试。
2. `ctest --test-dir build-ci -R "AuditServiceFallbackTest" --output-on-failure`：1/1 通过。
3. `dasall_infra` 与 `dasall_audit_service_fallback_unit_test` 定向构建通过。

## 8. 结论

1. `AUD-TODO-010` 已从“降级写入仍堆在 service 中”推进到“独立 AuditFallbackPipeline 骨架已落盘并可回归验证”。
2. fallback append 顺序和 fallback exhaustion 行为都已保留，但 facade 编排仍留给 011 处理，符合 010 只做单目标拆分的边界。
3. 下一顺位任务应进入 `AUD-TODO-011`，将 validator/pipeline/fallback 串成统一的 AuditServiceFacade 入口骨架。