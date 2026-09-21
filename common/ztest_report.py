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

#: "这一行里有取证价值的东西"的定位式（原生 token / ztest 结果行 / INFO / 收尾标记）。
#: 只用于判断"严格剥离后是不是把证据吞掉了"，不参与取值。
_EVIDENCE_ANCHOR_RE = re.compile(
    r"(?:" + _NATIVE_ALT + r"):(?:CTRL|TGT):"
    r"|(PASS|FAIL|SKIP) - \S+ in [\d.]+ seconds"
    r"|INFO \S+:\s"
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
     "tests": {name: {"result": "PASS|FAIL|SKIP", "reason": str}},
     "infos": {name: {k: v}},
     "execution": "SUCCESSFUL"|"FAILED"|None,
     "exec_conflict": bool, "exec_seen": ["FAILED", "SUCCESSFUL"]}

    reason 归因：zassert 的 "Assertion failed at f:l (func): msg" 打印在
    FAIL 行之前，解析器暂存断言行，遇到 FAIL 行时归到该项（§13.5）。

    ★ 矛盾标记 fail-closed（R6，2026-09-19）：同一份采集里**相反标记并存**时，
    **不得按"最后一条"消解** —— `execution` 一律取 `FAILED`（fail-closed），
    并置 `exec_conflict=True` / `exec_seen` 供报告显式写出矛盾。理由：last-wins
    会让"先失败后成功"（如固件自打标记晚于 ztest 的收尾行）翻成假 PASS。
    **只有一条标记时** `execution` 逐字不变（常规路径零影响）。
    """
    boot_seen = False
    role = None
    tests = {}
    infos = {}
    execution = None
    exec_seen = set()
    pending_assert = None

    for raw in lines:
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
    # R6：相反标记并存 ⇒ 矛盾证据不得按"最后一条"消解 ⇒ fail-closed 判 FAIL
    exec_conflict = len(exec_seen) > 1
    if exec_conflict:
        execution = "FAILED"
    return {"boot_seen": boot_seen, "role": role,
            "tests": tests, "infos": infos, "execution": execution,
            "exec_conflict": exec_conflict, "exec_seen": sorted(exec_seen)}
