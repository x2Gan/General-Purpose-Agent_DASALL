# DASALL Ubuntu DPKG 打包专项 TODO

最近更新时间：2026-05-07
阶段：Detailed Design -> Special TODO
适用范围：`debian/`、顶层/子模块 CMake install surface、`apps/cli`、`apps/daemon`、`access/include/daemon`、`llm/include`、`profiles/`、`tests/contract/access`、`tests/integration/access`、`scripts/packaging`
当前结论：Ubuntu DPKG 打包设计已 Ready，工程实现已进入 Implementation In Progress / Package Gate 03 Passed。当前仓库已具备 `dasall-cli`、`dasall-daemon`、`dasall_profiles`、LLM baseline 资产、一套 Debian source/binary package 骨架和一组可复用的 contract/integration tests；其中 PKG-TODO-001 已冻结 Ubuntu noble 基线下的 Build-Depends / Depends / Architecture 四包矩阵，PKG-TODO-002 已冻结 v1 package-mode profile 接缝=`/etc/default/dasall-daemon` + `EnvironmentFile` + `--profile-id` 且把 `/etc/dasall/daemon.json` 收敛为 `daemon.*` override-only conffile，PKG-TODO-003 已冻结 v1 QA gate 分层=`build-tree preflight -> package build -> lintian -> local rootful smoke -> autopkgtest` 与 qemu authoritative testbed 策略，PKG-TODO-004 已冻结统一安装态路径模型的唯一 owner=`infra/config::InstallLayout` 与共享 public surface=`infra/include/config/InstallLayout.h`，PKG-TODO-005 已引入 GNUInstallDirs 与顶层 install surface skeleton，使 `DESTDIR + cmake --install` 能产出非空 stage tree，PKG-TODO-006 已把 CLI / daemon 的系统安装名与默认安装前缀收敛到 `/usr/bin/dasall` 与 `/usr/sbin/dasall-daemon`，PKG-TODO-007 已把五档 profile 与 LLM prompt/provider baseline 资产落到 `/usr/share/dasall/` install surface，PKG-TODO-008 已落 `debian/control`、`changelog`、`rules`、`source/format`、`copyright` 五个 source-level 必需文件，PKG-TODO-009 已补齐 `.install` / systemd unit / postinst / README.Debian / `debian/tests/control` / package-local payload tree，并在 rootless toolroot + synthetic `.pkgadm` 环境下成功执行 `dpkg-buildpackage -us -uc -b` 产出四个 `.deb`。其中 `dasall-daemon` 已包含 `/etc/default/dasall-daemon`、`/etc/dasall/daemon.json`、systemd unit 与 `README.Debian`，`dasall-common` 继续包含 `/usr/share/dasall/` 资产树。当前待完成项已收敛为安装态路径收敛、installed-package smoke / autopkgtest / lintian gate 与 package-ready 证据收口；但不应再回退到 `0660 dasall group`、安装态 `dasall-cli` 文案，或把 `null`/ignore-restrictions 误当成正式 `autopkgtest` gate。

## 1. 概述与目标

### 1.1 文档头

本文档严格基于以下输入生成：

1. `docs/architecture/DASALL_Ubuntu平台DPKG打包方案设计.md`
2. `docs/plans/DASALL_工程落地实现步骤指引.md`
3. `docs/worklog/DASALL_开发执行记录.md`
4. `docs/todos/access/DASALL_access子系统专项TODO.md`
5. `docs/todos/daemon/DASALL-daemon本地控制面专项TODO.md`
6. `docs/todos/profiles/DASALL_profiles子系统专项TODO.md`
7. 当前工程现状：`CMakeLists.txt`、`profiles/CMakeLists.txt`、`apps/cli/CMakeLists.txt`、`apps/daemon/CMakeLists.txt`、`access/include/daemon/DaemonEndpointDefaults.h`、`apps/daemon/src/main.cpp`、`llm/include/LLMSubsystemConfig.h`、`tests/contract/access/CMakeLists.txt`、`tests/integration/access/CMakeLists.txt`、`tests/unit/apps/daemon/CMakeLists.txt`、`tests/unit/llm/CMakeLists.txt`、`tests/unit/profiles/CMakeLists.txt`
8. 最佳实践基线：Debian Policy maintainer scripts、Debian Maintainer's Guide 第 5 章、`dh_install(1)`、`dh_installdocs(1)`、`dh_installsystemd(1)`、`autopkgtest(1)`

编制原则：

1. 不改写已冻结的 Ubuntu DPKG 打包设计结论；TODO 只做 Design -> Build 落地编排。
2. 不让打包层倒逼 ADR-006/007/008 边界漂移；安装逻辑不接管 runtime policy、recovery 或 orchestration 所有权。
3. 每项任务必须包含代码目标、测试目标、验收命令三件套。
4. 能直接进入 Build 的任务不伪装成“继续研究”；设计缺口和环境缺口必须显式记录为 blocker 或前置任务。
5. 首版只围绕 `dasall`、`dasall-cli`、`dasall-daemon`、`dasall-common` 四包主线展开，不把 `gateway`、`simulator`、streaming、APT 仓库自动化混入 v1 必交付面。

### 1.2 专项目标

1. 把现有 Ubuntu DPKG 设计文档转换成可执行的工程任务序列。
2. 补齐 CMake install surface，使 `dh_auto_install` 能稳定产出 `debian/tmp` 安装面。
3. 清除 repo-relative 路径与开发态默认值，把 daemon/CLI/LLM/profile 资产收敛到安装态路径模型。
4. 落地 `debian/` 目录、systemd unit、package-local payload tree、maintainer scripts 和 autopkgtest 元数据。
5. 建立 package-ready 最小质量门：构建树 preflight、`dpkg-buildpackage`、`lintian`、fresh install / explicit enable、upgrade、remove/purge、autopkgtest。

### 1.3 范围边界

纳入范围：

1. Debian source package 元数据与四个 binary package 骨架。
2. CLI/daemon 二进制安装面、共享资产安装面、配置模板、systemd 交付物。
3. daemon socket 默认值、profile 资产根、LLM baseline 资产根的安装态收敛。
4. package-focused build-time tests、installed-package smoke、lintian/autopkgtest/release evidence。

不纳入范围：

1. 内部 APT 仓库服务器、GPG 密钥托管与发布自动化基础设施。
2. `gateway`、`simulator`、streaming、WebSocket、MQTT 的正式包交付实现。
3. 非 Ubuntu/Debian 发行渠道。
4. 运行期 plugin/OTA 签名体系本身。

## 2. 当前状态

### 2.1 当前代码与文档证据

