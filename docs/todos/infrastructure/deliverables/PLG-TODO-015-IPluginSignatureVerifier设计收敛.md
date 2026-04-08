# PLG-TODO-015 IPluginSignatureVerifier 设计收敛

日期：2026-04-08  
任务：PLG-TODO-015  
状态：D Gate PASS / Build PASS

## 1. 本地证据

1. docs/architecture/DASALL_infra_plugin模块详细设计.md 6.5.2 已冻结 SignatureReport v1 的字段集合，6.6.1 已冻结允许算法、trust level 次序、anchor purpose 与 rollback/freeze 语义。
2. docs/todos/infrastructure/DASALL_infrastructure_plugin组件专项TODO.md 已把 PLG-TODO-015 标记为在 014 之后可直接串行推进的接口级任务，验收边界收敛为“public header + compile/boundary tests + 证据回写”。
3. 现有 IPluginPolicyGate.h 与 OTA 的 IOTAPackageVerifier.h 已给出仓库当前的接口冻结模式：最小输入对象、可二值判定输出对象，以及 contract-shaped failure 之外不泄露下游实现细节。
4. PluginValidationPipeline 与 IPluginManager 当前仍保持 ref-only aggregation 边界，因此 015 不应把 verifier 接入 pipeline/manager public signature，也不应提前改动 validation result。

## 2. 外部参考

1. The Update Framework 强调签名系统必须显式防御未知算法、trust anchor 缺失和 rollback/freeze 风险；本轮据此把 allow-list、anchor purpose 与 last known good version 比较面收敛到 verifier 的最小输入对象，而不是留给实现期隐式扩写。

## 3. Design 结论

1. 新增 IPluginSignatureVerifier public header，并在同一头文件内冻结 `PluginTrustAnchorMaterial`、`PluginSignatureVerificationRequest` 与 `SignatureReport` 三个最小输入输出对象，保持本轮原子粒度。
2. `PluginTrustAnchorMaterial.anchor_purpose` 固定为 `plugin.package.verify`；`allowed_algorithms` 只允许 `ed25519` 与 `ecdsa-p256-sha256`；`minimum_trust_level` 复用 plugin 私有 trust level 顺序；`last_known_good_version` 仅用于 rollback 判定输入，不扩张到更复杂的元数据缓存。
3. `PluginSignatureVerificationRequest` 冻结为 `manifest + package_ref + signature_algorithm + trust_anchor` 四元组：manifest 使用 014 已冻结的 PluginManifest，避免再回退到 string-only 占位。
4. `SignatureReport` 冻结为 `verified/signer/algorithm/chain_status/inferred_trust_level/reason_code/evidence_ref` 七字段；其中 `chain_status` 只允许 detailed design 已冻结的词典。
5. 本轮只冻结 verifier public boundary，不接入 PluginValidationPipeline / IPluginManager aggregation；017 再统一处理 report 对象在 validation boundary 的正式接线与测试迁移。

## 4. Design -> Build 映射

| Design 项 | Build 落点 |
|---|---|
| 冻结 signature verifier public boundary | infra/include/plugin/IPluginSignatureVerifier.h |
| 冻结 trust anchor purpose、allow-list 与 trust threshold 输入面 | `PluginTrustAnchorMaterial::is_valid()` |
| 冻结 signature request 最小四元组 | `PluginSignatureVerificationRequest` |
| 冻结 SignatureReport v1 字段与 chain_status 词典 | `SignatureReport` + `PluginSignatureChainStatus` |
| 验证 compile 与 boundary freeze | PluginSignatureVerifierInterfaceCompileTest / PluginSignatureVerifierBoundaryContractTest |

## 5. Build 三件套

1. 代码目标：新增 infra/include/plugin/IPluginSignatureVerifier.h，并在 infra/CMakeLists.txt 中注册 public header。
2. 测试目标：新增 tests/unit/infra/plugin/PluginSignatureVerifierInterfaceTest.cpp，覆盖成功、算法不支持、rollback_rejected 三类路径；新增 tests/contract/smoke/PluginSignatureVerifierBoundaryContractTest.cpp，覆盖 anchor purpose 冻结、allow-list 冻结与“无原始密钥/证书链字段”边界。
3. 验收命令：
   - cmake -S . -B build-ci -G "Unix Makefiles"
   - cmake --build build-ci --target dasall_infra dasall_plugin_signature_verifier_interface_unit_test dasall_contract_plugin_signature_verifier_boundary_test
   - ctest --test-dir build-ci -N -R "PluginSignatureVerifierInterfaceCompileTest|PluginSignatureVerifierBoundaryContractTest"
   - ctest --test-dir build-ci --output-on-failure -R "PluginSignatureVerifierInterfaceCompileTest|PluginSignatureVerifierBoundaryContractTest"

## 6. 验收结果

1. `dasall_infra`、`dasall_plugin_signature_verifier_interface_unit_test` 与 `dasall_contract_plugin_signature_verifier_boundary_test` 全部构建通过。
2. `PluginSignatureVerifierInterfaceCompileTest` 与 `PluginSignatureVerifierBoundaryContractTest` 均已进入 CTest 图。
3. 两个测试 2/2 通过。

## 7. 风险与回退

1. 本轮没有把 verifier 接入 PluginValidationPipeline 或 IPluginManager，避免在 015 中提前触发 validation aggregation breaking review。
2. `SignatureReport` 当前先作为 verifier public boundary 的最小输出对象存在；017 若统一抽取 report 头文件或迁移 aggregation 边界，应保持 include 兼容，不得破坏 015 已冻结接口签名。
3. trust anchor 仍是只读引用语义；真实 secret/crypto adapter 接线不在本轮范围内，后续实现轮次若要引入具体 provider，应保持该输入对象不变。