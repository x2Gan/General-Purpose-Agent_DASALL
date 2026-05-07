# DASALL Ubuntu平台DPKG打包方案设计

文档版本：v1.0
日期：2026-05-07
状态：Draft

## 1. 模块概览

### 1.1 模块定位

本设计用于冻结 DASALL 在 Ubuntu 平台上的标准发行形态：以 Debian source package 为源头，以 DPKG 二进制包为安装载体，以受签名保护的 APT 仓库为默认分发渠道。

它覆盖的不是单个可执行文件如何被压成一个 `.deb`，而是一整套可长期维护的 Ubuntu 交付方案，重点解决以下问题：

1. DASALL 应该按什么粒度拆分为多个二进制包。
2. CLI、daemon、共享资产、配置、状态目录、systemd 单元分别落到哪个 Debian 约定路径。
3. 升级、回滚、配置保留、服务启停、依赖声明和分发信任链应该如何设计。
4. 现有仓库中的开发态默认值，如何收敛为 Ubuntu 包交付态的生产默认值。

本设计默认聚焦本地控制面与通用运行资产，即 `dasall-cli`、`dasall-daemon` 以及 profiles、Prompt baseline、Provider baseline 等共享静态资产。`gateway`、`simulator` 和未来插件生态只保留扩展位，不纳入 v1 必交付范围。

### 1.2 设计目标

1. 采用 Ubuntu / Debian 生态的主流打包路径，而不是自定义脚本拼装或一次性封装工具。
2. 保持 DASALL 现有架构边界不变，不让打包层倒逼 access、runtime、profiles、infra 的职责漂移。
3. 让本地 daemon 的运行目录、配置目录、状态目录、systemd 生命周期和权限模型符合 Debian Policy。
4. 支持长期升级维护，保证 conffile、目录迁移、服务升级和离线安装都有明确策略。
5. 让包拆分足够克制，既支持 headless daemon 部署，也避免把运行 profile 人为膨胀成大量 Debian 包变体。

### 1.3 模块边界

上游输入：

1. 根 CMake 工程构建出的 `dasall-cli`、`dasall-daemon` 等可执行目标。
2. `profiles/`、`llm/`、`knowledge/`、`infra/` 等子系统提供的静态资产与默认配置素材。
3. 现有设计文档中已经冻结的本地控制面、profile、审计、健康和安全边界。

下游输出：

1. Debian source package 与多二进制 `.deb` 产物。
2. Ubuntu 主机上的 `/usr`、`/etc`、`/run`、`/var/lib`、`/usr/share/doc` 文件布局。
3. `systemd` 服务单元、升级行为、配置保留与仓库签名策略。

非目标：

1. 不在本轮选择 Snap、Flatpak、AppImage 或 OCI 镜像作为主发行形态。
2. 不在本轮落地 `debian/` 目录、CMake install 规则、APT 仓库或 CI 流水线。
3. 不把 runtime profile 直接展开成多个 Ubuntu 包 SKU。
4. 不在本轮为 `gateway`、`simulator`、插件包、OTA 包做完整交付实现。

### 1.4 架构兼容目标与当前实现成熟度

| 维度 | 结论 | 说明 |
|---|---|---|
| 架构兼容目标 | Ready | 仓库已具备 `dasall-cli`、`dasall-daemon` 和共享资产基础，适合演化为标准 Debian 多二进制包 |
| 当前实现成熟度 | Not Ready | 根工程尚无 `install()` / `debian/` 交付层；daemon 仍保留开发态 socket 默认值与未冻结的 systemd 交付约定 |

### 1.5 范围冻结

纳入范围：

1. Ubuntu 平台下的 DPKG 包形态、包拆分、文件布局、版本命名、systemd 服务集成。
2. 共享资产、操作员配置、运行状态与签名分发的交付策略。
3. 维护脚本、配置迁移、升级/卸载语义和 QA Gate 的设计原则。

排除范围：

1. 具体 `debian/control`、`debian/rules`、`debian/*.install` 文件实现。
2. 实际的 APT 仓库部署、签名密钥托管和发布自动化。
3. 非 Ubuntu 发行渠道的优先级排序。

## 2. 约束清单

| Constraint ID | 来源 | 类型 | 约束描述 | 影响范围 |
|---|---|---|---|---|
| PKG-C001 | Debian Policy 3/4/5 | Must | 采用标准 Debian source package 元数据、关系字段、版本字段和 binary package 布局 | `debian/` 结构、版本命名 |
| PKG-C002 | Debian Policy 6 | Must | maintainer scripts 必须幂等、非交互、可重复执行，不得依赖人工确认完成正常升级 | `preinst/postinst/prerm/postrm` |
| PKG-C003 | Debian Policy 7 | Must | 二进制包之间的依赖、推荐、冲突与替换关系必须显式声明，不能靠 README 口头约束 | 包拆分、升级迁移 |
| PKG-C004 | Debian Policy 9/10 | Must | 运行时 socket 和临时状态必须落在 `/run`，操作员配置必须落在 `/etc`，共享静态资产必须落在 `/usr/share` | 文件系统布局 |
| PKG-C005 | Debian Policy 10.7 | Must | 只有允许操作员修改并需要 dpkg 保留的文件才应作为 conffile 管理 | 配置文件策略 |
| PKG-C006 | Debian Policy 9.3；`dh_installsystemd` | Must | systemd 服务启停、enable/disable、升级重启应由 debhelper 生成，不手写 `systemctl` 调用 | 服务生命周期 |
| PKG-C007 | `dpkg-maintscript-helper` | Should | 配置文件改名、删除、目录/符号链接切换必须使用标准 helper，而不是裸 shell 迁移 | 升级兼容 |
| PKG-C008 | DASALL daemon 本地控制面详细设计 | Must | v1 daemon 仍采用 direct-bind UDS 模式，不以 socket activation 为前提 | systemd 单元形态 |
| PKG-C009 | DASALL profiles 模块详细设计 | Must | runtime profile 是运行时策略资产，不应扩张成并行的 Debian 包矩阵 | 包粒度、资产组织 |
| PKG-C010 | DASALL ADR-006/007/008 | Must | 打包层只能承载部署和运维语义，不得让包安装逻辑越过 memory / runtime 的所有权边界 | 架构边界 |
| PKG-C011 | OWASP 授权基线；本地控制面设计 | Must | daemon 默认必须以最小权限运行，Unix socket 不得 world-writable，本地控制面不因“本机”而放弃授权边界 | 账户、socket 权限 |
| PKG-C012 | Debian/Ubuntu 主流实践 | Should | 常规安装通过 APT 仓库分发并用 Release/InRelease 签名建立信任链，`dpkg -i` 只作为离线或实验室安装路径 | 分发与签名 |

### 2.1 约束抽取结论

1. 本项目适合走“Debian source package + debhelper + 多二进制包”的标准路径，不适合用单文件封装器替代正式包元数据。
2. 文件布局必须把开发态路径和生产态路径分开，尤其要把 daemon socket 从 `/tmp` 收敛到 `/run`。
3. systemd 与配置迁移必须尽量声明式，减少手写 maintainer scripts。
4. DASALL 的运行 profile、权限边界与运行时所有权必须保持原有设计，不因打包而变成新的控制面。

## 3. 现状与缺口

