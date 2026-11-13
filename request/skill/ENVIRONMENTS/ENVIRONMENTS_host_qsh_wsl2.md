# ENVIRONMENTS.md 补充提案：qsh 宿主增加 WSL2

- **目标文件**：`810_driver_test/.claude/ENVIRONMENTS.md`
- **状态**：**未应用**（原文件已保持原样，当前 6765 B）
- **提案日期**：2026-10-27
- **证据**：`810_driver_test/request/wsl_environment/wsl_environment_ask/05_verification/verification_report.md`

## 1. 环境表（第 15 行）

原文：

```text
| `qsh` | `810_driver_test` | Windows（**也可能迁到 Linux**） | MCU · Zephyr · **RV32 E906** | 已适配 |
```

建议改为：

```text
| `qsh` | `810_driver_test` | Windows / WSL2（NAT；也可能迁到 Linux） | MCU · Zephyr · **RV32 E906** | 已适配 |
```

理由：qsh 目前实际在 WSL2（Ubuntu 22.04）上开发；宿主仍是**属性**，不改变环境身份
（平台/芯片族），也不改变启用集与休眠规则。

## 2. 宿主硬编码提示（第 39 行）

原文：

```text
> 调试器路径（见 `env-adapter` §4.1 替换清单）。迁到 Linux 宿主时这些会断，
```

建议改为：

```text
> 调试器路径（见 `env-adapter` §4.1 替换清单）。迁到 Linux / WSL2 宿主时这些会断，
```

## 3. 证据摘要（2026-10-27 WSL2 实测）

| 项 | 结果 |
|---|---|
| 宿主 | WSL2 Ubuntu 22.04，NAT，default via `172.24.48.1` |
| 构建 | `lsqsh_evb@2os/lsqsh/cpu1` hello_world 成功 |
| GDB / 上板 | `172.24.48.1:1025`，cpu1 `Start address 0x100c0000`，cpu2 `0x10000000`，均到 `arch_cpu_idle` |
| 串口 | COM24（CH342-B）= cpu1 console，抓到 `*** Booting Zephyr OS ...` + `Hello World!` |
| 自检 | `env_check.py` 20 PASS / 0 WARN / 0 FAIL |

## 4. 应用方式

- 由用户或 keeper 在确认后手动合并；**本提案不自动改原文档**。
- 若合并，只需改上述两行；其余段落（环境身份、归属、启用集）不变。
- 更细的宿主差异（DS 地址、串口、GDB 序列、interop 坑）见
  [`../../../../skill/qsh-debug/ref-host-wsl2.md`](../../../../skill/qsh-debug/ref-host-wsl2.md)。
