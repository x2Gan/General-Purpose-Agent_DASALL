# MEM-FIX-006 release-runner / local installed memory evidence 收口

日期：2026-05-18
来源任务：MEM-FIX-006
范围：release-runner workflow contract 下的本机 installed-package memory evidence；不使用 qemu / kvm 作为本轮收口证据

## 1. 任务重定义

本轮没有继续把 `MEM-FIX-006` 执行成“必须先拿到 qemu guest-side PASS 才能判定 Memory 收口”。真实阻塞点来自两处：

1. release runner 现有 memory evidence 缺少 authoritative owner，workflow 没有在 qemu gate 前固定本机 installed-package memory 证据。
2. packaging 构建会复用无版本号 SQLite source override cache，导致包内 `dasall-daemon` 实际编入 SQLite 3.46.1，安装态 daemon 在 `sqlite_min_version` gate 前 fail-closed，无法进入真实 memory smoke。

因此本轮把 `MEM-FIX-006` 重定义为：

1. 先修复 release-runner / packaging 根因，确保本机 installed-package memory smoke 可重复通过。
2. 在 `.github/workflows/release-package-gate.yml` 中固定 local installed memory evidence 步骤，并把 artifact 目录纳入 workflow contract。
3. 用真实 installed package 的 same-session 双轮 `dasall run` + SQLite DB 证据生成 `memory-proof.json`，作为本轮 authoritative evidence。
4. qemu / autopkgtest machine isolation 继续保留给 packaging / release 环境复核，但不再作为本轮 Memory 能力闭环的前置 blocker。

## 2. Design -> Build 映射

| Design 决策 | Build / 验证落点 | 通过条件 |
|---|---|---|
| packaging 不得再静默复用 stale SQLite source cache | `debian/rules`、`memory/CMakeLists.txt` | Debian build 不再命中无版本号旧 cache；若 override 版本不符，在 configure 阶段直接 fail-fast |
| release-runner 先固定 local installed memory evidence，再进入 qemu gate | `.github/workflows/release-package-gate.yml` | workflow 在 qemu step 前执行 `dpkg-buildpackage -us -uc -b` 与 `pkg_smoke_install.sh --explicit-start-check`，并落盘 package-smoke artifact |
| installed memory smoke 必须覆盖 same-session 双轮证据 | `scripts/packaging/pkg_smoke_install.sh` | 生成 `run-first.json`、`run-second.json`、`memory-proof.json`，证明 daemon explicit start、same-session 双轮 LLM、SQLite WAL / tables / turn / summary rows 成立 |
| runtime production direct path 不能丢失 `prepare_context()` 结果 | `runtime/src/AgentOrchestrator.cpp`、`tests/unit/runtime/RuntimeCognitionLoopSmokeTest.cpp` | 第二轮 direct LLM request 的 `constraints=` tag 必须带上第一轮 summary 正文，且剥离 `llm.origin=` 审计包装头 |

## 3. 代码落点

### 3.1 packaging / workflow

1. `debian/rules`
   - 将 SQLite source override cache 改为带版本号目录：`third_party/.cache/dasall_sqlite_autoconf-3510300-src`。
   - 避免 `clean rebuild` 继续静默复用旧的 3.46.1 source tree。
2. `memory/CMakeLists.txt`
   - 新增 `dasall_validate_sqlite_autoconf_source_dir()`；对 `sqlite3.h` 的 `SQLITE_VERSION_NUMBER` 做 configure-time 校验。
   - 当前 pin 为 autoconf `3510300`，对应 runtime version number `3051003`；若 override source dir 不匹配，直接 `FATAL_ERROR`。
3. `.github/workflows/release-package-gate.yml`
   - 新增 package-smoke artifact 目录准备步骤。
   - 在 qemu gate 前固定执行 local installed memory evidence：`dpkg-buildpackage -us -uc -b` + `bash scripts/packaging/pkg_smoke_install.sh --explicit-start-check`。

### 3.2 installed smoke

1. `scripts/packaging/pkg_smoke_install.sh`
   - `--explicit-start-check` 现固定执行 same-session 双轮 `dasall run`。
   - artifact 支持 `run-first.json`、`run-second.json`、`memory-proof.json`。
   - `memory-proof.json` 固定记录 `session_id`、expected marker、turn ids、WAL、core/vector table 数、summary/turn row 数与 latest summary prefix。
   - 修复 `json_extract_string()` 的 stdin/heredoc 冲突；修复 `query_sqlite_scalar_with_params()` 的 Python sqlite3 placeholder warning，消除 Python 3.14 前置 breakage。