| 目标 | 当前状态 | 缺口描述 | 风险等级 | 优先级 |
|---|---|---|---|---|
| CLI 可独立交付 | 已具备目标 | 仓库存在 `dasall-cli` 目标，但未定义安装名、manpage、包关系与升级语义 | Medium | P0 |
| daemon 可独立交付 | 已具备目标 | 仓库存在 `dasall-daemon` 目标，但未定义 service user、systemd unit、状态目录和包默认路径 | High | P0 |
| 共享资产可独立交付 | 部分实现 | profiles 与 LLM baseline 资产散落在源码树，尚未冻结安装清单与只读资产边界 | High | P0 |
| Ubuntu 生产态目录布局 | 缺失 | daemon 设计默认仍是 `/tmp/dasall/control.sock` 开发态路径，不适合作为正式包默认值 | High | P0 |
| 标准 Debian 元数据 | 缺失 | 根工程无 `debian/`、无 `install()`、无 control/changelog/rules/source format | High | P0 |
| 服务启停与升级策略 | 缺失 | 当前没有 `systemd` unit、没有升级重启策略、没有 package state 设计 | High | P1 |
| 配置保留与迁移 | 缺失 | 没有 conffile 边界、没有未来配置迁移机制，也没有 package-owned 与 operator-owned 文件划分 | High | P1 |
| QA Gate | 缺失 | 没有 `lintian`、`autopkgtest`、安装升级卸载 smoke gate | Medium | P1 |
| 签名分发 | 缺失 | 没有 `.changes/.dsc` 签名与内部 APT 仓库发布策略 | Medium | P2 |
| gateway / simulator 交付 | 预留 | 仓库有目标，但当前用户需求与本地控制面主线无关 | Low | P3 |

### 3.1 现状判断

当前最准确的结论是：DASALL 已具备被 Debian 化交付的工程基础，但仍停留在“build-ready target”而非“package-ready product”的阶段。也就是说，架构路径已经清楚，打包层尚未进入真正的工程冻结。

### 3.2 工程代码现状对打包的直接约束

基于当前工程代码，Ubuntu 包落地前至少有四个必须显式处理的安装面约束：

| 约束点 | 当前代码现状 | 若直接打包的风险 | 打包设计要求 |
|---|---|---|---|
| daemon 默认 socket 常量 | `access/include/daemon/DaemonEndpointDefaults.h` 固定为 `/tmp/dasall/control.sock` | CLI、daemon 和测试会继续指向开发态路径，生产权限模型无法收敛到 `/run` | 必须把默认值切到 `/run/dasall/daemon.sock`，且 CLI/daemon 共用同一 canonical 默认值 |
| daemon profile 资产根 | `apps/daemon/src/main.cpp` 通过 `current_path()/profiles` 构造 `profiles_root` | systemd 启动时工作目录不可依赖；一旦不在仓库根目录启动，daemon 无法发现 profile 资产 | 必须引入安装态只读资产根，推荐统一收敛到 `/usr/share/dasall` 派生 `profiles/` 子树 |
| LLM baseline 资产根 | `llm/include/LLMSubsystemConfig.h` 中 prompt/provider baseline root 仍是 `llm/assets/prompts` 与 `llm/assets/providers` | 安装后二进制若以任意 cwd 启动，将找不到 Prompt/Provider baseline 资产 | 必须把 baseline root 改为安装态绝对路径或统一资产根派生路径 |
| profile 逻辑路径与物理路径关系 | `infra/src/config/ConfigCenterFacade.cpp` 与多处测试把 `profiles/<id>/runtime_policy.yaml` 作为逻辑路径 | 若把逻辑路径误当成物理路径，会让代码、日志与安装路径耦合混乱 | 保留逻辑 `source_id` 语义，但实际文件解析必须由安装态 asset resolver 完成 |

这里有一个必须避免的伪修复：不能通过给 `systemd` unit 设置 `WorkingDirectory=/usr/share/dasall` 来掩盖 repo-relative 路径问题。这样只能让服务进程“凑巧能启动”，却无法解决 CLI、`--validate-only`、离线诊断和未来多入口场景的一致性。

## 4. 行业实践调研结论

### 4.1 Ubuntu / Debian 生态的稳定做法

1. 正式可维护的 Ubuntu 包通常以 Debian source package 为源头，而不是直接把构建目录用 `dpkg-deb` 或第三方工具打成单一 `.deb`。
2. 长生命周期服务通常拆成多个二进制包，至少区分 CLI、daemon 和共享静态资产，以避免 headless 场景被迫安装不需要的入口和调试资源。
3. 服务启停、enable/disable、升级重启由 debhelper 的 `dh_installsystemd` 统一生成，避免在 maintainer scripts 中手写 `systemctl`。
4. 操作员修改的配置放在 `/etc` 并交给 conffile 机制；只读默认资产放在 `/usr/share`，运行状态放在 `/run` 与 `/var/lib`。
5. 配置文件或目录迁移通过 `dpkg-maintscript-helper` 处理，而不是直接在 `postinst` 里 `mv/rm`，这样才能兼容 upgrade、abort-upgrade 和 purge 路径。
6. 分发信任通常建立在 APT 仓库的 Release / InRelease 签名与哈希链上，而不是在 package 安装阶段额外引入定制签名校验脚本。
7. 包质量的常规门禁不是“能生成 `.deb` 就算完成”，而是至少经过 `lintian`、安装卸载 smoke、升级 smoke 和 `autopkgtest`。

### 4.2 对 DASALL 的具体启示

| 行业实践 | 对 DASALL 的含义 |
|---|---|
| 多二进制包优于单一 fat deb | 本地控制面适合拆成 `cli + daemon + common`，既支持最小安装，也便于后续扩展 gateway/simulator |
| `/etc`、`/usr/share`、`/run`、`/var/lib` 分治 | 当前 daemon 的 `/tmp` 路径只能保留给开发态，正式包必须切到 `/run/dasall/` |
| debhelper 优先于手写脚本 | DASALL 应优先走 `dh` 序列、`dh_installsystemd` 和标准 helper，而不是自定义 shell orchestration |
| 升级迁移使用 helper | 未来只要 conffile 改名或目录重构，都要通过 `dpkg-maintscript-helper` 留出演进通道 |
| APT 仓库签名优先 | DASALL 不应在 package 安装期自行实现另一套“包签名验证器”；仓库签名和子系统自有签名各守其责 |
| profile 是运行时资产而不是包 SKU | `desktop_full`、`edge_balanced` 等 profile 仍由 runtime / profiles 选择，不应拆成多个 Ubuntu 变体包 |

## 5. 候选方案对比

### 5.1 打包工具链方案对比

| 方案名 | 工程复杂度 | 升级安全性 | systemd 集成 | 包关系表达力 | 结论 |
|---|---|---|---|---|---|
| 方案 A：CMake CPack 直接生成单个 `.deb` | 低 | 低 | 弱 | 弱 | 不采纳；更适合 demo，不适合 DASALL 这种多组件、长周期演进项目 |
| 方案 B：`fpm` / 手写 `dpkg-deb` 快速封装 | 低 | 中低 | 弱 | 中 | 不采纳；虽然快，但会把正式包治理推回脚本层 |
| 方案 C：Debian source package + `debhelper` + 多二进制包 | 中 | 高 | 高 | 高 | 采纳 |

### 5.2 二进制包拆分方案对比

| 方案名 | 包粒度 | 优点 | 风险 | 结论 |
|---|---|---|---|---|
| 方案 A：单一 `dasall` 大包 | 最粗 | 交付路径短 | 无法区分 headless、CLI-only、资产-only 场景；升级粒度过粗 | 不采纳 |
| 方案 B：`dasall-cli` + `dasall-daemon` + `dasall-common` + meta 包 | 适中 | 与仓库现状吻合，边界清晰，便于运维与扩展 | 需要设计包关系与文件布局 | 采纳 |
| 方案 C：按每个子系统拆十几个包 | 最细 | 理论上最灵活 | 过早包爆炸，版本锁复杂，Profile/资产边界会被包管理语义稀释 | 不采纳 |

## 6. 决策结论

### 6.1 最终选型

采纳以下组合方案：

1. 采用 `3.0 (quilt)` 的 Debian source package 形态维护 Ubuntu 打包层。
2. 采用 `debhelper` compat 13 风格的 `dh` 序列作为主构建入口。
3. 采用四包模型：`dasall`、`dasall-cli`、`dasall-daemon`、`dasall-common`。
4. 采用 `systemd service + direct-bind UDS` 作为 v1 守护进程交付形态，不以 socket activation 为前提。
5. 采用内部 APT 仓库签名作为主分发信任链，`dpkg -i` 只作为离线或实验室补充路径。

### 6.2 关键决策点