| 证据对象 | 当前状态 | 结论 |
|---|---|---|
| `docs/architecture/DASALL_Ubuntu平台DPKG打包方案设计.md` | 已完成方案设计、Debian 目录逐文件草案与首次安装 UX 策略 | Design Track 已具备实现起点 |
| `CMakeLists.txt` | 已引入 GNUInstallDirs 与共享安装变量，但仍未把 `/usr` prefix 直接固化到本地默认 preset | 顶层 install surface skeleton 已建立，正式 packaging prefix 继续由后续 `debian/rules` 配置驱动 |
| `profiles/CMakeLists.txt` | 已为五档 profile baseline 安装到 `/usr/share/dasall/profiles/` 补齐 install 规则 | `dasall-common` 已具稳定的 profile 资产安装面 |
| `apps/cli/CMakeLists.txt`、`apps/daemon/CMakeLists.txt` | executable target 已在 owning 子目录显式声明 install 规则，CLI/daemon 系统安装名已收敛到 `dasall` 与 `dasall-daemon` | stage install 已可直接产出 `/usr/bin/dasall` 与 `/usr/sbin/dasall-daemon` |
| `access/include/daemon/DaemonEndpointDefaults.h` | `kDefaultDaemonSocketPath` 仍是 `/tmp/dasall/control.sock` | 生产态 socket 路径尚未收敛到 `/run/dasall/daemon.sock` |
| `apps/daemon/src/main.cpp` | 仍通过 `std::filesystem::current_path() / "profiles"` 发现 assets | 安装后二进制不能依赖 repo cwd |
| `llm/include/LLMSubsystemConfig.h` | Prompt / Provider baseline root 默认仍是 `llm/assets/prompts` 与 `llm/assets/providers`；但 `llm/CMakeLists.txt` 已把 baseline 资产安装到 `/usr/share/dasall/llm/prompts` 与 `/usr/share/dasall/llm/providers` | LLM 安装态资产 payload 已存在，repo-relative consumer 默认值仍待 012 收敛 |
| `tests/contract/access/CMakeLists.txt` | 已有 `CliJsonOutputContractTest`、`CliExitCodeContractTest` | 可直接复用为 package preflight contract 基线 |
| `tests/integration/access/CMakeLists.txt` | 已有 `DaemonPingIntegrationTest`、`CliDaemonSocketPathIntegrationTest`、`DaemonBinaryUnarySmokeTest` 等 | 可直接复用为 package preflight integration 基线 |
| `debian/` | 已具 `control`、`changelog`、`rules`、`source/format`、`copyright` source skeleton，以及 `.install`、`dasall-daemon.service`、`dasall-daemon.postinst`、`dasall-daemon.README.Debian`、`debian/tests/control` 与 stable package-local payload tree；rootless `dpkg-buildpackage` 已可产出四包 | source/binary package skeleton 已建立，installed-package QA 仍待 016/017/018 补齐 |

### 2.2 总体判断

1. 设计层已经回答了“怎么打包”，但工程层还没有回答“如何生成可安装、可升级、可验证的 Debian 产物”。
2. 当前 package-ready 的 P0 缺口已收敛到两类：安装态路径未收敛、installed-package gate 缺失。
3. 现有 access/daemon/cli 的 contract/integration 测试足以作为打包前置验证切片，但还不足以替代真实 package install/upgrade/autopkgtest gate。

## 3. 约束条件

### 3.1 约束清单

| ID | 来源 | 类型 | 约束内容 | 对 TODO 的直接影响 |
|---|---|---|---|---|
| PKG-TC001 | 打包设计 6.1/7.1；Debian Policy | Must | v1 采用 Debian source package + debhelper + 多二进制包，不走 CPack/fpm 快捷封装 | 必须先补 install surface，再落 `debian/` |
| PKG-TC002 | 打包设计 7.2 | Must | v1 只冻结 `dasall`、`dasall-cli`、`dasall-daemon`、`dasall-common` 四包 | 不得把 `gateway`、`simulator` 夹带进首轮包 |
| PKG-TC003 | 打包设计 7.4/7.6；Maint Guide 5.3/5.10/5.11 | Must | `/etc` 放 conffile，`/usr/share` 放只读资产，`/run` 放 socket，`/var/lib` 放状态 | 任务必须显式区分配置、资产、状态目录 |
| PKG-TC004 | 打包设计 3.2/7.10.8 | Must | daemon / LLM / profile 资产根不能继续依赖 repo-relative 路径 | 必须补统一安装态路径模型 |
| PKG-TC005 | 打包设计 7.7.2 | Must-Not | 不允许通过 `WorkingDirectory=/usr/share/dasall` 掩盖路径问题 | 必须在代码层解决 asset root 解析 |
| PKG-TC006 | 打包设计 7.7.3；`dh_installsystemd(1)` | Must | 首次安装成功后默认不 enable、也不 start daemon | `debian/rules` / service / postinst 要按此语义设计 |
| PKG-TC007 | 打包设计 7.10.6；Debian Policy 6；Maint Guide 5.18 | Must | maintainer scripts 必须幂等、非交互、POSIX shell，且不手写 systemd 生命周期 | 自定义脚本只能保留极小主体 |
| PKG-TC008 | `dh_install(1)`；打包设计 7.10.3 | Must | `.install` 不能改名；需要改名时要用 package-local payload tree 或 `dh-exec` | `/etc/default/dasall-daemon` 与 `/etc/dasall/daemon.json` 不能靠非法 `.install` 重命名 |
| PKG-TC009 | `dh_installdocs(1)`；打包设计 7.10.1 | Must | `debian/package.README.Debian` 会自动安装；文档优先交给专用 helper | 文档安装不应塞回 `.install` 或 `postinst` |
| PKG-TC010 | `autopkgtest(1)`；打包设计 7.10.7 | Must | autopkgtest 验证的是安装后的二进制包，而不是 build tree | 必须补 installed-package smoke，而不是只跑 CTest |
| PKG-TC011 | ADR-006/007/008；打包设计 2 | Must | 打包层只承载部署/运维语义，不得接管 runtime policy、recovery、orchestrator 所有权 | 任务只允许做路径、配置入口、服务与包元数据收敛 |
| PKG-TC012 | 仓库 build-validation 经验；当前工程现状 | Should | 打包验收命令应优先采用显式 `cmake` / `ctest` / `dpkg-buildpackage`，不能只依赖 VS Code CMake Tools 状态 | 所有 Build 任务必须给出可直接复制的 shell 命令 |
| PKG-TC013 | 打包设计 7.8；`DMD-TODO-037`；`CLCFG-TODO-002` | Must | daemon 可继续以 `dasall:dasall` 运行，但 filesystem socket 默认值必须收敛到 `0600`，P0 operator path 只允许 root/sudo-only；`0660 dasall group` 仅作为后续演进项记录，不放宽到 world-writable | service、postinst、README.Debian、autopkgtest 必须守住最小权限并共享同一 operator 口径 |
| PKG-TC014 | 打包设计 7.10.8 | Must | install root、profile 资产根、LLM baseline root、socket 默认值、validate-only 路径是 package-ready 的硬前提 | 这些任务必须排在 `debian/` 骨架可验证之前 |

## 4. Design Track 映射

