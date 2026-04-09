# DASALL 工程协作与编码规范

## 1. 文档目的

本文档定义 DASALL 工程在阶段 A 起必须遵守的基础协作约定，覆盖以下内容：

- 编码规范
- 命名规范
- 分支策略
- 提交流程
- Pull Request 要求

该文档用于降低多人协作时的风格漂移、Review 成本和返工概率。

---

## 2. 适用范围

适用于以下目录：

- apps/
- contracts/
- runtime/
- cognition/
- llm/
- tools/
- memory/
- knowledge/
- services/
- multi_agent/
- platform/
- infra/
- tests/
- cmake/
- scripts/
- docs/

---

## 3. 编码规范

## 3.1 通用要求

1. 语言标准统一使用 C++20。
2. 源码文件使用 UTF-8 编码，换行使用 LF。
3. 缩进统一为 2 个空格，不使用 Tab。
4. 每个文件只承担单一主要职责，避免“超大文件”。
5. 不在未冻结接口前过度抽象，不做提前复杂化设计。

## 3.2 头文件与源码文件约定

1. 对外接口放在各模块 include/ 下。
2. 具体实现放在各模块 src/ 下。
3. 跨模块暴露的头文件路径统一使用：
   - 以模块 include/ 根为起点的稳定相对子路径，例如 `logging/ILogger.h`、`policy/PolicyTypes.h`、`linux/LinuxPlatformFactory.h`
   - 若头文件位于模块 include/ 根目录，可直接使用文件名，例如 `IInfrastructureService.h`、`IExecutionService.h`
   - 不再引入 `include/dasall/<module_name>/...` 这类额外模块目录嵌套
4. 头文件必须使用 `#pragma once`。
5. `.h` 用于接口、契约、轻量声明；`.cpp` 用于实现。

## 3.3 include 规则

1. include 顺序建议：
   - C/C++ 标准库
   - 第三方库
   - DASALL 本模块头文件
   - DASALL 跨模块头文件
2. 禁止使用相对路径跨模块 include，例如：
   - 不允许 `../runtime/...`
3. 跨模块依赖必须经过冻结接口，不直接 include 其他模块实现细节。

## 3.4 类与函数设计约定

1. 类职责保持单一，避免控制流、业务逻辑、I/O、状态存储混在同一类。
2. 函数长度优先控制在可读范围内，单个函数通常不超过 80 行；超出时应拆分。
3. 高风险动作、外部副作用、状态转换逻辑必须显式命名，不能隐藏在工具函数中。
4. 公共接口优先返回可判定成功/失败的结果，不依赖隐式异常表达业务失败。

## 3.5 注释约定

1. 注释只解释“为什么”，不解释显而易见的“做了什么”。
2. 接口头文件中的核心类型和关键字段应有简要语义说明。
3. 临时注释、调试注释、失效 TODO 不允许长期保留。
4. TODO 格式统一：
   - `TODO(dasall, owner, yyyy-mm-dd): description`

## 3.6 错误处理约定

1. 模块边界处必须保留明确错误语义。
2. 错误码、错误对象、失败原因优先通过 contracts/ 统一定义。
3. 禁止吞错；捕获异常后必须转换为可观测结果或日志。
4. 高风险失败必须能被 trace、metric 或 audit 感知。

## 3.7 测试约定

1. 新增公共接口时，应同步增加至少一个 unit 或 contract 测试。
2. 测试标签沿用：
   - `unit`
   - `contract`
3. 在 contracts 冻结前，tests/mocks 作为脚手架层存在，避免强绑定生产接口。
4. tests/mocks/include 作为测试支撑头文件的扁平 include 根；mock 头文件直接以 `MockExecutionService.h`、`MockLLMAdapter.h`、`MockMemoryStore.h`、`MockTool.h` 引用，断言辅助位于 `support/TestAssertions.h`，不再使用旧的嵌套 include 前缀。
5. 测试代码允许适度重复，优先可读性和意图表达。

---

## 4. 命名规范

## 4.1 目录命名

1. 顶层模块目录使用小写蛇形或小写单词：
   - `multi_agent`
   - `third_party`
2. 测试目录与产品目录保持镜像结构。

## 4.2 文件命名

1. C++ 头文件、实现文件使用 PascalCase：
   - `AgentOrchestrator.h`
   - `ModelRouter.cpp`
2. 测试文件使用 `*Test.cpp`：
   - `RuntimeSmokeTest.cpp`
3. CMake 模块文件使用 PascalCase：
   - `DASALLOptions.cmake`