1. 包交付层不使用 CPack / `fpm` 作为正式主方案。
2. 正式包默认路径从开发态 `/tmp/dasall/control.sock` 收敛为生产态 `/run/dasall/daemon.sock`。
3. profile 资产继续由 DASALL runtime 在运行时选择，不变成 Ubuntu 包矩阵。
4. v1 服务单元优先保证稳定启停与升级兼容，不承诺 zero-downtime restart，也不强推 socket activation。

### 6.3 放弃其他方案的理由

1. 不采纳单个 fat deb，因为它会把 CLI、daemon、资产和未来扩展目标绑死在同一升级单位内。
2. 不采纳第三方快速封装方案，因为 DASALL 的 daemon 生命周期、conffile 策略、资产边界和升级迁移都需要 Debian 原生能力。
3. 不采纳按 profile 或子系统超细拆包，因为当前阶段更重要的是稳住本地控制面交付和共享资产边界，而不是做过度商品化切分。

## 7. 详细设计

### 7.1 Source Package 设计

建议源包名固定为 `dasall`，采用以下基线：

1. `debian/source/format`：`3.0 (quilt)`。
2. `debian/control`：统一声明 source 与四个 binary package。
3. `debian/rules`：基于 `dh`，以 CMake / Ninja 作为底层 buildsystem。
4. `debian/changelog`：采用 Debian revision，可区分上游版本与打包修订。
5. `debian/copyright`、`debian/README.Debian`：显式说明内部资产、运行目录和 purge 语义。
6. `Rules-Requires-Root`：目标设为 `no`，优先支持 rootless build。

选择 `3.0 (quilt)` 而不是 `3.0 (native)` 的原因是：DASALL 未来需要明确区分“上游代码版本”和“Ubuntu 打包修订版本”，这对回归、热修与仓库追踪更稳定。

### 7.2 二进制包矩阵

| 包名 | Architecture | 主要内容 | 关系策略 | 说明 |
|---|---|---|---|---|
| `dasall` | `all` | meta package | `Depends: dasall-cli, dasall-daemon, dasall-common (= same version)` | 默认安装入口 |
| `dasall-cli` | `any` | 用户 CLI 命令、manpage、必要时后续补 shell completion | `Depends: dasall-common`; `Recommends: dasall-daemon` | 可单独安装做客户端或诊断工具 |
| `dasall-daemon` | `any` | 守护进程二进制、systemd unit、默认 daemon conffile | `Depends: dasall-common (= same version)`；可 `Recommends: dasall-cli` | headless 节点主包 |
| `dasall-common` | `all` | 五档 profile 资产、LLM Prompt baseline、LLM Provider baseline、README.Debian | 被 CLI / daemon 共同依赖 | 避免资产重复打包 |

预留但不纳入 v1 的包：

1. `dasall-gateway`
2. `dasall-simulator`
3. `dasall-plugin-*`
4. `dasall-dev`

#### 7.2.1 v1 固定纳入包的仓库内容

结合当前代码与测试面，v1 应冻结以下仓库内容进入 `dasall-common`：

| 源码树内容 | 安装目标 | 纳入原因 |
|---|---|---|
| `profiles/desktop_full/profile.cmake` + `runtime_policy.yaml` | `/usr/share/dasall/profiles/desktop_full/` | baseline 档位资产，已被 ProfileCatalog 与集成测试消费 |
| `profiles/cloud_full/profile.cmake` + `runtime_policy.yaml` | `/usr/share/dasall/profiles/cloud_full/` | baseline 档位资产，已被 ProfileCatalog 与集成测试消费 |
| `profiles/edge_balanced/profile.cmake` + `runtime_policy.yaml` | `/usr/share/dasall/profiles/edge_balanced/` | baseline 档位资产，已被 ProfileCatalog 与集成测试消费 |
| `profiles/edge_minimal/profile.cmake` + `runtime_policy.yaml` | `/usr/share/dasall/profiles/edge_minimal/` | baseline 档位资产，已被 ProfileCatalog 与集成测试消费 |
| `profiles/factory_test/profile.cmake` + `runtime_policy.yaml` | `/usr/share/dasall/profiles/factory_test/` | 当前仓库已把它视为 baseline profile matrix 的一部分，首版不应在打包层擅自删减 |
| `llm/assets/prompts/planner/default/` | `/usr/share/dasall/llm/prompts/planner/default/` | 当前唯一 baseline Prompt 资产包 |
| `llm/assets/providers/catalog.yaml` | `/usr/share/dasall/llm/providers/catalog.yaml` | 当前唯一 baseline Provider catalog 索引 |
| `llm/assets/providers/deepseek/` | `/usr/share/dasall/llm/providers/deepseek/` | 当前唯一 baseline Provider 资产包 |

#### 7.2.2 v1 明确不纳入包的仓库内容

以下内容在 v1 不应进入正式二进制包：

1. `tests/` 下的任何测试夹具、golden 数据或二进制测试目标。
2. `docs/architecture/` 的内部设计文档全文；二进制包只保留最小 `README.Debian`。
3. `apps/gateway` 与 `apps/simulator` 对应二进制与运行资产。
4. 仅供开发态使用的 build tree、仓库相对路径假设和临时 deployment fixture。

### 7.3 安装名称与目标路径

构建目标名可以继续保留仓库内部的下划线风格，但对系统安装名称应切换为面向操作系统和运维的 Debian 风格：

1. `dasall-cli` 安装为 `/usr/bin/dasall`。
2. `dasall-daemon` 安装为 `/usr/sbin/dasall-daemon`。
3. systemd `ExecStart` 只依赖安装路径，不依赖 CMake target 名。

这样做的原因是：target 名是构建内部细节，命令名和 service 名才是对操作系统与运维方的稳定契约。

### 7.4 文件系统布局

正式 Ubuntu 包安装后的推荐布局如下：

```text
/usr/bin/dasall
/usr/sbin/dasall-daemon
/usr/share/dasall/profiles/
/usr/share/dasall/llm/prompts/
/usr/share/dasall/llm/providers/
/etc/dasall/daemon.json
/lib/systemd/system/dasall-daemon.service
/usr/share/doc/dasall/README.Debian
/usr/share/doc/dasall-cli/
/usr/share/doc/dasall-daemon/
/run/dasall/daemon.sock
/var/lib/dasall/
```

布局原则：

1. `/usr/bin` 放用户命令；`/usr/sbin` 放系统守护进程可执行文件。
2. `/usr/share/dasall/` 放包内只读共享资产，当前 v1 实际包含 `profiles/`、`llm/prompts/` 和 `llm/providers/` 三类子树。
3. `/etc/dasall/daemon.json` 是操作员可修改的部署级 conffile。
4. `/run/dasall/daemon.sock` 是运行期 Unix socket，随服务生命周期存在，不进入持久化层。
5. `/var/lib/dasall/` 用于 daemon 持久状态、缓存索引、收据存储、LKG 等需要跨重启保留的运行数据。

### 7.5 开发态默认值到包交付态默认值的收敛

当前 daemon 设计文档中的默认值偏开发态，尤其 `daemon.socket_path` 仍是 `/tmp/dasall/control.sock`。Ubuntu 包交付态应做以下收敛：

| 项目 | 当前开发态语义 | Ubuntu 包交付态语义 |
|---|---|---|
| `daemon.socket_path` | `/tmp/dasall/control.sock` | `/run/dasall/daemon.sock` |
| daemon 启动方式 | 手工运行、测试夹具、直接 CLI 覆盖 | `systemd` 托管，默认经 `systemctl` 管理 |
| 共享资产来源 | 仓库源码树或 build tree | `/usr/share/dasall/` |
| 部署级覆盖 | 临时 `--config-file` 或测试文件 | `/etc/dasall/daemon.json` 为 canonical 配置入口 |
| 持久状态 | 临时目录或测试目录 | `/var/lib/dasall/` |

这不是推翻现有 daemon 设计，而是把其开发友好默认值映射成符合 Ubuntu 发行物约束的生产默认值。

### 7.6 配置与资产策略

