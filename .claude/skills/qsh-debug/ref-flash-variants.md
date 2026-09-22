# 烧录变体专题（低频）——参考

> **定位**：承接 qsh-debug §3.1/§3.2 的完整内容。标准场景 = SKILL.md §3 双核两遍
> 烧录法；本文件是两个变体专题：单核工程单遍烧录（otbn_test/otbn_selftest）、
> flash boot 破坏性复位测试（wdt_test）。用到对应工程时才需要本文件。

## 1. 单核工程烧录（otbn_test/otbn_selftest，★ `KNOWN_ISSUES.md` #23/#24）

与双核两遍烧录法**不同**，单核工程用**单遍烧录**（dbg_run.py 已固化）：

```bash
cd 810_driver_test/projects/otbn_test/tests
python scripts/dbg_run.py --case case_default --timeout 90   # 单遍：reset→load→continue 采首轮
python scripts/dbg_run.py --case case_default --no-load      # 免烧重跑（镜像已在 RAM，0 烧录）
python scripts/dbg_run.py --wpacket-size 0                   # 关闭写包提速（默认 16384）
```

- **单遍烧录成立**：reset 已冲刷 I-cache（两遍法在双核存在的"慢一拍"场景
  被覆盖），load 后 .data/.bss 天然干净；单遍与两遍法结果逐项一致（三连验证）
- **会话内二次 load 会静默挂死**（实测 2/2）：第一次 load 成功后 interrupt、
  再发 `load`，gdb 无任何输出（连 "Loading section" 都没有）→ 只能超时
  proc.kill()，且留 DebugServer 连接残留（按 ref-self-heal.md §2 重启才干净）。
  **不要在任何单会话流程里 load 第二次**
- **写包提速（41x）**：load 前 `set remote memory-write-packet-size 16384` +
  `set remotewritesize 16384`，CKLink load 实测 1KB/s → 41KB/s（192KB 镜像
  ~190s → ~5s）；stub 不接受时回 `Remote failure`/`E22` 立即失败
  - **★ 口径更正（2026-09-21 实测）**：请求包长 16384 / 2457 / 1024 → gdb **协商**
    `2493 / 1820 / 875 bytes/write`，**但加载吞吐恒 41 KB/s、墙钟恒 3~4s（与包长无关）**
    ⇒ **41 KB/s 才是链路瓶颈**。判吞吐看 gdb 的 `Transfer rate: N KB/sec`，
    **别从 `N bytes/write` 反推提速倍数**。双核 `load_and_run` 的"不设时慢 6.7 倍 / 41x"
    注释已按实测撤回。见 `qsh-debug` SKILL.md §3.1。
- **交互式流程（★ `KNOWN_ISSUES.md` #25）**：reset+load 后等 "Transfer rate" 再 continue。
  一次性 -ex 链的 continue 在 load 刚完成瞬间发出，DebugServer 状态不佳时
  被静默丢弃 → CPU 停 halt 无 BOOT，detach 恢复运行（回归 3/3 复现）；
  dbg_run.py / run_test.py 均为交互式（0 失败）。手工 GDB 也照此
- **免烧重跑 --no-load**：attach → `x/4wx <TARGET_ROM_START>` sanity（镜像在？非全 0）
  → `set $pc=<TARGET_ROM_START>` 重跑复位向量（重新初始化 .bss/.data，等效软复位）。
  **入口地址按构建板型定**：单核 1os = `0x10000000`，2os = `0x100c0000`
  （脚本内走环境变量 `TARGET_ROM_START`，勿硬编码，见 qsh-test-gen §1.5 / KNOWN_ISSUES #57）

## 2. flash boot 破坏性复位测试（bootrom 引导非 XIP）

首次完整跑通于 wdt_test `case_destructive_reset`（2026-09-07）：烧录含 bootram 引导头的
镜像到 flash，由 bootrom 引导真实复位——RAM 回归触发不了的硬件复位场景
（如 sec_iwdg）。完整软件流程见
`projects/wdt_test/tests/case_destructive_reset/run_destructive.sh`；
坑细节见 KNOWN_ISSUES #46-49。

