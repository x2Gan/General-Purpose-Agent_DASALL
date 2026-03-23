# DASALL boundary 治理与优化说明

## 1. 文档目的

本文档用于说明 `contracts/include/boundary/` 目录的功能、职责与存在原因，并对当前命名与目录组织的可发现性问题给出优化建议。

本文档重点回答以下问题：

1. `boundary/` 目录承担什么职责。
2. 为什么会出现 `M2ChecklistGuards.h`、`M3ChecklistGuards.h`、`M4ChecklistGuards.h`、`V1ReadyChecklistGuards.h` 这样的文件。
3. 当前设计是否需要优化，若需要，优化方向是什么。

---

## 2. 结论摘要

1. `boundary/` 目录不是普通对象定义目录，而是 contracts 的边界治理层。
2. 它的职责不是定义“系统里有哪些对象”，而是定义“这些对象能否进入稳定 contracts、如何演进、何时达到冻结和发布条件”。
3. `M2/M3/M4/V1Ready Checklist` 这类文件并非多余，它们是将冻结评审结论转为可执行 Gate 的实现。
4. 当前设计方向正确，但可发现性不足，已经值得进行一次轻量优化。

---

## 3. boundary 目录的职责定位

`boundary/` 目录主要承担四类治理职责：

1. 对象边界与准入规则
2. ADR 到对象字段边界的映射规则
3. 兼容性、字段演进与版本变更规则
4. 阶段冻结 Gate 与发布 readiness Gate

与其他 contracts 子目录的分工如下：

1. `agent/`、`context/`、`observation/`、`checkpoint/`、`tool/` 等目录回答“对象是什么”。
2. `boundary/` 回答“对象能不能进入 contracts、字段能不能存在、评审是否完成、变更是否安全”。

---

## 4. boundary 目录内容分层

### 4.1 对象边界与目录规则

代表文件：

1. [contracts/include/boundary/ObjectBoundaryCatalog.h](contracts/include/boundary/ObjectBoundaryCatalog.h)
2. [contracts/include/boundary/BoundaryGuards.h](contracts/include/boundary/BoundaryGuards.h)
3. [contracts/include/boundary/ContextBoundaryGuards.h](contracts/include/boundary/ContextBoundaryGuards.h)
4. [contracts/include/boundary/RecoveryBoundaryGuards.h](contracts/include/boundary/RecoveryBoundaryGuards.h)
5. [contracts/include/boundary/MultiAgentBoundaryGuards.h](contracts/include/boundary/MultiAgentBoundaryGuards.h)

这一层的职责是：

1. 定义哪些对象属于 Stable / Blocked / Deferred。
2. 定义哪些字段属于上下文禁区、恢复禁区、协同禁区。
3. 把“对象边界说明”从文档变成可程序化校验的规则。

### 4.2 ADR 映射与字段边界规则

代表文件：

1. [contracts/include/boundary/ADRFieldMappingGuards.h](contracts/include/boundary/ADRFieldMappingGuards.h)
2. [contracts/include/boundary/MainFlowOverlapGuards.h](contracts/include/boundary/MainFlowOverlapGuards.h)
3. [contracts/include/boundary/CrossCuttingContracts.h](contracts/include/boundary/CrossCuttingContracts.h)

这一层的职责是：

1. 将 ADR-006/007/008 与对象和字段禁区建立显式映射。
2. 让回归测试能够直接校验 ADR 约束，而不是依赖人工阅读 ADR 文本。
3. 避免“文档上边界正确，代码中边界漂移”。

### 4.3 兼容性、演进与版本治理

代表文件：

1. [contracts/include/boundary/CompatibilityGuards.h](contracts/include/boundary/CompatibilityGuards.h)
2. [contracts/include/boundary/FieldEvolutionGuards.h](contracts/include/boundary/FieldEvolutionGuards.h)
3. [contracts/include/boundary/EnumLifecycleGuards.h](contracts/include/boundary/EnumLifecycleGuards.h)
4. [contracts/include/boundary/VersionChangeSchema.h](contracts/include/boundary/VersionChangeSchema.h)
5. [contracts/include/boundary/VersionChangeGuards.h](contracts/include/boundary/VersionChangeGuards.h)
6. [contracts/include/boundary/BreakingReviewGuards.h](contracts/include/boundary/BreakingReviewGuards.h)
7. [contracts/include/boundary/TimeDeadlineGuards.h](contracts/include/boundary/TimeDeadlineGuards.h)

这一层的职责是：

1. 处理 legacy 字段兼容与归一化。
2. 将字段演进分类为 non-breaking / review-required / breaking。
3. 将版本变更记录和 breaking review 转为结构化、可验证规则。

### 4.4 里程碑 Gate 与发布 Readiness Gate

代表文件：

1. [contracts/include/boundary/M2ChecklistGuards.h](contracts/include/boundary/M2ChecklistGuards.h)
2. [contracts/include/boundary/M3ChecklistGuards.h](contracts/include/boundary/M3ChecklistGuards.h)
3. [contracts/include/boundary/M4ChecklistGuards.h](contracts/include/boundary/M4ChecklistGuards.h)
4. [contracts/include/boundary/V1ReadyChecklistGuards.h](contracts/include/boundary/V1ReadyChecklistGuards.h)
5. [contracts/include/boundary/CoverageMatrixGuards.h](contracts/include/boundary/CoverageMatrixGuards.h)
6. [contracts/include/boundary/DomainRolloutGuards.h](contracts/include/boundary/DomainRolloutGuards.h)
7. [contracts/include/boundary/InterfaceAdmissionGuards.h](contracts/include/boundary/InterfaceAdmissionGuards.h)