#### 7.6.1 配置层次

Ubuntu 包交付态下，daemon 配置来源收敛为四层，但只把部署层暴露给 dpkg：

1. 二进制内置 defaults：仍由代码或只读基线提供。
2. profile / baseline 资产：放在 `/usr/share/dasall/`，由 `dasall-common` 提供；当前 v1 实际子树为 `profiles/`、`llm/prompts/`、`llm/providers/`。
3. deployment override：固定为 `/etc/dasall/daemon.json`，由 `dasall-daemon` 提供并作为 conffile 管理。
4. runtime override：继续留在 daemon 运行时机制中，不由包管理系统直接写入。

#### 7.6.2 conffile 边界

`/etc/dasall/daemon.json` 应满足以下约束：

1. 它是操作员允许修改且需要在升级中保留的唯一 v1 daemon 主配置文件。
2. 包升级不得覆盖用户修改。
3. daemon 不得在运行时写回这个文件。
4. 与 profile 基线重合的键可以出现，但最终决策顺序仍服从 DASALL ConfigCenter 设计。

#### 7.6.3 共享资产边界

`dasall-common` 负责以下只读内容：

1. 五档 profile baseline
2. Prompt baseline
3. Provider baseline
4. 与这些 baseline 同包交付的 manifest / metadata

这些文件必须视为 package-owned 只读资产：

1. daemon 和 CLI 只能读取，不得修改。
2. 若未来支持 deployment overlay，应通过 `/etc` 或 `/var/lib` 新文件表达，而不是写回 `/usr/share/dasall/`。
3. 首版打包不得因为“看起来像测试配置”就删除 `factory_test` 这类 profile；只要它仍是 ProfileCatalog 和兼容性矩阵的一部分，就应随 `dasall-common` 一起交付。

### 7.7 systemd 服务设计

#### 7.7.1 服务模型

v1 固定采用 `dasall-daemon.service`，不提供 `dasall-daemon.socket` 作为正式交付前提。

原因：

1. 当前 daemon 详细设计已明确 v1 选择 direct-bind UDS，而非 socket activation。
2. 仓库的 platform/IIPC 设计尚未冻结 activated listener surface。
3. 先冻结 direct-bind service，不会阻断未来增加 `.socket` 单元，但可以避免过早承诺不成熟能力。

#### 7.7.2 单元建议

建议服务单元采用以下基线：

1. `Type=simple`
2. `ExecStartPre=/usr/sbin/dasall-daemon --validate-only --config-file /etc/dasall/daemon.json`
3. `ExecStart=/usr/sbin/dasall-daemon --config-file /etc/dasall/daemon.json`
4. `User=dasall`
5. `Group=dasall`
6. `RuntimeDirectory=dasall`
7. `StateDirectory=dasall`
8. `UMask=007`
9. `Restart=on-failure`
10. `RestartSec=2`
11. `NoNewPrivileges=yes`
12. `PrivateTmp=yes`
13. `ProtectSystem=strict`
14. `ProtectHome=yes`
15. `RestrictAddressFamilies=AF_UNIX`

其中：

1. `RuntimeDirectory=dasall` 用来承载 `/run/dasall/daemon.sock`。
2. `StateDirectory=dasall` 用来承载 `/var/lib/dasall/`。
3. `ExecStartPre` 用于复用现有 `--validate-only` 路径，在服务启动前做本地配置校验。
4. `Type=simple` 是 v1 稳妥基线；待 daemon 真正具备对外 supervisor notify 语义后，再评估切到 `Type=notify`。
5. 不应通过 `WorkingDirectory=/usr/share/dasall` 等方式补洞；一旦需要依赖 WorkingDirectory 才能启动，说明安装态路径收敛没有完成。

#### 7.7.3 服务启停与升级

建议通过 `dh_installsystemd` 管理以下语义：

1. fresh install 默认不 enable、也不 start `dasall-daemon.service`。
2. 安装成功后只输出一段简短、非交互式的首次部署提示，指导操作者进入配置与显式启用流程。
3. v1 升级路径同样优先保持显式人工控制，不承诺 zero-downtime，也不依赖包安装阶段自动拉起服务。
4. 只有在明确验证 daemon 能安全承受文件被替换而不中断主链，且完成“首装不自动启动”和“升级可控重启”两条语义拆分后，才评估切到更积极的 upgrade restart 策略。

因此，v1 的设计偏向“首装不惊扰、部署显式确认、升级安全优先”，而不是最小停机时间或安装即服务可用。

#### 7.7.4 首次安装后的提示策略

首次安装成功后，可以也应该给出必要提示，但提示必须满足 Debian maintainer script 的非交互约束：

1. 只输出一小段确定性的 next steps 文本，不进入 wizard，不要求用户现场回答问题。
2. 提示内容必须可在无控制终端场景下安全跳过；也就是说，即使提示没人看见，包也应保持已成功安装。
3. 详细说明放到 `README.Debian`，postinst 只打印最短可执行路径。

建议提示内容收敛为四类信息：

1. profile 选择入口：`/etc/default/dasall-daemon`
2. daemon 覆盖配置文件位置：`/etc/dasall/daemon.json`
3. 配置校验命令：`/usr/sbin/dasall-daemon --validate-only --profile-id=<profile-id> --config-file /etc/dasall/daemon.json`
4. 首次显式启动命令：`systemctl enable --now dasall-daemon.service`
5. 如需本地非 root CLI 访问，提示把操作员加入 `dasall` 组

不建议做法：

1. 在 `postinst` 中启动交互式首次部署向导
2. 依赖 `debconf` 去承载复杂部署流程
3. 把 README 全文直接刷到终端

如果后续希望让 APT 前端在升级时展示更完整的变更说明，可以额外提供 `NEWS.Debian`；但它不应替代首次安装后的简短 postinst 提示。

### 7.8 服务账户与本地权限模型

#### 7.8.1 账户策略

正式包应引入专用系统账户与组：

1. 用户：`dasall`
2. 组：`dasall`

推荐语义：

1. daemon 以 `dasall:dasall` 运行。
2. `/run/dasall/daemon.sock` 的拥有者为 `dasall:dasall`。
3. socket 权限默认 `0660`，不允许 `0666`。
4. 需要本地 CLI 访问的操作员，通过显式加入 `dasall` 组或走 root / sudo 路径，而不是放宽 socket 权限。

#### 7.8.2 用户创建策略

考虑 Ubuntu LTS 兼容性，设计上采用“双轨保守策略”：

1. v1 直接落地基线：保留一个最小、幂等的 `dasall-daemon.postinst` 用于创建 `dasall` system user/group，并让 `dh_installsystemd` 生成的服务启停片段继续通过 `#DEBHELPER#` 注入。
2. 若未来目标环境统一具备更新版 debhelper / sysusers 支持，可把自定义 user creation 迁移为更声明式的 `sysusers.d` 集成，并删除自定义脚本主体。

这样既保守兼容 Ubuntu LTS，也为后续声明式演进留出空间。

### 7.9 版本命名与关系字段策略

#### 7.9.1 版本命名

建议采用标准 Debian 版本语义：

1. 正式发布：`0.1.0-1`
2. 打包修订：`0.1.0-2`
3. 预发布：`0.1.0~rc1-1`
4. 快照构建：`0.1.0~git20260507.<shortsha>-1`

原则：

1. 上游版本表达 DASALL 本身的发布语义。
2. Debian revision 表达 Ubuntu 打包层修订。
3. 不滥用 epoch。

#### 7.9.2 关系字段

建议关系基线如下：

1. `dasall`：`Depends: dasall-cli, dasall-daemon, dasall-common (= ${binary:Version})`
2. `dasall-cli`：`Depends: ${shlibs:Depends}, ${misc:Depends}, dasall-common (= ${binary:Version})`
3. `dasall-cli`：`Recommends: dasall-daemon (= ${binary:Version})`
4. `dasall-daemon`：`Depends: ${shlibs:Depends}, ${misc:Depends}, dasall-common (= ${binary:Version})`
5. `dasall-common`：`Architecture: all`，可评估 `Multi-Arch: foreign`

