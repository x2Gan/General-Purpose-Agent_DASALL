# LLM-TODO-001 llm 公共 include 布局设计收敛

日期：2026-04-10
任务：LLM-TODO-001
状态：D Gate PASS / B Gate PASS

## 1. 本地证据

1. [docs/architecture/DASALL_llm子系统详细设计.md](../../../architecture/DASALL_llm子系统详细设计.md) 的 1.4 与 3.1 明确指出 `llm/CMakeLists.txt` 仍只编译 `src/placeholder.cpp`，且 `llm/include/` 当前为空；LLM Build 起步必须先补模块公共 include 根与稳定子目录。
2. [docs/todos/llm/DASALL_llm子系统专项TODO.md](../DASALL_llm子系统专项TODO.md) 将 LLM-TODO-001 明确为阶段 A 的首个串行任务，完成判定是“llm 公共 include 根稳定存在且 `dasall_llm` 可继续构建”。
3. [llm/CMakeLists.txt](../../../../llm/CMakeLists.txt) 在本轮前只通过原始 `${CMAKE_CURRENT_SOURCE_DIR}/include` 暴露 include 根，没有对子目录稳定性做任何显式校验；若目录骨架继续保持“空但未受管”，后续 005~011 的公共头文件任务容易在目录缺失时退化为临时修补。
4. [llm/include](../../../../llm/include) 当前已经作为模块公共根目录存在，但仓库内尚未有 `prompt/`、`provider/`、`route/`、`stream/` 四个子目录的可跟踪占位文件，因此需要把目录骨架显式落盘为本轮交付物，而不是仅依赖本地未跟踪空目录。

## 2. 外部参考

1. CMake 官方 `target_include_directories()` 文档指出，`PUBLIC` usage requirements 会进入目标的 `INTERFACE_INCLUDE_DIRECTORIES`，且应通过 `BUILD_INTERFACE` / `INSTALL_INTERFACE` 分离构建树与安装树的 include 语义。本轮据此把 `dasall_llm` 的公共 include 接线从裸路径收敛为显式的 build/install usage requirements，并保持后续头文件任务可直接复用。参考：https://cmake.org/cmake/help/latest/command/target_include_directories.html

## 3. Design 结论

1. `llm/include/` 继续作为 llm 模块公共 include 根，不新增 `llm/include/llm/` 这类重复嵌套层级；当前阶段只需要建立稳定子目录，不提前落具体接口头文件。
2. 首批稳定子目录固定为 `prompt/`、`provider/`、`route/`、`stream/`，与专项 TODO 中 005~011 的后续头文件落位一一对应，避免后续接口任务再修改目录拓扑。
3. `dasall_llm` 的 CMake 接线需要显式表达 include 根的 build/install 语义，并在配置期对 include 根和四个稳定子目录做 fail-fast 校验，防止“目录误删但构建仍静默通过”的漂移。
4. 本轮只解决公共 include 布局与 CMake 接线，不提前引入任何 llm 公共 ABI、共享 contracts 升格或单测注册逻辑；这些分别留给 LLM-TODO-002 及 005~011。

## 4. Design -> Build 映射

| Design 项 | Build 落点 |
|---|---|
| 建立 llm 公共 include 根的可跟踪骨架 | [llm/include/.gitkeep](../../../../llm/include/.gitkeep) |
| 建立 prompt/provider/route/stream 稳定子目录 | [llm/include/prompt/.gitkeep](../../../../llm/include/prompt/.gitkeep)、[llm/include/provider/.gitkeep](../../../../llm/include/provider/.gitkeep)、[llm/include/route/.gitkeep](../../../../llm/include/route/.gitkeep)、[llm/include/stream/.gitkeep](../../../../llm/include/stream/.gitkeep) |
| 收敛 llm include usage requirements 与目录存在性校验 | [llm/CMakeLists.txt](../../../../llm/CMakeLists.txt) |

## 5. Build 三件套

1. 代码目标：建立 `llm/include/` 公共根的可跟踪目录骨架，新增 `prompt/`、`provider/`、`route/`、`stream/` 四个稳定子目录，并更新 `llm/CMakeLists.txt`。
2. 测试目标：验证 `dasall_llm` 在新 include 布局下仍可稳定构建，且 CMake 在公共 include 根缺失时会在配置期显式失败，而不是把错误推迟到后续接口任务。
3. 验收命令：
   - `Build_CMakeTools` 构建目标 `dasall_llm`

## 6. 风险与回退

1. 本轮只建立目录骨架和 CMake 校验，不应提前在 `.gitkeep` 占位文件中暗示任何公共 ABI；具体接口与 supporting types 继续由 LLM-TODO-005~011 独立收敛。
2. 若后续仓库统一引入 install/export 规则，可在不改变当前 include 根与子目录布局的前提下扩展 `dasall_llm` 的公开头 file set；本轮不提前引入安装规则，避免把“目录骨架任务”扩张成分发方案任务。