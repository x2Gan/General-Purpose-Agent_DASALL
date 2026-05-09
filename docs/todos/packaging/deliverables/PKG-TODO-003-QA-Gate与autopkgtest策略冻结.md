# PKG-TODO-003 冻结 v1 packaging QA Gate 与 autopkgtest 运行策略

状态：Done
日期：2026-05-07
来源 TODO：docs/todos/packaging/DASALL_Ubuntu_DPKG打包专项TODO.md

## 1. 任务边界

1. 本任务只冻结 v1 packaging 的 QA gate 分层、`lintian` / `autopkgtest` / installed-package smoke 的职责边界，以及本地与 CI 的最小 testbed 策略。
2. 本任务不在 003 阶段落 `debian/tests/control`、`pkg-smoke-*` 脚本、rootful install/upgrade/remove/purge harness 或 `lintian` 实际命令包装器；这些实现分别继续由 009、015、016、017、018 承接。
3. 本任务必须回答两个问题：
   - build-tree preflight 与 installed-package gate 如何拆层，避免 `ctest`、`lintian`、`autopkgtest` 被混成一条门禁；
   - `autopkgtest` 在本地和 CI 分别使用哪类 testbed 才能覆盖 `systemd` 与 isolation 需求。

## 2. 当前事实

1. 当前仓库没有 `scripts/packaging/README.md`，也没有 `debian/tests/`；这说明 packaging QA 仍停留在设计草案，没有形成可执行入口与 testbed 约定。
2. 当前仓库已有一批 build-tree 可复用测试切片：CLI contract、daemon ping / socket path / binary smoke integration。这些可以守住 preflight，但还不能替代已安装包验证。
3. `docs/architecture/DASALL_Ubuntu平台DPKG打包方案设计.md` 的 7.10.5 已经提出三层分工：
   - build-time contract
   - build-time integration
   - installed-package validation 交给 `autopkgtest`
   但还没有把 `lintian`、fresh install、upgrade、remove/purge 与 testbed 选择固化成唯一运行策略。
4. `autopkgtest(1)` 明确：它测试的是“安装在 testbed 上的二进制包”，并通过虚拟化后端驱动测试环境；带 `isolation-machine` 约束的场景应交给具备机器级隔离的 testbed，而 qemu virt server 是官方推荐的保守默认值。
5. `lintian(1)` 明确：它是 Debian package 的静态分析工具，输入是 `.deb` / `.dsc` / `.changes` 等包产物，而不是源码树内的 CTest 切片。因此 `lintian` 必须位于 `dpkg-buildpackage` 之后、`autopkgtest` 之前的独立 gate。

## 3. 冻结结论

### 3.1 v1 QA gate 分层

v1 packaging QA 固定拆成五层，不允许混层：

1. Gate A: build-tree preflight
   - 目标：在源码树内用最小 contract/integration 切片守住 CLI/daemon/package 相关 surface。
   - 代表命令：`cmake --build ... --target dasall_packaging_preflight_tests` 或等价的 `ctest -R` 切片。
   - 不验证事项：不宣称已验证 `.deb` 内容、maintainer scripts、fresh install、upgrade、remove/purge。
2. Gate B: package build gate
   - 目标：确认 `dpkg-buildpackage -us -uc -b` 能生成 source/binary 产物。
   - 代表命令：`dpkg-buildpackage -us -uc -b`。
3. Gate C: static package scan
   - 目标：对产物做 policy/static scan。
   - 代表命令：`lintian ../*.changes`，必要时再按评审结果补 `debian/*.lintian-overrides`。
4. Gate D: local installed-package smoke
   - 目标：在本地 rootful 环境串行验证 fresh install、explicit enable/start、upgrade、remove/purge。
   - 代表载体：`scripts/packaging/pkg_smoke_install.sh`、`pkg_smoke_upgrade.sh`、`pkg_smoke_remove_purge.sh`、`validate_ubuntu_dpkg_v1.sh`。
   - 说明：这是安装生命周期语义门，不替代 `autopkgtest`。
5. Gate E: autopkgtest gate
   - 目标：在标准 testbed 上验证安装后的二进制包，以及 `debian/tests/control` 元数据是否自洽。
   - 代表命令：`python3 scripts/packaging/validate_autopkgtest_metadata.py` 与针对 `.changes`/`.deb` 的正式 testbed 运行。

### 3.2 `lintian` 与 `autopkgtest` 的职责边界

1. `lintian` 只负责静态包检查：control 字段、安装路径、debhelper 生成物、policy 违规、常见打包错误。
2. `autopkgtest` 只负责 installed-package 行为：服务能否在 testbed 中启动、资产是否已安装、CLI 是否能对安装后的 daemon 发起本地控制请求。
3. 不采纳“只要 `ctest` 绿就跳过 `lintian` / `autopkgtest`”的捷径；也不采纳“让 `autopkgtest` 顺便覆盖所有 local upgrade/remove/purge 场景”的过载方案。
4. `remove` / `purge` 语义和本地升级路径属于 Gate D 的 rootful smoke 主责，`autopkgtest` 只保留最小 installed-package smoke，不承担全部生命周期回归。