v1 不建议引入复杂的 `Provides` / virtual package 体系，除非未来确实出现多个可替换实现。

### 7.10 Debian 目录设计草案

#### 7.10.1 必选文件清单

以下是首版可直接落地的 `debian/` 目录建议文件集：

| 文件 | 归属 | 作用 | 备注 |
|---|---|---|---|
| `debian/control` | source | 定义 source package 与四个 binary package | 必须显式声明 `dasall`、`dasall-cli`、`dasall-daemon`、`dasall-common` |
| `debian/changelog` | source | Debian revision 与发布轨迹 | 版本命名遵循本设计 7.9.1 |
| `debian/rules` | source | `dh` 入口与少量 override | 不应承载大量拷贝脚本 |
| `debian/source/format` | source | 固定为 `3.0 (quilt)` | v1 必选 |
| `debian/copyright` | source | 版权与来源说明 | v1 必选 |
| `debian/dasall-daemon.README.Debian` | binary doc | 说明首次部署、运行目录、配置位置、数据保留与运维提示 | 推荐放入 `dasall-daemon`，而不是 `dasall-common` |
| `debian/dasall-daemon/etc/default/dasall-daemon` | binary config | systemd `EnvironmentFile`，承载当前代码仍需经命令行传入的 `profile_id` | 建议通过 package-local payload tree 安装 |
| `debian/dasall-daemon/etc/dasall/daemon.json` | binary config | daemon 覆盖配置模板 | 建议通过 package-local payload tree 安装 |
| `debian/dasall-cli.install` | binary | `dasall-cli` 安装清单 | 消费 `debian/tmp` 中的 CLI 安装面 |
| `debian/dasall-daemon.install` | binary | `dasall-daemon` 安装清单 | 仅消费 `debian/tmp` 中的 daemon 二进制安装面 |
| `debian/dasall-common.install` | binary | `dasall-common` 安装清单 | 消费 `debian/tmp` 中的 profiles 与 llm baseline 资产 |
| `debian/dasall-daemon.service` | binary | systemd unit | 由 `dh_installsystemd` 安装与注入 maintscript 片段 |
| `debian/dasall-daemon.postinst` | binary | 创建 system user/group、在 fresh install 后输出最小 next steps 提示，并保留 `#DEBHELPER#` 钩子 | v1 唯一建议保留的自定义 maintainer script 主体 |
| `debian/tests/control` | source | autopkgtest 用例定义 | v1 至少有 2 条 installed-package smoke |
| `debian/tests/pkg-smoke-local-control-plane` | source | 安装后 systemd + CLI 本地控制面 smoke | 建议 `needs-root` + `isolation-machine` |
| `debian/tests/pkg-smoke-common-assets` | source | 校验 `dasall-common` 资产存在性与最小一致性 | 不依赖外部网络 |

#### 7.10.2 条件性文件清单

以下文件建议作为条件性或延后项，而不是 v1 首日阻塞项：

| 文件 | 是否首日必需 | 说明 |
|---|---|---|
| `debian/dasall.1` 或等价 manpage 源文件 | 建议是 | 当前 CLI 采用 `help` / `version` 子命令，不适合直接依赖 `help2man`，更建议手写 manpage |
| `debian/dasall-cli.bash-completion` 等 completion 文件 | 否 | 等 CLI 参数面进一步冻结后再补 |
| `debian/dasall-daemon.NEWS` | 否 | 若后续希望在升级时展示重要变更，可作为补充，但不替代 postinst 的首次部署提示 |
| `debian/dasall-daemon.sysusers` | 否 | 仅当目标环境统一支持声明式 sysusers 时启用 |
| `debian/dasall-daemon.tmpfiles` | 否 | 当前有 `RuntimeDirectory` 与 `StateDirectory`，首版不强依赖 tmpfiles |
| `debian/not-installed` | 视实现而定 | 若首版 `install()` 面过宽，可用它显式排除 `gateway`、`simulator` 等非 v1 目标 |
| `debian/lintian-overrides` | 否 | 仅当真实包扫描出现无法接受但已评审的噪声时再加 |

#### 7.10.3 各二进制包的安装清单草案

建议每个 `.install` 文件承载如下语义：

| 文件 | 建议安装内容 | 安装目标 |
|---|---|---|
| `debian/dasall-cli.install` | `debian/tmp/usr/bin/dasall`；manpage | `/usr/bin/`、`/usr/share/man/man1/` |
| `debian/dasall-daemon.install` | `debian/tmp/usr/sbin/dasall-daemon` | `/usr/sbin/` |
| `debian/dasall-common.install` | `debian/tmp/usr/share/dasall/profiles/**`；`debian/tmp/usr/share/dasall/llm/prompts/**`；`debian/tmp/usr/share/dasall/llm/providers/**` | `/usr/share/dasall/` |
| `debian/dasall-daemon/` package-local payload tree | `etc/default/dasall-daemon`；`etc/dasall/daemon.json` | `/etc/default/`、`/etc/dasall/` |
| `debian/dasall-daemon.README.Debian` | 由 `dh_installdocs` 自动安装 | `/usr/share/doc/dasall-daemon/` |

执行原则：

1. `.install` 应优先消费 `dh_auto_install` 写入 `debian/tmp` 的安装面，而不是直接从源码树复制。
2. 若首版为了尽快起步，需要从 `debian/` 目录提供默认 `daemon.json` 模板，可以接受；但长期应收敛到由工程 install surface 统一产出。
3. meta package `dasall` 不需要 `.install` 文件，保持空壳依赖包即可。

#### 7.10.4 `debian/control` 直接落地草案

`debian/control` 在 v1 应至少具备以下结构：

1. `Source: dasall`
2. `Section: utils` 或内部约定 section
3. `Priority: optional`
4. `Maintainer:` 使用项目维护身份
5. `Rules-Requires-Root: no`
6. `Build-Depends:` 至少包含 `debhelper-compat (= 13)`、CMake 构建链，以及与当前 CMake 依赖树一致的开发包
7. 四个 binary package stanza：`dasall`、`dasall-cli`、`dasall-daemon`、`dasall-common`

其中各二进制包说明建议如下：

1. `dasall`：纯 meta package，不携带实际 payload。
2. `dasall-cli`：描述为 “DASALL local control plane client”。
3. `dasall-daemon`：描述为 “DASALL local control plane daemon”。
4. `dasall-common`：描述为 “DASALL shared runtime assets and baseline profiles”。

#### 7.10.5 `debian/rules` 直接落地草案

`debian/rules` 的首版职责应严格收敛在以下几点：

1. 调用 `dh`。
2. 指定 CMake buildsystem 与独立 build directory。
3. 通过 `override_dh_auto_configure` 把 `CMAKE_INSTALL_PREFIX=/usr` 等标准参数传给工程。
4. 通过 `override_dh_installsystemd` 为 `dasall-daemon.service` 显式应用 `--no-start --no-enable`，冻结“首装成功但不自动运行”的策略。
5. 通过 `override_dh_auto_test` 跑“打包相关最小验证集”，而不是把整个仓库集成测试全塞进包构建。

建议的最小验证集分层：

1. build-time contract：`CliJsonOutputContractTest`、`CliExitCodeContractTest`
2. build-time integration：`DaemonPingIntegrationTest`、`CliDaemonSocketPathIntegrationTest`、`DaemonBinaryUnarySmokeTest`
3. installed-package validation：交给 `autopkgtest`

这能把“构建树内验证”和“安装后验证”分开，避免 `dh_auto_test` 变成一个又慢又脆的全集成门。

#### 7.10.6 maintainer scripts 边界草案

v1 建议把自定义 maintainer scripts 压缩到最低：