| Design 项 | 设计锚点 | TODO 类型 | 对应任务 ID | 映射说明 |
|---|---|---|---|---|
| 四包矩阵与关系字段 | 7.2、7.9.2、7.10.4 | 依赖盘点 + source metadata | PKG-TODO-001、008、009 | 先冻结 Depends/Architecture，再落 control/install/service |
| 安装态布局与 install root | 3.2、7.4、7.5、7.10.8 | 接缝收敛 + install surface | PKG-TODO-004、005、006、007、010、011、012 | 先统一路径模型，再让 CMake/代码/包骨架共享同一布局 |
| 首次安装 UX 与 systemd 语义 | 7.7、7.8、7.10.6、7.10.9 | 接缝收敛 + service/config + maintainer scripts | PKG-TODO-002、009、013、014 | 守住“首装不自动运行 + postinst 最小提示 + README.Debian” |
| debhelper helper 正确用法 | 7.10.1、7.10.3、7.10.5 | 骨架任务 | PKG-TODO-008、009 | `.install`、`dh_installdocs`、`dh_installsystemd` 分工必须在骨架期一次定对 |
| package-focused preflight 与 installed-package gate | 7.10.5、7.10.7、7.14 | QA 设计 + 测试实现 | PKG-TODO-003、015、016、017、018 | 现有 CTest 只守 preflight，安装后 gate 另立 |
| docs/manpage/operator guidance | 7.7.4、7.10.1、7.10.2、7.10.9 | 运维文档与交付说明 | PKG-TODO-014、018 | README/manpage/NEWS 路径与 operator onboarding 一起收口 |

## 5. Build Track 映射

| Build 切片 | 当前缺口 | 对应任务 ID | 预期产物 |
|---|---|---|---|
| 顶层与子模块 install surface | GNUInstallDirs + CLI/daemon system install naming + common assets install 已建立 | 无 | `debian/tmp` 可消费的标准安装面 |
| 路径与默认值收敛 | socket 仍在 `/tmp`；daemon/LLM 仍依赖 repo-relative 路径 | PKG-TODO-010、011、012、013 | 安装态可运行的 CLI/daemon/LLM/profile path model |
| Debian source metadata | source/binary package skeleton 已建立，rootless package build 已可产出四包，installed-package QA 仍缺 | PKG-TODO-001、008、009、016、017 | 可执行的 source/binary package skeleton 与 build gate 证据 |
| 服务/配置/首次部署 UX | daemon service/config/doc skeleton 已落地，但 rootful install / explicit-start smoke 仍缺 | PKG-TODO-002、009、013、014、017 | daemon package payload 与 operator onboarding |
| 测试与发布 gate | 只有 build-tree tests，没有 `lintian` / install smoke / autopkgtest | PKG-TODO-003、015、016、017、018 | package preflight、installed-package gate、交付证据 |

## 6. 任务表

### 6.1 前置补设计 / 接缝收敛任务

| Task ID | Status | 任务标题 | 设计依据 | 精确范围 | 粒度 | 代码目标 | 目标函数/接口/数据结构 | 测试目标 | 验收命令 | 前置任务 | 关联阻塞项 | 解阻条件 | 交付物 | 完成判定 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| PKG-TODO-001 | Done | 实测并冻结 Build-Depends / Depends / Architecture 矩阵 | 7.2、7.9.2、7.10.4；Maint Guide control/install | `Build-Depends`、`Depends`、`Recommends`、`Architecture: any/all`、`adduser` 运行时依赖 | L2 | `debian/control`；`docs/todos/packaging/deliverables/PKG-TODO-001-依赖矩阵冻结.md` | `Source/Package stanza` 字段、Build-Depends 列表 | process：依赖矩阵一致性检查 | `rg -n "Build-Depends|Architecture: any|Architecture: all|adduser|dasall-common|dasall-daemon" docs/architecture/DASALL_Ubuntu平台DPKG打包方案设计.md docs/todos/packaging/DASALL_Ubuntu_DPKG打包专项TODO.md docs/todos/packaging/deliverables/PKG-TODO-001-依赖矩阵冻结.md` | 无 | PKG-BLK-04 ✅ 已解阻 | 完成目标 Ubuntu 版本上的依赖盘点 | `debian/control` 初稿；依赖矩阵 deliverable | 仅当四包关系、Build-Depends、Architecture 与运行时依赖全部形成唯一口径，且不再保留占位字段时完成 |
| PKG-TODO-002 | Done | 冻结 package-mode profile 选择接缝与配置 ownership | 7.7.4、7.10.9；当前 `DaemonEntryConfigLoader` 仅支持 `daemon.*` 覆盖 | `/etc/default/dasall-daemon`、`/etc/dasall/daemon.json`、`--profile-id`、`EnvironmentFile` | L2 | `apps/daemon/src/main.cpp`；`apps/daemon/src/DaemonEntryConfigLoader.*`；`debian/dasall-daemon.service`；`debian/dasall-daemon/etc/default/dasall-daemon` | `--profile-id`、`--config-file`、package-mode 输入模型 | unit：`DaemonEntryConfigProjectionTest`；design/process：service/config 一致性 | `rg -n "DASALL_DAEMON_PROFILE_ID|/etc/default/dasall-daemon|/etc/dasall/daemon.json|EnvironmentFile|daemon\.\*" docs/architecture/DASALL_Ubuntu平台DPKG打包方案设计.md docs/todos/packaging/DASALL_Ubuntu_DPKG打包专项TODO.md docs/todos/packaging/deliverables/PKG-TODO-002-package-mode-profile接缝与配置ownership冻结.md` | 无 | PKG-BLK-03 ✅ 已解阻 | v1 明确采用 `EnvironmentFile + --profile-id`，并把 `daemon.json` 固定为 `daemon.*` override-only | `docs/todos/packaging/deliverables/PKG-TODO-002-package-mode-profile接缝与配置ownership冻结.md`；service/config 统一口径 | 仅当 profile 选择入口、daemon.json 边界和 systemd ExecStart 参数形成唯一实现口径时完成 |
| PKG-TODO-003 | Done | 冻结 v1 packaging QA gate 与 autopkgtest 运行策略 | 7.10.5、7.10.7、7.14；`autopkgtest(1)` | preflight 切片、install smoke、upgrade smoke、remove/purge、autopkgtest 虚拟化策略 | L2 | `docs/todos/packaging/deliverables/PKG-TODO-003-QA-Gate与autopkgtest策略冻结.md`；`scripts/packaging/README.md` | `autopkgtest` testbed strategy、`lintian`、fresh install/upgrade gate matrix | process：gate matrix 校验 | `rg -n "autopkgtest|lintian|preflight|install smoke|upgrade smoke|remove|purge|qemu|package-ready|build-ready" docs/architecture/DASALL_Ubuntu平台DPKG打包方案设计.md docs/todos/packaging/DASALL_Ubuntu_DPKG打包专项TODO.md docs/todos/packaging/deliverables/PKG-TODO-003-QA-Gate与autopkgtest策略冻结.md scripts/packaging/README.md` | 无 | PKG-BLK-05（策略已冻结，待 016/017 落实现） | 已明确本地与 CI 两套最小 testbed 策略，并拆清 build-tree preflight、local rootful smoke 与 `autopkgtest` 边界 | QA 策略 deliverable；`scripts/packaging/README.md` | 仅当 build-tree gate 与 installed-package gate 的边界、命令和环境前提全部冻结时完成 |
| PKG-TODO-004 | Done | 冻结统一安装态布局 owner 与 path resolver 归属 | 3.2、7.4、7.5、7.10.8；当前路径分散在 access/apps/daemon/llm | `/usr/share/dasall`、`/etc/dasall`、`/run/dasall`、`/var/lib/dasall` 的代码 owner 与 API surface | L2 | `docs/architecture/DASALL_Ubuntu平台DPKG打包方案设计.md`；`docs/todos/packaging/deliverables/PKG-TODO-004-InstallLayout与PathResolver归属冻结.md`；后续共享头文件落位说明 | `InstallLayout` / asset resolver / daemon config root / socket root 归属 | process：设计与代码锚点一致性 | `rg -n "InstallLayout|/usr/share/dasall|/etc/dasall|/run/dasall|/var/lib/dasall|current_path\(\)|baseline_root|kDefaultDaemonSocketPath" docs/architecture/DASALL_Ubuntu平台DPKG打包方案设计.md docs/todos/packaging/DASALL_Ubuntu_DPKG打包专项TODO.md docs/todos/packaging/deliverables/PKG-TODO-004-InstallLayout与PathResolver归属冻结.md apps/daemon/src/main.cpp access/include/daemon/DaemonEndpointDefaults.h profiles/include/ProfileCatalog.h llm/include/LLMSubsystemConfig.h` | 无 | PKG-BLK-02（owner 已冻结，待 010/011/012 落实现） | 已明确共享安装态路径的唯一 owner 与 shared public surface 落位 | InstallLayout 归属 deliverable | 仅当 install layout 的 owner、导出位置、调用边界不再依赖口头约定时完成 |

