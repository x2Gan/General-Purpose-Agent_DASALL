# MEM-FIX-007 installed maintenance 正向证据收口

日期：2026-05-18
来源任务：MEM-FIX-007
范围：本机 installed-package maintenance authoritative evidence；不使用 qemu / kvm 作为本轮收口证据

## 1. 任务重定义

本轮没有把 `MEM-FIX-007` 扩写成新的 access / daemon 协议能力，也没有把 installed maintenance 证据继续外推为 qemu / soak gate。真实缺口只有一件事：当前树虽然已经实现 `run_maintenance()`，但安装态缺少一个可重复、可落盘、可被 package smoke 调用的正向证据 owner。

因此本轮把 `MEM-FIX-007` 重定义为：

1. 增加一个窄的 installed maintenance proof helper，直接复用真实 daemon profile/config、install layout 与 `create_memory_manager()`，而不是扩写新的产品协议面。
2. helper 必须在真实 SQLite DB 上 seed retention / quarantine / WAL proof data，再执行 `run_maintenance()`，输出结构化 JSON。
3. `scripts/packaging/pkg_smoke_install.sh --explicit-start-check` 必须固定调用该 helper，并落盘 `memory-maintenance-proof.json`。
4. authoritative 证据限定为本机 installed package；qemu / autopkgtest / soak 继续留在 packaging / release 环境复核，不作为本轮 blocker。

## 2. Design -> Build 映射

| Design 决策 | Build / 验证落点 | 通过条件 |
|---|---|---|
| installed maintenance proof 必须走真实安装态 profile/config/layout | `apps/daemon/src/MemoryMaintenanceProofRunner.*`、`apps/daemon/src/MemoryMaintenanceProofMain.cpp` | helper 能加载 `/etc/default/dasall-daemon` 与 `/etc/dasall/daemon.json` 对应 profile/config，创建真实 `IMemoryManager` 并输出 JSON proof |
| package-private helper 必须以稳定安装路径随包发布 | `apps/daemon/CMakeLists.txt`、`debian/dasall-daemon.install` | helper 进入 `dasall-daemon` 包，安装路径固定为 `/usr/lib/dasall/dasall-memory-maintenance-proof` |
| installed smoke 必须把 maintenance 证据固化为 artifact | `scripts/packaging/pkg_smoke_install.sh` | `--explicit-start-check` 生成 `memory-maintenance-proof.json`，并断言 checkpoint / retention / quarantine cleanup 关键字段 |
| build-tree 与 installed 两层都要有最小验证 | `tests/unit/apps/daemon/MemoryMaintenanceProofRunnerTest.cpp`、`build/vscode-linux-ninja/apps/daemon/dasall-memory-maintenance-proof`、`dpkg-buildpackage -us -uc -b`、`pkg_smoke_install.sh --explicit-start-check` | runner 单测、helper CLI、Debian 构包与本机 installed smoke 全部可重复通过 |

## 3. 代码落点

### 3.1 daemon helper

1. `apps/daemon/src/MemoryMaintenanceProofRunner.h/.cpp`
   - 新增 `collect_memory_maintenance_proof()`，通过 `DaemonEntryConfigLoader`、`ProfileCatalog`、`BuildProfileResolver`、`MemoryConfigProjector`、`InstallLayout` 解析真实安装态 memory 配置。
   - 在真实 SQLite DB 上 seed 一条 session、`retention_turns + 1` 条 turns、一条保护最旧 turn 的 summary，以及一条过期 quarantine 记录。
   - 调用 `IMemoryManager::run_maintenance()` 后重新查询 DB，验证 checkpoint、retention、quarantine cleanup 是否生效，并汇总为 `MemoryMaintenanceProofResult`。
2. `apps/daemon/src/MemoryMaintenanceProofMain.cpp`
   - 新增 helper entrypoint，支持 `--json`、`--profile-id`、`--config-file`、`--state-root`，输出结构化 JSON。

### 3.2 测试与打包

1. `tests/unit/apps/daemon/MemoryMaintenanceProofRunnerTest.cpp`
   - 新增 build-tree focused validation，断言 `turns_before = retention_turns + 1`、`turns_after = retention_turns`、`checkpoint_executed=true`、`checkpoint_wal_pages_remaining=0`、`quarantine_rows_after=0`。
