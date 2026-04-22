# Runtime Fixture Root

此目录承载 runtime-owned 的测试资产根。

当前约束：

1. `checkpoints/` 保存 deterministic checkpoint fixtures，供 replay compatibility 与 resume regression 使用。
2. 026/028/029/030 若新增 runtime-local fixture，只能落在此根目录或其子目录，不得散落到 production 路径。
3. 本目录用于 runtime-local gate 和 compatibility gate，不代表 true cross-module integration 资产。