| 文件 | 是否需要 | 建议职责 |
|---|---|---|
| `debian/dasall-daemon.postinst` | 是 | 幂等创建 `dasall` system user/group；当 `$1=configure` 且无旧版本参数时输出一次最小首次部署提示；并保留 `#DEBHELPER#` 以让 `dh_installsystemd` 注入服务逻辑 |
| `debian/dasall-daemon.prerm` | 原则上否 | 除非后续出现必须在停止前执行的迁移逻辑，否则不自定义 |
| `debian/dasall-daemon.postrm` | 原则上否 | `remove` / `purge` 主要交给 dpkg conffile 机制与 debhelper；不主动清空 `/var/lib/dasall` |
| `debian/dasall-daemon.preinst` | 原则上否 | 只有发生 conffile rename 或目录切换时，才引入并配合 `dpkg-maintscript-helper` |

这里的核心约束是：

1. 不在 maintainer scripts 中访问网络。
2. 不在 maintainer scripts 中做 runtime policy 裁定。
3. 不在 maintainer scripts 中用 shell 手写 systemd enable/start/stop。
4. 首次安装提示必须是非交互、可跳过、一次性文本，不得阻塞 unattended install。
5. 只有当 conffile/目录迁移确实发生时，才引入 `dpkg-maintscript-helper` 片段。

#### 7.10.7 autopkgtest 草案

建议首版 `debian/tests/control` 至少定义两条测试：

1. `pkg-smoke-local-control-plane`
2. `pkg-smoke-common-assets`

推荐语义如下：

| 测试名 | Depends | Restrictions | 验证目标 |
|---|---|---|---|
| `pkg-smoke-local-control-plane` | `@, systemd` | `needs-root`, `isolation-machine` | 服务能启动、socket 存在、CLI `ping` / `readiness` / `version` 工作 |
| `pkg-smoke-common-assets` | `@` | `isolation-container` 或空 | 五档 profile 与两类 LLM baseline 资产全部存在 |

`pkg-smoke-local-control-plane` 建议检查步骤：

1. `systemctl enable --now dasall-daemon.service`
2. `systemctl is-active dasall-daemon.service`
3. `test -S /run/dasall/daemon.sock`
4. 校验 socket owner/group/mode
5. 执行 `dasall ping --json`
6. 执行 `dasall readiness --json`
7. 执行 `dasall version`

`pkg-smoke-common-assets` 建议检查步骤：

1. 校验 `/usr/share/dasall/profiles/desktop_full/runtime_policy.yaml` 等五档资产存在
2. 校验 `/usr/share/dasall/llm/prompts/planner/default/manifest.yaml` 存在
3. 校验 `/usr/share/dasall/llm/providers/catalog.yaml` 与 `deepseek/manifest.yaml` 存在

#### 7.10.8 落地前必须补齐的安装面收敛项

为了让上面的 `debian/` 草案真正可落地，工程代码必须在首轮实现中补齐以下收敛项：

1. 引入统一安装态只读资产根，推荐 `/usr/share/dasall`。
2. daemon 的 profile loader 不再依赖 `current_path()/profiles`。
3. LLM Prompt/Provider baseline root 不再依赖仓库相对路径。
4. CLI / daemon 的默认 socket 常量切换到 `/run/dasall/daemon.sock`。
5. `ExecStartPre --validate-only` 所依赖的配置与资产解析路径在 systemd 下可稳定工作。

这些不是“额外优化项”，而是 package-ready 的硬前提。

#### 7.10.9 `debian/` 逐文件可执行草案

以下内容不是“目录清单说明”，而是首版可以直接抄成文件骨架的模板。使用前提有三条：

1. 工程侧已经按 7.10.8 补齐 install surface。
2. 文中所有尖括号占位内容都必须在首轮落地时替换。
3. 当前代码里 `deployment_config` 只支持 `daemon.*` 覆盖，不支持在配置文件内设置 `profile_id`；因此 profile 选择必须先放在 `systemd EnvironmentFile`，而不是塞进 `/etc/dasall/daemon.json`。

#### 7.10.9.1 `debian/control`

建议首版直接写成：

```debcontrol
Source: dasall
Section: utils
Priority: optional
Maintainer: x2Gan <Whisky.Gan@gmail.com>
Build-Depends:
 debhelper-compat (= 13),
 cmake,
 ninja-build,
 pkgconf
Standards-Version: 4.7.0
Rules-Requires-Root: no
Homepage: https://github.com/x2Gan/General-Purpose-Agent_DASALL
Vcs-Git: https://github.com/x2Gan/General-Purpose-Agent_DASALL.git
Vcs-Browser: https://github.com/x2Gan/General-Purpose-Agent_DASALL
Testsuite: autopkgtest

Package: dasall
Architecture: all
Depends:
 ${misc:Depends},
 dasall-cli (= ${binary:Version}),
 dasall-daemon (= ${binary:Version}),
 dasall-common (= ${binary:Version})
Description: DASALL meta package
 DASALL is a local-first Agent OS distribution for Ubuntu.
 .
 This package is a meta package that pulls in the supported
 CLI, daemon and shared runtime assets.

Package: dasall-cli
Architecture: any
Depends:
 ${shlibs:Depends},
 ${misc:Depends},
 dasall-common (= ${binary:Version})
Recommends:
 dasall-daemon (= ${binary:Version})
Description: DASALL local control plane client
 The DASALL CLI sends local control requests to the DASALL daemon
 over the Unix domain socket.
 .
 Install this package on operator workstations or nodes that need
 the supported command line surface.

Package: dasall-daemon
Architecture: any
Depends:
 ${shlibs:Depends},
 ${misc:Depends},
 adduser,
 dasall-common (= ${binary:Version})
Description: DASALL local control plane daemon
 The DASALL daemon hosts the local control plane and owns the
 Unix domain socket used by the CLI.
 .
 The service is installed but not enabled or started automatically
 on first install. Operators must complete first deployment steps
 explicitly before enabling the service.

Package: dasall-common
Architecture: all
Depends:
 ${misc:Depends}
Description: DASALL shared runtime assets and baseline profiles
 This package contains shared read-only assets required by the
 supported DASALL runtime packages, including baseline profiles and
 LLM prompt/provider assets.
```

字段约束：

1. `Build-Depends` 在 noble 基线先冻结为 `debhelper-compat (= 13), cmake, ninja-build, pkgconf`；`g++`/`make` 等 build-essential 依赖按 Debian Policy 7.7 不显式列入 control。
2. `dasall-daemon` 若继续在 `postinst` 使用 `adduser` / `addgroup`，就应把 `adduser` 显式列入运行时依赖。
3. `dasall` 是 meta package，不要给它再塞 payload 文件。

#### 7.10.9.2 `debian/changelog`

建议首版直接写成：

```text
dasall (0.1.0-1) <ubuntu-series>; urgency=medium

  * Initial Debian packaging for DASALL.
  * Split delivery into dasall-cli, dasall-daemon and dasall-common.
  * Keep dasall-daemon disabled and stopped on first install.

 -- <DASALL Release Engineering> <packaging@example.com>  <rfc2822-timestamp>
```

占位规则：

1. `<ubuntu-series>` 用目标发行版代号，如 `noble`；若先走通用源包流，可先用 `unstable`。
2. `<rfc2822-timestamp>` 必须替换成符合 Debian changelog 语法的时间戳。

#### 7.10.9.3 `debian/rules`

建议首版直接写成：

```makefile
#!/usr/bin/make -f

export DH_VERBOSE = 1
export DEB_BUILD_MAINT_OPTIONS = hardening=+all

BUILD_DIR := obj-$(DEB_HOST_GNU_TYPE)

%:
	dh $@ --buildsystem=cmake --builddirectory=$(BUILD_DIR)

override_dh_auto_configure:
	dh_auto_configure --builddirectory=$(BUILD_DIR) -- \
		-DCMAKE_BUILD_TYPE=RelWithDebInfo \
		-DCMAKE_INSTALL_PREFIX=/usr

override_dh_installsystemd:
	dh_installsystemd --no-start --no-enable

override_dh_auto_test:
ifeq (,$(filter nocheck,$(DEB_BUILD_OPTIONS)))
	ctest --test-dir $(BUILD_DIR) --output-on-failure \
		-R '^(CliJsonOutputContractTest|CliExitCodeContractTest|DaemonPingIntegrationTest|CliDaemonSocketPathIntegrationTest|DaemonBinaryUnarySmokeTest)$$'
else
	@echo "Skipping tests because DEB_BUILD_OPTIONS contains nocheck"
endif
```

