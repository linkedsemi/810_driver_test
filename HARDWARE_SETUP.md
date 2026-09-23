# 硬件环境配置

## 板级要求

- **板卡**：`lsqsh_evb@2os`（双核 AMP）
  - cpu1 = target (uart1)
  - cpu2 = controller (uart5)
- **接线**：
  - I3C5 ↔ I3C6 跨核对连
  - CKLink 连 JTAG
  - 两路 UART 115200 8N1

## COM 口映射（非固化，动态识别）

**⚠️ 串口号不固定**，每次拔插或重启后可能变化，必须按 VID:PID 动态识别。

### 识别规则

| 设备 | VID:PID | 角色 | 识别方法 |
|------|---------|------|----------|
| CKLink Serial | 32BF:B210 | 调试器 | **排除**，不是目标口 |
| MCP2200 | 04D8:00DD | controller (cpu2) | 按 VID:PID 匹配 |
| CH342 | 1A86:55D2 | target (cpu1) | 按 VID:PID 匹配（可能有多个） |

### 动态识别方法

```python
# 参考 regression/precheck/run_precheck.py 中的识别逻辑
import serial.tools.list_ports

for port in serial.tools.list_ports.comports():
    if port.vid == 0x04D8 and port.pid == 0x00DD:
        controller_port = port.device  # MCP2200
    elif port.vid == 0x1A86 and port.pid == 0x55D2:
        target_port = port.device      # CH342
    elif port.vid == 0x32BF and port.pid == 0xB210:
        debug_port = port.device       # CKLink (排除)
```

### 常见 COM 号（仅供参考，非固定）

- COM14: CKLink Serial（调试器）
- COM15: MCP2200（controller）
- COM16/17: CH342（target）

**注意**：实际使用时必须动态识别，不要硬编码 COM 号！

## DebugServer 配置

| 项 | 值 |
|----|---|
| GDB | `riscv64-unknown-elf-gdb`（无 Python） |
| Server | `127.0.0.1:1025` ← PC 进程 `C:\uaer_app\debug_server\bin\DebugServerConsole.exe` |
| 互斥 | DebugServer ↔ FlashProgrammer 同时只能一个占 CKLink |

## 环境预检

每次测试前确认：
1. 无 FlashProgrammer 残留进程
2. DebugServer 在线（GDB 连一次判断）
3. 串口空闲
4. 批量预检：`python regression/precheck/run_precheck.py`

```bash
# 快速自检（tasklist 逐项 grep）
tasklist | grep -i DebugServer    # 1) DebugServer 已启动
tasklist | grep -i csky           # 2) 无 FlashProgrammer 残留
tasklist | grep -i python         # 3) 串口空闲
```

任一项异常，先按 `CLAUDE.md` §5 / `qsh-debug` skill 排查。

## GPIO Toggle 引脚

所有测试固件自动包含两个 GPIO 翻转线程：
- **PA05**（优先级 8，高）：500ms 周期翻转
- **PA07**（优先级 12，低）：500ms 周期翻转

用于示波器观察系统负载和时序稳定性。
