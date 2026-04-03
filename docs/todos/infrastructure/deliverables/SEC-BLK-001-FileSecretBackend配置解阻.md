# SEC-BLK-001 FileSecretBackend 配置解阻

日期：2026-04-03
任务：SEC-BLK-001
状态：解阻 PASS

## 1. 本地证据

1. docs/todos/infrastructure/DASALL_infrastructure_secret组件专项TODO.md 将 SEC-TODO-007 标记为 Blocked，根因明确为“file backend 加密与根目录策略未冻结”。
2. docs/architecture/DASALL_infra_secret模块详细设计.md 6.9 已冻结 `infra.secret.file.root_dir = secrets/`，覆盖层级为部署；同时冻结 `infra.secret.file.encrypt_at_rest = true`，覆盖层级为默认/Profile。
3. 同一设计文档 6.2/6.7/6.9 已明确 file backend 只负责 metadata fetch 与受控 materialize，不允许写明文临时文件，也不允许绕过 zeroize/release 语义。
4. 当前仓库里尚无 secret file backend 实现源码，因此 blocker 的真实缺口不是“实现不可行”，而是 TODO 中仍保留旧的“策略未冻结”状态，没有把既有设计证据回链到 SEC-TODO-007。

## 2. 外部参考

1. OWASP Secrets Management Cheat Sheet 建议将 secret 生命周期、最小权限、审计和内存暴露窗口显式建模，并强调 secrets at rest 应受加密保护、plaintext in memory 的时间窗要尽量缩短；这支持 DASALL 继续保持 `encrypt_at_rest=true` 的最小默认策略，并把 file backend 限定为受控 materialize + release/zeroize。
2. Azure Key Vault best practices 明确建议不要把 secret store 当作普通配置存储，同时建议按安全边界拆分 secret 存储并开启自动轮换/审计；这支持 DASALL 将 file backend 的根目录和静态加密策略独立为 secret 专属配置，而不是与普通 config 混放。

## 3. 阻塞修复与设计结论

阻塞结论：

1. SEC-BLK-001 已具备解阻条件。`file.root_dir` 与 `encrypt_at_rest` 的最小策略已经在 secret 详细设计 6.9 冻结，且覆盖层级已经写明为部署与默认/Profile，不再存在“策略未知导致无法实现”的上下文阻塞。

最小 blocker-fix：

1. 将 secret 专项 TODO 中的 SEC-BLK-001 改写为“已解阻（2026-04-03）”，把证据回链到 secret 详细设计 6.9 与本交付件。
2. 将 SEC-TODO-007 的 blocker 列从 `SEC-BLK-001` 切换为已解阻说明，允许 FileSecretBackend 直接进入实现轮。
3. 保持当前范围只做 blocker 状态收敛，不提前落盘 FileSecretBackend.cpp 或 profile 运行时实现。

设计结论：

1. `infra.secret.file.root_dir` 固定为部署层可覆盖项，默认相对根目录为 `secrets/`。
2. `infra.secret.file.encrypt_at_rest` 固定为默认开启，可由 Profile 显式裁剪，但本轮 FileSecretBackend 最小骨架按 `true` 语义实现和测试。
3. FileSecretBackend v1 只读取受控 root_dir 下的密文记录，不创建明文临时文件，不把 file path 或 plaintext 字段暴露到公共对象。
4. 若后续实现回退为明文存储、允许越过 root_dir 读取任意路径，或把 `encrypt_at_rest` 默认值改为 `false`，则 SEC-BLK-001 需要重新转回 Blocked。

## 4. Design -> Build 映射

| Design 项 | Build 落点 |
|---|---|
| 冻结 `infra.secret.file.root_dir` 部署层约束 | infra/src/secret/backends/FileSecretBackend.cpp |
| 冻结 `infra.secret.file.encrypt_at_rest=true` 最小默认策略 | infra/src/secret/backends/FileSecretBackend.cpp |
| 禁止 file backend 暴露明文路径和临时文件 | tests/unit/infra/secret/FileSecretBackendTest.cpp |
| 把 blocker 状态回链到 TODO 和后续执行轮 | docs/todos/infrastructure/DASALL_infrastructure_secret组件专项TODO.md |

## 5. 对 SEC-TODO-007 的直接交接

1. SEC-TODO-007 可以从“受 SEC-BLK-001 阻塞”转为“可执行”，并按 root_dir/encrypt_at_rest 的既有设计语义落盘 FileSecretBackend 最小骨架。
2. 后续实现只允许支持：
   - root_dir 边界约束
   - encrypted-at-rest 默认开启语义
   - metadata fetch + controlled materialize
   - backend unavailable 和 invalid path 的明确错误码
3. 后续实现不得：
   - 写明文临时文件
   - 越过 root_dir 访问任意文件
   - 把物理路径细节注入 contracts 或公共 metadata 对象

## 6. 风险与回退

1. 若后续 FileSecretBackend 用测试便利性为理由绕过 `encrypt_at_rest=true` 约束，本 blocker 需要重新打开。
2. 若 profile/deployment 层后来引入更细粒度的 file backend 策略，应以追加配置方式演进，不能破坏现有 root_dir/encrypt_at_rest 语义。
3. 本轮只解阻设计与 TODO 状态，不提前落盘 file backend 实现；真正的构建和单测验收留给 SEC-TODO-007。