这一层的职责是：

1. 将每个冻结阶段的评审结论转为 Gate 输入和统一校验结果。
2. 为 smoke tests、contract tests、CI gate 提供统一判定逻辑。
3. 避免“评审结论只存在会议纪要里，无法程序化复核”。

---

## 5. 为什么会存在 M2/M3/M4ChecklistGuards 这类文件

### 5.1 它们不是普通 field guard，而是里程碑 Gate

这些文件的核心作用不是校验单个对象字段，而是把：

1. M2：横切基础对象冻结完成条件
2. M3：主链路对象冻结完成条件
3. M4：边界对象冻结完成条件
4. V1Ready：contracts V1 发布完成条件

编码成统一的布尔 Gate 验证函数。

### 5.2 它们解决的是“评审结论可执行化”问题

冻结工程与普通模块实现不同。普通实现看“代码写完、测试通过”；冻结工程还必须证明：

1. 哪些结论已经被冻结。
2. 哪些条件不满足时必须阻断进入下一阶段。
3. 哪些 Contract Tests 和 Checklist 是阶段准入门槛。

因此，`M2/M3/M4/V1ReadyChecklistGuards` 的本质是：

1. 把文档中的 DoD 转成代码。
2. 把阶段性“Go / Blocked”逻辑转成 CI 能执行的规则。

### 5.3 它们的合理性

这类文件在 freeze 阶段是合理且必要的，因为它们：

1. 防止评审结论只停留在文档里。
2. 防止 freeze gate 漂移。
3. 为 smoke tests 和 CI gate 提供统一入口。

---

## 6. 当前设计的问题

当前 `boundary/` 目录的问题不在于方向错误，而在于“可发现性不够”。

### 6.1 文件名过度依赖里程碑缩写

`M2ChecklistGuards.h`、`M3ChecklistGuards.h`、`M4ChecklistGuards.h`、`V1ReadyChecklistGuards.h` 对当前参与 freeze 的成员是清楚的，但对后续维护者不够直观。

### 6.2 普通边界守卫与里程碑 Gate 混放

`BoundaryGuards`、`ContextBoundaryGuards` 这类是对象边界守卫；`M3ChecklistGuards`、`V1ReadyChecklistGuards` 这类是里程碑 Gate。两类资产时间尺度和职责不同，但当前都在同一层目录下，容易让目录逐渐失去自解释性。

### 6.3 目录缺少统一索引

当前目录没有一个总览文件来说明：

1. 哪些文件是 catalog。
2. 哪些文件是 ADR mapping。
3. 哪些文件是 compatibility / versioning。
4. 哪些文件是 milestone gate。

这会导致未来新成员只能靠文件名猜测功能。

---

## 7. 是否需要优化

结论：**需要优化，但不需要推翻。**

### 7.1 不建议做的事

1. 不建议删除 `M2/M3/M4/V1ReadyChecklistGuards`。
2. 不建议把里程碑 Gate 从 contracts 层移走。
3. 不建议把所有 boundary 逻辑塞回各子域目录。

原因：这些规则本质上仍然属于 contracts 治理层，而不是业务模块内部实现。

### 7.2 建议做的事

#### A. 增加 boundary 索引说明

建议新增以下资产之一：

1. `contracts/include/boundary/README.md`
2. 或 `contracts/include/boundary/BoundaryIndex.h`

至少说明：

1. boundary 目录的四类职责。
2. 各文件分组。
3. M2/M3/M4/V1Ready 与 WP / milestone 的对应关系。

#### B. 将 milestone gate 与普通 guard 做目录分组

建议新增子目录：

1. `boundary/catalog/`
2. `boundary/guards/`
3. `boundary/governance/`
4. `boundary/milestones/`
5. `boundary/adr/`

其中：

1. `M2ChecklistGuards.h`
2. `M3ChecklistGuards.h`
3. `M4ChecklistGuards.h`
4. `V1ReadyChecklistGuards.h`
5. `CoverageMatrixGuards.h`
6. `DomainRolloutGuards.h`

建议收拢到 `boundary/milestones/`。

#### C. 为 milestone gate 提供更语义化别名

建议在保留原文件名兼容的同时，增加更直观的别名头文件，例如：

1. `CrossCuttingFreezeChecklist.h`
2. `MainFlowFreezeChecklist.h`
3. `BoundaryFreezeChecklist.h`
4. `ContractsV1ReadyChecklist.h`

这样既保留历史任务编号可追溯性，又提高后续维护可读性。

---

## 8. 推荐优化顺序

1. 先新增 boundary 索引说明文档或索引头文件。
2. 再引入 milestone 子目录或语义化别名头文件。
3. 最后再做 include 路径和测试引用的收敛迁移。

这样可以先提高可发现性，再做代码组织调整，避免一次性大改影响现有 freeze 证据链。

---

## 9. 结论

1. `boundary/` 是 contracts 的边界治理层，不是普通对象定义目录。
2. `M2/M3/M4/V1Ready Checklist` 这类文件存在的原因是将评审结论转为可执行 Gate。
3. 当前设计是合理的，但命名与组织已经暴露出可发现性问题。
4. 建议进行一次轻量优化，重点是索引、分组和语义化别名，而不是删除或推翻现有 Gate 体系。