字段约束：

1. 这里故意不在 `debian/rules` 中写手工 `cp`，让安装内容全部回到 `.install` 文件和工程 install surface。
2. 如果工程后续需要 `CMAKE_INSTALL_SYSCONFDIR`、`CMAKE_INSTALL_LOCALSTATEDIR` 等 GNUInstallDirs 参数，再追加到 `override_dh_auto_configure`，不要在首版凭空发明私有 CMake 变量。

#### 7.10.9.4 `debian/source/format`

建议首版直接写成：

```text
3.0 (quilt)
```

#### 7.10.9.5 `debian/copyright`

建议首版直接写成：

```text
Format: https://www.debian.org/doc/packaging-manuals/copyright-format/1.0/
Upstream-Name: DASALL
Upstream-Contact: <DASALL Maintainer Team> <packaging@example.com>
Source: https://<source-repository-url>

Files: *
Copyright: 2026 <copyright-holder>
License: DASALL-Proprietary
 <Insert the full DASALL license text here.>
 .
 <If internal-only distribution is intended, state the distribution boundary here.>

Files: third_party/*
Copyright: <third-party-copyright-holder>
License: <third-party-license-id>
 <Insert the corresponding third-party license text here when those files are shipped.>
```

字段约束：

1. 如果 `third_party/` 内容不会进入任何二进制包，可以删掉第二个 stanza。
2. 不要把“仓库整体许可证”与“实际被装进包里的文件许可证”混为一谈，按最终 payload 列明即可。

#### 7.10.9.6 `debian/dasall-daemon/etc/default/dasall-daemon`

建议首版直接写成：

```sh
DASALL_DAEMON_PROFILE_ID=desktop_full
```

原因：当前代码只支持通过 `--profile-id` 选择 profile，尚不支持在 `/etc/dasall/daemon.json` 中声明 profile。

#### 7.10.9.7 `debian/dasall-daemon/etc/dasall/daemon.json`

建议首版直接写成：

```json
{
  "daemon": {
	"socket_path": "/run/dasall/daemon.sock",
	"startup_mode": "direct_bind",
	"diag_enabled": false,
	"override_enabled": false,
	"watchdog_enabled": false,
	"log_format": "text"
  }
}
```

字段约束：

1. 当前 parser 按文件扩展名分派格式，因此包里若命名为 `daemon.json`，内容就必须保持 JSON，而不是 YAML。
2. 当前 parser 会拒绝空的 `daemon` 对象，因此不能把模板写成 `{ "daemon": {} }`。
3. 这个文件只承载 `daemon.*` 覆盖项，不承载 `profile_id`。

#### 7.10.9.8 `debian/dasall-cli.install`

建议首版直接写成：

```text
usr/bin/dasall
```

若同一轮补齐 manpage，再追加：`usr/share/man/man1/dasall.1`。

#### 7.10.9.9 `debian/dasall-daemon.install`

建议首版直接写成：

```text
usr/sbin/dasall-daemon
```
		"socket_path": "/run/dasall/daemon.sock",
		"startup_mode": "direct_bind",
		"diag_enabled": false,
		"override_enabled": false,
		"watchdog_enabled": false,
		"log_format": "text"

#### 7.10.9.10 `debian/dasall-common.install`

建议首版直接写成：

```text
usr/share/dasall/profiles/
usr/share/dasall/llm/prompts/
usr/share/dasall/llm/providers/
```

#### 7.10.9.11 `debian/dasall-daemon.service`

建议首版直接写成：

```ini
[Unit]
Description=DASALL local control plane daemon
After=local-fs.target
ConditionPathExists=/etc/dasall/daemon.json

[Service]
Type=simple
Environment=DASALL_DAEMON_PROFILE_ID=desktop_full
EnvironmentFile=-/etc/default/dasall-daemon
User=dasall
Group=dasall
RuntimeDirectory=dasall
RuntimeDirectoryMode=0770
StateDirectory=dasall
StateDirectoryMode=0750
UMask=007
ExecStartPre=/usr/sbin/dasall-daemon --validate-only --profile-id=${DASALL_DAEMON_PROFILE_ID} --config-file /etc/dasall/daemon.json
ExecStart=/usr/sbin/dasall-daemon --profile-id=${DASALL_DAEMON_PROFILE_ID} --config-file /etc/dasall/daemon.json
Restart=on-failure
RestartSec=2
NoNewPrivileges=yes
PrivateTmp=yes
ProtectSystem=strict
ProtectHome=yes
RestrictAddressFamilies=AF_UNIX

[Install]
WantedBy=multi-user.target
```

字段约束：

1. `Environment=` 提供保底 profile，`EnvironmentFile=` 提供运维可编辑入口，这样即使 `/etc/default/dasall-daemon` 缺失，服务也仍有确定默认值。
2. `ExecStartPre` 和 `ExecStart` 都显式带 `--profile-id`，是为了绕开当前代码还不能从 deployment config 读取 profile 的限制。
3. 这份 unit 仍然依赖 7.10.8 中列出的路径收敛项已经完成，否则 `ExecStartPre` 会先失败。

#### 7.10.9.12 `debian/dasall-daemon.postinst`

建议首版直接写成：

```sh
#!/bin/sh
set -eu

create_dasall_group() {
	if ! getent group dasall >/dev/null 2>&1; then
		addgroup --system dasall
	fi
}

create_dasall_user() {
	if ! getent passwd dasall >/dev/null 2>&1; then
		adduser \
			--system \
			--ingroup dasall \
			--home /var/lib/dasall \
			--no-create-home \
			--disabled-login \
			--shell /usr/sbin/nologin \
			dasall
	fi
}

print_first_install_hint() {
	cat <<'EOF'
DASALL was installed successfully.
The daemon was not started automatically.

Next steps:
  1. Select the runtime profile in /etc/default/dasall-daemon
  2. Review daemon overrides in /etc/dasall/daemon.json
  3. Validate: /usr/sbin/dasall-daemon --validate-only --profile-id=<profile-id> --config-file /etc/dasall/daemon.json
  4. Start: systemctl enable --now dasall-daemon.service
  5. Optional: add local operators to the dasall group for non-root CLI access
EOF
}

case "${1:-}" in
	configure)
		create_dasall_group
		create_dasall_user
		if [ -z "${2:-}" ]; then
			print_first_install_hint
		fi
		;;
esac

#DEBHELPER#

exit 0
```

字段约束：

1. 这里故意不手写 `systemctl enable`、`start` 或 `stop`，把服务生命周期完全交给 `dh_installsystemd`。
2. 首次安装提示只打印文字，不读取用户输入，也不依赖 TTY。

#### 7.10.9.13 `debian/dasall-daemon.README.Debian`

建议首版直接写成：

```text
DASALL on Ubuntu
================

This package installs the DASALL daemon but does not start it automatically
on first install.

First deployment steps
----------------------

1. Select the runtime profile in /etc/default/dasall-daemon.
   The shipped default is desktop_full.

2. Review daemon overrides in /etc/dasall/daemon.json.
   This file only supports daemon.* keys.

3. Validate the deployment configuration:

   /usr/sbin/dasall-daemon --validate-only --profile-id=<profile-id> --config-file /etc/dasall/daemon.json

4. Enable and start the daemon explicitly:

   systemctl enable --now dasall-daemon.service

5. Verify the service:

   systemctl status dasall-daemon.service
   dasall ping --json

Runtime paths
-------------

Read-only assets: /usr/share/dasall/
Configuration:    /etc/default/dasall-daemon and /etc/dasall/daemon.json
Runtime socket:   /run/dasall/daemon.sock
State data:       /var/lib/dasall/

Local CLI access
----------------

To allow non-root local CLI access, add the operator to the dasall group,
then start a new login session before using the CLI.
```