### 6.2 Install Surface 与 debian 骨架任务

| Task ID | Status | 任务标题 | 设计依据 | 精确范围 | 粒度 | 代码目标 | 目标函数/接口/数据结构 | 测试目标 | 验收命令 | 前置任务 | 关联阻塞项 | 解阻条件 | 交付物 | 完成判定 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| PKG-TODO-005 | Done | 引入 GNUInstallDirs 与顶层 install surface 骨架 | 7.1、7.10.5、7.10.8 | 顶层 CMake、公共安装变量、`dh_auto_install` 所需基础 install 面 | L2 | `CMakeLists.txt`；`apps/CMakeLists.txt`；`profiles/CMakeLists.txt`；`llm/CMakeLists.txt`；必要时 `access/CMakeLists.txt` | GNUInstallDirs、`install(TARGETS ...)` / `install(DIRECTORY ...)` 骨架 | build：stage install discoverability | `cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall-cli dasall-daemon dasall_profiles && rm -rf stage/pkg && DESTDIR="$PWD/stage/pkg" cmake --install build-ci && find stage/pkg -maxdepth 4 | sort` | PKG-TODO-004 | PKG-BLK-01 ✅ 已解阻 | 统一安装变量与 non-empty stage install 已成立 | 更新后的 CMake install skeleton | 仅当 `cmake --install` 能产出非空 stage tree，且不再需要手工拷贝 build 产物时完成 |
| PKG-TODO-006 | Done | 为 CLI / daemon 二进制补齐系统安装名与 install 规则 | 7.3、7.4、7.10.3 | `/usr/bin/dasall`、`/usr/sbin/dasall-daemon` 的 `OUTPUT_NAME` / install 规则 | L3 | `apps/cli/CMakeLists.txt`；`apps/daemon/CMakeLists.txt`；`apps/CMakeLists.txt`；`CMakeLists.txt`；`runtime/src/AgentFacade.cpp`；`tests/integration/access/DaemonBinaryUnarySmokeTest.cpp` | executable `OUTPUT_NAME`、`install(TARGETS ...)`、默认 `/usr` prefix、runtime-local stub init gate、binary smoke socket workspace | contract：`CliJsonOutputContractTest`、`CliExitCodeContractTest`；integration：`DaemonBinaryUnarySmokeTest`；unit：`RuntimeControlPlaneSurfaceTest` | `cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall-cli dasall-daemon dasall-cli_json_output_contract_test dasall-cli_exit_code_contract_test dasall_access_daemon_binary_unary_smoke_integration_test && ctest --test-dir build-ci -R "CliJsonOutputContractTest|CliExitCodeContractTest|DaemonBinaryUnarySmokeTest" --output-on-failure && rm -rf stage/pkg && DESTDIR="$PWD/stage/pkg" cmake --install build-ci && test -x stage/pkg/usr/bin/dasall && test -x stage/pkg/usr/sbin/dasall-daemon` | PKG-TODO-005 | 无 | 顶层 install surface 已建立，且 runtime-local stub init gate 已恢复为可接受最小空桩路径 | CLI/daemon 安装面与二进制命名 | 仅当 stage install 中实际出现 `/usr/bin/dasall` 与 `/usr/sbin/dasall-daemon`，且相关 contract/integration 继续通过时完成 |
| PKG-TODO-007 | Done | 为 profiles / LLM baseline 资产补齐 `dasall-common` install surface | 7.2.1、7.4、7.6、7.10.3 | 五档 profile、Prompt baseline、Provider baseline 安装到 `/usr/share/dasall/` | L2 | `profiles/CMakeLists.txt`；`llm/CMakeLists.txt` | `install(DIRECTORY ...)` for profile / prompt / provider assets | unit：`ProfileCatalogTest`、`RuntimePolicyProviderTest`、`LLMSubsystemConfigProjectionTest` | `cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_profile_catalog_unit_test dasall_runtime_policy_provider_unit_test dasall_llm_subsystem_config_projection_unit_test && ctest --test-dir build-ci -R "ProfileCatalogTest|RuntimePolicyProviderTest|LLMSubsystemConfigProjectionTest" --output-on-failure && rm -rf stage/pkg && DESTDIR="$PWD/stage/pkg" cmake --install build-ci && test -f stage/pkg/usr/share/dasall/profiles/desktop_full/runtime_policy.yaml && test -f stage/pkg/usr/share/dasall/llm/prompts/planner/default/manifest.yaml && test -f stage/pkg/usr/share/dasall/llm/providers/catalog.yaml` | PKG-TODO-004、005 | 无 | asset install root 与 install 规则已统一；repo-relative consumer 默认值继续由 010/011/012 收敛 | `dasall-common` 资产安装面 | 仅当 stage install 中出现完整 profile / prompt / provider 树，且现有 profile/LLM 单测保持绿色时完成 |
| PKG-TODO-008 | Done | 新建 Debian source package 元数据骨架 | 7.1、7.9、7.10.1、7.10.4；Maint Guide 第 5 章 | `debian/control`、`debian/changelog`、`debian/rules`、`debian/source/format`、`debian/copyright` | L2 | `debian/control`；`debian/changelog`；`debian/rules`；`debian/source/format`；`debian/copyright` | source package metadata / debhelper 入口 | build：`dpkg-parsechangelog`、`dpkg-checkbuilddeps --admindir=.pkgadm`、`debian/rules` preflight | `dpkg-parsechangelog && dpkg-checkbuilddeps --admindir="$PWD/.pkgadm" && test -x debian/rules && sed -n '1,120p' debian/control` | PKG-TODO-001、005、006、007 | 无 | 依赖矩阵与 install surface 已形成最小闭环；当前宿主机缺失系统级 `debhelper-compat`，故以 rootless 本地 dpkg 状态库补充 minimal build-deps validate | source package skeleton | 仅当 `debian/` 必需 source files 全部落盘，且 changelog/control/rules/source format 语法通过最小检查时完成 |
| PKG-TODO-009 | Done | 新建 binary package payload skeleton 与 service/config/doc 骨架 | 7.7、7.8、7.10.1、7.10.3、7.10.6、7.10.9 | `.install`、`dasall-daemon.service`、`postinst`、`README.Debian`、package-local payload tree、`debian/tests/control` 骨架 | L2 | `debian/dasall-cli.install`；`debian/dasall-daemon.install`；`debian/dasall-common.install`；`debian/dasall-daemon.service`；`debian/dasall-daemon.postinst`；`debian/dasall-daemon.README.Debian`；`debian/package-assets/dasall-daemon/etc/default/dasall-daemon`；`debian/package-assets/dasall-daemon/etc/dasall/daemon.json`；`debian/tests/control` | service/config/doc/package payload skeleton | build/package inspect：deb 内容与 control area 可检查 | `dpkg-buildpackage -us -uc -b && dpkg-deb -c ../dasall-daemon_*.deb | rg "etc/default/dasall-daemon|etc/dasall/daemon.json|lib/systemd/system/dasall-daemon.service|usr/share/doc/dasall-daemon/README.Debian" && dpkg-deb -c ../dasall-common_*.deb | rg "usr/share/dasall/"` | PKG-TODO-001、002、003、008 | PKG-BLK-01 ✅ 已解阻、PKG-BLK-03 ✅ 已解阻 | source metadata 与 install surface 已可驱动构包 | binary package skeleton | 仅当三类 binary skeleton 可被 `dpkg-buildpackage` 消费，且包内容与设计文档一致时完成 |