2. `runtime/src/AgentOrchestrator.cpp`
   - production LLM direct path 现把 `ContextPacket.summary_memory` 桥接到 responder prompt 的 `constraints` 槽位。
   - 桥接时会剥离 `llm.origin=...` 审计包装头，仅保留 summary 正文，避免 prompt 因“不要输出路由/调试信息”而忽略 marker。
3. `tests/unit/runtime/RuntimeCognitionLoopSmokeTest.cpp`
   - 新增同 session 双轮回归：第二轮发给 `MockLLMManager` 的 `constraints=` tag 必须包含第一轮 summary 正文 `mem-fix-006-local-proof`，且不得包含 `llm.origin=`。

## 4. 验证证据

### 4.1 构建 /回归

| 验证项 | 结果 |
|---|---|
| stale SQLite override guard | 以旧 cache `/home/gangan/DASALL/third_party/.cache/dasall_sqlite_autoconf-src` 做最窄 configure，按预期在 `memory/CMakeLists.txt` 报 `expected SQLITE_VERSION_NUMBER=3051003 ... got 3046001` |
| `Build_CMakeTools(buildTargets=["dasall_runtime_cognition_loop_smoke_unit_test"])` | PASS |
| 直接执行 `./build/vscode-linux-ninja/tests/unit/runtime/dasall_runtime_cognition_loop_smoke_unit_test` | exit `0` |
| 安装态 daemon strings | `strings -a /usr/sbin/dasall-daemon | rg 'session_summary='` 命中 `; session_summary=`；`strings -a /usr/sbin/dasall-daemon | rg '3\.51\.3|3\.46\.1'` 仅命中 `3.51.3` |

### 4.2 authoritative installed smoke

命令：

```text
DASALL_DEEPSEEK_API_KEY_FILE="$HOME/.local/share/dasall/secrets/deepseek-prod.secret" \
DASALL_PACKAGE_SMOKE_ARTIFACT_DIR=/tmp/dasall-mem-fix-006-proof4.d5xayL \
bash scripts/packaging/pkg_smoke_install.sh --explicit-start-check
```

结果：PASS，`rc=0`，log 尾部为 `[pkg-smoke-install] install smoke passed`。

artifact：

1. `/tmp/dasall-mem-fix-006-proof4.d5xayL/run-first.json`
2. `/tmp/dasall-mem-fix-006-proof4.d5xayL/run-second.json`
3. `/tmp/dasall-mem-fix-006-proof4.d5xayL/memory-proof.json`

`memory-proof.json` 实际内容：

```json
{
  "session_id": "pkg-smoke-memory-session",
  "expected_marker": "mem-fix-006-local-proof",
  "first_turn_id": "pkg-smoke-memory-turn-001-llm-response",
  "second_turn_id": "pkg-smoke-memory-turn-002-llm-response",
  "journal_mode": "wal",
  "core_table_count": 5,
  "vector_table_count": 1,
  "llm_turn_writeback_count": 1,
  "llm_summary_writeback_count": 1,
  "session_summary_count_after_first": 1,
  "session_turn_count_after_second": 2,
  "session_summary_count_after_second": 2,
  "latest_summary_source_turn_ids_json": "[\"pkg-smoke-memory-turn-002-llm-response\"]",
  "latest_summary_text_prefix": "llm.origin=deepseek-prod/deepseek-reasoner model=deepseek-v4-flash finish_reason=stop\nmem-fix-006-local-proof"
}
```

补充：同一条 smoke 复验后 `warning_hits=0`，Python sqlite3 `DeprecationWarning` 已清零。

## 5. 结论与边界

结论：`MEM-FIX-006` 现可按“release-runner contract + local installed authoritative memory evidence”口径判定完成。

已闭合：

1. packaging stale SQLite source root cause 已修复，不再产出安装态 daemon 启动即失败的坏包。
2. release workflow 现已在 qemu gate 前固定 local installed memory evidence owner。
3. installed package 已能稳定给出 same-session 双轮 marker 证据，并生成 `memory-proof.json`。
4. runtime production direct path 现在真实消费 `prepare_context()` 产出的 summary memory，而不是只把当前 user input 发给 LLM。

不外推：

1. 本轮不宣称 qemu / autopkgtest guest-side 重新执行通过；machine isolation 仍属于 packaging / release 环境复核层。
2. 本轮不宣称 L6 soak / production confidence；外部 provider 长稳态仍需独立 gate。
3. `summary_reuse_count` 不是当前实现的真实 installed 口径：现实现是一轮一条 summary row，因此本轮证据固定为 same-session recall + turn/summary 落库，而不是伪造“合并后单 summary 复用”。
