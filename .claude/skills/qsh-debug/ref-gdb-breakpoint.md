# 断点抓现场 QSH 命令模板——参考

> **定位**：承接 qsh-debug §6 的完整内容。方法论（断在"状态被破坏之前"、一轮只抓
> 一个时刻）见 embedded-debug §2；本文件是 QSH 双核固件的裸地址断点命令模板与
> 统一 GDB 接口说明。

## 1. 统一 GDB 接口（★ 优先于手写 .gdb batch 脚本）

`.claude/tools/gdb/gdb_proxy.py`（JSON 输出，agent 易解析），三后端按环境自动降级：

1. **MCP**（agent 工具列表有 `gdb_*` 时最优，需项目根 `.mcp.json` 注册
   `.claude/tools/gdb/gdb_mcp_server.py`）
2. **pygdbmi 交互**（`pip install pygdbmi`，单连接复用，多步会话高效）
3. **batch**（无依赖兜底，每次新进程，等价 §2 裸 gdb 模板）

子命令：`connect --elf` / `load --pc` / `break --addr` / `continue --timeout` /
`read-mem --addr` / `read-reg --name` / `backtrace` / `exec --cmd`（原始命令
万能兜底）/ `status` / `disconnect`；完整用法、JSON 格式与 MCP 注册方式见
`.claude/tools/gdb/README.md`。后端③场景（无 Python 依赖）仍照用 §2 模板。

## 2. QSH 双核固件裸地址断点模板

目标固件无调试信息时用 nm 查符号地址，链接地址 = 加载地址：

```bash
# 找符号地址
riscv64-unknown-elf-nm <build>/target/zephyr/zephyr.elf | grep <symbol>
riscv64-unknown-elf-gdb -q \
  -ex "target remote 127.0.0.1:1025" -ex "file <target>/zephyr/zephyr.elf" \
  -ex "reset" -ex "load" \
  -ex "restore <controller>/zephyr/zephyr.bin binary 0x10000000" \
  -ex "set \$pc=0x100c0000" \
  -ex "break *0x<addr>" -ex "continue" \
  -ex "x/dw 0x<变量地址>" -ex "x/wx 0x<寄存器地址>" \
  -ex "detach" -ex "quit"
```

要点：
- 一轮断点只抓一个时刻；命中后读现场、`detach` 让目标继续跑，避免影响时序
- 变量用 `x/dw`/`x/dh`（有符号），寄存器用 `x/wx`，数组用 `x/Nbx`
- 临时调试脚本（如 probe_*.py）用完必须清理（SKILL.md §8）
- 需要验证"哪个分支被执行"时，直接断在该分支代码处而非回调入口
