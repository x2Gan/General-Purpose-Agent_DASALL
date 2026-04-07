# OTA-TODO-015 OTA 构建入口接线收敛

日期：2026-04-07
任务：OTA-TODO-015
状态：已完成

## 1. 输入依据

1. docs/todos/infrastructure/DASALL_infrastructure_ota组件专项TODO.md 将 OTA-TODO-015 定义为“接线 ota 到 infra CMake 构建入口”，完成判定是 ota 文件进入 `dasall_infra`，且 placeholder 不再是唯一源码入口。
2. docs/architecture/DASALL_infra_OTA模块详细设计.md 7/8.1 要求 OTA 以 `infra/src/ota` 私有实现 + `infra/include/ota` 公共接口的形式落入基础设施构建图，不越权扩写 contracts。
3. 当前仓库已在多轮骨架任务中把 OTA 源码逐步加入 `infra/CMakeLists.txt`，但 OTA public headers 仍散落在全局 public header 列表中，缺少任务级统一入口。

## 2. 研究学习结果

### 2.1 本地证据

1. `infra/CMakeLists.txt` 已具备 `DASALL_INFRA_OTA_SOURCES` 与 `DASALL_INFRA_OTA_PRIVATE_HEADERS`，说明 OTA 实现和私有头的聚合入口已经存在，015 的最小缺口是把 public headers 也纳入同一收口面。
2. `dasall_infra` 通过 `target_sources(... PRIVATE ...)` 与 `PUBLIC_HEADER` 属性统一描述库的构建输入和导出头清单，因此新增 OTA public header 列表不需要改变现有库边界。
3. 001~014 已让 OTA public headers 与私有实现同时存在，继续把 OTA 头文件散放在全局列表中，会增加后续 016/017 审查时的接线噪音。

### 2.2 外部参考

1. CMake 官方 `target_sources` 文档说明，源文件应按 target-local scope 明确挂接，重复调用会按声明顺序追加到同一 target；这支持把 OTA 源/私有头维持在独立列表下集中接入，而不是分散到多个无关片段。

### 2.3 可落地启发

1. 015 不需要再发明新的库目标或 profile 分支，只需要在现有 `dasall_infra` 上收敛 OTA public/private 列表即可。
2. 为保持仓库既有风格，应沿用 `PUBLIC_HEADER` 聚合方式，而不是在本轮单独引入新的安装/导出机制。
3. OTA public header 列表独立后，后续 OTA 文件增量能在同一片段完成接线，降低漏配概率。

## 3. Design 原子清单

| D 子项 | 设计目标 | 输入依据 | 产出 | 完成判定 |
|---|---|---|---|---|
| D1 | 冻结 OTA public/private 构建入口边界 | OTA 设计 7/8.1 | infra/CMakeLists.txt | OTA 源、私有头、公共头各有统一列表 |
| D2 | 保持 `dasall_infra` 目标与现有导出风格兼容 | 代码现状 | infra/CMakeLists.txt | 继续复用 `target_sources` 与 `PUBLIC_HEADER` |
| D3 | 锁定 015 Build 三件套 | OTA TODO 015 | 本交付物 + TODO 回写 | 有代码目标、测试目标、验收命令 |

## 4. D Gate 结论

### 4.1 Design -> Build 映射

| Design 结论 | Build 落地 |
|---|---|
| OTA 构建入口要集中治理 | 新增 `DASALL_INFRA_OTA_PUBLIC_HEADERS`，与既有 `DASALL_INFRA_OTA_SOURCES / PRIVATE_HEADERS` 对齐 |
| 不能改写现有 `dasall_infra` 导出模式 | 继续复用 `PUBLIC_HEADER` 属性，而不是引入新的导出机制 |
| 本轮只做库构建接线 | 不触碰 tests 注册与 gate；discoverability 留给 016 |

### 4.2 Build 三件套

1. 代码目标：在 `infra/CMakeLists.txt` 中收敛 OTA public/private 构建入口。
2. 测试目标：确认 `dasall_infra` 仍可完整编译，OTA 接线没有破坏库目标。
3. 验收命令：
   - `cmake -S . -B build-ci -G "Unix Makefiles"`
   - `cmake --build build-ci --target dasall_infra`

### 4.3 D Gate

结论：PASS。

理由：

1. 015 的收口范围仍严格停留在 infra/ota 与 `dasall_infra` 构建入口，没有越界到 tests/runtime/contracts。
2. Build 三件套已经锁定，且验收出口可以通过库目标构建结果二值判定。

## 5. Build 落地结果

1. 在 `infra/CMakeLists.txt` 新增 `DASALL_INFRA_OTA_PUBLIC_HEADERS`，集中列出 `IBootControlAdapter.h`、`IInstallExecutor.h`、`IOTAManager.h`、`IOTAPackageVerifier.h` 与 `OTATypes.h`。
2. 将散落在 `DASALL_INFRA_PUBLIC_HEADERS` 内部的 OTA public headers 替换为 `${DASALL_INFRA_OTA_PUBLIC_HEADERS}`，使 OTA public/private 接线都通过 OTA 专属变量收敛。
3. 保持既有 `DASALL_INFRA_OTA_SOURCES` 和 `DASALL_INFRA_OTA_PRIVATE_HEADERS` 不变，避免引入额外导出机制或组件边界变化。

## 6. Build 合规复核

1. 边界：本轮只收敛 `dasall_infra` 的 OTA 构建入口，不引入新的 public contract，也不调整 OTA 源码行为。
2. 根因处理：解决的是 OTA public header 分散在全局清单中的维护噪音，而不是用 TODO 回写掩盖接线结构欠清晰的问题。
3. 测试出口：015 以 `dasall_infra` 构建成功作为二值出口；测试 discoverability 和标签一致性将在 016 单独处理。
4. 兼容性：保留现有 `PUBLIC_HEADER` 风格，未改变 install/export 方式。

## 7. 验证结果

1. `cmake -S . -B build-ci -G "Unix Makefiles"`：通过。
2. `cmake --build build-ci --target dasall_infra`：通过。

## 8. 结论

1. OTA-TODO-015 已完成，OTA public/private 头文件与实现源现在通过 OTA 专属列表统一接入 `dasall_infra`。
2. 015 完成后，后续 016 可以只关注测试入口、聚合目标与 discoverability，不再重复处理 OTA 库构建入口问题。