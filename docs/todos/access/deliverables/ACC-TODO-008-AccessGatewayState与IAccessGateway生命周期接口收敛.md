# ACC-TODO-008 设计收敛文档

## 1. 任务定义

定义 `AccessGatewayState` 枚举与扩展 `IAccessGateway` 接口的生命周期方法，用以支持 Access Gateway 的初始化、就绪判断、优雅关闭等生命周期管理。

---

## 2. 设计边界与职责

### 2.1 边界说明

- **所有权**：AccessGateway facade
- **用途**：entry 壳层（CLI/daemon/gateway/simulator）与 AccessGateway 通信的生命周期信号接口
- **消费方**：`apps/cli/main.cpp`、`apps/daemon/main.cpp`、`apps/gateway/main.cpp`、`apps/simulator/main.cpp`
- **不涵盖**：runtime 内部状态机、scheduler 预算、持久化状态

### 2.2 职责分配

| 对象 | 职责 | 拥有者 |
|------|------|--------|
| `AccessGatewayState` 枚举 | 定义 5 个生命周期状态常量 | `access/include/AccessTypes.h` |
| `IAccessGateway::state()` | 返回当前精确状态 | `access/include/IAccessGateway.h` |
| `IAccessGateway::is_ready()` | 二值判定是否就绪（Ready 态） | `access/include/IAccessGateway.h` |
| `IAccessGateway::shutdown()` | 触发优雅排空并进入关闭流程 | `access/include/IAccessGateway.h` |
| 状态转移实现 | 在 `AccessGateway::init()`、`shutdown()` 等方法中实现 | Phase 3 之后任务（ACC-TODO-024） |

---

## 3. 数据结构与接口定义

### 3.1 AccessGatewayState 枚举

```cpp
enum class AccessGatewayState {
  Uninitialized = 0,    // 未初始化：init() 前的初始状态
  Initializing = 1,     // 正在初始化：注册 adapter、pipeline 阶段
  Ready = 2,            // 就绪：唯一可接受新请求的状态
  Draining = 3,         // 排空中：shutdown() 已触发，不再接受新请求
  ShutDown = 4,         // 已终止：所有资源已释放，网关已停止
};
```

**状态语义**：

| 状态 | submit() 行为 | publish_result() 行为 | 说明 |
|------|---|---|---|
| `Uninitialized` | 返回 `InternalError` | 返回 false | init() 前无法工作 |
| `Initializing` | 返回 `InternalError` | 返回 false | 初始化阶段 |
| `Ready` | ✅ 正常处理 | ✅ 正常发布 | **唯一接受请求的状态** |
| `Draining` | 返回 `ShuttingDown` | ✅ 继续处理已受理发布 | 排空模式，禁新请求 |
| `ShutDown` | 返回 `ShuttingDown` | 返回 false | 已终止 |

### 3.2 IAccessGateway 接口扩展

```cpp
class IAccessGateway {
 public:
  virtual ~IAccessGateway() = default;

  // 既有方法
  virtual bool init() = 0;
  virtual RuntimeDispatchResult submit(const InboundPacket& packet) = 0;
  virtual bool publish_result(const PublishEnvelope& envelope) = 0;

  // 生命周期新增方法
  virtual AccessGatewayState state() const = 0;
  virtual bool is_ready() const = 0;
  virtual void shutdown(std::chrono::milliseconds drain_timeout) = 0;
};
```

**新增方法语义**：

| 方法 | 签名 | 用途 | 返回值 | 线程安全 |
|------|------|------|--------|--------|
| `state()` | `AccessGatewayState state() const = 0` | 获取当前精确生命周期状态 | `Uninitialized / Initializing / Ready / Draining / ShutDown` | 必须线程安全（内部用原子变量或锁） |
| `is_ready()` | `bool is_ready() const = 0` | 快速判断是否就绪（可接受请求） | `true` 当且仅当 `state() == Ready` | 必须线程安全 |
| `shutdown()` | `void shutdown(std::chrono::milliseconds drain_timeout) = 0` | 触发优雅排空，最多等待 `drain_timeout` 后强制关闭 | 无返回值，副作用为转换到 `Draining` 再到 `ShutDown` | 可多次调用（幂等），仅第一次生效 |

