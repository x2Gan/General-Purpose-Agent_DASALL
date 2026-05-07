# PKG-TODO-004 冻结统一安装态布局 owner 与 path resolver 归属

状态：Done
日期：2026-05-07
来源 TODO：docs/todos/packaging/DASALL_Ubuntu_DPKG打包专项TODO.md

## 1. 任务边界

1. 本任务只冻结安装态路径模型的唯一 owner、未来共享 public surface 落位，以及各子系统对这些路径的消费边界。
2. 本任务不在 004 阶段直接修改 `kDefaultDaemonSocketPath`、`current_path()/profiles`、`LLMSubsystemConfig.h` 的 repo-relative 默认值；这些具体补丁继续由 010、011、012 承接。
3. 本任务必须回答三个问题：
   - `/usr/share/dasall`、`/etc/dasall`、`/run/dasall`、`/var/lib/dasall` 的安装态路径模型到底由谁拥有；
   - daemon/profile/LLM 这些消费者应通过什么共享接口拿到路径，而不是继续各自硬编码；
   - 哪些方案必须明确禁止，避免后续用 `WorkingDirectory`、局部常量或 repo-relative 默认值继续补洞。

## 2. 当前事实

1. `access/include/daemon/DaemonEndpointDefaults.h` 仍把 `kDefaultDaemonSocketPath` 固定为 `/tmp/dasall/control.sock`，说明 canonical socket 默认值今天仍由 access 面暴露。
2. `apps/daemon/src/main.cpp` 仍把 `DaemonEntryConfigLoadRequest.profiles_root` 设为 `std::filesystem::current_path() / "profiles"`，说明 daemon 入口自己在做路径派生。
3. `profiles/include/ProfileCatalog.h` 仍把 `profiles_root` 的默认值写成 `"profiles"`，说明 profiles 模块本身也保留了 repo-relative 假设。
4. `llm/include/LLMSubsystemConfig.h` 仍把 Prompt / Provider baseline root 默认值写成 `llm/assets/prompts` 与 `llm/assets/providers`，说明 LLM 子系统也在本模块 public surface 内携带 repo-relative 默认路径。
5. 当前代码树中不存在现成的 `InstallLayout` 或 `PathResolver` 共享抽象；安装态路径模型仍分散在 access、apps/daemon、profiles、llm 各处。

## 3. 冻结结论

### 3.1 唯一 owner

安装态路径模型的唯一 owner 冻结为 `infra/config`，而不是 access、apps/daemon、profiles、llm 或 platform：

1. `contracts` 不拥有任何 Ubuntu/DPKG 安装路径，因为这些路径属于部署与运行时基础设施，而不是跨边界语义契约。
2. `access` 只应消费 canonical daemon endpoint，不再拥有安装态 socket 默认值的最终定义。
3. `apps/daemon` 只应消费“已解析好的 profile assets root / daemon config path / socket path”，不再直接根据 `cwd` 或局部规则推导安装态路径。
4. `profiles` 只应消费传入的 `profiles_root`，不拥有包安装根的推导逻辑。
5. `llm` 只应消费 Prompt / Provider baseline root，不拥有 Ubuntu 安装态根目录的最终定义。
6. `platform` 负责宿主机能力与 provider，不负责 Debian/Ubuntu package layout policy；因此也不是 owner。

选择 `infra/config` 的理由只有一条：它已经是 access、profiles、llm、apps/daemon 都能依赖的共享基础设施层，最适合承载“安装态路径模型”这种跨模块但非业务语义的系统配置基线。

### 3.2 未来共享 public surface 落位

v1 后续实现统一落在以下 public surface：

1. 头文件位置：`infra/include/config/InstallLayout.h`
2. 命名空间：`dasall::infra::config`
3. 最小导出对象：
   - `struct InstallLayout`
   - `[[nodiscard]] InstallLayout packaged_install_layout()` 或等价命名的工厂函数
4. `InstallLayout` 至少承载以下字段：
   - `readonly_assets_root` -> `/usr/share/dasall`
   - `profiles_root` -> `/usr/share/dasall/profiles`
   - `llm_prompts_root` -> `/usr/share/dasall/llm/prompts`
   - `llm_providers_root` -> `/usr/share/dasall/llm/providers`
   - `daemon_config_path` -> `/etc/dasall/daemon.json`
   - `daemon_socket_path` -> `/run/dasall/daemon.sock`
   - `state_root` -> `/var/lib/dasall`

这里冻结的是 owner 与落位，不是最终函数签名的每一个细节；但后续若偏离 `infra/include/config/InstallLayout.h` 这个唯一落点，必须新开任务而不能私自扩散到各子系统。

### 3.3 消费边界

1. access
   - 继续暴露 daemon endpoint 相关协议类型是允许的。
   - 不再新增新的安装态路径常量。
   - `kDefaultDaemonSocketPath` 后续应改为消费 `infra/config` 的 install layout，而不是继续在 access 里拥有 canonical installed path。