### 6.3 路径收敛 / 服务 / 配置实现任务

| Task ID | Status | 任务标题 | 设计依据 | 精确范围 | 粒度 | 代码目标 | 目标函数/接口/数据结构 | 测试目标 | 验收命令 | 前置任务 | 关联阻塞项 | 解阻条件 | 交付物 | 完成判定 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| PKG-TODO-010 | NotStarted | 切换 canonical socket 默认值到 `/run/dasall/daemon.sock` | 3.2、7.4、7.5、7.10.8 | CLI/daemon/profile 基线统一收敛生产态 socket 路径 | L3 | `access/include/daemon/DaemonEndpointDefaults.h`；`profiles/*/runtime_policy.yaml`；相关 daemon/access 测试 | `kDefaultDaemonSocketPath`、profile baseline 中的 `daemon.socket_path` | unit：`DaemonEntryConfigProjectionTest`；integration：`CliDaemonSocketPathIntegrationTest`、`DaemonBinaryUnarySmokeTest` | `cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall-daemon_entry_config_projection_unit_test dasall_access_cli_daemon_socket_path_integration_test dasall_access_daemon_binary_unary_smoke_integration_test && ctest --test-dir build-ci -R "DaemonEntryConfigProjectionTest|CliDaemonSocketPathIntegrationTest|DaemonBinaryUnarySmokeTest" --output-on-failure` | PKG-TODO-004、005 | PKG-BLK-02 | 统一安装态路径 owner 已冻结 | socket 默认值收敛补丁 | 仅当默认 socket 值、profile baseline 和 CLI/daemon 相关集成测试全部收敛到 `/run/dasall/daemon.sock` 时完成 |
| PKG-TODO-011 | NotStarted | 引入 install-aware asset root 并修复 daemon profile 发现路径 | 3.2、7.5、7.10.8；当前 `main.cpp` 使用 `current_path()/profiles` | daemon `profiles_root` 解析、validate-only 与 systemd 下的资产发现 | L2 | `apps/daemon/src/main.cpp`；必要时新增共享 `InstallLayout` / asset resolver 头源文件；相关 tests | `DaemonEntryConfigLoadRequest.profiles_root`、安装态 asset resolver | unit：`DaemonEntryConfigProjectionTest`；新增 unit：`DaemonInstalledAssetRootTest`；integration：`DaemonBinaryUnarySmokeTest` | `cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall-daemon_entry_config_projection_unit_test dasall_daemon_installed_asset_root_unit_test dasall_access_daemon_binary_unary_smoke_integration_test && ctest --test-dir build-ci -R "DaemonEntryConfigProjectionTest|DaemonInstalledAssetRootTest|DaemonBinaryUnarySmokeTest" --output-on-failure` | PKG-TODO-004、005、007 | PKG-BLK-02 | 统一安装态路径模型可被代码消费 | daemon asset root 收敛补丁 | 仅当 daemon 不再依赖仓库 cwd，且 validate-only / binary smoke 可在安装态路径模型下通过时完成 |
| PKG-TODO-012 | NotStarted | 切换 LLM Prompt / Provider baseline root 到安装态路径 | 3.2、7.2.1、7.10.8；当前 `LLMSubsystemConfig.h` 为 repo-relative root | `PromptAssetSourceConfig.baseline_root`、`ProviderCatalogSourceConfig.baseline_root` | L3 | `llm/include/LLMSubsystemConfig.h`；必要时 `llm/src/*`；相关 unit tests | `baseline_root` 默认值与投影逻辑 | unit：`LLMSubsystemConfigProjectionTest`；新增 unit：`LLMBaselineAssetPathTest` | `cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_llm_subsystem_config_projection_unit_test dasall_llm_baseline_asset_path_unit_test && ctest --test-dir build-ci -R "LLMSubsystemConfigProjectionTest|LLMBaselineAssetPathTest" --output-on-failure` | PKG-TODO-004、005、007 | PKG-BLK-02 | `dasall-common` 资产安装面已稳定 | LLM baseline root 收敛补丁 | 仅当 LLM baseline 默认值全部切换到安装态路径模型，且 projection 单测继续通过时完成 |
| PKG-TODO-013 | NotStarted | 落 package-mode service/config 语义与 daemon 首次部署输入面 | 7.7、7.7.4、7.8、7.10.6、7.10.9 | service `ExecStartPre/ExecStart`、EnvironmentFile、daemon.json 模板、最小 postinst 提示（`sudo dasall config`） | L2 | `debian/dasall-daemon.service`；`debian/dasall-daemon.postinst`；`debian/dasall-daemon/etc/default/dasall-daemon`；`debian/dasall-daemon/etc/dasall/daemon.json`；必要时 daemon config 相关源码 | `ExecStartPre`、`ExecStart`、`print_first_install_hint()`、profile/config 组合入口 | package inspect：daemon 包内容；installed-package：`pkg-smoke-local-control-plane` | `dpkg-buildpackage -us -uc -b && dpkg-deb -c ../dasall-daemon_*.deb | rg "etc/default/dasall-daemon|etc/dasall/daemon.json|lib/systemd/system/dasall-daemon.service" && dpkg-deb -e ../dasall-daemon_*.deb ../build-ci/pkg-inspect-daemon && rg -n "DASALL was installed successfully|sudo dasall config|enable --now dasall-daemon.service|#DEBHELPER#" ../build-ci/pkg-inspect-daemon/postinst` | PKG-TODO-002、009、011 | PKG-BLK-03 | package-mode profile/config seam 已冻结 | service/config/postinst 实现 | 仅当 daemon package 的 service、配置模板、EnvironmentFile、postinst hint 全部与设计文档一致，且 postinst 不再建议 `dasall` 组 access 或安装态 `dasall-cli` 时完成 |
| PKG-TODO-014 | NotStarted | 补齐 operator docs、manpage 与首次部署文档安装面 | 7.7.4、7.10.1、7.10.2、7.10.9；`dh_installdocs(1)` | `README.Debian`、CLI manpage、必要时 `NEWS.Debian`；统一安装态 `dasall config` root/sudo-only onboarding | L2 | `debian/dasall-daemon.README.Debian`；`debian/dasall.1` 或 `debian/dasall-cli.manpages`；必要时 `debian/dasall-daemon.NEWS` | README / manpage / root-sudo-only operator onboarding 文档 | package inspect：文档是否正确落到 doc/man 路径 | `dpkg-buildpackage -us -uc -b && dpkg-deb -c ../dasall-daemon_*.deb | rg "usr/share/doc/dasall-daemon/README.Debian" && dpkg-deb -c ../dasall-cli_*.deb | rg "usr/share/man/man1/dasall.1"` | PKG-TODO-009、013 | 无 | binary package skeleton 与 service/config 语义已落定 | operator docs / manpage | 仅当 README.Debian、manpage 与首次部署提示形成一致口径，且文档由正确 helper 自动安装并统一使用 `dasall config` root/sudo-only 语义时完成 |

