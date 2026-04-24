---
title: ACC-TODO-030 交付物：SimulatorProtocolAdapter 与确定性刺激组合根
date: 2026-04-24
version: 1.0
phase: v1-access-impl
status: Done
---

## 1. 任务目标

实现 `SimulatorProtocolAdapter` 与 `apps/simulator` 确定性刺激组合根，为测试框架和工厂环境提供可重放、可断言的 access 入口。

**设计约束来源**：Access 详设 6.14.9、6.15.3、8.1；模块隔离 L2

---

## 2. 完成物清单

### 2.1 代码工件

| 文件 | 行数 | 变动类型 | 说明 |
|---|---|---|---|
| `apps/simulator/src/SimulatorProtocolAdapter.h` | 60 | 新建 | simulator adapter 类定义，实现 IProtocolAdapter 接口 |
| `apps/simulator/src/SimulatorProtocolAdapter.cpp` | 58 | 新建 | simulator adapter 实现 |
| `apps/simulator/src/main.cpp` | 75 | 修订 | 从 placeholder 升级到 deterministic subject stub 初始化 + 信号处理 |
| `apps/simulator/CMakeLists.txt` | +8 | 修订 | 增加 SimulatorProtocolAdapter 源文件和 include 目录 |
| `tests/unit/access/SimulatorProtocolAdapterTest.cpp` | 113 | 新建 | 6 个 adapter 功能单元测试 |
| `tests/unit/access/SimulatorProtocolAdapterDeterministicTest.cpp` | 145 | 新建 | 5 个确定性行为验证单元测试 |
| `tests/unit/access/CMakeLists.txt` | +70 | 修订 | 注册 2 个 simulator 测试目标 |

**总计**：新建 3 个代码文件 + 2 个测试文件 + 修订 2 个 CMakeLists.txt，代码行数 ~350 行

### 2.2 验收命令与结果

```bash
# 编译
cmake --build build-ci --target dasall_simulator_protocol_adapter_unit_test \
  dasall_simulator_protocol_adapter_deterministic_unit_test dasall_simulator

# 结果：编译成功，0 warning，simulator 二进制 92K

# 测试
ctest --test-dir build-ci -R "SimulatorProtocolAdapterTest|SimulatorProtocolAdapterDeterministicTest" \
  --output-on-failure

# 结果：
# Test #463: SimulatorProtocolAdapterTest ..................   Passed    0.00 sec
# Test #464: SimulatorProtocolAdapterDeterministicTest ...   Passed    0.00 sec
# 100% tests passed, 0 tests failed out of 2
```

### 2.3 测试覆盖

| 测试文件 | 用例 | 覆盖范围 |
|---|---|---|
| `SimulatorProtocolAdapterTest` | 6 个 | can_handle 协议匹配、空/非空 JSON 解析、fixture 主体注入、encode 200 accepted 响应 |
| `SimulatorProtocolAdapterDeterministicTest` | 5 个 | 多次 decode 一致性、不同 fixture 可区分、encode 幂等性、fixture 不被修改、重放场景确定性 |
| **总计** | 11 个 | 可重放测试输入、受控 subject stub、确定性语义全覆盖 |

---

## 3. 核心设计实现

### 3.1 SimulatorProtocolAdapter 类

```cpp
class SimulatorProtocolAdapter final : public dasall::access::IProtocolAdapter {
public:
  explicit SimulatorProtocolAdapter(const DeterministicSubjectStub& subject = {})
      : subject_(subject) {}

  // IProtocolAdapter 实现
  bool can_handle(std::string_view entry_type, std::string_view protocol_kind) const override;
  [[nodiscard]] dasall::access::InboundPacket decode() override;
  bool encode(const dasall::access::PublishEnvelope& envelope) override;

  // 测试框架集成接口
  void set_active_request(const std::string& request_body);
  const std::string& active_response_body() const { return response_body_; }
};
```

**关键语义**：
- `can_handle("simulator", "deterministic_test")` 精确匹配 simulator 入口
- `decode()` 从 fixture 注入的 `DeterministicSubjectStub` 读取 actor_ref，写入 packet_id
- `encode()` 返回 202 accepted + result_id/session_id（不执行真实 runtime）
- 仅提供无状态翻译，不管理生命周期或会话

### 3.2 DeterministicSubjectStub 结构体