### 3.3 autopkgtest 场景与 testbed 策略

v1 `autopkgtest` 最少冻结两条测试：

1. `pkg-smoke-local-control-plane`
   - 目标：验证 `dasall-daemon.service`、socket、CLI `ping/readiness/version`。
   - 限制：`needs-root`、`isolation-machine`。
   - authoritative testbed：qemu 机器级隔离环境。
   - 理由：该用例依赖 `systemd`、服务启停和 filesystem socket 观察，不应依赖 `null` 或人为忽略 isolation 的快捷模式作为正式 gate。
2. `pkg-smoke-common-assets`
   - 目标：验证 profile 与 LLM baseline 资产已安装。
   - 限制：`isolation-container` 或空；如果与 `pkg-smoke-local-control-plane` 共享一次 qemu 运行，也可直接在机器 testbed 内执行。
   - local quick loop：允许后续在 container/unshare 类 testbed 上单独跑这条用例，以缩短资产检查反馈时间。

### 3.4 本地与 CI 的最小运行策略

1. 本地最小策略
   - 元数据校验：`python3 scripts/packaging/validate_autopkgtest_metadata.py`
   - authoritative smoke：优先使用 qemu testbed 运行完整 suite。
   - 快速资产回路：只在明确不涉及 `systemd`/machine isolation 时，才允许单独跑 `pkg-smoke-common-assets` 的 container/unshare 变体。
2. CI 最小策略
   - 必须跑 Gate A、B、C。
   - 若 CI 宣称 package-ready，则必须在 qemu 级 testbed 上跑 Gate E 的完整 suite。
   - CI 不允许用 `null` virtualization 或 `--ignore-restrictions=isolation-machine` 代替正式 `pkg-smoke-local-control-plane` gate。
3. build-ready 与 package-ready 的声明边界
   - 只过 Gate A/B/C：最多宣称 build-ready packaging skeleton。
   - 只有 Gate D/E 也闭合后，才允许宣称 package gate ready / package-ready。

## 4. 明确不采纳的方案

1. 不采纳“把整个仓库 CTest 全量塞进 `override_dh_auto_test`”方案，因为这会让 package build 对无关长链路测试过度耦合。
2. 不采纳“仅依赖 `lintian` 代替安装后测试”方案，因为它无法证明服务启动、socket 权限和 CLI 行为。
3. 不采纳“只跑 `autopkgtest --validate .` 就视为通过 autopkgtest gate”方案，因为 metadata validate 不是 installed-package run。
4. 不采纳“用 `null` 或忽略 restriction 的方式跑 `pkg-smoke-local-control-plane` 作为正式 gate”方案，因为这会绕过 machine-level systemd 语义。

## 5. 对后续任务的直接约束

1. PKG-TODO-015 只能把 build-tree contract/integration 收敛为 packaging preflight target，不得把 installed-package smoke 混进 `cmake --build` 的默认路径。
2. PKG-TODO-016 必须把 `debian/tests/control` 固定为两条最小 smoke，并把 `pkg-smoke-local-control-plane` 设计成 machine-isolation 优先。
3. PKG-TODO-017 必须把 fresh install、upgrade、remove/purge 放到 rootful shell harness，而不是塞回 `autopkgtest`。
4. PKG-TODO-018 只有在 Gate A~E 都有正式命令和结果归档后，才允许关闭 PKG-BLK-05。
5. `scripts/packaging/README.md` 必须成为这套 gate 的入口说明，明确区分“build-tree preflight”“local rootful smoke”“autopkgtest qemu run”三类执行面。

## 6. 推荐的最小命令口径

1. build-tree preflight：

   `cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_packaging_preflight_tests`

2. package build：

   `dpkg-buildpackage -us -uc -b`

3. static package scan：

   `lintian ../*.changes`

4. autopkgtest metadata validate：

   `python3 scripts/packaging/validate_autopkgtest_metadata.py`

5. authoritative autopkgtest run：

   `autopkgtest ../dasall_*.changes -- qemu <image-or-config>`

6. local rootful lifecycle smoke：

   `bash scripts/packaging/validate_ubuntu_dpkg_v1.sh`

## 7. 验证口径

1. 设计验收使用以下命令：

   `rg -n "autopkgtest|lintian|preflight|install smoke|upgrade smoke|remove|purge|qemu|package-ready|build-ready" docs/architecture/DASALL_Ubuntu平台DPKG打包方案设计.md docs/todos/packaging/DASALL_Ubuntu_DPKG打包专项TODO.md docs/todos/packaging/deliverables/PKG-TODO-003-QA-Gate与autopkgtest策略冻结.md`

2. 通过标准：
   - `lintian`、local rootful smoke、`autopkgtest` 不再混成同一 gate。
   - `pkg-smoke-local-control-plane` 的 authoritative testbed 明确为 qemu/machine isolation。
   - 文档能明确区分 build-ready 与 package-ready 两种声明边界。