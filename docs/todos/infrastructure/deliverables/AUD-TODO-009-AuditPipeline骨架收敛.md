# AUD-TODO-009 AuditPipeline 骨架收敛

日期：2026-04-03  
任务：AUD-TODO-009  
状态：已完成

## 1. 输入依据

1. [docs/todos/infrastructure/DASALL_infrastructure_audit组件专项TODO.md](docs/todos/infrastructure/DASALL_infrastructure_audit组件专项TODO.md) 已将 `AUD-TODO-009` 定义为“实现 AuditPipeline 主写骨架”，完成标准是让验证通过的事件进入 append-only 主写路径，并使主写失败可被上层捕获。
2. [docs/architecture/DASALL_infra_audit模块详细设计.md](docs/architecture/DASALL_infra_audit模块详细设计.md) 6.2/6.3/6.7/6.8 已冻结 `AuditPipeline` 的职责：只承接 validator 已通过的 `AuditEvent`，执行 append-only 主写，并在写失败时把错误交还上层，不直接负责 fallback 或恢复裁定。
3. `AUD-TODO-008` 已完成 internal `AuditValidator`，当前 `AuditService` 已具备统一 write 输入校验入口，可以安全把主写逻辑继续向 `AuditPipeline` 拆分。

## 2. 研究学习结果

### 2.1 本地证据

1. audit 设计要求 `AuditServiceFacade -> AuditValidator -> AuditPipeline -> AuditFallbackPipeline` 顺序固定，009 只能处理主写 append 环节。
2. 当前 `AuditServiceFallbackTest` 已覆盖主写成功、主写转 fallback 和 fallback exhaustion，因此 009 的重构必须在不破坏既有 service 行为的前提下完成。
3. `AuditPipeline` 暂时不需要暴露 public interface；internal 骨架即可满足“append-only 主写 + 上层可捕获失败”的最小目标。

### 2.2 外部参考

1. OWASP Logging Cheat Sheet 强调审计/安全日志应与普通日志分离，并要求显式测试日志写入故障、容量耗尽或文件系统不可用等场景；这支持把主写 append 路径单独建模，并把容量耗尽视为结构化失败，而不是在 service 里隐式吞没。

### 2.3 可落地启发

1. `AuditPipeline` 首版只应建模 append-only record store 与容量耗尽，不把 fallback 细节提前塞进去。
2. pipeline 的失败结果要保留 `AuditErrorCode::WriteFail` 和可定位的 stage/message，方便后续 011 facade 统一错误映射。
3. 现阶段最稳妥的落法是让 pipeline 作为 internal helper 操作现有 primary record store，避免为了 009 提前改 public `AuditService` 头文件。

## 3. Design 原子清单

| D 子项 | 设计目标 | 输入依据 | 产出 | 完成判定 |
|---|---|---|---|---|
| D1 | 冻结主写 append-only 的 internal 输入/输出 | audit 设计 6.2/6.3/6.7 | `AuditPipelineWriteResult`、`AuditPipeline` | pipeline 只负责 append，不承接 fallback |
| D2 | 收敛容量耗尽与未绑定存储两类失败 | audit 设计 6.8；AuditErrors | internal 主写失败分类 | 主写失败可由上层捕获并继续走降级路径 |
| D3 | 锁定 Build 三件套 | 009 任务行；现有 unit 测试面 | 代码目标、测试目标、验收命令 | Build 前已有明确验证出口 |

## 4. D Gate 结论

### 4.1 Design -> Build 映射

| Design 结论 | Build 落地 |
|---|---|
| 主写 append-only 路径独立成 internal helper | 新增 [infra/src/audit/AuditPipeline.h](infra/src/audit/AuditPipeline.h) 与 [infra/src/audit/AuditPipeline.cpp](infra/src/audit/AuditPipeline.cpp) |
| `AuditService` 在 validator 之后只委托 pipeline 做主写 append | 更新 [infra/src/audit/AuditService.cpp](infra/src/audit/AuditService.cpp) ，移除主写 vector/capacity 直接判断 |
| 009 的单测证据仍复用 `AuditServiceFallbackTest` | 更新 [tests/unit/infra/AuditServiceFallbackTest.cpp](tests/unit/infra/AuditServiceFallbackTest.cpp)，新增主写 append-only 顺序正例 |

### 4.2 Build 三件套

1. 代码目标：新增 `infra/src/audit/AuditPipeline.h/.cpp`，并把 `AuditService` 的主写 append 切到 pipeline。
2. 测试目标：扩展 `AuditServiceFallbackTest`，覆盖主写 append-only 顺序和容量内成功路径，同时保持 fallback 回归不变。
3. 验收命令：
   - `cmake --build build-ci --target dasall_infra dasall_audit_service_fallback_unit_test`
   - `ctest --test-dir build-ci -N -R "AuditServiceFallbackTest"`
   - `ctest --test-dir build-ci -R "AuditServiceFallbackTest" --output-on-failure`

### 4.3 D Gate

结论：PASS。

理由：

1. pipeline 的 internal 边界、失败分类和测试出口都已明确，不依赖额外 blocker。
2. 本轮仍然只处理主写 append 骨架，没有跨到 fallback 或 facade 任务。
3. public `AuditService` 头文件和 `IAuditLogger` 对外契约没有变化。

## 5. Build 合规提醒

1. `AuditPipeline` 只允许执行主写 append，不得提前实现 fallback、metrics 或 health 更新。
2. 测试至少覆盖 1 个正例和 1 个负例；本轮正例为主写 append-only 顺序，负例继续复用既有 fallback exhaustion 回归。
3. `infra/CMakeLists.txt` 只追加 `AuditPipeline.cpp` 的最小 source 接线，不在本轮关闭 `AUD-TODO-016`。

## 6. Build 落地结果

1. 新增 [infra/src/audit/AuditPipeline.h](infra/src/audit/AuditPipeline.h) 与 [infra/src/audit/AuditPipeline.cpp](infra/src/audit/AuditPipeline.cpp)，定义 internal `AuditPipelineWriteResult` 和 append-only `AuditPipeline`。
2. 更新 [infra/src/audit/AuditService.cpp](infra/src/audit/AuditService.cpp)，在 validator 通过后改为实例化 `AuditPipeline` 执行主写 append。
3. 更新 [infra/CMakeLists.txt](infra/CMakeLists.txt)，将 `AuditPipeline.cpp` 纳入 `dasall_infra` 构建图。
4. 更新 [tests/unit/infra/AuditServiceFallbackTest.cpp](tests/unit/infra/AuditServiceFallbackTest.cpp)，补充主写 append-only 顺序正例，并保持 fallback 回归断言不变。

## 7. 验证结果

1. `ctest --test-dir build-ci -N -R "AuditServiceFallbackTest"`：发现 1 个定向测试。
2. `ctest --test-dir build-ci -R "AuditServiceFallbackTest" --output-on-failure`：1/1 通过。
3. `dasall_infra` 与 `dasall_audit_service_fallback_unit_test` 定向构建通过。

## 8. 结论

1. `AUD-TODO-009` 已从“主写逻辑仍堆在 service 中”推进到“独立 AuditPipeline append-only 骨架已落盘并可回归验证”。
2. 主写 append-only 路径已经独立，但 fallback 仍留在 `AuditService` 中，符合 009 只做单目标拆分的边界。
3. 下一顺位任务应进入 `AUD-TODO-010`，把降级写入链路从 `AuditService` 继续拆分到独立 `AuditFallbackPipeline`。