```cpp
struct DeterministicSubjectStub {
  std::string actor_ref;                ///< fixture 注入的确定性主体标识
  std::vector<std::string> granted_actions;  ///< 测试权限白名单
  std::string override_source;          ///< v1 保留，不使用
};
```

**确定性保证**：
- 同一 fixture 对象多次 decode 产生同一 packet_id
- 不同 fixture 产生不同 packet_id
- fixture 成员在 adapter 使用过程中不被修改

### 3.3 Main Composition Root 简化版

```cpp
int main() {
  // 创建确定性主体
  DeterministicSubjectStub deterministic_subject{
      .actor_ref = "simulator_test_actor",
      .granted_actions = {"read", "write", "execute"},
      .override_source = ""
  };

  // 创建 adapter
  auto simulator_adapter =
      std::make_shared<SimulatorProtocolAdapter>(deterministic_subject);

  // 输出初始化完成，然后等待测试框架驱动
  std::cout << "dasall_simulator initialized with deterministic adapter" << std::endl;
  
  // v1 版本：被动等待信号或测试框架请求
  while (!shutdown_requested) {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
  
  return 0;
}
```

---

## 4. 边界与依赖

### 4.1 架构边界

| 边界 | 说明 |
|---|---|
| 入口类型 | simulator 仅在测试/工厂 profile 启用，不进入生产路径 |
| Protocol | 不支持 WS/MQTT/streaming，仅支持确定性同步 JSON packet |
| Runtime | 不执行实际 runtime 语义，仅返回 AcceptedAsync pending 状态 |
| 主体 | 由 fixture 装配注入，无运行时认证或动态查询 |
| 权限 | 由 fixture granted_actions 列表表达，不走真实 policy engine |

### 4.2 前置依赖

- `IProtocolAdapter` 公共接口：✅ 已定义 (ACC-TODO-006)
- `AccessTypes` supporting types：✅ 已定义 (ACC-TODO-009、012)
- `ProtocolAdapterRegistry` (可选)：✅ 已定义 (ACC-TODO-013)
- `apps/simulator` CMake 框架：✅ 已存在

### 4.3 后置依赖

- `apps/simulator` binary 可由 dashboard / CI 启动
- 测试框架调用 `set_active_request()` 注入 fixture JSON
- 验收命令可通过 simulator executable 与测试目标双重验证

---

## 5. 测试方法学

### 5.1 SimulatorProtocolAdapterTest（6 个）

| 用例 | 断言 | 用途 |
|---|---|---|
| `can_handle_deterministic_test_protocol` | entry="simulator" && protocol="deterministic_test" → true | 正确的入口匹配 |
| `rejects_non_simulator_entries` | entry≠"simulator" → false | 非 simulator 入口拒绝 |
| `decode_empty_request` | 空 request → entry_type="simulator" + peer_ref="simulator_local" | 默认包格式 |
| `decode_extracts_entry_type` | JSON 有 entry_type 字段 → 提取到 packet.entry_type | JSON 解析 |
| `uses_deterministic_actor_ref` | fixture actor_ref → packet_id 一致 | fixture 注入 |
| `encode_returns_true` | envelope.result_id + session_id → 202 accepted JSON | 响应格式 |

### 5.2 SimulatorProtocolAdapterDeterministicTest（5 个）

| 用例 | 断言 | 用途 |
|---|---|---|
| `multiple_decode_identical_packets` | N 次 decode 同一 fixture → 相同 packet_id/entry_type | 单实例确定性 |
| `different_fixtures_different_packets` | fixture1 vs fixture2 → 不同 packet_id | 多 fixture 可区分 |
| `multiple_encode_consistent` | N 次 encode 同一 envelope → 相同响应 JSON | 幂等性 |
| `fixture_actions_unchanged` | fixture.granted_actions 在 decode 前后相同 | fixture 不被污染 |
| `replay_scenario_deterministic` | fixture1 + request1 × 2 → 相同 packet | 重放可再现 |

---

## 6. 质量指标

| 指标 | 结果 | 判定 |
|---|---|---|
| 编译成功率 | 100% | ✅ |
| 编译警告数 | 0 | ✅ |
| 测试通过率 | 100% (11/11) | ✅ |
| 代码行数 | ~350 行（含注释、测试） | ✅ |
| 接口完整性 | IProtocolAdapter 3/3 必需方法实现 | ✅ |
| 确定性覆盖 | 单实例、多实例、重放三个维度 | ✅ |
| 隔离度 | 无 httplib、runtime、policy 依赖 | ✅ |
| 交付物完整性 | 代码 + 测试 + CMake + 文档 | ✅ |

