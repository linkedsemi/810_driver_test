#!/usr/bin/env python3
"""ztest 原生输出解析器（810_driver_test 公共模块）。

输入：串口采集行（原始，含 ANSI/回车）
输出：见 parse_ztest docstring。所有正则用 re.search（§10.5：
shell 提示符重绘会污染行首，不锚定 ^）。
"""
import re

ANSI_RE = re.compile(r"\x1b\[[0-9;?]*[A-Za-z]")

_BOOT_RE = re.compile(r"ZT_BOOT (\S+)")
_RESULT_RE = re.compile(r"(PASS|FAIL|SKIP) - (\S+) in [\d.]+ seconds")
_INFO_RE = re.compile(r"INFO (\S+):\s*(.*)")
_EXEC_RE = re.compile(r"PROJECT EXECUTION (SUCCESSFUL|FAILED)")
_ASSERT_RE = re.compile(r"Assertion failed at\s+(.+?):\s*(.*)")

# ---------------------------------------------------------------- R9（2026-09-29）
# 执行期间的**致命异常**（fail-closed，见 `common/judge.py` 的 R9 段）。
#
# 形状**以真归档为准**（`reports/**/serial_*.log` 全量普查，286 份）：
#
#     >>> ZEPHYR FATAL ERROR 0: CPU exception on CPU 0      （168 处，mbedtls 等）
#     >>> ZEPHYR FATAL ERROR 4: Kernel panic on CPU 0       （  3 处，dma_test）
#      mcause: 7, Store/AMO access fault                    （126 处，随横幅同现）
#      mcause: 5, Load access fault                         （ 42 处）
#      mcause: 11, Environment call from M-mode             （  3 处）
#     Current thread: 0x100874f8 (aes_bad_params)           （随横幅同现）
#
# 只锚**横幅本体**（`ZEPHYR FATAL ERROR`，z_fatal_error 的固定文案），不逐字
# 枚举 reason 段（`CPU exception` / `Kernel panic`）—— 新增 reason 文案不该让
# 守卫静默失效。`mcause` / `Current thread` 只作**取证补充**（原因描述 +
# 线程名归属），不单独触发判定。
_FATAL_RE = re.compile(r"ZEPHYR FATAL ERROR[^\r\n]*")
_FATAL_CAUSE_RE = re.compile(r"mcause:\s*\d+,\s*[^\r\n]*")
_FATAL_THREAD_RE = re.compile(r"Current thread:\s*0x[0-9a-fA-F]+\s*\(([^)]+)\)")
#: `START - <name>`：ztest 逐项输出里该项执行窗口的**左边界**（右边界 = 该项的
#: 结果行）。真归档 286 份普查：`START - ` 从不带前导空白（0 处）。
_START_RE = re.compile(r"START - (\S+)")

#: R9 逐项理由串（**文本真源**；`common/judge.py` 只转出，判定链从那里 import）。
#: 与"断言失败"**严格分开表述** —— 读者不能把"线程被异常打断"读成"断言没过"。
FATAL_ITEM_NOTE = (
    "CPU 异常（执行期间致命异常，**非断言失败**）：本项执行窗口内固件打印了 "
    "`{evidence}`（归档第 {line} 行{cause}）=> 测试线程被内核异常/panic 打断，"
    "ztest 的 `ztest_test_fail()` 未被调用，逐项结果停在**初值 PASS** —— "
    "那是假 PASS。按本模块/判定框架『任何不确定都不允许产生 PASS』判 FAIL。"
    "★ 串口该项仍会打印 `{token_result}`，报告以本理由为准（勿读成断言失败）。"
)


def fatal_item_note(evidence, line=None, cause="", token_result="PASS"):
    """R9：致命异常项的理由串（唯一真源，`parse_ztest` 与报告共用）。"""
    return FATAL_ITEM_NOTE.format(
        evidence=evidence,
        line=line if line is not None else "?",
        cause=f"；{cause}" if cause else "",
        token_result=token_result)