2. `apps/daemon/CMakeLists.txt`
   - 新增 `dasall_memory_maintenance_proof_tool`，并将安装目录固定到 `lib/dasall`，避免误入 Debian multiarch libdir。
3. `debian/dasall-daemon.install`
   - 将 helper 固定打入 `usr/lib/dasall/`。
4. `scripts/packaging/pkg_smoke_install.sh`
   - `--explicit-start-check` 在已有 `memory-proof.json` 之后，新增 helper 调用与 `memory-maintenance-proof.json` 落盘。
   - helper 调用前显式从 `/etc/default/dasall-daemon` 读取 `DASALL_DAEMON_PROFILE_ID`，避免依赖当前 shell 环境变量。

## 4. 验证证据

### 4.1 build-tree focused validation

| 验证项 | 结果 |
|---|---|
| `Build_CMakeTools(buildTargets=["dasall-daemon_memory_maintenance_proof_runner_unit_test"])` | PASS |
| `RunCtest_CMakeTools(tests=["MemoryMaintenanceProofRunnerTest"])` | 工具层返回仓库已知的泛化 `生成失败`，不足以判定测试本身失败 |
| 直接执行 `./build/vscode-linux-ninja/tests/unit/apps/daemon/dasall-daemon_memory_maintenance_proof_runner_unit_test` | exit `0` |
| `Build_CMakeTools(buildTargets=["dasall_memory_maintenance_proof_tool"])` | PASS |
| 直接执行 `./build/vscode-linux-ninja/apps/daemon/dasall-memory-maintenance-proof --profile-id edge_minimal --state-root <temp> --json` | exit `0`；`ok=true`、`checkpoint_executed=true`、`turns_before=121`、`turns_after=120`、`quarantine_rows_after=0` |
| `sh -n scripts/packaging/pkg_smoke_install.sh` | PASS |

### 4.2 authoritative installed smoke

构包命令：

```text
dpkg-buildpackage -us -uc -b
```

结果：PASS；helper 已安装到 `debian/tmp/usr/lib/dasall/dasall-memory-maintenance-proof`，并进入 `dasall-daemon` 包的 `/usr/lib/dasall/dasall-memory-maintenance-proof`。

installed smoke 命令：

```text
DASALL_DEEPSEEK_API_KEY_FILE="$HOME/.local/share/dasall/secrets/deepseek-prod.secret" \
DASALL_PACKAGE_SMOKE_ARTIFACT_DIR=/tmp/dasall-mem-fix-007-proof.lp9BEK \
sh scripts/packaging/pkg_smoke_install.sh --explicit-start-check
```

结果：PASS，artifact 目录 `/tmp/dasall-mem-fix-007-proof.lp9BEK` 现包含：

1. `run-first.json`
2. `run-second.json`
3. `memory-proof.json`
4. `memory-maintenance-proof.json`

`memory-maintenance-proof.json` 关键字段：

```json
{
  "ok": true,
  "turns_before": 481,
  "turns_after": 480,
  "retention_turns": 480,
  "quarantine_rows_after": 0,
  "protected_turn_retained": true,
  "purged_turn_removed": true,
  "newest_turn_retained": true,
  "wal_bytes_before": 510912,
  "maintenance_report": {
    "checkpoint_executed": true,
    "checkpoint_wal_pages_remaining": 0
  }
}
```

## 5. 结论与边界

结论：`MEM-FIX-007` 现可按“本机 installed-package maintenance authoritative evidence”口径判定完成。

已闭合：

1. installed maintenance 已有稳定 owner：`/usr/lib/dasall/dasall-memory-maintenance-proof` + `pkg_smoke_install.sh --explicit-start-check`。
2. checkpoint / retention / quarantine cleanup 现在可在真实安装态 DB 上被观测、断言并落盘为 `memory-maintenance-proof.json`。
3. helper 打包路径已收口到 `/usr/lib/dasall`，不会再因为 multiarch libdir 偏移导致 `dh_install` 漏包。

不外推：

1. 本轮不宣称 qemu / autopkgtest guest-side maintenance 重新执行通过。
2. 本轮不宣称 L6 soak / production long-run confidence；那仍属于 release / operations 独立 gate。
3. 本轮没有扩写 access / daemon 协议面；installed maintenance 证据 owner 保持 package-private helper，而不是产品入口 API。