- **烧录对象**：`zephyr_flash_bram_*.bin` @0x08000000（如
  `zephyr_flash_bram_xip_lsqsh_evb_2os_lsqsh_cpu1.bin`），**不是 zephyr.bin**。
  zephyr.bin 是裸应用无 bootrom 引导头，bootrom 不认（烧了 PC 停 bootrom 不引导）；
  flash_bram bin = 引导头 + 应用。文件名里的 "xip" 是 HAL 生成的固定命名
  （`SOC_LSQSH_BUILD_OUTPUT_BRAM_BIN` 产物名后缀），**不代表该固件用了 XIP 板型**
- **构建**：普通 2os 固件即可（如 `lsqsh_evb@2os/lsqsh/cpu1`，入口/文本在 RAM，
  bootrom 加载到 RAM 引导执行），**不需要 cpu1_xip 板型，不需要专门 XIP 配置**
- **触发复位**：`python flash_program.py --bin <flash_bram.bin>@0x08000000 --reset-and-run`
  （硬复位 ConnectType=1，不 halt CPU）
- **★ 不要用 GDB 触发 flash boot**：GDB `reset`+`detach` 会 halt CPU（detach 不清 halt），
  PC 停 0x1000000 bootrom，固件不启动（KNOWN_ISSUES #49）——flash boot 必须靠硬复位/断电
- **CKLink 独占**：烧 flash 前先杀 DebugServer/CSKYFlashProgramer（互斥双向，SKILL.md §2.2）；
  复位触发前也要退出 DebugServer
- **跨复位状态机**：测试代码用 `__noinit` 变量跨复位记录阶段（warm reset 保留 RAM
  noinit）；WDT halt 被调试器暂停时会暂停计数（WDT_OPT_PAUSE_HALTED_BY_DBG），
  故复位触发必须用不 halt 的硬复位
- **GDB 清 noinit 重启测试**：
  `riscv64-unknown-elf-nm elf | grep test_phase` 找地址 →
  gdb `set {int}0x<addr> = 0`（run_destructive.sh 用它清 test_phase/phase0_passed 后重跑）
- **★ 采集窗口不得按"标称超时"设（2026-09-20）**：`WDT_TIMEOUT_MS 2000` 是驱动给的**标称**值，
  sec_iwdg 实际复位延迟受 IWDT 时钟/复位域影响可达 **~34s**（实测 PHASE 0 后 33.9s 才重启，
  另一样例 4.1s ⇒ 不确定）⇒ 18/20/25s 窗内"再无输出"**被误判成"没复位"**。
  **判据优先取跨复位 `__noinit` 终态**（`test_phase==2`），只能看串口时窗取 **≥60s**。
  同类：`app_iwdg_cpu2_test case_reset`、`case_pmu_iwdg_full_reset`（KNOWN_ISSUES #100）。
- **★ 2os 上 cpu2 不会自己起（2026-09-20）**：**本板 bootrom 只引导 cpu1** —— 把 cpu2 镜像烧到
  flash `0x08100000` **不等于**它会启动。cpu1 build 若 `# CONFIG_CPU2_IMAGE_HEADER is not set`，
  `boot_cpu2()` 只 `app_cpu_dereset_by_addr(0x10000000)`、**不搬运镜像** ⇒ cpu2 RAM 里是上次残留
  （GDB 读 `0x10000000` 非本次构建的 ELF）。**判别**：读 `0x10000000` 的 ELF magic + `e_entry`，
  与本次 cpu2 产物 `objdump` 对照；**修法**：把 cpu2 镜像先 staging 进 RAM（GDB `restore` 或
  cpu1 boot 搬运），或按 SPEC_REFERENCE §6.7.4 换 CPU1 构建变体。实例：`app_iwdg_cpu2_test case_reset`
  的 `run_reset.sh`（文件头自述已列该未校准项）。见 KNOWN_ISSUES #102 / SPEC_REFERENCE §6.7.3。
