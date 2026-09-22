# 环境预检 + 自愈升级策略（低频流程）——参考

> **定位**：承接 qsh-debug §2.4/§2.5 的完整内容。STRATEGY_TEST 启动 run_all.py 前的
> 逐项预检清单、遇到问题时的自愈阶梯与超时升级规则。日常使用只需 SKILL.md §2.1-2.3；
> 跑批量回归预检 / 环境问题自愈超时升级时才需要本文件。

## 1. 环境预检（Supervisor 执行前必做）

执行任何碰硬件的任务前必须确认环境就绪。**STRATEGY_TEST 启动 run_all.py 前，
runner 必须逐项手动验证**（不得委托 run_all.py 内置预检——其 erase 在 DebugServer
未启动时会静默成功、precheck 探测连接本身制造残留）：

```text
1. DebugServer 进程存在？
   tasklist | grep -i DebugServer
   → 无 → 启动（SKILL.md §2.1）→ 等 5s 稳定期 → 重验

2. Port 1025 就绪？
   netstat -ano | grep ":1025.*LISTENING"
   → 仅用于「刚启动后等就绪」的秒级轮询；判在线唯一靠项3 GDB，勿用 netstat 判离线
   → 无 → 等 5s 重试 → 仍无 → 重启 DebugServer（§2 四步）→ 等 5s → 重验

3. GDB 能连上（★ 在线判定唯一依据）？
   riscv64-unknown-elf-gdb -batch -ex "target remote 127.0.0.1:1025" -ex detach -ex quit 2>&1 | tail -5
   → 含 "detached" 或退出码 0 = 通
   → 失败 → 自愈四步（§2）→ 等 5s → 重验

4. 串口数量够（排除 CKLink）？
   python -c "from serial.tools.list_ports import comports; ps=[p.device for p in comports() if '32bf' not in (p.hwid or '').lower() and 'cklink' not in (p.description or '').lower()]; print(f'{len(ps)} ports: {ps}')"
   → 双核需 ≥2，单核需 ≥1 → 不够则报告缺串口
   → ★ 实际判据比字面更严（G-ENV-COM5，2026-09-22）：预检的**自动发现**分支要求
     "**全部**非 CKLink 口都能打开" —— 任意一个**无关口**（桌面新插入、被别的进程占用的
     CP210x 等）`PermissionError` 即**整轮预检 FAIL、批量中止**（**不是**被测板串口坏）。
     判据：`precheck_report.md` 写 `不可开: ['COMxx']` 而其余口正常 ⇒ 无关口拖后腿
     （**先确认被占的不是本板 CTRL/TGT 口**；是则属真缺串口，另按缺串口处置）。
     处置：显式指定 `PRE_CTRL_UART`/`PRE_TGT_UART`（该分支**只校验这两个口**）。运行期
     `common/acquire.py::discover_ports()` 对打不开的口只 WARN 不中止 ⇒ **只此一关严格**。

5. 无 FlashProgrammer 冲突？
   tasklist | grep -i csky
   → 有 → taskkill //F //PID <pid> → 重验

6. 无 1025 ESTABLISHED 残留（上次会话遗留）？
   netstat -ano | grep ":1025.*ESTABLISHED"
   → 有 → clean_gdb_residue（§2 四步）→ 重验
```

预检失败 → 自愈 → 重试 1 次 → 仍失败 → 升级（报告环境问题，需人工检查硬件连接）。

## 2. 脚本化自愈四步（run_test.py 的 `clean_gdb_residue`）

> **实测纠正**：detach 不能真正释放连接（任何"连接→detach→退出"的 gdb 都留
> 残留，**探测连接本身也制造残留**）→ 残留深时 detach 无效，唯一可靠 = 重启
> DebugServer 进程。

```text
① netstat 找 1025 ESTABLISHED 对端 PID 杀 gdb
② taskkill //F //IM DebugServerConsole.exe
③ netstat 精确杀 1025 LISTENING 兜底
④ Popen 重启 + stdout 重定向日志，轮询日志出现 `DebuggerServer$` 提示符判就绪（≤30s），
   不能只固定 sleep（CKLink 初始化耗时随机）
```

## 3. 自愈升级策略（超时升级，2026-09-07 用户裁定）

**核心原则：先穷尽自愈手段，超时后才升级。升级必须带结构化信息。**

### 自愈阶梯（遇到问题时按顺序执行，不跳步）

```text
环境问题（DebugServer/串口/CKLink）：
  Step 1: taskkill //F //PID（双斜杠，MSYS 兼容）杀残留进程
  Step 2: taskkill //F //IM DebugServerConsole.exe（按名字杀）
  Step 3: 确认 port 1025 干净（netstat）→ 启动 DebugServer（SKILL.md §2.1）
  Step 4: 轮询 netstat 等 port 1025 LISTENING（秒级，不要固定 sleep）
  Step 5: GDB 连 127.0.0.1:1025 验证在线
  Step 6: 仍失败 → 拔插 CKLink USB（物理释放）→ 回 Step 3
  Step 7: 以上全部失败 → 超时升级

构建失败：
  Step 1: 查 KNOWN_ISSUES.md 匹配已知坑 → 按坑修复 → 重建
  Step 2: clean build（删 build 目录重建）→ 回 Step 1
  Step 3: 仍失败 → 超时升级

烧录/GDB 连接失败：
  Step 1: 杀残留 GDB（netstat 查 1025 ESTABLISHED 对端 PID）
  Step 2: 重启 DebugServer（同上 Step 2-4）
  Step 3: GDB 连接 → 尝试 reset + load
  Step 4: 仍失败 → 超时升级
```

**关键约束：每步做完后必须验证结果再进下一步（不能盲跳）。**

### 超时升级规则

| 自愈阶段 | 超时上限 | 到期后 |
|---------|---------|-------|
| 环境类（DebugServer/串口/CKLink） | **30 秒** | 升级 |
| 构建类 | **1 分钟** | 升级 |
| 烧录/GDB 类 | **30 秒** | 升级 |

超时 = 从第一次遇到问题到当前的墙钟时间（不是步数）。超时前完成所有阶梯步骤。

### 升级格式（必须结构化，禁止模糊求助）

升级消息模板：
```text
🔺 自愈超时（已耗时 X 分钟），需要人工介入

问题: <一句话描述>
已尝试:
  ① taskkill //F //PID xxx → <结果>
  ② 重启 DebugServer → <结果>
  ③ ...
最后错误: <粘贴关键错误信息>
建议操作: <具体可执行的下一步>
```

**禁止**："需要用户操作"（不够具体）、"请手动关闭 DebugServer"（不说原因）、反复发同一条消息。

### 直接升级（不走超时阶梯）

1. 同一 item 重试次数耗尽仍失败
2. 连续 3 个 item ESCALATE（可能是系统性问题）
3. 需要修改硬件连接（接线改动、检查焊点）
4. 需要修改非本项目的代码（Zephyr 上游、HAL 库源码）
5. 不确定属于哪种失败类型（诊断无把握）