### 6.4 测试支撑 / 安装验证 / Gate 收口任务

| Task ID | Status | 任务标题 | 设计依据 | 精确范围 | 粒度 | 代码目标 | 目标函数/接口/数据结构 | 测试目标 | 验收命令 | 前置任务 | 关联阻塞项 | 解阻条件 | 交付物 | 完成判定 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| PKG-TODO-015 | NotStarted | 新增 package-focused build-time preflight target | 7.10.5、7.14；当前已存在 access contract/integration slice | 把 package 相关 contract/integration 收敛为专用 custom target | L2 | `tests/CMakeLists.txt`；必要时 `tests/packaging/` 或 `scripts/packaging/packaging_preflight.cmake` | `dasall_packaging_preflight_tests` custom target | contract：`CliJsonOutputContractTest`、`CliExitCodeContractTest`；integration：`DaemonPingIntegrationTest`、`CliDaemonSocketPathIntegrationTest`、`DaemonBinaryUnarySmokeTest` | `cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_packaging_preflight_tests` | PKG-TODO-006、010、011、012 | 无 | 相关 preflight tests 已具稳定 discoverability | packaging preflight target | 仅当 package preflight 拥有统一 CMake 入口，且不再依赖散写 regex 才能复验时完成 |
| PKG-TODO-016 | NotStarted | 落 autopkgtest installed-package smoke 与 metadata 校验 | 7.10.7、7.14；`autopkgtest(1)` | `debian/tests/control`、`pkg-smoke-local-control-plane`、`pkg-smoke-common-assets`、metadata validate | L2 | `debian/tests/control`；`debian/tests/pkg-smoke-local-control-plane`；`debian/tests/pkg-smoke-common-assets`；必要时 `scripts/packaging/autopkgtest-*.cfg` | autopkgtest metadata 与 installed-package smoke | installed-package：autopkgtest validate/run | `autopkgtest --validate . && test -x debian/tests/pkg-smoke-local-control-plane && test -x debian/tests/pkg-smoke-common-assets` | PKG-TODO-003、009、013、015 | PKG-BLK-05 | autopkgtest testbed 策略已冻结 | autopkgtest control + scripts | 仅当 autopkgtest control 与两个 smoke script 全部落盘、可被 validate，且不再混淆 build-tree 与 installed-package 验证时完成 |
| PKG-TODO-017 | NotStarted | 新增 fresh-install / explicit-enable / upgrade / remove-purge smoke harness | 7.7.3、7.12、7.14 | rootful 本地 smoke、upgrade 保留 conffile、remove/purge 语义、统一一键验收脚本 | L2 | `scripts/packaging/pkg_smoke_install.sh`；`scripts/packaging/pkg_smoke_upgrade.sh`；`scripts/packaging/pkg_smoke_remove_purge.sh`；`scripts/packaging/validate_ubuntu_dpkg_v1.sh` | install/upgrade/remove/purge harness | installed-package：fresh install、explicit start、upgrade、remove/purge smoke | `bash scripts/packaging/validate_ubuntu_dpkg_v1.sh` | PKG-TODO-009、013、015、016 | PKG-BLK-05 | 本地 rootful smoke 环境与 autopkgtest 前提就绪 | packaging smoke harness + one-shot validator | 仅当 package 验收拥有统一 one-shot 脚本，且 install/upgrade/remove/purge 语义都可自动回归时完成 |
| PKG-TODO-018 | NotStarted | 回写 packaging Gate、交付证据与专项收口结论 | 7.14；本专项 TODO | Gate 状态、交付物、worklog、残余风险与后续范围冻结 | L2 | `docs/todos/packaging/DASALL_Ubuntu_DPKG打包专项TODO.md`；`docs/todos/packaging/deliverables/PKG-TODO-018-Ubuntu-DPKG-Gate与交付证据收口.md`；`docs/worklog/DASALL_开发执行记录.md` | `PKG-GATE-*`、交付证据矩阵、残余风险 | process + release evidence：gate 状态一致性 | `rg -n "PKG-GATE-|PKG-TODO-018|记录 #" docs/todos/packaging/DASALL_Ubuntu_DPKG打包专项TODO.md docs/todos/packaging/deliverables/PKG-TODO-018-Ubuntu-DPKG-Gate与交付证据收口.md docs/worklog/DASALL_开发执行记录.md && bash scripts/packaging/validate_ubuntu_dpkg_v1.sh` | PKG-TODO-015、016、017 | 无 | package preflight、installed-package gate 与 smoke harness 均已稳定 | Gate 收口 deliverable 与 worklog 证据 | 仅当每一条 packaging gate 都有正式命令、通过/失败结论、长期可追溯路径和残余风险说明时完成 |