#### 7.10.9.14 `debian/tests/control`

建议首版直接写成：

```text
Tests: pkg-smoke-local-control-plane
Depends: @, systemd
Restrictions: needs-root, isolation-machine

Tests: pkg-smoke-common-assets
Depends: @
Restrictions: isolation-container
```

#### 7.10.9.15 `debian/tests/pkg-smoke-local-control-plane`

建议首版直接写成：

```sh
#!/bin/sh
set -eu

. /etc/default/dasall-daemon
: "${DASALL_DAEMON_PROFILE_ID:?missing DASALL_DAEMON_PROFILE_ID}"

cleanup() {
	systemctl disable --now dasall-daemon.service >/dev/null 2>&1 || true
}

trap cleanup EXIT

/usr/sbin/dasall-daemon \
	--validate-only \
	--profile-id="${DASALL_DAEMON_PROFILE_ID}" \
	--config-file /etc/dasall/daemon.json

systemctl daemon-reload
systemctl enable --now dasall-daemon.service
systemctl is-active --quiet dasall-daemon.service

test -S /run/dasall/daemon.sock
test "$(stat -c '%U:%G' /run/dasall/daemon.sock)" = "dasall:dasall"
test "$(stat -c '%a' /run/dasall/daemon.sock)" = "660"

dasall ping --json >/dev/null
dasall readiness --json >/dev/null
dasall version >/dev/null
```

#### 7.10.9.16 `debian/tests/pkg-smoke-common-assets`

建议首版直接写成：

```sh
#!/bin/sh
set -eu

for profile in desktop_full cloud_full edge_balanced edge_minimal factory_test; do
	test -f "/usr/share/dasall/profiles/${profile}/profile.cmake"
	test -f "/usr/share/dasall/profiles/${profile}/runtime_policy.yaml"
done

test -f /usr/share/dasall/llm/prompts/planner/default/manifest.yaml
test -f /usr/share/dasall/llm/providers/catalog.yaml
test -f /usr/share/dasall/llm/providers/deepseek/manifest.yaml
```

#### 7.10.9.17 首轮落地时可以省略的文件

以下文件在首轮可以显式不创建：

1. `debian/dasall.install`，因为 `dasall` 是 meta package。
2. `debian/dasall-daemon.preinst`、`prerm`、`postrm`，因为首版还没有 conffile rename 或复杂迁移。
3. `debian/compat`，因为已采用 `debhelper-compat (= 13)`。

### 7.11 构建与安装编排策略

正式工程落地时，应遵循以下原则：

1. 包构建入口是 `dpkg-buildpackage` / `debuild`，而不是直接调用 `cmake --build` 后手工拷贝。
2. 底层 buildsystem 仍可复用 CMake + Ninja，但要通过 `dh_auto_configure` / `dh_auto_build` / `dh_auto_test` 接入。
3. 包安装清单应来自正式 install manifest，而不是在 `debian/rules` 中硬编码大量 `cp`。
4. 若测试链过重，可以允许 `DEB_BUILD_OPTIONS=nocheck` 跳过测试，但正式发布流水线必须单独跑完整 gate。

换句话说：Ubuntu 打包层应该消费稳定的 install surface，而不是直接消费 build tree。

### 7.12 升级、卸载与迁移语义

#### 7.12.1 Maintainer Scripts 原则

maintainer scripts 必须满足：

1. 幂等
2. 非交互
3. 不写业务逻辑
4. 不在安装期访问网络
5. 不在安装期做 runtime policy 裁定

#### 7.12.2 conffile / 路径迁移

未来若出现以下变化，必须通过 `dpkg-maintscript-helper` 处理：

1. `/etc/dasall/daemon.json` 改名
2. `/etc/dasall/daemon.json` 被拆分或删除
3. `/var/lib/dasall` 与其他路径之间发生目录/符号链接切换

禁止做法：

1. 在 `postinst` 中无条件 `mv` 用户配置。
2. 在 `prerm` / `postrm` 中直接暴力删除用户可能修改过的配置。

#### 7.12.3 remove 与 purge 语义

建议语义如下：

1. `remove`：移除包文件、停服务，但保留 `/etc/dasall/daemon.json` 与 `/var/lib/dasall/`。
2. `purge`：移除 conffile，但默认仍保守保留 `/var/lib/dasall/` 中可能包含的业务状态、记忆或审计证据。

选择保守保留 `/var/lib/dasall/` 的原因是：DASALL 是状态型 Agent OS，持久化内容可能包含上下文、审计、任务证据或用户资产；把 purge 等同于“清空一切业务数据”风险过高。真正的数据销毁应由显式管理命令或独立运维手册承担。

### 7.13 安全与信任链设计

#### 7.13.1 分发信任链

v1 采用标准 Ubuntu 路径：

1. `.dsc` / `.changes` 由发布方 GPG 签名。
2. APT 仓库通过 `Release` / `InRelease` 提供索引签名与哈希链。
3. 常规生产安装通过 `apt install dasall` 完成。
4. `dpkg -i *.deb` 仅作为离线、实验室或故障恢复路径。

#### 7.13.2 与子系统签名边界的关系

包分发信任与 DASALL 子系统内的签名语义必须分层：

1. Ubuntu 包签名解决“发行物是否可信”。
2. plugin / OTA / asset 内部签名解决“运行期装载对象是否可信”。
3. package 安装脚本不替代 plugin / OTA 的运行期验签逻辑。

#### 7.13.3 最小权限原则

1. daemon 默认不监听 TCP / HTTP，只监听本地 Unix socket。
2. socket 权限默认对 `dasall` 组可读写，对 others 拒绝。
3. systemd unit 不授予额外 Linux capabilities，除非后续出现明确需求和单独评审。

### 7.14 QA 与发布 Gate

正式工程实现时，建议至少具备以下 gate：

1. `lintian` 静态检查
2. clean install smoke
3. upgrade smoke
4. remove / purge smoke
5. `autopkgtest` 本地控制面用例
6. 内部 APT 仓库签名与索引一致性检查

建议的 `autopkgtest` 最小场景：

1. 安装 `dasall-cli`、`dasall-daemon`、`dasall-common`
2. 验证 `dasall-daemon.service` 能启动
3. 验证 `/run/dasall/daemon.sock` 被创建且权限正确
4. 执行 `dasall ping --json`
5. 执行 `dasall version`
6. 做一次升级后重启 smoke

### 7.15 预留扩展点

v1 明确保留但不立即实现的扩展点包括：

1. `dasall-daemon.socket` 与 socket activation
2. `Type=notify` 与外部 supervisor READY/WATCHDOG 集成
3. `dasall-gateway` / `dasall-simulator` 独立包
4. 资产按域拆分为 `dasall-llm-assets`、`dasall-knowledge-assets` 等更细粒度包
5. 更声明式的 `sysusers.d` / `tmpfiles.d` 集成

这些扩展点都不能改变本设计的四条底线：

1. 仍以 Debian 原生打包为主。
2. 仍以 `cli + daemon + common` 为核心包骨架。
3. 仍保持 profile 是运行时资产，而不是 Ubuntu SKU。
4. 仍保持 direct-bind daemon 是 v1 正式交付基线。

## 8. 设计结论摘要

1. DASALL 在 Ubuntu 上的正式交付形态应是标准 Debian source package，而不是单一封装 `.deb`。
2. v1 推荐四包模型：`dasall`、`dasall-cli`、`dasall-daemon`、`dasall-common`。
3. 正式包必须把 daemon socket 路径从开发态 `/tmp` 收敛到生产态 `/run/dasall/daemon.sock`。
4. systemd 采用 direct-bind service，不以前置要求 socket activation。
5. 配置、只读资产、运行状态必须分别落在 `/etc`、`/usr/share`、`/run` / `/var/lib`。
6. 维护脚本必须最小化，并通过 debhelper / `dpkg-maintscript-helper` 处理服务与迁移问题。
7. 分发信任链应依赖 APT 仓库签名，package 安装期不再发明另一套签名体系。
