# qsh-debug 补充提案：ref-host-wsl2.md

- **目标目录**：`810_driver_test/.claude/lib/qsh/skills/qsh-debug/`
- **状态**：**未合并**（本轮不改 skill 库）
- **实际文件**：`/home/eunice/zephyr_workspace/ls1010_port_spi_i2s/skill/qsh-debug/ref-host-wsl2.md`
- **提案日期**：2026-10-27

## 1. 建议动作

1. 把 `skill/qsh-debug/ref-host-wsl2.md` 复制到
   `810_driver_test/.claude/lib/qsh/skills/qsh-debug/ref-host-wsl2.md`。
2. 在 `qsh-debug/SKILL.md` 顶部“详细参考（按需读）”列表追加一行：

   ```markdown
   [ref-host-wsl2.md](ref-host-wsl2.md) WSL2 宿主差异（NAT/DebugServer/串口/interop）
   ```

3. 可选：在 `ENVIRONMENTS.md` §宿主带来的差异 中把 `ref-host-linux.md` 的示例补成
   `ref-host-linux.md` / `ref-host-wsl2.md`；该项另见
   [`../ENVIRONMENTS/ENVIRONMENTS_host_qsh_wsl2.md`](../ENVIRONMENTS/ENVIRONMENTS_host_qsh_wsl2.md)。

## 2. 内容边界

- **只写宿主差异**：WSL2 的 NAT 地址、DS 路径/启动方式、GDB 序列、串口采集、interop 坑。
- **不复制平台主体**：烧录法、双核两遍/单核单遍、串口识别规则仍在 `qsh-debug/SKILL.md`。
- 每条结论标【实测】/【假设·待验】，证据指向本次 `05_verification/`。

## 3. 为什么放 ref 而不是新开环境

`ENVIRONMENTS.md` 已明确：宿主是环境的**属性**，不参与环境命名；同一平台可能换宿主。
WSL2 与 Windows 的差异是局部的，放进 `qsh-debug/ref-host-wsl2.md` 即可，不新开环境、
不复制整套 skill。

## 4. 合并前检查

- [ ] 复制后 `qsh-debug/SKILL.md` 的链接可达
- [ ] 不修改 `lib/qsh/` 其他文件
- [ ] 跑 `bootstrap.py qsh`（若库结构变更）
- [ ] 用 `skill-auditor` 验证链接完整性
