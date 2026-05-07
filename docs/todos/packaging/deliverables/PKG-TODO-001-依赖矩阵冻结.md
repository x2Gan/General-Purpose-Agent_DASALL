# PKG-TODO-001 实测并冻结 Build-Depends / Depends / Architecture 矩阵

状态：Done
日期：2026-05-07
来源 TODO：docs/todos/packaging/DASALL_Ubuntu_DPKG打包专项TODO.md

## 1. 任务边界

1. 本任务只冻结 Ubuntu noble 目标面向 v1 四包模型的 Build-Depends / Depends / Recommends / Architecture 口径，不提前落 `debian/changelog`、`debian/rules`、`.install`、service 或 maintainer scripts。
2. 本任务直接消费当前工程可观测事实：顶层工程使用 CMake + Ninja；源码树当前只显式 `find_package(Threads REQUIRED)`，其余第三方依赖由 vendored / FetchContent 路径承接；daemon 运行时 user/group 创建需求已经在打包设计中固定为 `adduser`。
3. 本任务不把 `gateway`、`simulator`、streaming 或未来 plugin 包夹带进首版包矩阵；依赖冻结只覆盖 `dasall`、`dasall-cli`、`dasall-daemon`、`dasall-common`。
4. 本任务冻结的是 control 字段语义与矩阵，不把 source package 其余元数据 owner 扩张到 008；`debian/control` 正式文件落盘继续由 PKG-TODO-008 统一承接，但 001 必须先把字段值收敛到唯一口径。

## 2. 当前事实与冲突来源

1. 当前仓库还没有 `debian/` 目录，`docs/architecture/DASALL_Ubuntu平台DPKG打包方案设计.md` 的 `debian/control` 草案仍保留 `<native-build-dependency-1>`、`<project-homepage>` 等占位字段，因此后续 008/009 无法直接继承稳定依赖矩阵。
2. 当前工作区实测环境为 Ubuntu noble；`cmake`、`ninja-build`、`pkgconf` 已安装可见，说明 v1 打包至少要把这些非 build-essential 工具链包纳入 Build-Depends 的冻结口径。
3. 当前工程显式依赖面非常克制：顶层仅通过 CMake / Ninja 构建，`memory/CMakeLists.txt` 只显式声明 `find_package(Threads REQUIRED)`；SQLite 和 `cpp-httplib` 都通过源码树 / FetchContent 解决，而不是依赖系统 `-dev` 包。这意味着 001 不应臆造当前代码并未消费的系统库 Build-Depends。
4. Debian Policy 5.2/5.6.8 要求 source stanza 与 binary stanzas 分别明确 `Build-Depends` 与 `Architecture`；Policy 7.7 进一步明确：build-essential 二进制包可以不出现在 `Build-Depends` 中。因此 `g++` 虽在当前主机可见，但不应作为 001 的显式 Build-Depends 冻结值。
5. 打包设计 7.2、7.9.2 与 7.10.4 已经给出四包关系草案：`dasall` 是 meta package，`dasall-cli` / `dasall-daemon` 是 `Architecture: any`，`dasall-common` 是 `Architecture: all`。001 的职责不是重新发明矩阵，而是把这组草案去占位、去歧义、绑定到当前 noble 基线。

## 3. 冻结结论

### 3.1 Source Build-Depends 基线

1. Ubuntu noble v1 source package 的最小 Build-Depends 冻结为：`debhelper-compat (= 13), cmake, ninja-build, pkgconf`。
2. `g++`、`make`、`dpkg-dev` 等 build-essential 基线不单列进 001 的显式 `Build-Depends`；这是遵循 Debian Policy 7.7 的刻意收敛，而不是遗漏。
3. 在当前工程进入 install surface / debian 骨架实现前，不再新增任何源码未实证消费的系统 `-dev` 包。若后续 005~009 引入新的系统依赖，必须通过增量任务更新矩阵，而不能在 001 阶段预埋占位。

### 3.2 Binary package 关系矩阵