# D2 原生逐项 token（§6.9.1 的"同构 token"拼写，供**命令通道型**固件使用：
# 它们不注册 ztest，逐项证据由各自的 token 宏打印）：
#     <PREFIX>:<CTRL|TGT>:(PASS|FAIL|SKIP):<name>[:<reason>]
#     <PREFIX>:<CTRL|TGT>:INFO:<name>:k=v,k=v,...
# 其余 21 个族的 token 是 ztest 原生输出（D1），走上面的 `_RESULT_RE`。
#
# ★ 前缀**登记白名单**（§6.9.5 框架项）：新形态只由一个前缀白名单认领，避免
# 每加一个工程就改一次正则，也避免硬编码前缀把白名单外的行误吞。判别式含
# `:(CTRL|TGT):` 段 ⇒ 各 D2 工程自己的 SUMMARY/BOOT/DONE 行（第三段不是
# PASS/FAIL/SKIP/INFO）不会被误读成结果行。
#
# ⚠ **不得给前缀加"词边界"**（曾试 `(?<![\w])`，被真归档证伪）：串口行里
# token 常紧贴前一条输出/时间戳残留（实测 `…:00.005,0I3C_I2C_TEST:CTRL:PASS:x`
# —— 前缀前一个字符是 `0`）⇒ 词边界会把 5 个真结果项整批漏掉。
NATIVE_PREFIXES = ("I3C_I2C_TEST", "RTC_TEST", "MBOX_TEST", "I2C_PURE_TEST",
                   "APP_IWDG_TEST", "I3C_TEST")
_NATIVE_ALT = "|".join(re.escape(p) for p in NATIVE_PREFIXES)

_NATIVE_RESULT_RE = re.compile(
    r"(?:" + _NATIVE_ALT +
    r"):(?:CTRL|TGT):(PASS|FAIL|SKIP):([^\s:]+)(?::(.*))?")
_NATIVE_INFO_RE = re.compile(
    r"(?:" + _NATIVE_ALT + r"):(?:CTRL|TGT):INFO:([^\s:]+):(.*)")


#: 本固件 log 后端发出的**完整**转义形状：SGR（`\x1b[<params>m`）。
#: 唯一来源 = `zephyr/subsys/logging/log_output.c` 的 `LOG_COLOR_CODE_*`
#: （`\x1B[0m` / `\x1B[1;31m` / `\x1B[1;33m` …），全部以 `m` 收尾。
#: 它是"回退剥离"的锚：见 strip_line。
_SGR_RE = re.compile(r"\x1b\[[0-9;]*m")

#: "这一行里有取证价值的东西"的定位式（原生 token / ztest 结果行 / INFO / 收尾标记 /
#: R9 致命异常横幅）。只用于判断"严格剥离后是不是把证据吞掉了"，不参与取值。
#: ★ R9 增补 `ZEPHYR FATAL ERROR`：它是判定输入之一，若被截断转义吞掉首字母
#:   （`\x1b[1;3` + 续写 `1m>>> ZEPHYR…` 的已知交错，见 strip_line），
#:   解析器会漏掉异常 ⇒ 又是假 PASS。加锚后这类行退回"只剥 SGR"，证据首字母保住。
_EVIDENCE_ANCHOR_RE = re.compile(
    r"(?:" + _NATIVE_ALT + r"):(?:CTRL|TGT):"
    r"|(PASS|FAIL|SKIP) - \S+ in [\d.]+ seconds"
    r"|INFO \S+:\s"
    r"|ZEPHYR FATAL ERROR"
    r"|PROJECT EXECUTION (SUCCESSFUL|FAILED)")


