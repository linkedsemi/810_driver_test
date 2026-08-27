# write_delay_read_len 偶发 actual=64 假象：master 读段 DCNT 固定导致的 0xff 填充

日期：2026-08-27  状态：已闭环（判定层吸收，非驱动 bug）

## 现象

`write_delay_read_len`（'Z' 协议）len=32 扫描中**偶发**（6 次运行仅 1 次出现 11 个步）
`actual=64`：master 读段返回字节数 = rd 缓冲大小（64）而非 32。判定 `actual==len`
直接 FAIL——设计为"欠载测试"的用例却报"多收"。

## 根因（机制闭环）

master 读段 `msg[1]` 的 len = `sizeof(rd)` = **固定 64**（DCNT=64），与写段内容
（len=17/32/64）无关。读段的终止只由 **target 侧 T-bit=0** 驱动：

- **正常终止**：target 供完 len 字节（末字节带 END0）→ 下一字节 T-bit=0 → master
  正常收尾，`actual=len`。
- **中途欠载**：target 供完 32 字节后 FIFO 空（第 33 字节 T-bit=0）——若此 T-bit=0
  未被驱动判为"合法终止"（END0 未到，欠载填充路径），**master 硬件继续读满 DCNT=64，
  后续字节全部是 0xff（FIFO 空读值，真实读回，非缓冲残留）** → `actual=64` 假象。

判别证据：full 32 步（正常终止）`rd[32..35]=0xaa`（**栈残留**——驱动只写了 32
字节）；actual=64 步 `rd[32..]=0xff`（**真读回**——驱动读了 64 字节）。对照实验
确认 0xff 不是残留：残留是 0xaa（首步前栈值）或上一步读回值，不会是 0xff。

## 起始欠载 vs 中途欠载（T-bit 行为差异）

| 场景 | 触发 | 表现 |
|---|---|---|
| 起始欠载 | prefill 未赶上（读段首字节时 FIFO 空） | 首字节 T-bit=0 → master 收 0 字节（`actual=0`，偶发 -EIO=NACK） |
| 中途欠载 | 供满 len 后 FIFO 空（如供 32 字节后） | 第 33 字节 T-bit=0 → master **读满 DCNT=64 收 0xff**（`actual=64` 假象） |

实测：len=17/64 的 short 步都是起始欠载（`actual=0`）；只有 len=32 的 short 步
出现中途欠载（`actual=64`）——len=17/64 的扫描在 prefill 没赶上时首字节就空。

## 证据链

1. target 'Z' 解析全程正确（分桶探针 z=[0 125 116 116 0]，len 字节 17/32/64 无错位）。
2. actual=64 步 `rd[0..31]=0x10..0x2f` 连续（target 真实供 32 字节）+
   `rd[32..]=0xff`（真实读回，对照 full 步 0xaa 栈残留）。
3. len=32 扫描中 short 步的 target 供数边界（bnd）偶发为 30/31（FIFO 空时刻抖动）。

## 修复（controller 判定层，测试固件，非驱动）

**判定从 `actual==len` 改为 pattern 前缀完整性（bnd ≥ len）**：

```c
/* pattern 前缀完整性：第一个 ≠ 0x10+i 的下标 = target 实际供数边界 */
int bnd = actual;
for (int i = 0; i < 64; i++) {
    if (rd[i] != (uint8_t)(0x10 + i)) { bnd = i; break; }
}
if (bnd >= (int)len) {
    /* 成功档：target 供满 len；actual 被 DCNT 放大时多收 0xff 是
     * master 侧硬件行为，非 target 欠供 */
    critical = step;
    validate_pattern(rd, len, 0x10, "delay-read-len");
} else { ... 欠载步 ... }
```

安全性：`bnd ≥ len ⟺ rd[0..len-1] 全对`。缓冲残留只会把 bnd 往大推，不会把
"真欠载"（断裂点 < len）改判成功——判定无污染风险。

## 验证

- 12m5/6m25 双 case 多轮 dbg_run：PASS（含偶发 first_short=30 的短步被正确归类，
  不再 FAIL）。
- 探针清理后仍 PASS。

## 通用教训

1. **master 读段 actual 与 target 供数边界可能不一致**：DCNT 固定 + 欠载填充时
   actual 会被放大——**判定优先用内容校验（pattern 前缀），不依赖长度相等**。
2. **中途欠载与起始欠载的 T-bit 行为不同**：起始欠载（首字节空）→ NACK/0 字节；
   中途欠载（供满后空）→ 读满 DCNT 收 0xff。驱动对两种 T-bit=0 的收尾路径不同。
3. 调试判别"残留 vs 真读回"：用对照步（full 步）的栈残留值（0xaa）作基准。

## 附：判定②基准演进（len17 波动发现）

实测多次运行：**len17 critical 波动 120-143 档（6000-7150ns），len32/64 稳定在
117-121 档且互差 ≤3 档**。len17（1 字节续发）的临界档受 ISR 抖动放大（单字节
窗口宽松，prefill 部分赶上即成功）；len32/64（16/48 字节续发）的临界档稳定。
**判定②基准从 len17 改为 len32↔len64 互比**（同为多字节续发；64 需 3 倍续发量，
若续发跟不上 T-bit 间隔会显著缩小——实测差 ≤3 档 = 续发路径性能充足）。
