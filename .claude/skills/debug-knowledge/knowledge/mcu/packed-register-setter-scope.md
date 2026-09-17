# 打包寄存器：HAL setter 作用域是整个寄存器，不是单个条目（mcu 域实例 #4）

> 方法学佐证：`embedded-debug` §1（分层定位到硬件层）。

## 现象
调用某 HAL setter "改第 N 个条目/字段"，结果**相邻的条目/字段被连带改写或清零**——
功能不符预期，而 setter 不报错、值看着"合法"。反汇编/源码层面难察觉，因为调用本身没错。

## 根因
许多外设把多个独立条目**打包进一个寄存器**（一个 32 位寄存器含 N 个条目/子字段，
寄存器按固定 stride 排布）。HAL 若提供的是"**整寄存器写**"型 setter
（`set(base, idx, val)` 实际落点 = `base + REG0 + 寄存器对齐偏移`），它写的就不是第 idx 个
条目，而是 idx 所落的**整个寄存器** → 同寄存器里的其余条目一起被覆盖。

**判据：看 setter 是否做读改写（RMW）**。只读源码/头文件时容易想当然认为 idx 是"条目索引"，
必须先确认 HAL 里 idx → 寄存器偏移的换算，以及是否按 stride 对齐。

## 判定方法
- 读 HAL setter 实现：定位 idx 到寄存器偏移的换算与是否 `read-modify-write`；
  若直接对 `base + REG + offset` 赋值一个 `val`，即为整寄存器写。
- 找同类是否有 `_by_idx` / `_field` 变体（通常内部 `ROUND_DOWN(idx, STRIDE)` 后读改写）。
- 写-回读验证：单条目改后读回整寄存器，看其余条目是否被动。

## 处理
- **单条目/单字段改** → 用 RMW 型 API（`*_by_idx()` / `*_field()`）；无此 API 时自行
  `读整寄存器 → 改目标字节/位域 → 写回`。
- **保存/恢复**（用例前后快照）必须按**整寄存器**粒度做，否则恢复不完整。
- 实例（IOPMP）：`PMPCFG_STRIDE = 4` 字节/寄存器、一个 CFGx 含 4 个条目；
  `iopmp_set_pmpxcfg(base, idx, val)` 写整个 CFGx（连带改另外 3 条），
  单条目改须 `iopmp_set_pmpxcfg_by_idx()`。项目级细节见 `810_driver_test/KNOWN_ISSUES.md` #65④。

## 关联
- 方法学：`embedded-debug` §1（分层→硬件层）
- 同域邻居：`debug-knowledge/mcu/reg-bitwidth-write-readback.md`（位宽以写-回读为准）、
  `pinmux-dual-state-electrical-residual.md`（寄存器"全对"实为残留）
- 实操：`qsh-debug` §5（硬件层寄存器：查 reg 头文件 + 写-回读）