4. 文档文件建议使用清晰中文名或稳定英文名，避免“最终版”“最新”之类命名。

## 4.3 类型命名

1. 类、结构体、枚举、类型别名使用 PascalCase。
2. 接口统一使用 `I` 前缀：
   - `IAgent`
   - `ILLMManager`
3. 枚举值使用 PascalCase 或全大写风格，但同一枚举内必须一致。

## 4.4 函数命名

1. 函数、方法使用 lower_snake_case。
2. 布尔判断函数优先使用可读语义：
   - `is_ready()`
   - `has_budget()`
   - `needs_clarification()`
3. 工厂函数、构建函数建议使用：
   - `create_*`
   - `build_*`
   - `make_*`

## 4.5 变量命名

1. 普通变量使用 lower_snake_case。
2. 成员变量统一以 `_` 结尾：
   - `session_id_`
   - `call_count_`
3. 常量使用 `kPascalCase`：
   - `kDefaultTimeoutMs`
4. 宏仅用于必须依赖预处理器的场景，命名使用全大写下划线。

## 4.6 命名空间约定

1. 主命名空间统一使用：
   - `dasall::<module_name>`
2. 测试辅助命名空间使用：
   - `dasall::tests::...`

---

## 5. 分支策略

## 5.1 主分支定义

1. `main`：始终保持可构建、可通过 CI 的主干分支。
2. 不允许直接在 `main` 上提交日常开发代码。

## 5.2 分支命名规范

开发分支统一采用：

- `feature/<module>-<topic>`
- `fix/<module>-<topic>`
- `refactor/<module>-<topic>`
- `docs/<topic>`
- `test/<module>-<topic>`
- `chore/<topic>`
- `release/<version>`

示例：

- `feature/contracts-agent-request`
- `fix/runtime-timeout-budget`
- `docs/stage-a-guidelines`
- `test/tools-policy-gate`

## 5.3 分支使用规则

1. 一个分支聚焦一个主题，不混入无关改动。
2. 大任务拆分为多个小分支，小步合并。
3. 合并前必须通过对应 CI。
4. 长周期分支应定期同步 `main`，避免积累大冲突。

---

## 6. 提交流程

## 6.1 提交格式

提交信息统一采用：

`type(scope): summary`

支持的 `type`：

- `feat`
- `fix`
- `refactor`
- `test`
- `docs`
- `build`
- `ci`
- `chore`

示例：

- `feat(contracts): add agent request skeleton`
- `fix(runtime): correct budget controller timeout path`
- `test(memory): add working memory smoke test`
- `docs(process): add engineering collaboration guide`

## 6.2 提交内容要求

1. 单次提交只做一类变更，避免混合“重构 + 功能 + 格式化”。
2. 提交前至少完成本地构建或相关测试。
3. 不提交编译产物、临时文件、日志文件、编辑器缓存。
4. 不把大规模格式化与功能改动混在一个提交里。

## 6.3 推荐提交流程

1. 从 `main` 切出主题分支。
2. 本地开发并运行相关脚本：
   - `bash scripts/ci/build.sh`
   - `bash scripts/ci/unit_tests.sh`
   - `bash scripts/ci/contract_tests.sh`
3. 自检变更范围。
4. 提交清晰、聚焦的 commit。
5. 发起 PR，等待 Review 和 CI 通过。

---

## 7. Pull Request 约定

1. PR 必须说明变更目的、影响范围、验证方式。
2. PR 应附带：
   - 关联阶段/任务
   - 测试结果
   - 风险点
   - 是否影响 contracts/
3. 涉及以下内容时，必须重点 Review：
   - contracts/
   - runtime/
   - tools/ 中的执行治理链路
   - profile 裁剪逻辑
4. 单个 PR 优先保持在可有效审查范围内；过大时应拆分。

---

## 8. 阶段 A 到阶段 B 的特殊约束

1. 在 contracts 冻结前，不允许跨模块直接依赖未来实现细节。
2. 优先建立契约、测试基座、构建规则，不提前写重业务逻辑。
3. mocks 允许先以 scaffold 方式存在，待阶段 B 后逐步替换为正式接口 mock。

---

## 9. 规范落地文件

本仓库当前通过以下文件辅助执行规范：

- `/home/gangan/DASALL-OS/.editorconfig`
- `/home/gangan/DASALL-OS/.clang-format`
- `/home/gangan/DASALL-OS/.gitmessage.txt`
- `/home/gangan/DASALL-OS/.github/pull_request_template.md`

新增工程工具链时，应优先复用这些约定，不另起一套风格。
