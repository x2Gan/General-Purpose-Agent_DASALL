# KNO-TODO-005 knowledge 公共 include 与测试/CMake 骨架设计收敛

- 日期：2026-04-21
- 任务：KNO-TODO-005
- 状态：已收敛
- 对应 Blocker：无

## 1. 输入与约束

1. 当前 knowledge 模块已经进入顶层构建图，但仍停留在 `placeholder.cpp + 空 tests` 组合；如果不先建立 include 根、file set 与 discoverability 拓扑，`KNO-TODO-006`、`007`、`025` 都会被迫在实现期回头修改目录、CMake 和测试聚合链。
2. repo memory `infra-header-layout` 已固定“根层 include 只放子系统级共享契约；组件级 public API 放子目录，不保留重复 wrapper”的规则，因此 005 必须先把 knowledge 的 root public headers 落到 `knowledge/include/`，而不是继续把 public 面散落在 `src/` 或临时测试头里。
3. SSOT `InfraIntegrationTopology` 与专项 TODO 9.3 已要求新增核心链路至少拥有 1 条可 discover 的 integration smoke；005 的目标不是证明 retrieval 已可用，而是让 `ctest -N` 可以稳定发现 knowledge 的 unit / integration 挂点。
4. 本轮必须保持边界：不提前冻结 `IKnowledgeService::init/retrieve/health_snapshot/request_refresh` 的正式签名，也不提前定义 `KnowledgeConfigProjector` 或 `KnowledgeTelemetry` 的字段语义；这些都属于 006/007/025。

## 2. 本地证据

| 证据 | 观察结果 | 结论 |
|---|---|---|
| `knowledge/CMakeLists.txt` 初始状态 | 仅编译 `src/placeholder.cpp`，public include 指向尚不存在的 `knowledge/include` | 模块已入构建图，但仍是 placeholder-only |
| `tests/unit/knowledge/CMakeLists.txt` 初始状态 | 仅有注释占位 | knowledge unit discoverability 为 0 |
| `tests/integration/CMakeLists.txt` 初始状态 | 未接入 `knowledge` 子目录 | knowledge integration discoverability 为 0 |
| tools/memory 现有拓扑 | 已采用“子目录 CMake 注册宏 + 导出 target 列表到顶层”的模式 | 005 应复用该模式，而不是在顶层硬编码 knowledge 单测/集成细节 |

## 3. 设计结论

### 3.1 knowledge 模块骨架

1. `knowledge/CMakeLists.txt` 切换为显式 `public_headers` file set，先把 root public headers 锚定到：
   - `knowledge/include/KnowledgeTypes.h`
   - `knowledge/include/KnowledgeErrors.h`
   - `knowledge/include/IKnowledgeService.h`
2. 编译锚点由 `knowledge/src/placeholder.cpp` 改为 `knowledge/src/KnowledgeBuildSkeleton.cpp`，该源文件只负责验证 skeleton headers 可编译，不承担任何业务逻辑。
3. 005 只建立最小 skeleton declarations：
   - `KnowledgeTypes.h` 先落类型名与空结构骨架；
   - `KnowledgeErrors.h` 先落最小枚举壳；
   - `IKnowledgeService.h` 先落抽象基类骨架；
   后续 006 在同一文件内填充稳定 ABI。

### 3.2 unit / integration discoverability

1. `tests/unit/knowledge/CMakeLists.txt` 必须至少注册 1 个 unit target：`dasall_knowledge_interface_surface_unit_test`。
2. `tests/integration/knowledge/CMakeLists.txt` 必须至少注册 1 个 integration smoke：`KnowledgeIntegrationTopologySmokeTest`。
3. 顶层 `tests/unit/CMakeLists.txt` 与 `tests/integration/CMakeLists.txt` 均通过 `${DASALL_KNOWLEDGE_*_TEST_EXECUTABLE_TARGETS}` 变量聚合 knowledge 子树，避免后续 006~030 每新增一个测试都手工回改顶层列表。
4. `KnowledgeIntegrationTopologySmokeTest` 只验证目录与 CMake 拓扑事实：include 根存在、top-level integration 接线存在、knowledge 模块不再引用 `placeholder.cpp`。它不是功能 smoke，也不替代 retrieval / degrade / observability integration。

