# Copilot 仓库记忆备份

## 备份元数据

- 备份日期：2026-04-02
- 工作区：/home/gangan/DASALL-Agent
- 记忆作用域：/memories/repo/
- 原始存储路径：/home/gangan/.vscode-server/data/User/workspaceStorage/960f5844887adccee3a9027fe9c4b47a-2/GitHub.copilot-chat/memory-tool/memories/repo
- 归档文件：copilot-repo-memory-2026-04-02.tar.gz
- SHA-256：b739ce329e76ebc731ca33ded1924e5ed79b8a23691fef1f808c45503156b9eb

## 快照说明

当前仓库记忆共 2 条，均已写入归档文件，同时以下 Markdown 文本可作为人工恢复冗余。

## 文件快照

### build-validation.md

```md
- DASALL-Agent: VS Code CMake Tools may fail with “无法配置项目”; use `cmake -S . -B build-ci -G Ninja` plus targeted `cmake --build build-ci --target ...` and `ctest --test-dir build-ci ...` as the verified fallback validation path.
```

### infra-header-layout.md

```md
- infra/include 根目录只放子系统级共享契约：IInfrastructureService.h、InfraContext.h、InfraErrorCode.h、LogEvent.h。
- 组件级 public API 统一放各自子目录：audit/、config/、diagnostics/、health/、logging/、metrics/、ota/、plugin/、policy/、secret/、tracing/、watchdog/。
- 不保留根层重复入口、兼容 include 或纯转发 wrapper；调整 public headers 时同步更新 infra/CMakeLists.txt、tests 和 active 设计/TODO 文档。
```

## 手工恢复最小单位

恢复时应至少重建以下两个文件：

- /memories/repo/build-validation.md
- /memories/repo/infra-header-layout.md

如果归档文件不可用，可直接以本文件中的 Markdown 内容为准重建。