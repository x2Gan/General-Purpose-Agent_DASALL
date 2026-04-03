# SEC-TODO-007 FileSecretBackend 骨架收敛

日期：2026-04-03
任务：SEC-TODO-007
状态：已完成

## 1. 输入依据

1. docs/todos/infrastructure/DASALL_infrastructure_secret组件专项TODO.md 已将 SEC-TODO-007 定义为“实现 FileSecretBackend 最小骨架”，验收出口为本地路径读取、错误路径和 backend unavailable。
2. SEC-BLK-001 已于 2026-04-03 解阻，secret 详细设计 6.9 已冻结 `infra.secret.file.root_dir = secrets/` 与 `infra.secret.file.encrypt_at_rest = true` 的最小策略。
3. ISecretBackend、SecretTypes、SecretErrors 和 MockSecretBackend 已先行落盘，因此本轮只需补 file backend internal 实现、单测和最小接线。

## 2. 研究学习结果

### 2.1 本地证据

1. MockSecretBackend 已把 access_context 驱动的 materialize、backend status 和最小 CMake/test 接线打通，因此 file backend 可以沿用同一 internal backend 形态，而不必重新设计测试入口。
2. secret 详细设计 6.9 明确 root_dir 是部署层边界、encrypt_at_rest 默认开启，因此 file backend 首轮必须体现“路径只能落在 root_dir 下”和“磁盘内容不直接存放明文”这两个约束。
3. 当前 secret public contract 不允许暴露 `file_path` 字段，因此 file backend 只能通过 `cipher_ref` 之类的 opaque reference 暴露文件来源，而不能把物理路径上抬为公共字段。

### 2.2 外部参考

1. OWASP Secrets Management Cheat Sheet 建议 secrets at rest 受保护、materialize 窗口最小化，并避免把 plaintext 暴露到不必要的中间介质，这支持 file backend 在 fixture 读取后直接进入 SecureBuffer，而不生成临时明文文件。
2. Azure Key Vault best practices 强调 secret store 不是普通配置中心，这支持 DASALL 将 file backend 的 root_dir 和 payload 解析保持在 secret 专属实现内，而不让普通 config 流程直接消费其文件结构。

### 2.3 可落地启发

1. File backend v1 可以用 `ciphertext_hex` fixture 充当“非明文磁盘载荷”的最小占位，不在本轮引入真实密码学依赖。
2. backend unavailable 最适合直接映射为 root_dir 缺失或不可读，这样无需额外注入开关也能覆盖真实故障路径。
3. “不写明文临时文件”可以通过目录中文件数量不增加来做最小可执行断言。

## 3. Design 原子清单

| D 子项 | 设计目标 | 输入依据 | 产出 | 完成判定 |
|---|---|---|---|---|
| D1 | 冻结 root_dir 与 encrypt_at_rest 最小实现面 | secret 设计 6.9；SEC-BLK-001 | FileSecretBackend.h | 仅允许 root_dir 下的 `.secret` 文件路径 |
| D2 | 实现 fetch/materialize 最小链路 | secret 设计 6.2/6.7 | FileSecretBackend.cpp | 本地路径读取成功、缺失路径和 root_dir 不可用可二值判定 |
| D3 | 验证无额外明文文件 | 编码规范 3.6；任务完成判定 | FileSecretBackendTest.cpp | materialize 后目录中 regular file 数量不增加 |

## 4. D Gate 结论

### 4.1 Design -> Build 映射

| Design 结论 | Build 落地 |
|---|---|
| root_dir 边界只允许安全 secret_name 解析到 `.secret` 文件 | infra/src/secret/backends/FileSecretBackend.cpp |
| encrypt_at_rest 默认开启并消费 `ciphertext_hex` fixture | infra/src/secret/backends/FileSecretBackend.cpp |
| 不生成额外明文临时文件 | tests/unit/infra/secret/FileSecretBackendTest.cpp |
| CMake 和 unit discoverability 收口 | infra/CMakeLists.txt；tests/unit/CMakeLists.txt；tests/unit/infra/CMakeLists.txt |

### 4.2 Build 三件套

1. 代码目标：新增 FileSecretBackend internal 头/源文件，并把源码与 unit test target 纳入现有构建图。
2. 测试目标：新增 FileSecretBackendTest，覆盖本地路径读取成功、缺失路径和 backend unavailable 三条路径，并验证 ctest discoverability。
3. 验收命令：
   - `cmake -S . -B build-ci -G "Unix Makefiles"`
   - `cmake --build build-ci --target dasall_infra dasall_file_secret_backend_unit_test`
   - `ctest --test-dir build-ci -N -R FileSecretBackendTest`
   - `ctest --test-dir build-ci --output-on-failure -R FileSecretBackendTest`

### 4.3 D Gate

结论：PASS。

理由：

1. SEC-BLK-001 已解阻，本轮不存在新的前置 blocker。
2. 任务边界保持在 file backend fetch/materialize、单测和最小接线，没有提前扩张到 rotation/facade/lease registry。

## 5. Build 落地结果

1. 新增 infra/src/secret/backends/FileSecretBackend.h 与 infra/src/secret/backends/FileSecretBackend.cpp，落盘 root_dir 安全解析、key=value 文件解析、`ciphertext_hex` 解码、backend unavailable/status 和最小 skeleton lifecycle 实现。
2. 新增 tests/unit/infra/secret/FileSecretBackendTest.cpp，覆盖成功路径、缺失路径和 root_dir 缺失路径，并断言 materialize 不会创建额外明文文件。
3. 更新 infra/CMakeLists.txt、tests/unit/CMakeLists.txt、tests/unit/infra/CMakeLists.txt，将 file backend 实现与新 unit test 纳入构建图和 `dasall_unit_tests` 聚合目标。

## 6. 验证结果

1. `cmake -S . -B build-ci -G "Unix Makefiles"`：通过。
2. `cmake --build build-ci --target dasall_infra dasall_file_secret_backend_unit_test`：通过。
3. `ctest --test-dir build-ci -N -R FileSecretBackendTest`：通过，发现 1 个定向测试。
4. `ctest --test-dir build-ci --output-on-failure -R FileSecretBackendTest`：通过，1/1 tests passed。

## 7. 结论

1. SEC-TODO-007 已把 secret backend 骨架从 mock 扩展到 file，实现了 root_dir/encrypt_at_rest 约束下的最小本地读取链路。
2. 后续 SEC-TODO-008 可以直接在 mock/file backend 之上落盘 SecretManagerFacade 的 get/materialize/release 主链。