1. `dasall`
   - `Architecture: all`
   - `Depends: ${misc:Depends}, dasall-cli (= ${binary:Version}), dasall-daemon (= ${binary:Version}), dasall-common (= ${binary:Version})`
2. `dasall-cli`
   - `Architecture: any`
   - `Depends: ${shlibs:Depends}, ${misc:Depends}, dasall-common (= ${binary:Version})`
   - `Recommends: dasall-daemon (= ${binary:Version})`
3. `dasall-daemon`
   - `Architecture: any`
   - `Depends: ${shlibs:Depends}, ${misc:Depends}, adduser, dasall-common (= ${binary:Version})`
4. `dasall-common`
   - `Architecture: all`
   - `Depends: ${misc:Depends}`

### 3.3 `adduser` 与 Architecture 归属

1. `adduser` 只属于 `dasall-daemon` 的运行时依赖，因为当前打包设计明确只有 daemon 包的 `postinst` 需要负责 `dasall` system user/group 的幂等创建。
2. `dasall` 与 `dasall-common` 保持 `Architecture: all`，前者因为是纯 meta package，后者因为 v1 只承载只读 profile / LLM baseline 资产。
3. `dasall-cli` 与 `dasall-daemon` 保持 `Architecture: any`，因为它们安装真实 ELF 二进制并依赖 `${shlibs:Depends}`。
4. 001 不在当前阶段引入 `Multi-Arch` 冻结值；这项评估继续留给 008/009 与实际 payload 验证一起决定，避免在无 `.deb` 产物的前提下做过度承诺。

### 3.4 001 对 `debian/control` 初稿的唯一口径

1. 001 冻结的不是“示意性矩阵”，而是 008 写 `debian/control` 时必须逐字继承的依赖/架构关系。
2. 当前 repo URL 已可确定为 GitHub 仓库 `https://github.com/x2Gan/General-Purpose-Agent_DASALL`；后续 008 可直接把 `Homepage`、`Vcs-Git`、`Vcs-Browser` 固化到该仓库 URL，而无需再保留 `<vcs-*>` 占位。
3. 若 maintainer 身份在 008 前发生项目级调整，只允许改 maintainer 相关字段；不得借机重开 001 已冻结的 Build-Depends / Depends / Architecture 结论。

## 4. 对后续 Build 的直接约束

1. PKG-TODO-008 创建 `debian/control` 时，source stanza 的 Build-Depends 只能使用 `debhelper-compat (= 13), cmake, ninja-build, pkgconf` 这一组最小值起步；如果新增系统依赖，必须回链到具体 CMake / link 证据。
2. PKG-TODO-009 与后续 binary skeleton 只能围绕四包矩阵展开，不得把 `gateway`、`simulator` 或测试产物塞入首轮 Depends/Architecture 关系。
3. PKG-TODO-013 的 daemon `postinst` 若继续承担 user/group 创建职责，`adduser` 依赖必须保留在 `dasall-daemon`，不得漂移到 meta package 或 `dasall-common`。
4. 若 005~007 把某些共享资产从 `dasall-common` 拆出新的 binary package，必须先开新冻结任务，不得直接改写 001 的四包主线。

## 5. 验证口径

1. 设计验收使用以下命令：

   `rg -n "Build-Depends|Architecture: any|Architecture: all|adduser|dasall-common|dasall-daemon" docs/architecture/DASALL_Ubuntu平台DPKG打包方案设计.md docs/todos/packaging/DASALL_Ubuntu_DPKG打包专项TODO.md docs/todos/packaging/deliverables/PKG-TODO-001-依赖矩阵冻结.md`

2. 通过标准：
   - `Build-Depends` 不再保留 `<native-build-dependency-*>` 占位。
   - 四包 `Architecture: any/all` 形成唯一口径。
   - `adduser` 只出现在 `dasall-daemon` 的运行时依赖语境。
   - `dasall-common` / `dasall-daemon` 的关系与四包主线在设计文档、TODO 与 deliverable 三处一致。