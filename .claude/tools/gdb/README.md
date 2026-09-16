# GDB 调试工具集

统一 GDB 调试接口，三个后端自动降级。agent 按环境自动选最优。

## 后端优先级

| 优先级 | 后端 | 检测方式 | 特点 |
|--------|------|---------|------|
| ① | MCP Server | agent 工具列表有 `gdb_*` | 原生调用，最无缝 |
| ② | pygdbmi | `pip install pygdbmi` | 交互式，单连接复用 |
| ③ | batch | 无依赖 | 兜底，每次新进程 |

## 快速开始

### 方式 1：CLI 中间件（无需安装依赖，batch 模式兜底）

```bash
# 连接 + 加载固件
python gdb_proxy.py connect --elf build/qsh/otbn_sm3_hmac.elf
python gdb_proxy.py load --pc 0x10000000

# 设断点 + 运行
python gdb_proxy.py break --addr 0x10003ef4
python gdb_proxy.py continue --timeout 30

# 读现场
python gdb_proxy.py read-mem --addr 0x40010018          # OTBN STATUS
python gdb_proxy.py read-mem --addr 0x4001001C          # OTBN ERR_BITS
python gdb_proxy.py read-reg --name pc
python gdb_proxy.py backtrace

# 原始 GDB 命令（万能兜底）
python gdb_proxy.py exec --cmd "info registers"

# 断开
python gdb_proxy.py disconnect

# 查询状态
python gdb_proxy.py status
```

所有输出为 JSON 格式，方便 agent 解析。

### 方式 2：MCP Server（需要 `pip install pygdbmi mcp`）

注册到 Claude Code（项目根目录 `.mcp.json`）：

```json
{
  "mcpServers": {
    "gdb": {
      "command": "python3",
      "args": ["810_driver_test/.claude/tools/gdb/gdb_mcp_server.py"]
    }
  }
}
```

注册后 agent 直接调用 `gdb_connect`、`gdb_read_memory` 等原生工具。

## Agent 后端选择逻辑

```
if MCP 工具有 "gdb_connect":
    用 MCP（最优）
elif python3 -c "import pygdbmi" 成功:
    用 gdb_proxy.py（交互模式）
else:
    用 gdb_proxy.py（batch 兜底）或手写 .gdb 脚本
```

## 环境要求

| 项 | 值 |
|---|---|
| GDB | `riscv64-unknown-elf-gdb`（在 PATH 中） |
| DebugServer | `127.0.0.1:1025`（XuanTie DebugServerConsole） |
| 目标 | RISC-V 32-bit (XuanTie E906) |
| Python | 3.8+ |

可选依赖：
- `pip install pygdbmi` — 启用交互模式
- `pip install mcp` — 启用 MCP Server

## 调试示例：抓 OTBN 寄存器

```bash
# 一行搞定：连接 → 加载 → 断点 → 运行 → 读寄存器 → 断开
python gdb_proxy.py connect --elf build/qsh/firmware.elf
python gdb_proxy.py load --pc 0x10000000
python gdb_proxy.py break --addr 0x100040c4   # HAL_OTBN_SM3_Final
python gdb_proxy.py continue --timeout 25
python gdb_proxy.py read-mem --addr 0x40010018  # OTBN STATUS
python gdb_proxy.py read-mem --addr 0x4001001C  # OTBN ERR_BITS
python gdb_proxy.py disconnect
```

每步返回 JSON：
```json
{"ok": true, "backend": "batch", "command": "read-mem",
 "data": {"addr": "0x40010018", "values": ["0x00000001"]}}
```