def strip_line(raw):
    """ANSI 剥离 + 去回车。

    ★ **截断转义不得吞掉紧随其后的证据首字母**（2026-09-20 修）。
    串口是**两方非原子共享**的：主核的 `printk`（本核 token + 从核转发行）
    与 deferred log 线程的 UART 后端各写各的（`CONFIG_LOG_MODE_DEFERRED=y` 且
    `CONFIG_LOG_PRINTK=n` ⇒ 两个独立写入方；`CONFIG_PRINTK_SYNC` 在单核 SoC 上
    默认 **n** ⇒ 连两次 printk 之间也无互斥）。实际交错形态见真归档：

        [00:00:00.005,000] \\x1b[1;3I3C_I2C_TEST:CTRL:PASS:pinctrl_states

    log 线程写到 `\\x1b[1;3` 被 printk 切开，printk 整行插进去后 log 线程续写
    `1m…`。此时 ANSI_RE 的 `[A-Za-z]` 收尾把 token 的 `I` 当成了转义末字节 ⇒
    严格剥离后得到 `3C_I2C_TEST:CTRL:PASS:…` ⇒ 该 PASS 项查不到 ⇒ §12.3 内存兜底
    补齐 ⇒ **判定不变，但 `src` 由 `token` 降级为 `mem`**（批量必须以 token 为准，
    等于证据被降级）。

    ⚠ 这不是单出口引入的：`_NATIVE_PREFIXES` 上方那条"不得加词边界"的注释
    （实测 `…:00.005,0I3C_I2C_TEST:…`）就是同一个交错在**单出口之前**留下的疤。

    修法（**消费者侧，唯一收口**）：先按现行 ANSI_RE 严格剥离；**仅当**"原始行里
    有证据锚、严格剥离后却没了"时，退回只剥 SGR（完整形态）—— 截断的那个转义
    原样留在行里（无害，取值只用 search 定位），token 首字母得以保住。
    该回退是**纯增益**：锚点在严格剥离后仍在 ⇒ 逐字返回旧结果（既有解析零变化）；
    原始行本来就没有锚 ⇒ 同样返回旧结果。

    为什么不改发送侧：受害行**可以是主核自己的 token**（真归档
    `reports/2026-09-20-11/…/serial_CTRL.log` 里被吞的是 `:CTRL:PASS:`，不是
    转发行）⇒ 只改 `rlog.c` 的转发路径盖不住它。控制台级互斥要动 `zephyr/**`
    （后端写入不加 printk 的锁），超出本仓边界；根因侧的正解是让两方共用一个
    写入者或去掉转义（`CONFIG_LOG_BACKEND_SHOW_COLOR=n`），已作为建议上报。
    """
    ln = ANSI_RE.sub("", raw).rstrip("\r")
    if _EVIDENCE_ANCHOR_RE.search(ln) or not _EVIDENCE_ANCHOR_RE.search(raw):
        return ln
    return _SGR_RE.sub("", raw).rstrip("\r")


def parse_kv(text):
    """INFO 值串 → `{键: 值}`。**分隔符同时容忍逗号与空白**。

    本库两种写法都有（`rtc_ls` / `trng` / `mbedtls` … 用逗号；`io_probe` /
    `spid` 用空格）::

        cross_links=54 verdict=CONNECTED          # 空格分隔
        scl_PJ10_PJ08=2 sda_PJ11_PJ09=2          # 空格分隔
        id=ab cd ef,cap_byte=0x12                 # 逗号分隔，但**值内含空格**

    ★ 逐段判定，不做无条件"空格切分"：先按 `,` 切段，只有当**某段第一个 `=` 之后
    的剩余部分里还出现 `word=`** 时才把该段按空白继续切成多个 kv。这样
    `pins=75 not_controlled=2` 的第二键不再被吞（旧实现按 `,` 切 ⇒ 整段塞进
    `pins` 的值里），而 `id=ab cd ef`（空格是**值的一部分**）也**不会**被误切。
    """
    out = {}
    for seg in text.split(","):
        if "=" not in seg:
            continue
        rest = seg.split("=", 1)[1]
        if re.search(r"(?:^|\s)[A-Za-z_][\w.]*\s*=", rest):
            # 段内还有空格分隔的 kv ⇒ 按空白切（`piece` 不含空白的才是 kv）
            for piece in seg.split():
                if "=" in piece:
                    k, _, v = piece.partition("=")
                    out[k.strip()] = v.strip()
        else:
            k, _, v = seg.partition("=")
            out[k.strip()] = v.strip()
    return out


# 兼容旧名（本模块内部 + 既有单元测试都按 `_parse_kv` 引用）
_parse_kv = parse_kv