---

## 4. 流程与时序

### 4.1 正常初始化流程

```
init() 调用
  ↓
state() == Initializing (注册 adapter、pipeline)
  ↓
state() == Ready (可接受请求)
```

### 4.2 运行与关闭流程

```
state() == Ready (接受请求)
  ↓ 
shutdown(drain_timeout) 调用
  ↓
state() == Draining (新请求返回 ShuttingDown，inflight 继续)
  ↓ (等待 drain_timeout 或 inflight 完成)
state() == ShutDown (所有资源释放)
```

### 4.3 关键时序点

1. **T0**：`init()` 开始 → `state() == Initializing`
2. **T1**：`init()` 完成 → `state() == Ready`
3. **T2**：`shutdown(drain_timeout)` 调用 → `state() == Draining`
4. **T3**：超时或 inflight 完成 → `state() == ShutDown`

---

## 5. 文件范围

### 5.1 新增/修改文件

| 文件 | 操作 | 内容说明 |
|------|------|--------|
| `access/include/AccessTypes.h` | 新增 `AccessGatewayState` 枚举 | 5 个状态常量定义 |
| `access/include/IAccessGateway.h` | 扩展 3 个新方法 | `state()`, `is_ready()`, `shutdown()` |

### 5.2 测试文件

| 文件 | 用途 |
|------|------|
| `tests/unit/access/AccessInterfaceSurfaceTest.cpp` | 验证枚举值、方法签名、const 正确性 |
| `tests/unit/access/AccessGatewayLifecycleTest.cpp` | 验证生命周期状态转移、is_ready() 二值判定、shutdown() 幂等性 |

---

## 6. 约束与设计决策

1. **线程安全**：`state()` 和 `is_ready()` 必须是线程安全的幂等读操作（无副作用）
2. **幂等性**：`shutdown()` 可多次调用，仅第一次有效，后续调用被忽略
3. **向后兼容**：既有 `init()`、`submit()`、`publish_result()` 保持不变
4. **不涵盖运行治理**：生命周期状态只反映网关 I/O 层就绪，不反映 runtime 内部状态
5. **drain_timeout 语义**：从 `shutdown()` 调用时起开始计时，而不是从 Draining 状态开始

---

## 7. Design Gate 检查清单

- [x] 状态枚举与方法签名符合详设 6.7 与 6.15
- [x] 生命周期转移图完整且符合优雅关闭流程  
- [x] 状态转移表与 submit/publish 行为对应关系明确
- [x] 线程安全性与幂等性约束明确
- [x] 测试出口与验收命令可二值判定
- [x] 无跨工作包扩张，仅涉及 Access 内部接口
- [x] 向后兼容性保证（既有方法不改）

**D Gate 结论**：✅ **PASS** - 设计充分、约束明确、可直接进入 Build

---

## 8. 映射到 Build 任务

| Design 结论 | 对应 Build 任务 | Build 责任 |
|---|---|---|
| `AccessGatewayState` 枚举 5 个状态 | ACC-TODO-008-B (定义 AccessTypes) | 新增枚举定义 + 测试值覆盖 |
| `state()` / `is_ready()` 方法签名 | ACC-TODO-008-B (扩展 IAccessGateway) | 新增纯虚方法 + 接口测试 |
| `shutdown()` drain_timeout 参数 | ACC-TODO-024 (AccessGateway facade) | 实现优雅关闭逻辑（后续任务） |
| 生命周期状态机转移实现 | ACC-TODO-024 (AccessGateway facade) | 状态原子变量管理、转移规则实现（后续任务） |