---

## 7. 与已完成任务的协调

### 7.1 与 026/028 的关系

- **ACC-TODO-026** (HttpProtocolAdapter)：gateway 入口壳层，HTTP unary/async 首版 ✅
- **ACC-TODO-028** (HealthProbeHandler)：gateway 健康检查 + CORS/安全头 ✅
- **ACC-TODO-030** (SimulatorProtocolAdapter)：simulator 确定性测试 🎯
  
这三个 adapter 共同涵盖 v1 入口范围，各自独立无交叉依赖。

### 7.2 与 027 的衔接

- **ACC-TODO-027** (TaskQueryHandler)：gateway 上的 receipt query/cancel handler，已完成 ✅
- **ACC-TODO-030** (SimulatorProtocolAdapter)：simulator 上的 deterministic packet decode/encode
  
两者功能边界清晰：027 负责 gateway receipt 查询路径，030 负责 simulator 确定性请求适配。

---

## 8. 风险与缺口

### 8.1 当前版本限制

| 限制 | 说明 | 后续处理 |
|---|---|---|
| 仅 JSON 协议 | 不支持 Protobuf/MessagePack/Binary | v2 扩展时补 codec adapter |
| 无状态化会话 | 不跟踪 session 生命周期 | 测试框架负责 session 管理 |
| 固定响应格式 | 总是返回 202 accepted pending | v2 可支持 mock 其他状态 |
| 主体无验证 | fixture 注入的 subject 不走认证链 | v2 可补 auth mock chain |
| 权限无评估 | granted_actions 仅作注释，不真正评估 | v2 可补 policy mock gate |

### 8.2 与生产路径隔离

✅ **已确保隔离**：
- `can_handle("simulator", ...)` 只返回 true 当且仅当两个参数皆匹配
- `apps/simulator/CMakeLists.txt` 独立构建，不链入生产库
- main.cpp 仅在 simulator profile 启用
- 无 runtime 调用、无 policy 执行、无认证副作用

---

## 9. 验收清单

- [x] SimulatorProtocolAdapter header/implementation 完成
- [x] DeterministicSubjectStub 结构定义
- [x] apps/simulator/main.cpp 更新为 deterministic stub 初始化
- [x] apps/simulator/CMakeLists.txt 更新
- [x] SimulatorProtocolAdapterTest 6 个用例通过
- [x] SimulatorProtocolAdapterDeterministicTest 5 个用例通过
- [x] tests/unit/access/CMakeLists.txt 注册两个测试目标
- [x] dasall_simulator 二进制成功构建（92K）
- [x] 编译 0 errors、0 warnings
- [x] 交付物说明文档完成

---

## 10. 后续计划

### 10.1 与其他任务的序列

已完成四大 adapter 入口：
1. ✅ ACC-TODO-025：CLI client
2. ✅ ACC-TODO-026：HTTP unary/async
3. ✅ ACC-TODO-028：HTTP health/CORS
4. ✅ ACC-TODO-027：receipt query/cancel handler
5. ✅ ACC-TODO-030：simulator deterministic

### 10.2 开放任务（非本 TODO 范围）

- ACC-TODO-031~036：测试、profile、contracts、Gate 回写
- ACC-TODO-037~038：daemon 本地控制面与 integration smoke
- ACC-TODO-035：延后的 streaming/WS/MQTT gate

---

## 11. 参考

- Access 详设 6.14.9（P2 延后与扩展入口）
- Access 详设 6.15.3（线程模型）
- Access 详设 8.1（入口壳层定义）
- 本 TODO 主体文档
- [ACC-TODO-026-HttpProtocolAdapter与gateway组合根](./ACC-TODO-026-HttpProtocolAdapter与gateway组合根收敛.md)
- [ACC-TODO-027-TaskQueryHandler与ownership校验路径](./ACC-TODO-027-TaskQueryHandler与ownership校验路径收敛.md)

---

**签署**：

- **任务编号**：ACC-TODO-030
- **完成日期**：2026-04-24
- **验证状态**：Done
- **交付状态**：Ready for v1 integration tests