def parse_ztest(lines, ctrl_banners=(), tgt_banners=()):
    """解析一段采集行，返回 dict：

    {"boot_seen": bool, "role": "CTRL"|"TGT"|None,
     "tests": {name: {"result": "PASS|FAIL|SKIP", "reason": str,
                      # ↓ 仅"执行期间有致命异常"的项才有（R9）
                      "token_result": "PASS|FAIL|SKIP"|None,
                      "fatal": [{"text":…, "line":…, "cause":…, "thread":…}]}},
     "infos": {name: {k: v}},
     "execution": "SUCCESSFUL"|"FAILED"|None,
     "exec_conflict": bool, "exec_seen": ["FAILED", "SUCCESSFUL"],
     "fatal_events": [{"line":…, "text":…, "cause":…, "thread":…,
                       "item":…, "owner":…}]}

    reason 归因：zassert 的 "Assertion failed at f:l (func): msg" 打印在
    FAIL 行之前，解析器暂存断言行，遇到 FAIL 行时归到该项（§13.5）。

    ★ 矛盾标记 fail-closed（R6，2026-09-19）：同一份采集里**相反标记并存**时，
    **不得按"最后一条"消解** —— `execution` 一律取 `FAILED`（fail-closed），
    并置 `exec_conflict=True` / `exec_seen` 供报告显式写出矛盾。理由：last-wins
    会让"先失败后成功"（如固件自打标记晚于 ztest 的收尾行）翻成假 PASS。
    **只有一条标记时** `execution` 逐字不变（常规路径零影响）。

    ★ R9（2026-09-29，**执行期间致命异常 fail-closed**）：ztest 的逐项结果**初值
    是 PASS**，只有显式 `ztest_test_fail()` 才置 FAIL。CPU 异常/内核 panic 把测试
    线程打断 ⇒ `ztest_test_fail()` 从未被调用 ⇒ 串口仍打印 `PASS - <name>`。
    真归档实例（`reports/2026-09-29-1/mbedtls_test_case_hw_all/serial_TGT.log`
    L104-134）4 项负例/鲁棒性用例（`aes_bad_params` / `ecdsa_tamper_verify` /
    `sha256_bad_params` / `sha384_bad_params`）全部如此 —— 专用于证明错误处理的
    用例"全绿"等于**没验证**。

    判据（**窗口归属**，不是"整份采集里有异常"）：项 X 的执行窗口 = `START - X`
    到 X 自己的结果行；窗口内出现 `ZEPHYR FATAL ERROR` ⇒ X 落 `result="FAIL"`、
    `reason` 走 `fatal_item_note()`（写明**是 CPU 异常、不是断言失败**），原 token
    判定保留在 `token_result`（不抹掉证据，与 R8 的 `mem(非证据:PASS)` 同款）。

    归属优先级：横幅附近 `Current thread: 0x… (NAME)` 的 NAME（**线程名是异常发生
    时刻的真相**，不受 deferred log 行序影响；仅当 NAME 确实是本份采集里的测试项时
    采信）> 窗口起点 `START - <name>`。窗口内无 START 的异常记为未归属
    （`owner=None`），**不**自动改判任何项（不扩大爆炸半径）。

    项在异常后**没打出结果行**时照样建条目（否则该家族按 token 表取项时会**静默
    少一项**，仍是假 PASS）。
    """
    boot_seen = False
    role = None
    tests = {}
    infos = {}
    execution = None
    exec_seen = set()
    pending_assert = None
    # R9：当前测试项窗口 + 已见过的测试项名（归属判定用）
    current = None
    known_items = set()
    fatal_events = []
    pending_cause = None

    for idx, raw in enumerate(lines, 1):
        ln = strip_line(raw)
        if role is None:
            for b in ctrl_banners:
                if b in ln:
                    role = "CTRL"
            if role is None:
                for b in tgt_banners:
                    if b in ln:
                        role = "TGT"
        if _BOOT_RE.search(ln):
            boot_seen = True
            continue
        # R9：当前测试项窗口的左边界（`START - <name>`）。真归档里本行不被任何
        # 其它模式认领，故就地记录后继续走下面的既有分支（不短路）。
        m = _START_RE.search(ln)
        if m:
            current = m.group(1)
            known_items.add(current)
            pending_cause = None          # 异常原因行不得跨项泄漏
        # R9：致命异常横幅 —— 归入当前窗口，并在紧随其后的行里取线程名（真正的
        # 归属依据）；`mcause:` 行在其**之前**出现，故先缓存、随横幅一并取证。
        m = _FATAL_RE.search(ln)
        if m:
            ev = {"line": idx, "text": m.group(0).strip(), "cause": pending_cause,
                  "thread": None, "item": current, "owner": None}
            pending_cause = None
            if isinstance(lines, (list, tuple)):
                for nxt in lines[idx:idx + 4]:
                    tm = _FATAL_THREAD_RE.search(strip_line(nxt))
                    if tm:
                        ev["thread"] = tm.group(1)
                        break
            fatal_events.append(ev)
            continue
        m = _FATAL_CAUSE_RE.search(ln)
        if m:
            pending_cause = m.group(0).strip()
            continue
        # R7：先试原生 token（`i3c_i2c_switch_test`）；不匹配再走 ztest 原生格式。
        m = _NATIVE_RESULT_RE.search(ln)
        if m:
            st, name = m.group(1), m.group(2)
            entry = tests.setdefault(name, {"result": st, "reason": ""})
            entry["result"] = st
            reason = (m.group(3) or "").strip()
            if st == "FAIL" and reason:
                entry["reason"] = reason
            pending_assert = None
            current = None                # R9：本项窗口的右边界
            pending_cause = None          # R9：异常原因行不得跨项泄漏
            known_items.add(name)
            continue
        m = _NATIVE_INFO_RE.search(ln)
        if m:
            infos.setdefault(m.group(1), {}).update(_parse_kv(m.group(2)))
            continue
        m = _RESULT_RE.search(ln)
        if m:
            st, name = m.group(1), m.group(2)
            entry = tests.setdefault(name, {"result": st, "reason": ""})
            entry["result"] = st
            if st == "FAIL" and pending_assert:
                entry["reason"] = pending_assert
            pending_assert = None
            current = None                # R9：本项窗口的右边界
            pending_cause = None          # R9：异常原因行不得跨项泄漏
            known_items.add(name)
            continue
        m = _ASSERT_RE.search(ln)
        if m:
            pending_assert = f"{m.group(1).strip()}: {m.group(2).strip()}"
            continue
        m = _INFO_RE.search(ln)
        if m:
            infos.setdefault(m.group(1), {}).update(_parse_kv(m.group(2)))
            continue
        m = _EXEC_RE.search(ln)
        if m:
            execution = m.group(1)
            exec_seen.add(m.group(1))
    # R9：致命异常归属 + fail-closed 改判（逐项理由串见 fatal_item_note）。
    for ev in fatal_events:
        owner = ev["thread"] if ev["thread"] in known_items else ev["item"]
        ev["owner"] = owner
        if owner is None:
            continue
        entry = tests.get(owner)
        if entry is None:                 # 该项被异常打断、从未打出结果行
            entry = tests[owner] = {"result": "FAIL", "reason": ""}
            entry["token_result"] = None
        elif "token_result" not in entry:
            # 原 token 判定**不抹掉**（与 R8 的 `mem(非证据:PASS)` 同款：
            # 取值留档，人可查；判定以本理由为准）。
            entry["token_result"] = entry.get("result")
        entry.setdefault("fatal", []).append(
            {"text": ev["text"], "line": ev["line"], "cause": ev["cause"],
             "thread": ev["thread"]})
        prev = entry.get("reason") or ""
        entry["result"] = "FAIL"
        entry["reason"] = fatal_item_note(
            ev["text"], ev["line"], ev["cause"] or "",
            entry["token_result"] or "无结果行") + (
                f"；原断言理由：{prev}" if prev else "")
    # R6：相反标记并存 ⇒ 矛盾证据不得按"最后一条"消解 ⇒ fail-closed 判 FAIL
    exec_conflict = len(exec_seen) > 1
    if exec_conflict:
        execution = "FAILED"
    return {"boot_seen": boot_seen, "role": role,
            "tests": tests, "infos": infos, "execution": execution,
            "exec_conflict": exec_conflict, "exec_seen": sorted(exec_seen),
            "fatal_events": fatal_events}