2. apps/daemon
   - `main.cpp` 后续必须通过 `packaged_install_layout()` 或等价入口拿到 `profiles_root`、`daemon_config_path`、`daemon_socket_path`。
   - 不允许继续使用 `current_path()/profiles`、字符串拼接 `/etc/dasall/...` 或 `WorkingDirectory` 依赖来派生路径。
3. profiles
   - `ProfileCatalog` 保留“消费某个 path”的能力，但不再拥有安装态默认根的最终定义。
   - `ProfileCatalog(std::filesystem::path profiles_root = "profiles")` 这类默认值只能视作过渡态，不能当成 package-mode 的长期 owner。
4. llm
   - `PromptAssetSourceConfig.baseline_root` 与 `ProviderCatalogSourceConfig.baseline_root` 继续是可投影字段。
   - 这些字段的 package-mode 默认值必须统一来自 `InstallLayout`，而不是继续在 `LLMSubsystemConfig.h` 里保留 repo-relative 字符串常量。
5. tests
   - 单测和集成测试可以继续显式注入临时目录或仓库路径。
   - 测试夹具中的 repo path 只属于测试输入，不得反向决定 production owner。

### 3.4 四类安装态路径的责任划分

| 路径 | owner | consumer 边界 | 说明 |
|---|---|---|---|
| `/usr/share/dasall` | `infra/config::InstallLayout.readonly_assets_root` | daemon、profiles、llm 只读消费 | package-owned read-only assets 根 |
| `/etc/dasall/daemon.json` | `infra/config::InstallLayout.daemon_config_path` | daemon/service/postinst 读取；operator 修改 | deployment conffile，不属于 baseline asset resolver |
| `/run/dasall/daemon.sock` | `infra/config::InstallLayout.daemon_socket_path` | access/cli/daemon 共同消费 | canonical installed socket path，不再由 access 独占定义 |
| `/var/lib/dasall` | `infra/config::InstallLayout.state_root` | daemon/runtime 持久状态消费 | package delivery 只声明位置，不把业务状态策略下沉给 access/llm |

## 4. 明确不采纳的方案

1. 不采纳把安装态路径模型继续分散在 `DaemonEndpointDefaults.h`、`main.cpp`、`ProfileCatalog.h`、`LLMSubsystemConfig.h` 各自默认值中的方案。
2. 不采纳通过 `systemd WorkingDirectory=/usr/share/dasall` 掩盖 repo-relative 路径问题的方案。
3. 不采纳把 `InstallLayout` 放进 `contracts` 的方案，因为这会把 package layout policy 误升级成跨边界共享语义。
4. 不采纳把 path resolver 放进 `apps/daemon` 私有实现的方案，因为 CLI、LLM、profiles 与 future tooling 也需要消费同一模型。
5. 不采纳“让 tests 当前怎么写，production 就跟着怎么默认”的方案；测试注入路径不等于安装态 owner。

## 5. 对后续任务的直接约束

1. PKG-TODO-010 必须把 canonical socket 默认值的 owner 从 access 私有常量迁到 `infra/config` 的 install layout。
2. PKG-TODO-011 必须让 daemon profile 发现路径消费 `InstallLayout.profiles_root`，而不是继续使用 `cwd` 或 `WorkingDirectory`。
3. PKG-TODO-012 必须让 Prompt / Provider baseline root 的 package-mode 默认值消费 `InstallLayout.llm_prompts_root` 与 `InstallLayout.llm_providers_root`。
4. PKG-TODO-013 必须让 systemd unit、README 与 deployment config 样例共同消费同一 install layout 口径。
5. 若 010/011/012 任一实现尝试绕开 `infra/include/config/InstallLayout.h`，应视为偏离 004 冻结结论。

## 6. 验证口径

1. 设计验收使用以下命令：

   `rg -n "InstallLayout|/usr/share/dasall|/etc/dasall|/run/dasall|/var/lib/dasall|current_path\(\)|baseline_root|kDefaultDaemonSocketPath" docs/architecture/DASALL_Ubuntu平台DPKG打包方案设计.md docs/todos/packaging/DASALL_Ubuntu_DPKG打包专项TODO.md docs/todos/packaging/deliverables/PKG-TODO-004-InstallLayout与PathResolver归属冻结.md apps/daemon/src/main.cpp access/include/daemon/DaemonEndpointDefaults.h profiles/include/ProfileCatalog.h llm/include/LLMSubsystemConfig.h`

2. 通过标准：
   - owner 已明确固定在 `infra/config`，而不是 access/apps/daemon/llm/profiles 任一局部模块。
   - 四类安装态路径都有单点 owner 与明确 consumer 边界。
   - 后续 010/011/012/013 的实现路径不再依赖口头约定。