## 7. 执行顺序建议

### 7.1 串并行编排

| 阶段 | 任务 ID | 串并行建议 | 说明 |
|---|---|---|---|
| A 依赖与路径冻结 | PKG-TODO-001 ~ 004 | 串行起步 | 这是后续所有 Build 任务的口径基线，先把依赖、profile 输入面、QA 策略、install layout owner 固定下来 |
| B install surface 建骨架 | PKG-TODO-005 ~ 007 | 005 串行；006/007 可并行 | 先有 GNUInstallDirs/install surface，再分别接 CLI/daemon 与 common assets |
| C `debian/` source/binary skeleton | PKG-TODO-008、009 | 串行 | 先 source metadata，再 binary payload/service/config/doc skeleton |
| D 路径与运行模式收敛 | PKG-TODO-010 ~ 014 | 010/011/012 可并行；013 依赖 002/009/011；014 依赖 009/013 | 这是 package-ready 的核心实现段 |
| E 测试与发布 gate | PKG-TODO-015 ~ 017 | 串行 | 先 preflight，再 autopkgtest metadata，再 install/upgrade/remove/purge 一键验收 |
| F 交付证据收口 | PKG-TODO-018 | 串行 | 只有在正式 gate 稳定后才能收口 |

### 7.2 必过门禁表

| Gate ID | 门禁名称 | 触发时机 | 通过条件 | 未通过处理 |
|---|---|---|---|---|
| PKG-GATE-01 | Install Surface Gate | 进入 `debian/` 实作前 | `DESTDIR` + `cmake --install` 能产出 CLI/daemon/common 的 stage tree | 停止 `debian/` 骨架推进，先修 install surface |
| PKG-GATE-02 | Path Convergence Gate | 进入 package build 前 | socket 默认值、daemon profile root、LLM baseline root 全部脱离 repo-relative | 停止构包，只修路径收敛 |
| PKG-GATE-03 | Source Package Build Gate | `debian/` 骨架完成后 | `dpkg-buildpackage -us -uc -b` 可生成四包产物 | 停止 package QA，只修 metadata / install list |
| PKG-GATE-04 | Fresh Install UX Gate | daemon package 初次安装后 | 包安装成功、服务默认不启动、README.Debian 可见、postinst hint 正确 | 停止进入 upgrade/autopkgtest，先修首装语义 |
| PKG-GATE-05 | Explicit Start Gate | fresh install 通过后 | `systemctl enable --now` 启动成功，socket owner/mode 正确，`dasall ping/readiness/version` 正常 | 停止升级与 purge 验证，先修 service/config/path |
| PKG-GATE-06 | Upgrade / Conffile Gate | fresh install + explicit start 通过后 | 升级不覆写 `/etc/dasall/daemon.json`，不出现惊扰式自动拉起 | 停止 release 结论，先修 conffile / maintscript |
| PKG-GATE-07 | Autopkgtest Gate | package build 与 local smoke 通过后 | `pkg-smoke-local-control-plane`、`pkg-smoke-common-assets` 在 testbed 全绿 | 停止收口，先修 installed-package tests |
| PKG-GATE-08 | Lintian / Evidence Gate | 最终收口前 | `lintian` 无未评审 blocker，Gate / deliverable / worklog 证据一致 | 不得宣称 package-ready |

## 8. 阻塞项与解阻条件

| 阻塞项 ID | 阻塞描述 | 影响任务 | 解阻条件 | 最小解阻动作 | 回退策略 |
|---|---|---|---|---|---|
| PKG-BLK-01 | 已由 PKG-TODO-005 解阻：顶层已引入 GNUInstallDirs，共享安装变量和 CLI/daemon install skeleton，`DESTDIR + cmake --install build-ci` 可产出非空 stage tree | PKG-TODO-006 ~ 009 | 顶层与关键子模块已有稳定 `cmake --install` 输出 | 已完成 | 若后续 install 面退化，回退到 stage install proof-of-concept 并重新补 skeleton |
| PKG-BLK-02 | repo-relative 路径实现仍未清除：PKG-TODO-004 已冻结 owner=`infra/config::InstallLayout`，但 daemon / profiles / LLM 仍保留 module-local 默认值与 `cwd` 假设 | PKG-TODO-010、011、012、013 | install-aware path resolver 按 frozen owner 落地且相关 tests 全绿 | 完成 PKG-TODO-010、011、012 | 若路径收敛暂时失败，不允许用 systemd `WorkingDirectory` 伪修复 |
| PKG-BLK-03 | 已由 PKG-TODO-002 解阻：v1 package-mode profile 选择固定为 `/etc/default/dasall-daemon` + `EnvironmentFile` + `--profile-id`，`/etc/dasall/daemon.json` 固定为 `daemon.*` override-only | PKG-TODO-002、009、013 | 明确 v1 采用 `EnvironmentFile + --profile-id` 还是补新 schema | 已完成 | 若后续要把 `profile_id` 迁入 deployment config，必须新开 schema 扩展任务而不是改写 v1 基线 |
| PKG-BLK-04 | ✅ 已解阻：`debian/control` 的 Build-Depends / Depends 已由 PKG-TODO-001 冻结，并由 PKG-TODO-008 落盘到 source metadata skeleton | PKG-TODO-001、008、009 | 目标 Ubuntu 版本上的依赖盘点完成 | 已由 PKG-TODO-001、008 完成 | 在依赖未冻结前，禁止给出“可直接构包”的结论 |
| PKG-BLK-05 | installed-package QA 实现尚未具备：PKG-TODO-003 已冻结 `lintian` / `autopkgtest` / qemu testbed 策略，但仍缺 `debian/tests` metadata/scripts 与本地 rootful smoke harness | PKG-TODO-016、017、018 | QA 策略已冻结，且 installed-package testbed 实现与 smoke harness 全部落地 | 完成 PKG-TODO-016、017 | 若 testbed 暂缺，只能宣称“build-ready packaging skeleton”，不得宣称 package gate closed |

## 9. 测试矩阵与统一验收命令

### 9.1 Build-time 单元 / 契约 / 集成矩阵

| 对应任务 | 测试清单 | 目的 |
|---|---|---|
| PKG-TODO-006 | `CliJsonOutputContractTest`、`CliExitCodeContractTest` | 守住 CLI 安装名与 contract 输出面，不让 packaging 改坏 CLI surface |
| PKG-TODO-007 | `ProfileCatalogTest`、`RuntimePolicyProviderTest`、`LLMSubsystemConfigProjectionTest` | 守住共享资产与配置投影对 `dasall-common` 的依赖面 |
| PKG-TODO-010 | `DaemonEntryConfigProjectionTest`、`CliDaemonSocketPathIntegrationTest`、`DaemonBinaryUnarySmokeTest` | 守住 socket 默认值从 `/tmp` 到 `/run` 的一致性 |
| PKG-TODO-011 | `DaemonEntryConfigProjectionTest`、`DaemonInstalledAssetRootTest`、`DaemonBinaryUnarySmokeTest` | 守住 daemon 安装态 profile 资产发现 |
| PKG-TODO-012 | `LLMSubsystemConfigProjectionTest`、`LLMBaselineAssetPathTest` | 守住 LLM baseline root 的安装态切换 |
| PKG-TODO-015 | `CliJsonOutputContractTest`、`CliExitCodeContractTest`、`DaemonPingIntegrationTest`、`CliDaemonSocketPathIntegrationTest`、`DaemonBinaryUnarySmokeTest` | 形成 package preflight 最小可复验入口 |

