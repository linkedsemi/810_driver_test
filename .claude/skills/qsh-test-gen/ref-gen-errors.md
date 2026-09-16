# 常见生成错误速查

生成后编译/烧录/运行出错时查此表（现象→根因→修法）。

| 错误 | 原因 | 修法 |
|---|---|---|
| ztest 输出不出现 | test_main 返回导致 LOG_PANIC 死窗/停摆 | `k_thread_join(K_FOREVER)`；另查 prj.conf `CONFIG_ZTEST_VERBOSE_OUTPUT=y`（§10.26） |
| ISR 里 LOG 导致时序崩 | 违反 §4.2 | 改记账，dispatcher 汇总打印 |
| IBI/HJ 卡死 | ISR 里调 i3c_ibi_raise（拿 mutex） | 经事件队列 |
| 编译过但烧录不跑 | overlay 缓存旧 SRAM 地址 | `rm -rf build/*` 全量重建 |
| 串口角色全 ? | 连接残留 / 口号错 | qsh-debug §2.3 / §4.1 |
| INFO 宏编译错（"macro passed N arguments" / format 警告） | variadic 宏把格式串与数值参数一起透传，格式串既拼进 fmt 又重复入参 → 参数错位 | 用公共 `ZT_INFO(name, fmt, ...)` 宏（printk 直出，见 common/ztest_glue.h），勿自造 INFO 宏 |
| run 脚本 build 失败 "source directory -D does not exist" | west build 的 `-D` 必须连写 `-DVAR=VAL`，拆开会被当 source 目录 | `["west","build","-p","auto","-b",BOARD,"-d",build, src, f"-DDTC_OVERLAY_FILE={overlay}"]` |
| cmake 报 Invalid character escape '\z' | Windows 反斜杠路径传进 cmake 被转义 | 路径统一 `.as_posix()`（正斜杠） |
| run 脚本 ValueError: too many values to unpack | token 正则 `(TGT)` 用了捕获组，groups() 多出角色组 | 固定文本用非捕获组 `(?:TGT)`，只有变量用 `(...)` |
| run 脚本 TypeError: write_text() got unexpected keyword 'mode' | Python 3.5+ 的 Path.write_text 无 mode 参数 | 用 `with open(path, "a"/"w", encoding=...)` |
| dbg_run.py / run_test.py 串口 0 行（单核工程） | 脚本漏 `set {int}0x4002200c = 2`（cpu2 hold），cpu2 自启劫持 uart1 | ★ **模板必须注入**：gdb 命令序列在 `reset` 前加 `set {int}0x4002200c = 2`（APP_CPU_SRST bit1）；所有 dbg_run.py / run_test.py 模板必须包含此步骤 |
| overlay 编译后 printk 无输出 | overlay 缺 `chosen { zephyr,console = &uart1; };`，DT_CHOSEN 为 NULL → uart_console_init 跳过 | ★ **模板 overlay 必须含 chosen 块**：单核工程 overlay 显式声明 `chosen { zephyr,console = &uart1; };`；双核 cpu2 侧走 cpu2_console_uart5.overlay |
| 测试代码 store fault 挂死 → 串口 0 行 | k_malloc 返回 NULL → memset(NULL) → 崩溃；极具迷惑性表现为"串口无输出" | ★ **测试代码禁用堆分配**：不用 k_malloc/k_calloc，改用静态数组（`static uint8_t buf[N]`）；prj.conf 不依赖 CONFIG_HEAP_MEM_POOL_SIZE；排查串口无输出时**先查崩溃再查配置** |
| run 脚本归档只有 .json 没有 .md | 模板归档段漏 `write_markdown_report` | 抄已迁移 run_test.py：md writer + update_summary 报告路径指 .md（§1.6，2026-09-08） |
| 增删测试项后 dump mem 兜底错位/假 MISSING | run_test.py 与 dump_results.py 各硬编码一份 TEST_NAMES，只改其一；新项插在链接序**前部**时旧列表整体移位一格 | **两份 TEST_NAMES 必须同改**（都是 ztest 链接序，勿用 protocol.h 枚举序，两者不一致）；构建后 `nm --numeric-sort <elf> \| grep z_ztest_unit_test` 复核全序，并同步更新两处注释里的实测序清单（2026-09-09 broadcast 漏同步 dump_results 实例） |