### 3.3 禁止事项

1. 禁止在 005 中提前把 `IKnowledgeService` 方法签名、`KnowledgeQuery` 字段或 `KnowledgeErrorCode -> ErrorInfo` 映射写成已冻结事实；这些属于 006。
2. 禁止为了让 `ctest -N` 变绿而增加与 knowledge 无关的空测试；必须让 discoverability 直接锚定 knowledge 自己的 unit/integration 子树。
3. 禁止保留 `placeholder.cpp` 继续作为 knowledge 模块的主编译锚点；005 完成后知识库不应再处于 placeholder-only 状态。

## 4. Design -> Build 映射

| Design 项 | Build / 文档落点 |
|---|---|
| root public headers file set | `knowledge/CMakeLists.txt`、`knowledge/include/KnowledgeTypes.h`、`knowledge/include/KnowledgeErrors.h`、`knowledge/include/IKnowledgeService.h` |
| 编译锚点退出 placeholder-only | `knowledge/src/KnowledgeBuildSkeleton.cpp` |
| knowledge unit discoverability | `tests/unit/knowledge/CMakeLists.txt`、`tests/unit/knowledge/KnowledgeInterfaceSurfaceSkeletonTest.cpp` |
| knowledge integration discoverability | `tests/integration/knowledge/CMakeLists.txt`、`tests/integration/knowledge/KnowledgeIntegrationTopologySmokeTest.cpp` |
| 顶层聚合接线 | `tests/unit/CMakeLists.txt`、`tests/integration/CMakeLists.txt` |
| TODO / worklog 回写 | `docs/todos/knowledge/DASALL_knowledge子系统专项TODO.md`、`docs/worklog/DASALL_开发执行记录.md` |

## 5. 本任务三件套

- 代码目标：建立 knowledge root public headers、替换模块编译锚点、接通 unit/integration discoverability。
- 测试目标：`dasall_knowledge_interface_surface_unit_test` 与 `KnowledgeIntegrationTopologySmokeTest` 均可被 `ctest -N` 发现。
- 验收命令：

```bash
cmake -S . -B build-ci -G "Unix Makefiles" && \
cmake --build build-ci --target dasall_knowledge dasall_unit_tests dasall_integration_tests && \
ctest --test-dir build-ci -N
```

## 6. 风险与回退

1. 风险：005 只建立 skeleton declarations，尚未冻结真实字段与错误语义。
   - 处置：后续 006 只在既有 headers 上增量填充 ABI，不再改目录和 discoverability 拓扑。
2. 风险：如果 integration topology smoke 被误当成功能 smoke，后续可能漏掉 retrieval / config / observability 专项集成用例。
   - 处置：明确将其限定为 topology/discoverability gate，真实功能验证继续拆给 027/028/029/030。
3. 风险：如果继续保留 placeholder-only 源文件，后续每个任务都会反复修改 knowledge/CMakeLists。
   - 处置：005 一次性把编译锚点切换到 `KnowledgeBuildSkeleton.cpp`，后续仅增量追加真实实现源。

## 7. 收敛结论

1. Knowledge 已具备正式 `include/` 根、public header file set 和非-placeholder 编译锚点。
2. `dasall_knowledge_interface_surface_unit_test` 与 `KnowledgeIntegrationTopologySmokeTest` 已为 knowledge 建立最小可发现的 unit/integration 骨架。
3. 005 完成后，006/007/025 的工作重点可以回到 ABI、配置投影和 observability 语义本身，而不再受目录/CMake/discoverability 拓扑牵制。