### 9.2 Installed-package / package QA 矩阵

| 对应任务 | 测试清单 | 目的 |
|---|---|---|
| PKG-TODO-013 | `pkg-smoke-local-control-plane` | 验证 daemon 包的 service/config/postinst/README.Debian 组合语义 |
| PKG-TODO-016 | `pkg-smoke-local-control-plane`、`pkg-smoke-common-assets` | 验证 installed-package smoke 与 autopkgtest metadata |
| PKG-TODO-017 | `pkg_smoke_install.sh`、`pkg_smoke_upgrade.sh`、`pkg_smoke_remove_purge.sh` | 验证 fresh install、显式启动、升级、remove/purge 路径 |

### 9.3 质量门矩阵

| Gate ID | 正式命令 | 对应任务 |
|---|---|---|
| PKG-GATE-01 | `DESTDIR="$PWD/stage/pkg" cmake --install build-ci` | PKG-TODO-005、006、007 |
| PKG-GATE-02 | `ctest --test-dir build-ci -R "DaemonEntryConfigProjectionTest|CliDaemonSocketPathIntegrationTest|LLMSubsystemConfigProjectionTest" --output-on-failure` | PKG-TODO-010、011、012 |
| PKG-GATE-03 | `dpkg-buildpackage -us -uc -b` | PKG-TODO-008、009 |
| PKG-GATE-04 | `bash scripts/packaging/pkg_smoke_install.sh` | PKG-TODO-013、017 |
| PKG-GATE-05 | `bash scripts/packaging/pkg_smoke_install.sh --explicit-start-check` | PKG-TODO-013、017 |
| PKG-GATE-06 | `bash scripts/packaging/pkg_smoke_upgrade.sh` | PKG-TODO-017 |
| PKG-GATE-07 | `autopkgtest ../dasall_*.changes -- qemu <ubuntu-testbed-image>` | PKG-TODO-016、017 |
| PKG-GATE-08 | `lintian ../*.changes` | PKG-TODO-017、018 |

### 9.4 统一验收命令

正式 one-shot 验收命令冻结为：

```bash
bash scripts/packaging/validate_ubuntu_dpkg_v1.sh
```

在 PKG-TODO-017 完成之前，临时分段验收命令基线为：

```bash
cmake -S . -B build-ci -G Ninja && \
cmake --build build-ci --target dasall_packaging_preflight_tests && \
dpkg-buildpackage -us -uc -b && \
autopkgtest --validate .
```

## 10. 风险与回退策略

| 风险 ID | 风险描述 | 对应设计 Risk | 触发条件 | 缓解动作 | 回退策略 |
|---|---|---|---|---|---|
| PKG-RISK-01 | install surface 过宽，把非 v1 目标一并带进包 | TODO 新增 | `cmake --install` 产出 `gateway` / `simulator` / test assets | 用 `debian/not-installed` 或精确 install rules 收口 | 回退到最小 CLI/daemon/common 安装面 |
| PKG-RISK-02 | repo-relative 路径未完全清掉，服务只能在特定 cwd 运行 | 详设缺口 7.10.8 | daemon/LLM 在安装态失败但开发态正常 | 先补 shared install layout，再修 daemon/LLM 路径 | 禁止以 `WorkingDirectory` 或 wrapper script 掩盖 |
| PKG-RISK-03 | maintainer scripts 过度复杂，升级/卸载路径不可预测 | TODO 新增 | `postinst/prerm/postrm` 开始承载业务逻辑 | 限制为 user/group + hint，其他一律交 helper 或显式运维命令 | 退回最小脚本主体 |
| PKG-RISK-04 | autopkgtest / rootful smoke 环境迟迟未落地，导致“能构包但不能证明可安装” | TODO 新增 | 只有 `.deb` 产物，没有 installed-package evidence | 提前冻结 QA 策略和 smoke harness | 只允许宣称 skeleton ready，不允许 package-ready |
| PKG-RISK-05 | manpage / README.Debian / first-install hint 三处口径漂移 | TODO 新增 | 包安装提示与文档内容不一致 | 统一由 PKG-TODO-014 收口 operator docs | 回退到 README.Debian 为唯一说明源，并缩减 postinst hint |

## 11. 可行性结论

1. 本专项可立即进入执行，不需要再重做总体打包设计；当前真正缺的是工程 install surface、路径收敛和 package QA。
2. Install surface 与 `debian/` source/binary skeleton 已完成；下一阶段聚焦 PKG-TODO-010 ~ 017 的安装态路径收敛与 installed-package QA，最后由 PKG-TODO-018 收口 gate 证据与残余风险。
3. 当前最关键的 P0 不是 `debian/control` 文本本身，而是让 `cmake --install` 和安装态路径模型先成立；否则 `debian/` 目录只会变成无法稳定维护的手工拷贝层。
4. 若执行中出现资源限制，允许把 `manpage`、`NEWS.Debian`、`sysusers.d`、`tmpfiles.d` 延后，但不允许延后 install surface、socket/path convergence、first-install UX 和 installed-package smoke。

## 12. 未决问题处置表

| OQ ID | 问题 | 当前处置 | 影响任务 | 后续触发条件 |
|---|---|---|---|---|
| PKG-OQ-01 | `profile_id` 是否应长期留在 `/etc/default/dasall-daemon`，还是后续扩展到 deployment config schema | v1 采纳 `/etc/default` + `EnvironmentFile`；schema 扩展延后 | PKG-TODO-002、013 | 若 daemon deployment config 要统一承载 profile 选择，再开增量设计 |
| PKG-OQ-02 | CLI manpage 首版是否手写，还是暂时接受后置 | 建议手写；若资源不足可不阻塞四包构建，但不能阻塞 README.Debian/operator docs | PKG-TODO-014 | 当 CLI surface 再冻结一轮时可追加 help2man / docbook 路线评估 |
| PKG-OQ-03 | 是否在 v1 引入 `sysusers.d` / `tmpfiles.d` | v1 延后，保留 `postinst + RuntimeDirectory/StateDirectory` 基线 | PKG-TODO-009、013 | 目标 Ubuntu 版本与 debhelper 支持面稳定后再评估 |
| PKG-OQ-04 | 何时把 `gateway` / `simulator` 纳入正式包 | 明确延后到本地控制面 package gate 闭合之后 | 无 | 等 `dasall`/`cli`/`daemon`/`common` 四包长期稳定再开第二阶段 |