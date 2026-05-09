# DASALL Packaging Scripts README

## 1. 目的

这个目录承载 Ubuntu DPKG v1 的 packaging 验证入口。`PKG-TODO-003` 先冻结运行策略与 gate 分层；`PKG-TODO-016` 已落 `debian/tests/*` installed-package smoke 脚本，`PKG-TODO-017` 已补齐本地 rootful lifecycle harness，并在本机通过 `dpkg-buildpackage -us -uc -b` + `bash scripts/packaging/validate_ubuntu_dpkg_v1.sh` 完成 fresh install / explicit start / upgrade / remove-purge 验收。

当前结论只有一条：不要把 build-tree preflight、local installed-package smoke、`autopkgtest` 混成同一种验证；当前剩余正式 gate 只包括 `lintian`、qemu authoritative `autopkgtest` 和证据收口。

## 2. Gate 入口

| Gate | 目标 | 代表命令 | 归属任务 |
|---|---|---|---|
| build-tree preflight | 在源码树内验证 package 相关 contract / integration 切片 | `cmake --build <build-dir> --target dasall_packaging_preflight_tests` | PKG-TODO-015 |
| package build | 生成四包 `.deb` / `.changes` 产物 | `dpkg-buildpackage -us -uc -b` | PKG-TODO-009 |
| static package scan | 对包产物跑 policy/static analysis | `lintian ../*.changes` | PKG-TODO-015 |
| local installed-package smoke | fresh install、explicit enable/start、upgrade、remove/purge | `bash scripts/packaging/validate_ubuntu_dpkg_v1.sh` | PKG-TODO-017 |
| autopkgtest metadata validate | 校验 `debian/tests/control` 语法与元数据 | `python3 scripts/packaging/validate_autopkgtest_metadata.py` | PKG-TODO-016 |
| autopkgtest installed-package run | 在 testbed 中验证安装后的包行为 | `autopkgtest ../dasall_*.changes -- qemu <image-or-config>` | PKG-TODO-016 |

## 3. testbed 策略

### 3.1 authoritative testbed

1. `pkg-smoke-local-control-plane` 的正式 gate 固定使用 qemu 或其他 machine-level isolation testbed。
2. 原因是该用例依赖 `systemd`、服务启停、socket 文件权限和 CLI 对安装后 daemon 的访问。
3. 不把 `null` virtualization 或 `--ignore-restrictions=isolation-machine` 视为正式验收路径。

### 3.2 local quick loop

1. 在没有完整 testbed 的场景下，开发者仍可先跑：

   `python3 scripts/packaging/validate_autopkgtest_metadata.py`

2. 后续若 `pkg-smoke-common-assets` 被拆成纯资产检查，可允许它在 container/unshare 类 testbed 上单独快速回归。
3. 这种 quick loop 不能替代 `pkg-smoke-local-control-plane` 的 machine-isolation gate。
4. 部分 Ubuntu 24.04 `autopkgtest` 版本在 `--validate` 仍会触发 testbed setup；本仓库已提供仅包装 `parse_debian_source()` 的本地兼容 shim `scripts/packaging/validate_autopkgtest_metadata.py` 作为 metadata quick loop，但不能把它当成 installed-package run。

### 3.3 CI 最小要求

1. CI 至少串行执行 build-tree preflight、package build、`lintian`。
2. 若 CI 输出 package-ready 结论，则必须额外执行 qemu testbed 上的 `autopkgtest` installed-package run。
3. local rootful lifecycle smoke 与 qemu `autopkgtest` 共同组成 installed-package gate；缺一不可。

## 4. 已落盘文件

当前目录已经落盘以下 packaging validator / smoke harness：

1. `scripts/packaging/validate_ubuntu_dpkg_v1.sh`
2. `scripts/packaging/pkg_smoke_install.sh`
3. `scripts/packaging/pkg_smoke_upgrade.sh`
4. `scripts/packaging/pkg_smoke_remove_purge.sh`
5. `scripts/packaging/validate_autopkgtest_metadata.py`

仍按需待定的只有：

6. `scripts/packaging/autopkgtest-*.cfg`（仅当 qemu / CI 配置需要固化时新增）

## 5. 不做什么

1. 不在这个目录里复写仓库通用 CTest 入口。
2. 不把 maintainer scripts、`debian/tests/*` 和 rootful lifecycle smoke 合并成一个黑盒脚本。
3. 不在 003 阶段提供伪可执行脚本占位；当前 README 只负责固定职责边界与命令口径。