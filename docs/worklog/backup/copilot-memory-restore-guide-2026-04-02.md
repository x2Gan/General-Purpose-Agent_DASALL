# Copilot 记忆备份与恢复教程

## 适用范围

本教程针对当前环境中的 Linux + VS Code Server 场景：Copilot 记忆实际存放在 ~/.vscode-server/data/User/ 下。

如果新设备使用的是本地桌面版 VS Code 而不是 VS Code Server，请先将路径前缀替换为本地 VS Code 的 User 数据目录，再执行相同思路。

## 本次备份产物

- copilot-repo-memory-2026-04-02.tar.gz
  - 仓库记忆归档包
- copilot-user-memory-2026-04-02.tar.gz
  - 用户记忆归档包
- copilot-repo-memory-backup-2026-04-02.md
  - 仓库记忆 Markdown 冗余
- copilot-user-memory-backup-2026-04-02.md
  - 用户记忆 Markdown 冗余
- copilot-session-record-2026-04-02.md
  - 本次会话记录汇总

## 恢复方案总览

推荐优先级如下：

1. 优先恢复 tar 包，速度快且保留原始文件结构。
2. 如果 tar 包不可用，使用本目录中的 Markdown 冗余文档进行人工恢复。
3. 仓库长期约束建议再同步沉淀到仓库内的版本化说明文件，避免再次完全依赖本机 storage。

## 方案一：通过 tar 包恢复用户记忆

### 目标路径

用户记忆目标目录：

```bash
~/.vscode-server/data/User/globalStorage/github.copilot-chat/memory-tool/
```

### 恢复步骤

1. 在新设备打开任意一个 VS Code 会话，让 Copilot Chat 初始化一次。
2. 确认目标目录已存在；不存在时手动创建。
3. 在目标目录执行解包，归档内会恢复 memories/preferences.md。

示例命令：

```bash
mkdir -p ~/.vscode-server/data/User/globalStorage/github.copilot-chat/memory-tool
tar -C ~/.vscode-server/data/User/globalStorage/github.copilot-chat/memory-tool -xzf docs/worklog/backup/copilot-user-memory-2026-04-02.tar.gz
```

### 恢复后验证

```bash
cat ~/.vscode-server/data/User/globalStorage/github.copilot-chat/memory-tool/memories/preferences.md
```

或直接在 Copilot Chat 中发出恢复校验指令：

```text
请查看 /memories/preferences.md，并确认其中包含备份文档中的两条用户偏好。
```

## 方案二：通过 tar 包恢复仓库记忆

### 目标路径

仓库记忆位于某个 workspaceStorage 哈希目录下：

```bash
~/.vscode-server/data/User/workspaceStorage/<workspace-hash>/GitHub.copilot-chat/memory-tool/
```

其中 <workspace-hash> 在新设备上可能变化，因此不要硬编码旧主机的哈希值。

### 恢复步骤

1. 在新设备中先打开一次目标仓库 /home/gangan/DASALL-Agent。
2. 让 Copilot Chat 在该仓库里至少运行一次，确保对应的 memory-tool 目录被创建。
3. 定位最近生成或最近变更的 GitHub.copilot-chat/memory-tool 目录。
4. 将 tar 包解压到该目录，归档内会恢复 memories/repo/ 下的文件。

示例定位命令：

```bash
find ~/.vscode-server/data/User/workspaceStorage -path '*/GitHub.copilot-chat/memory-tool' -type d -printf '%TY-%Tm-%Td %TH:%TM:%TS %p\n' | sort -r | head
```

示例恢复命令：

```bash
tar -C ~/.vscode-server/data/User/workspaceStorage/<workspace-hash>/GitHub.copilot-chat/memory-tool -xzf docs/worklog/backup/copilot-repo-memory-2026-04-02.tar.gz
```

### 恢复后验证

```bash
find ~/.vscode-server/data/User/workspaceStorage/<workspace-hash>/GitHub.copilot-chat/memory-tool/memories/repo -maxdepth 1 -type f -print
```

或在 Copilot Chat 中发出恢复校验指令：

```text
请列出 /memories/repo/ 下的文件，并读取 build-validation.md 与 infra-header-layout.md，确认内容与 docs/worklog/backup 下的备份文档一致。
```

## 方案三：根据 Markdown 冗余文档人工恢复

当 tar 包丢失、损坏，或者新设备上的 workspaceStorage 目录难以精确定位时，直接用本目录中的 Markdown 冗余文档恢复最稳。

### 恢复用户记忆的 Copilot 指令

```text
请读取 docs/worklog/backup/copilot-user-memory-backup-2026-04-02.md，并将其中的内容恢复到 /memories/preferences.md。
如果文件已存在，请只补齐缺失内容，不要删除无关偏好。
```

### 恢复仓库记忆的 Copilot 指令

```text
请读取 docs/worklog/backup/copilot-repo-memory-backup-2026-04-02.md，并按文档内容恢复 /memories/repo/build-validation.md 和 /memories/repo/infra-header-layout.md。
如果文件已存在，请以备份文档为准校对并补齐缺失项，不要覆盖无关仓库记忆。
```

## 何时优先用 Markdown 冗余恢复

- 新设备上 workspaceStorage 哈希目录难以快速定位。
- 只想恢复核心偏好与仓库事实，不追求原始归档的完全一致性。
- 希望在恢复前先人工检查、裁剪、清洗旧记忆内容。

## 校验归档完整性

恢复前可先校验归档包：

```bash
sha256sum docs/worklog/backup/copilot-repo-memory-2026-04-02.tar.gz
sha256sum docs/worklog/backup/copilot-user-memory-2026-04-02.tar.gz
```

期望值：

- copilot-repo-memory-2026-04-02.tar.gz
  - b739ce329e76ebc731ca33ded1924e5ed79b8a23691fef1f808c45503156b9eb
- copilot-user-memory-2026-04-02.tar.gz
  - a1bda080942fee944f9f2c6dae2186c332a235acd33e9104bee93c795bfb3da8

## 建议的迁移顺序

1. 先把本目录完整带到新设备。
2. 先恢复用户记忆，再恢复仓库记忆。
3. 完成后让 Copilot 读取并验证 /memories/ 与 /memories/repo/。
4. 对于长期稳定的规则，再补一份仓库内版本化说明文件，降低未来再次迁移的摩擦。