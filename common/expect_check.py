#!/usr/bin/env python3
"""G-C：**独立预期真源**与检查器（2026-09-20）。

## 这个文件解决什么

改造前的验证链只有一条：**「改动前后逐字节相同」**。它只证"输出没变"，
**不证"输出仍对"** —— 新旧代码若犯同一个错，比对必然通过。

本模块补上"仍然对"的**独立证据来源**：对每个测试项，从 `tests/COVERAGE.md`
的 `## 独立预期（机器可读）` 段读出一条**不由被测代码算出**的断言，
拿归档报告里的**实测 INFO 值**去校验，产出

    AGREE / DISAGREE / NO_EXPECTATION (+ NO_ACTUAL / PSEUDO_*)

## 预期文件格式（写在 `projects/<proj>/tests/COVERAGE.md` 末尾）

    ## 独立预期（机器可读）

    ```expect
    # <测试项> | <断言>[; <断言>...] | <来源标签>[:说明]
    set_get_roundtrip   | delta_s in [0,5]          | handcalc:1Hz tick 最大丢秒窗口
    calibration_negative| calib_reg == 0x36007FFE; ctrl.b1 == 1 | handcalc:rtc_ls.c 公式手工代入
    slave_addr_scan     | !none                     | !none:观测项，无判别力（原文自述）
    ```

* 一行一项；`#` 起首为注释；空行忽略；``` ``` ``` 围栏之外的任何行不解析。
* **断言** = `key op value`；`op` ∈ {`==` `!=` `<` `<=` `>` `>=` `in` `~` `absent`}。
  - `in [lo,hi]` 闭区间；`~` 正则（值整体匹配）；`absent` = 该 key 不得出现/不得有值。
  - `key` 是归档 INFO 里的键（如 `chi_square` / `calib_reg` / `ret`）。
    核限定写法 `core::key`（双核工程，如 `CTRL::ret`）暂未启用——见 §已知缺口。
* **来源标签**（决定独立性判定的后备依据，但**不是**唯一依据）：
  - 独立·强：`handcalc`(手算) `spec`(外部规范/数据手册/DT 属性/寄存器位定义)
    `golden`(黄金向量) `crosscheck`(跨来源交叉)
  - 独立·弱：`contract`(API 契约值，判别力真实但弱)
    `driversrc`(**转录自被测驱动实现**——只证"没变"不证"仍对"，
    既有缺陷会被一起抄进预期)
  - **非独立**：`samecode`（用被测代码自己的公式反解/回读复算）
  - `!none`：本项**无确定预期**（显式标注的覆盖缺口，不是"已验证无需预期"）
* 名字与断言里**不得出现裸 `|`**（会串列）；需要时用 `~` 正则转义。
* 双核工程用**不带核前缀**的项名（`test_x`），检查器会自动剥掉报告里的
  `cpu2::` / `CTRL::` 前缀再匹配；同一条预期因此自动作用于两核。

## 三个判定（+ 两种"有预期但没数据"的缺口）

| 判定 | 含义 |
|------|------|
| `AGREE` | 有独立预期，且实测值满足它 ⇒ **这批 PASS 有独立支撑** |
| `DISAGREE` | 有独立预期，实测值**违反**它 ⇒ ★ 高价值发现 |
| `NO_EXPECTATION` | 该项**没有独立预期**（含显式 `!none`）⇒ 覆盖缺口之一 |
| `NO_ACTUAL` | 有预期，但归档里**取不到断言用到的实测键** ⇒ 缺口之二（缺 INFO 通道/键名不符） |
| `PSEUDO_*` | 有"预期"但**永远不可能 DISAGREE** ⇒ 伪独立，**不计入覆盖** |

两条**方向性**标记（比判定本身更该看）：

| 标记 | 形态 | 含义 |
|------|------|------|
| ★ **假 PASS 风险** | `DISAGREE` + 固件判 `PASS` | 实测违反外部真源却判过 ⇒ 这个 PASS 不可信，**必须停下报告** |
| ○ 独立佐证 | `DISAGREE` + 固件判 `FAIL` | 与固件同向 ⇒ 对**已知缺陷**的独立佐证，不是新发现 |
| （信息）| `AGREE` + 固件判 `FAIL` | 断言只覆盖了本项**一部分**判据（INFO 常只报前缀阶段），不构成矛盾 |

`NO_ACTUAL` 的两条判据：归档里**一个实测键都没有**；或**断言用到的键一个都对不上**
（旧归档缺新固件的键 / 键名写错）。后者单独列键名 —— 因为**"没数据" ≠ "违规"**，
把它算成 DISAGREE 会制造假发现。**部分**键缺失时：判定只用能取到的键，
缺失的键在 `note` 里逐条列出（不让 AGREE 显得比实际强）。

## 伪独立预期的识别（§3.2 关键防线）

三道闸，任一中标即判 `PSEUDO_*`，**不计入"有独立预期"的分子**：

1. **标注闸**：来源标签是 `samecode`（自己算自己）。
2. **关键词闸**：来源说明或 COVERAGE 的「预期来源」列里出现
   `公式反推` / `复算` / `同源` / `回读驱动` / `驱动自身公式` 等黑名单短语。
3. **判别力闸（探针法，会真跑）**：拿一组"错值探针"（0/±1/边界/极大/极小/字符串噪声）
   灌进断言谓词，**若没有任何一个探针被拒**，说明该断言恒真 ⇒ `PSEUDO_TAUTOLOGY`。
   这一闸抓的是 `range:lo=0,hi=2^64` 这类"区间宽到永不为假"和正则 `.*`。

独立性判据（题面给定）：**预期值必须不由被测代码算出**。例：`calibration_negative`
的 `0x36007FFE` 是把手算值代进 `(54<<24)|32766` 得来的 ⇒ 独立；
而"用驱动自己的公式反解"⇒ 不独立（闸 1/2）。

## 用法

    python common/expect_check.py --case trng_test:case_default
    python common/expect_check.py --case rtc_ls_test:case_default --session reports/2026-09-20-4
    python common/expect_check.py --audit              # 全库覆盖率审计
    python common/expect_check.py --audit --json

纯离线：只读 `projects/*/tests/COVERAGE.md` 与 `reports/` 归档，不碰硬件、
不起子进程、不写任何文件（除非 `--out`）。
"""
from __future__ import annotations

import argparse
import json
import re
import sys
from dataclasses import dataclass, field
from pathlib import Path

__all__ = [
    "AGREE", "DISAGREE", "NO_EXPECTATION", "NO_ACTUAL", "PSEUDO",
    "INDEPENDENT", "PSEUDO_TAUTOLOGY", "PSEUDO_SAME_SOURCE", "UNVERIFIED",
    "ExpectEntry", "Assertion", "ItemVerdict", "resolve_actual",
    "parse_expect_block", "extract_actuals", "classify", "check_case",
    "audit", "coverage_rows_of", "discriminating_power",
]

# ----------------------------------------------------------------- 词表
AGREE = "AGREE"
DISAGREE = "DISAGREE"
NO_EXPECTATION = "NO_EXPECTATION"
NO_ACTUAL = "NO_ACTUAL"

INDEPENDENT = "INDEPENDENT"
PSEUDO_TAUTOLOGY = "PSEUDO_TAUTOLOGY"          # 恒真：探针全通过
PSEUDO_SAME_SOURCE = "PSEUDO_SAME_SOURCE"      # 同源复算：自己算自己
UNVERIFIED = "UNVERIFIED"                      # 标签/关键词都没判出来

#: 伪独立：有"预期"但不可能 DISAGREE ⇒ 不算独立证据
PSEUDO = (PSEUDO_TAUTOLOGY, PSEUDO_SAME_SOURCE)
PSEUDO_LABEL = "PSEUDO"

STRONG = "strong"
WEAK = "weak"

#: 来源标签 → (独立性, 强度)
PROV_TAGS = {
    "handcalc":  (INDEPENDENT, STRONG),
    "spec":      (INDEPENDENT, STRONG),
    "golden":    (INDEPENDENT, STRONG),
    "crosscheck": (INDEPENDENT, STRONG),
    "contract":  (INDEPENDENT, WEAK),   # 契约值不由被测代码算，但判别力弱
    "driversrc": (INDEPENDENT, WEAK),   # 见下方 DRIVERSRC_NOTE
    "samecode":  (PSEUDO_SAME_SOURCE, WEAK),
}

#: 「驱动源码转录」类预期的**局限**说明（报表里原样带上，别让读者高估它）。
#:
#: `预期来源` 列写着"驱动源码"的行，其预期值是从**被测驱动自己的实现**里读出来的
#: （例：`sys_write32(0x11f, CR1)` ⇒ 断言 CR1==0x11f）。这类断言**有判别力**
#: （驱动写值一变就 DISAGREE），但它**只能发现"变了"，不能发现"一直错着"** ——
#: 若写预期时驱动已经把 CR1 算错，断言就是把那个错值抄了一遍。故计入"有独立预期"
#: 但一律记为 **weak**，并在报告里单列计数，避免与 `handcalc`/`spec`/`golden` 混为一谈。
DRIVERSRC_NOTE = ("预期值转录自被测驱动实现：只证『没变』，不证『仍对』"
                  "（既有缺陷会被一起抄进预期）")

#: 关键词闸：出现任一即判同源复算（**非**独立证据）
_SAME_SOURCE_WORDS = (
    "公式反推", "公式复算", "同源", "同代码", "回读驱动", "驱动自身公式",
    "用驱动自己", "同一公式", "自身公式", "samecode", "same-code",
)

#: 判别力闸的探针（"错值"候选：正常断言应当拒掉其中绝大多数）
_NUM_PROBES = [
    0, 1, -1, 2, 3, 7, 8, 15, 16, 255, 256, 4095, 4096, 65535, 0xFFFF,
    0x10000, 0x7FFFFFFF, -0x80000000, 0xFFFFFFFF, 2 ** 63 - 1, -2 ** 63,
    100, 1000, 10 ** 6, 10 ** 9, -10 ** 9, 10 ** 18, -10 ** 18,
    0.5, -0.5, 1.5, 2.5, 99.9,
]
_STR_PROBES = ["", "-", "0", "1", "x", "PASS", "FAIL", "0xA5", "up", "down", "N/A"]

_EXPECT_FENCE = "```expect"


# =========================================================== 断言与条目模型
@dataclass
class Assertion:
    """一条 `key op value` 断言。"""
    key: str
    op: str
    value: object
    raw: str
    #: 关系型断言的**右端是另一个 INFO 键**时记在这里（如 `max_after <= first_after`）。
    #: 跨键不变量（堆不单调上涨、两轮时间单调…）是常见且强力的独立预期，
    #: 但把它硬编码成字面量就等于把某一次的实测值抄进预期 —— 那才是真的编预期。
    rhs_key: str = None

    def test(self, actual, lookup=None):
        """对一个实测值求值；`actual` 为 None（键缺失）时的语义见各 op。"""
        op = self.op
        if op == "absent":
            return actual is None or str(actual).strip() == ""
        if actual is None:
            return False
        if op == "~":
            return re.search(self.value, str(actual)) is not None
        if self.rhs_key is not None and lookup is not None:
            other = lookup(self.rhs_key)
            if other is None:
                return False
            eq = _loose_eq(actual, other)
            if op in ("==", "!="):
                return eq if op == "==" else not eq
            a, b = _as_num(actual), _as_num(other)
            if a is None or b is None:
                return _cmp_str(str(actual), str(other), op)
            return {"<": a < b, "<=": a <= b, ">": a > b, ">=": a >= b}[op]
        if op in ("==", "!="):
            eq = _loose_eq(actual, self.value)
            return eq if op == "==" else not eq
        if op == "in":
            # ★ 区间断言必须先于 `_as_num(self.value)` 处理：self.value 是 tuple，
            #   `_as_num` 对它恒返回 None，会掉进"非数值"分支被判成**不满足** ——
            #   于是「区间宽到恒真」这类断言反而次次被拒，探针闸（§3.2 恒真检测）
            #   完全失效。见 §验证「区间恒真探针」。
            a = _as_num(actual)
            if a is None:
                return False
            lo, hi = self.value
            return _as_num(lo) <= a <= _as_num(hi)
        a = _as_num(actual)
        b = _as_num(self.value)
        if a is None or b is None:
            # 非数值：`<`/`>` 退化到字符串比较
            return _cmp_str(str(actual), str(self.value), op)
        if op == "in":
            lo, hi = self.value
            return lo <= a <= hi
        return {"<": a < b, "<=": a <= b, ">": a > b, ">=": a >= b}[op]


@dataclass
class ExpectEntry:
    """`## 独立预期` 段里的一行。"""
    name: str
    assertions: list = field(default_factory=list)
    tag: str = ""
    note: str = ""
    line_no: int = 0
    raw: str = ""

    @property
    def is_none(self):
        return self.tag == "!none" or not self.assertions


@dataclass
class ItemVerdict:
    name: str
    verdict: str
    independence: str = UNVERIFIED
    strength: str = WEAK
    actual: dict = field(default_factory=dict)
    failed: list = field(default_factory=list)
    note: str = ""
    #: 归档里该项的**判定结果**（PASS/FAIL/SKIP/…）。整个练习问的就是
    #: "这个 PASS 对不对"，所以判定结果必须和独立预期摆在一起看。
    result: str = ""

    @property
    def conflict(self):
        """**假 PASS 风险**：实测值违反外部真源，固件却判 PASS ⇒ 这个 PASS 不可信。

        这是唯一需要"停下报告"的方向。
        """
        return self.verdict == DISAGREE and self.result == "PASS"

    @property
    def corroborate(self):
        """独立预期与固件**同向**判失败 ⇒ 对已知缺陷的独立佐证（不是新发现）。"""
        return self.verdict == DISAGREE and self.result == "FAIL"

    def as_dict(self):
        return {"name": self.name, "verdict": self.verdict,
                "independence": self.independence, "strength": self.strength,
                "result": self.result, "conflict": self.conflict,
                "corroborate": self.corroborate,
                "actual": self.actual, "failed": self.failed, "note": self.note}


# --------------------------------------------------------------- 值比较工具
def _as_num(v):
    if isinstance(v, bool):
        return None
    if isinstance(v, (int, float)):
        return float(v)
    s = str(v).strip()
    if not s:
        return None
    try:
        if re.fullmatch(r"[-+]?0[xX][0-9a-fA-F]+", s):
            return float(int(s, 16))
        if re.fullmatch(r"[-+]?0[bB][01]+", s):
            return float(int(s, 2))
        return float(s)
    except ValueError:
        return None


def _loose_eq(actual, expected):
    """数值优先、退化为字符串的相等（容忍 `0x0f` vs `15` vs `0x0F`）。"""
    a, b = _as_num(actual), _as_num(expected)
    if a is not None and b is not None:
        return a == b
    return str(actual).strip() == str(expected).strip()


def _cmp_str(a, b, op):
    return {"<": a < b, "<=": a <= b, ">": a > b, ">=": a >= b}[op]


# =============================================================== 格式解析
def _parse_value(tok):
    tok = tok.strip()
    m = re.fullmatch(r"\[\s*(.+?)\s*,\s*(.+?)\s*\]", tok)
    if m:
        return (_num_or_str(m.group(1)), _num_or_str(m.group(2)))
    if len(tok) >= 2 and tok[0] == tok[-1] and tok[0] in "\"'":
        return tok[1:-1]
    return _num_or_str(tok)


def _num_or_str(tok):
    n = _as_num(tok)
    if n is not None and re.fullmatch(r"[-+]?(0[xXbB])?[0-9a-fA-F.]+", tok.strip()):
        return int(n) if float(n).is_integer() else n
    return tok.strip()


_ASSERT_RE = re.compile(r"^\s*([A-Za-z_][\w.]*)\s*(==|!=|<=|>=|<|>|~)\s*(.+?)\s*$")
_IN_RE = re.compile(r"^\s*([A-Za-z_][\w.]*)\s+in\s+(\[.+?\])\s*$", re.I)
_ABSENT_RE = re.compile(r"^\s*([A-Za-z_][\w.]*)\s+absent\s*$", re.I)


def parse_assertion(text):
    """`key op value` → Assertion；解析失败返回 None。"""
    s = text.strip()
    if not s:
        return None
    m = _ABSENT_RE.match(s)
    if m:
        return Assertion(m.group(1), "absent", None, s)
    m = _IN_RE.match(s)
    if m:
        val = _parse_value(m.group(2))
        if isinstance(val, tuple):
            return Assertion(m.group(1), "in", val, s)
        return None
    m = _ASSERT_RE.match(s)
    if m:
        key, op, rhs = m.group(1), m.group(2), m.group(3).strip()
        if rhs.startswith(":"):          # 容错 `!=:0xA5` 写法
            rhs = rhs[1:].strip()
        if op == "~":
            return Assertion(key, "~", rhs.strip("\"'"), s)
        if (op in ("<", "<=", ">", ">=") and _as_num(rhs) is None
                and re.fullmatch(r"[A-Za-z_][\w.]*", rhs)):
            # 关系断言的右端是**另一个 INFO 键** ⇒ 跨键不变量（如堆不单调上涨）
            return Assertion(key, op, rhs, s, rhs_key=rhs)
        return Assertion(key, op, _parse_value(rhs), s)
    return None


def parse_expect_block(cov_path):
    """解析 `## 独立预期（机器可读）` 的 ```expect 围栏 → `{name: ExpectEntry}`。

    只认围栏内的行；围栏外的任何内容（含既有覆盖矩阵）一律不碰。
    """
    path = Path(cov_path)
    try:
        text = path.read_text(encoding="utf-8")
    except OSError:
        return {}
    out = {}
    inside = False
    for i, line in enumerate(text.splitlines(), 1):
        s = line.strip()
        if not inside:
            if s.startswith(_EXPECT_FENCE):
                inside = True
            continue
        if s.startswith("```"):
            break
        if not s or s.startswith("#"):
            continue
        parts = [p.strip() for p in s.split("|")]
        if len(parts) < 2:
            continue
        name, body = parts[0], parts[1]
        tag_note = parts[2] if len(parts) > 2 else ""
        tag, _, note = tag_note.partition(":")
        entry = ExpectEntry(name=name, tag=tag.strip(), note=note.strip(),
                            line_no=i, raw=s)
        if entry.tag != "!none":
            for chunk in body.split(";"):
                a = parse_assertion(chunk)
                if a is not None:
                    entry.assertions.append(a)
        out[name] = entry
    return out


# =========================================================== 独立性判定
def _probes_for(assertion):
    """给断言生成"错值探针"。

    ★ 区间断言**不加** `lo-1` / `hi+1`：那等于用断言自身构造出一个必然被拒的值，
      任何有界区间都会因此显得"有判别力"，恒真检测（§3.2 闸 3）当场失效。
      探针只用**与断言无关**的固定错值集，外加区间中点（保证至少有一个探针落在
      区间内 —— 否则像 `in [1e19,1e20]` 这种落在探针跨度之外的区间会被误判恒真）。
    """
    if assertion.op == "~" or isinstance(assertion.value, str):
        probes = list(_STR_PROBES)
        # `!= x` 必须能被 x 本身否掉，否则必是恒真
        if assertion.op == "!=" and isinstance(assertion.value, str):
            probes.append(assertion.value)
        return probes
    probes = list(_NUM_PROBES)
    if isinstance(assertion.value, tuple):
        lo, hi = (_as_num(assertion.value[0]), _as_num(assertion.value[1]))
        if lo is not None and hi is not None:
            # 符号域收窄：`lo >= 0` 的区间（无符号量）里，"负数探针被拒"不构成判别力
            # —— 无符号字段本来就取不到负数。不做这一步，`in [0, 2^64-1]` 会被
            # 负探针"救活"，恒真检测形同虚设。
            # ★ 已知局限：`x >= 0` 这类单向比较不做同款收窄（无法从断言本身判断
            #   量是否有符号）⇒ 该形态的恒真可能漏报，属保守方向（宁漏勿误报）。
            if lo >= 0:
                probes = [p for p in probes if _as_num(p) is not None and _as_num(p) >= 0]
            elif hi <= 0:
                probes = [p for p in probes if _as_num(p) is not None and _as_num(p) <= 0]
            probes += [lo, hi, (lo + hi) / 2.0]
    elif assertion.op == "!=":
        probes.append(assertion.value)
    return probes


def discriminating_power(assertion):
    """判别力闸：返回被拒的探针个数。0 ⇒ 恒真 ⇒ 伪独立。

    跨键断言（`key op rhs_key`）没有字面量可喂，故用 `lookup=lambda k: 0` 把右端
    当 0 来探：`<`/`<=` 对正探针必拒、`>`/`>=` 对负探针必拒 ⇒ 正常都能拒掉一批；
    真被拒 0 个说明这条关系式对任何左端值都成立（例如 `x >= -1e18`），属恒真。
    """
    rejected = 0
    lookup = (lambda k: 0) if getattr(assertion, "rhs_key", None) else None
    for p in _probes_for(assertion):
        try:
            if not assertion.test(p, lookup):
                rejected += 1
        except Exception:                       # noqa: BLE001
            rejected += 1
    return rejected


def classify(entry, coverage_source=""):
    """给定条目 → `(independence, strength, note)`。

    `coverage_source`：COVERAGE 覆盖矩阵里该项的「预期来源」列原文（关键词闸要用）。
    """
    if entry is None or entry.is_none:
        return UNVERIFIED, WEAK, "无独立预期（显式标注）"
    tag = entry.tag.strip().lower()
    base_indep, base_strength = PROV_TAGS.get(tag, (UNVERIFIED, WEAK))

    # 闸 1：标注
    if base_indep == PSEUDO_SAME_SOURCE:
        return PSEUDO_SAME_SOURCE, WEAK, f"来源标签 `{tag}` = 同源复算（自己算自己）"

    # 闸 2：关键词
    hay = (entry.note or "") + " " + (coverage_source or "")
    for w in _SAME_SOURCE_WORDS:
        if w in hay:
            return PSEUDO_SAME_SOURCE, WEAK, f"来源含同源短语 `{w}`"

    # 闸 3：判别力（真跑探针）
    weakest = None
    for a in entry.assertions:
        n = discriminating_power(a)
        if n == 0:
            weakest = a
            break
    if weakest is not None:
        return (PSEUDO_TAUTOLOGY, WEAK,
                f"断言 `{weakest.raw}` 恒真：{len(_probes_for(weakest))} 个错值探针全部通过")

    if tag == "driversrc":
        return base_indep, base_strength, DRIVERSRC_NOTE
    return base_indep, base_strength, ""


# =============================================================== 实测值提取
def expand_kv(text):
    """`a=1,b=2` / `pins=75 not_controlled=2` → `{'a':'1','b':'2'}`。

    分隔符**同时**容忍逗号与空白：本库两种写法都有（`rtc_ls` 用逗号、
    `io_probe` / `spid` 用空格）。规则**收敛到 `ztest_report.parse_kv`** ——
    判定/渲染路径（L2）用的是同一个函数，此处若自留一套就必然与它分叉。

    与"先 split 逗号再 partition"的差别：`scl_PJ10_PJ08=2 sda_PJ11_PJ09=2`
    这种空格式不会被整体塞进一个键（第二个键不丢）；而 `id=ab cd ef` 这种
    **空格属值的一部分**的逗号式，值也不会被截断。

    整串没有 `=`（如 `fire1`）时记到 `value` 键 —— 那是单值型 INFO 的通用形态。
    """
    if text is None:
        return {}
    s = str(text).strip()
    if not s or s == "-":
        return {}
    import ztest_report
    out = ztest_report.parse_kv(s)
    if not out:
        out["value"] = s
    return out


def _merge_actual(target, name, pairs):
    for k, v in pairs.items():
        target.setdefault(name, {})[k] = v


def _walk_json(node, out):
    """把 json 归档里**形状各异**的 INFO 通道归一成 `{name: {k: v}}`。

    覆盖到的三种既有形状（不改任何生产者）：

      1. 单 case 平表：`{infos:{name:{k:v}|{value:"k=v,..."}}}`
         （rtc_ls / trng / spi / spif / spid / i2cf / io_probe / otbn / mbedtls / iwdg）
      2. 聚表：`{cases:[{tests:[{name, info: "k=v,..."}]}]}`（i3c_dualcore）
      3. `{items:[{name, detail: "k=v,..."|自由文本, info: "..."}]}`（mbox / eth）
    """
    if not isinstance(node, dict):
        return
    infos = node.get("infos")
    if isinstance(infos, dict):
        for name, v in infos.items():
            if isinstance(v, dict):
                if set(v.keys()) == {"value"}:
                    _merge_actual(out, name, expand_kv(v["value"]))
                else:
                    _merge_actual(out, name, {str(k): str(x) for k, x in v.items()})
            else:
                _merge_actual(out, name, expand_kv(v))
    for key in ("cases",):
        for c in node.get(key) or []:
            _walk_json(c, out)
    for key in ("tests", "items"):
        for it in node.get(key) or []:
            if not isinstance(it, dict):
                continue
            name = it.get("name")
            if not name:
                continue
            for fld in ("info", "detail"):
                raw = it.get(fld)
                if isinstance(raw, str) and "=" in raw:
                    _merge_actual(out, name, expand_kv(raw))


def extract_actuals_from_json(path):
    try:
        d = json.loads(Path(path).read_text(encoding="utf-8"))
    except (OSError, ValueError):
        return {}
    out = {}
    _walk_json(d, out)
    return out


_MD_INFO_HEADERS = ("实测(INFO)", "实际(INFO)", "实测（INFO）")


def extract_actuals_from_md(path):
    """从报告 .md 的 `实测(INFO)` 列取实测值（兼容旧表头 `实际(INFO)`）。"""
    try:
        text = Path(path).read_text(encoding="utf-8")
    except OSError:
        return {}
    out = {}
    idx = None
    for line in text.splitlines():
        if not line.lstrip().startswith("|"):
            idx = None
            continue
        cells = _parse_row(line)
        if not cells:
            continue
        if idx is None:
            for i, c in enumerate(cells):
                if any(h in c for h in _MD_INFO_HEADERS):
                    idx = i
                    break
            continue
        if _is_sep(cells):
            continue
        if len(cells) <= idx:
            continue
        name = cells[0].strip().strip("`").strip()
        if not re.match(r"^[A-Za-z_][\w:.]*$", name):
            continue
        _merge_actual(out, name, expand_kv(cells[idx]))
    return out


def _strip_md(c):
    return c.replace("\\|", "|").strip()


try:                                    # 复用 judge.py 的规范表格解析（转义竖线不串列）
    sys.path.insert(0, str(Path(__file__).resolve().parent))
    from judge import parse_md_table_row as _parse_row      # noqa: E402
except Exception:                       # noqa: BLE001
    def _parse_row(line):               # 退路：朴素切分
        s = line.strip()
        if not s.startswith("|"):
            return None
        return [c.strip() for c in s.strip("|").split("|")]


def _is_sep(cells):
    return bool(cells) and all(re.fullmatch(r":?-{2,}:?", c) for c in cells)


def _resolve_cols(header):
    """按**列名**定位关键列（不按固定下标）——各工程 COVERAGE 表头列序不统一。

    例：`io_probe_test` 的表是 `# | 测试项 | 触达代码路径 | 维度 | 预期数据 | ...`
    （比模板多一个 `#` 列），固定下标会整体错位。
    """
    def find(*names):
        for i, c in enumerate(header):
            for n in names:
                if n in c:
                    return i
        return None
    return find("测试项", "用例名"), find("预期数据", "期望数据"), find("预期来源")


def _reported_results(case_dir):
    """归档里 `{测试项: 判定结果}`（PASS/FAIL/SKIP/NOT_RUN…）。

    与 `_reported_items` 用同一组来源，只是取 `result` 而不是 `name`。
    """
    case_dir = Path(case_dir)
    for j in sorted(case_dir.glob("*_report.json")):
        try:
            d = json.loads(j.read_text(encoding="utf-8"))
        except (OSError, ValueError):
            continue
        out = {}
        for name, res in (d.get("results") or {}).items():
            out[name] = str(res)
        for key in ("cases",):
            for c in d.get(key) or []:
                for t in c.get("tests") or []:
                    if t.get("name"):
                        out[t["name"]] = str(t.get("result") or "")
        for key in ("items", "tests"):
            for t in d.get(key) or []:
                if isinstance(t, dict) and t.get("name"):
                    out.setdefault(t["name"], str(t.get("result") or ""))
        if out:
            return out
    return {}


def coverage_rows_of(cov_path):
    """COVERAGE 覆盖矩阵 → `{测试项: 预期来源列原文}`。

    列下标**按表头名解析**（各工程列序不一致，见 `_resolve_cols`）；
    只认含 `预期数据` 列头的表，其余表整表忽略。
    """
    path = Path(cov_path)
    try:
        text = path.read_text(encoding="utf-8")
    except OSError:
        return {}
    out, cols = {}, None
    for line in text.splitlines():
        if not line.lstrip().startswith("|"):
            cols = None
            continue
        cells = _parse_row(line)
        if cells is None or not cells:
            continue
        if cols is None:
            if any("预期数据" in c for c in cells):
                cols = _resolve_cols(cells)
            continue
        if _is_sep(cells):
            continue
        n_i = cols[0] if cols[0] is not None else 0
        e_i = cols[1]
        s_i = cols[2]
        if e_i is None or len(cells) <= e_i or n_i >= len(cells):
            continue
        name = cells[n_i].strip().strip("`").strip()
        if not re.match(r"^[A-Za-z_][\w:.]*$", name):
            continue
        out[name] = cells[s_i] if (s_i is not None and s_i < len(cells)) else ""
    return out


_SERIAL_INFO_RE = re.compile(r"INFO ([^\s:]+):\s*(.*)")


def extract_actuals_from_serial(case_dir):
    """从 `serial_*.log`（L0 原始采集）补回 INFO 键。

    **为什么需要它**：归档 `.json` 的 `infos` 是各工程自己解析出来的，而多数解析器
    按**逗号**切 kv（`_(parse_kv)`），遇到**空格分隔**的 INFO（`io_probe` / `spid`：
    `scl_PJ10_PJ08=2 sda_PJ11_PJ09=2`）会把后一半整段塞进第一个键的值里 ⇒ 第二个键
    在报告里**永久丢失**。原始串口日志没这问题，故这里按 `INFO <名>: <kv>` 重扫一遍，
    用与 `expand_kv` 同一套（逗号/空白都认）的规则补键。

    只补**json/报告里没有的键**（`setdefault` 语义），不改既有值 ⇒ 不改变既有判定。
    """
    out = {}
    try:
        import ztest_report
        strip = ztest_report.strip_line
    except Exception:                       # noqa: BLE001
        strip = lambda s: s.rstrip("\r")    # noqa: E731
    for f in sorted(Path(case_dir).glob("serial_*.log")):
        try:
            raw = f.read_text(encoding="utf-8", errors="replace")
        except OSError:
            continue
        for line in raw.splitlines():
            m = _SERIAL_INFO_RE.search(strip(line))
            if m:
                _merge_actual(out, m.group(1), expand_kv(m.group(2)))
    return out


def extract_actuals(case_dir):
    """归档目录 → `{测试项: {键: 值}}`。

    三层合并（先到先得，json 权威）：
      1. `*_report.json` —— 机器可读、字段最规整；
      2. `*_report.md` 的 `实测(INFO)` 列 —— 渲染侧视图；
      3. `serial_*.log` —— **补回解析器丢掉的键**（见 from_serial 的说明）。
    """
    case_dir = Path(case_dir)
    out = {}

    def _mangled(v):
        """值里含空白或 `=` ⇒ 是"空格分隔被逗号解析"吞并后的残渣。"""
        return isinstance(v, str) and (" " in v.strip() or "=" in v.strip())

    def _merge_in(name, kv, overwrite):
        d = out.setdefault(name, {})
        for k, v in kv.items():
            if overwrite or k not in d:
                d[k] = v
            elif _mangled(d[k]) and not _mangled(v):
                # 同一键在 json 里是被吞并的残渣、在原始日志里是干净值 ⇒ 取干净的。
                # （例：`cross_links` 在 json 里是 "1 verdict=CONNECTED"，日志里是 "1"）
                d[k] = v

    for j in sorted(case_dir.glob("*_report.json")):
        for name, kv in extract_actuals_from_json(j).items():
            _merge_in(name, kv, True)
    for m in sorted(case_dir.glob("*_report.md")):
        for name, kv in extract_actuals_from_md(m).items():
            _merge_in(name, kv, False)
    for name, kv in extract_actuals_from_serial(case_dir).items():
        _merge_in(name, kv, False)
    return out


# =============================================================== 单 case 检查
def _reported_items(case_dir):
    """归档里出现过的测试项名（判定宇宙 = 实际跑过的项）。"""
    case_dir = Path(case_dir)
    for j in sorted(case_dir.glob("*_report.json")):
        try:
            d = json.loads(j.read_text(encoding="utf-8"))
        except (OSError, ValueError):
            continue
        names = list(d.get("results") or {})
        for key in ("cases",):
            for c in d.get(key) or []:
                for t in c.get("tests") or []:
                    if t.get("name"):
                        names.append(t["name"])
        for key in ("items", "tests"):
            for t in d.get(key) or []:
                if isinstance(t, dict) and t.get("name"):
                    names.append(t["name"])
        if names:
            # `run::timeout` 这类是**运行元信息**（`kind=NOTE`），不是测试项：
            # 留在列表里只会变成一条永久的 NO_EXPECTATION 噪音。
            names = [n for n in names if not str(n).startswith("run::")]
            seen, uniq = set(), []
            for n in names:
                if n not in seen:
                    seen.add(n)
                    uniq.append(n)
            return uniq
    return []


_CORE_PREFIX_RE = re.compile(r"^[A-Za-z0-9_]+::")


def norm_name(name):
    """归一名字：剥掉双核报告的核前缀 `cpu1::` / `CTRL::` / `TGT::`。

    双核工程的**报告**键名带核前缀（`cpu2::test_load_ticks_1000ms`），而
    **COVERAGE 覆盖矩阵**用不带前缀的项名。两边不归一就永远对不上，
    预期段会被误报成"未登记"。归一后同一条预期自动同时作用于两核
    （对双核而言"同一项在两侧应满足同一预期"正是想要的语义）。
    """
    return _CORE_PREFIX_RE.sub("", str(name))


def _entry_index(entries):
    """`{原名: entry}` + `{归一名: entry}` 的联合查找表（原名优先）。"""
    out = {}
    for k, v in entries.items():
        out.setdefault(norm_name(k), v)
    out.update(entries)
    return out


_BIT_KEY_RE = re.compile(r"^(.+)\.b(\d+)$")


def resolve_actual(actual, key):
    """按断言 key 取实测值；支持 `<寄存器>.b<N>` 取第 N 位。

    位取语法是为「黄金值断言要落在寄存器**位域**上」准备的（例：`ctrl.b1 == 1`
    对应 COVERAGE 里的 `CTRL.CALIB_EN(BIT1)=1`）。没有它就只能断言整个 CTRL
    字，而 CTRL 其余位是上一状态残留、**不是**确定预期（写下去就是编预期）。
    """
    if key in actual:
        return actual[key]
    m = _BIT_KEY_RE.match(key)
    if m:
        base = _as_num(actual.get(m.group(1)))
        if base is None:
            return None
        return int(int(base) >> int(m.group(2))) & 1
    return None


def find_case_dir(root, project, case, session=None):
    """在 `reports/<会话>/<工程>_<case>/` 里找该 case 的归档目录（默认取最新会话）。

    归档层级是 `reports/<date>-<idx>/<project>_<case>/` —— 会话目录是 case 目录的
    **父级**（不是祖父级）。
    """
    root = Path(root)
    pat = f"{project}_{case}"
    if session:
        d = Path(session) / pat
        return d if d.is_dir() else None
    cands = sorted((root / "reports").glob(f"*/{pat}"))
    return cands[-1] if cands else None


def check_case(project, case, case_dir, cov_path=None, root=None):
    """核心入口：对给定 case 的归档判定结果逐项校验。

    返回 `{"project","case","case_dir","items":[...],"stats":{...}}`。
    """
    root = Path(root or Path(__file__).resolve().parent.parent)
    cov_path = Path(cov_path) if cov_path else root / "projects" / project / "tests" / "COVERAGE.md"
    entries = parse_expect_block(cov_path)
    index = _entry_index(entries)
    cov_src = coverage_rows_of(cov_path)
    cov_src_norm = {norm_name(k): v for k, v in cov_src.items()}
    actuals = extract_actuals(case_dir) if case_dir else {}
    reported = _reported_items(case_dir) if case_dir else []
    results = _reported_results(case_dir) if case_dir else {}

    names, seen, seen_norm = [], set(), set()
    for n in reported:
        if n not in seen:
            seen.add(n)
            seen_norm.add(norm_name(n))
            names.append(n)
    # 预期段里**归一后已被报告项覆盖**的条目不再单列（否则双核工程会把
    # `cpu2::x` 与 `x` 列成两项，且不带前缀的那份永远判 NO_ACTUAL）。
    for n in entries:
        if n in seen or norm_name(n) in seen_norm:
            continue
        names.append(n)

    items = []
    for name in names:
        entry = index.get(name) or index.get(norm_name(name))
        actual = actuals.get(name)
        if not actual:
            # 报告键带核前缀、预期段不带（或反之）：按归一名再找一次
            for k, v in actuals.items():
                if norm_name(k) == norm_name(name):
                    actual = v
                    break
        actual = actual or {}
        src = cov_src.get(name, cov_src_norm.get(norm_name(name), ""))
        indep, strength, why = classify(entry, src)
        res = results.get(name) or results.get(norm_name(name)) or ""
        if not res:
            for k, r in results.items():
                if norm_name(k) == norm_name(name):
                    res = r
                    break
        v = ItemVerdict(name, NO_EXPECTATION, indep, strength, actual, result=res)
        if entry is None:
            v.note = "COVERAGE 无该项（或未登记独立预期）"
        elif entry.is_none:
            v.note = entry.note or "显式标注无独立预期"
            v.verdict = NO_EXPECTATION
        elif indep in PSEUDO:
            v.verdict = PSEUDO_LABEL
            v.note = why
        elif not actual:
            v.verdict = NO_ACTUAL
            v.note = "归档里无该项实测值（缺 INFO 通道）"
        else:
            failed, unresolved = [], []
            lookup = (lambda k: resolve_actual(actual, k))
            for a in entry.assertions:
                if a.op != "absent" and resolve_actual(actual, a.key) is None:
                    unresolved.append(a)
            if len(unresolved) == len(entry.assertions):
                # 断言键**一个都对不上**归档的 INFO ⇒ 这是"键名/固件版本对不上"
                # （典型：旧归档缺新固件才上报的键、或预期里键名写错），**不是**
                # 实测违规。判 DISAGREE 会把这一类误报成"发现"，故归 NO_ACTUAL。
                v.verdict = NO_ACTUAL
                v.note = ("归档里没有断言用到的任何实测键（缺 INFO 通道 / 键名不符）"
                          "；期望键：" + "、".join(a.key for a in entry.assertions))
            else:
                for a in entry.assertions:
                    if a.op != "absent" and resolve_actual(actual, a.key) is None:
                        continue          # 键缺失 = 没数据，不计入判定（见下 note）
                    val = resolve_actual(actual, a.key)
                    if not a.test(val, lookup):
                        rhs = (f" vs {a.rhs_key}={lookup(a.rhs_key)!r}"
                               if a.rhs_key else "")
                        failed.append(f"{a.raw}（实测 {a.key}={val!r}{rhs}）")
                v.verdict = DISAGREE if failed else AGREE
                v.failed = failed
                if unresolved:
                    # 「没数据」≠「违规」：键缺失的断言**不计入判定**（否则旧归档会被
                    # 误报成 DISAGREE），但必须显式写出来，别让 AGREE 看起来比实际强。
                    v.note = ("部分断言无对应实测键、**未计入判定**："
                              + "、".join(a.key for a in unresolved))
                if v.verdict == AGREE and res == "FAIL":
                    # 期望成立、固件却判 FAIL ⇒ 通常是**本项断言只覆盖了它的一部分
                    # 判据**（INFO 只报了前缀阶段），不是"矛盾"。如实标注，别当发现。
                    v.note = ((v.note + "；" if v.note else "")
                              + "独立预期只覆盖该项的部分判据：固件另判 FAIL"
                                "（见报告失败原因列），本检查器不能据此下结论")
        items.append(v)

    stats = {k: 0 for k in (AGREE, DISAGREE, NO_EXPECTATION, NO_ACTUAL, PSEUDO_LABEL)}
    strong_agree = 0
    conflict = corroborate = 0
    for v in items:
        stats[v.verdict] = stats.get(v.verdict, 0) + 1
        if v.verdict == AGREE and v.strength == STRONG:
            strong_agree += 1
        if v.conflict:
            conflict += 1
        if v.corroborate:
            corroborate += 1
    stats["strong_agree"] = strong_agree
    stats["conflict"] = conflict
    stats["corroborate"] = corroborate
    stats["total"] = len(items)
    return {"project": project, "case": case, "case_dir": str(case_dir) if case_dir else None,
            "cov_path": str(cov_path), "items": [v.as_dict() for v in items],
            "stats": stats}


# =============================================================== 全库审计
def audit(root=None):
    """全库覆盖率审计：每个工程多少项有/没有独立预期。"""
    root = Path(root or Path(__file__).resolve().parent.parent)
    rows = []
    tot = {"rows": 0, "modelled": 0, "none": 0, "missing": 0,
           "independent": 0, "pseudo": 0, "strong": 0, "with_actual": 0}
    for cov in sorted((root / "projects").glob("*/tests/COVERAGE.md")):
        project = cov.parent.parent.name
        matrix = coverage_rows_of(cov)
        entries = parse_expect_block(cov)
        index = _entry_index(entries)
        n_rows = len(matrix)
        covered, none_l, missing = [], [], []
        for n in matrix:
            e = index.get(n)
            if e is None:
                missing.append(n)
            elif e.is_none:
                none_l.append(n)
            else:
                covered.append(n)
        indep = pseudo = strong = 0
        for n in covered:
            i, s, _ = classify(index[n], matrix.get(n, ""))
            if i in PSEUDO:
                pseudo += 1
            else:
                indep += 1
                if s == STRONG:
                    strong += 1
        has_block = bool(entries)
        rows.append({"project": project, "rows": n_rows,
                     "covered": len(covered), "none_marked": len(none_l),
                     "unmarked": len(missing), "independent": indep,
                     "pseudo": pseudo, "strong": strong,
                     "weak": indep - strong, "has_expect_block": has_block})
        tot["rows"] += n_rows
        tot["modelled"] += len(covered)
        tot["none"] += len(none_l)
        tot["missing"] += len(missing)
        tot["independent"] += indep
        tot["pseudo"] += pseudo
        tot["strong"] += strong
    tot["weak"] = tot["independent"] - tot["strong"]
    return {"projects": rows, "totals": tot}


# =============================================================== CLI
def _print_case(rep):
    st = rep["stats"]
    print(f"case: {rep['project']}:{rep['case']}")
    print(f"  archive: {rep['case_dir']}")
    print(f"  coverage: {rep['cov_path']}")
    print()
    print("  %-38s %-9s %-15s %-11s %s" % ("测试项", "固件判定", "独立校验", "独立性", "实测(INFO)"))
    print("  " + "-" * 118)
    for it in rep["items"]:
        act = ",".join(f"{k}={v}" for k, v in it["actual"].items()) or "-"
        marks = []
        if it.get("conflict"):
            marks.append("★ 与固件判定方向矛盾")
        if it["failed"]:
            marks.append("; ".join(it["failed"]))
        if it["note"]:
            marks.append(it["note"])
        extra = ("  << " + " | ".join(marks)) if marks else ""
        print("  %-38s %-9s %-15s %-11s %s%s" % (
            it["name"], it.get("result") or "-", it["verdict"],
            it["independence"][:11], act[:56], extra[:118]))
    print()
    print("  汇总: AGREE=%d (strong=%d) / DISAGREE=%d / NO_EXPECTATION=%d / "
          "NO_ACTUAL=%d / PSEUDO=%d  (共 %d 项)" % (
              st[AGREE], st.get("strong_agree", 0), st[DISAGREE],
              st[NO_EXPECTATION], st[NO_ACTUAL], st[PSEUDO_LABEL], st["total"]))
    if st.get("conflict"):
        print("  ★ 有 %d 项 **假 PASS 风险**（独立预期违反、固件却判 PASS）"
              " —— 高价值发现，必须停下报告。" % st["conflict"])
    if st.get("corroborate"):
        print("  ○ 有 %d 项独立预期与固件**同向**判失败（已知缺陷的独立佐证，非新发现）。"
              % st["corroborate"])


def _print_audit(rep):
    t = rep["totals"]
    print("%-24s %6s %8s %8s %8s %12s %7s %7s" % (
        "工程", "行数", "已建模", "!none", "未标注", "有独立预期", "其中强", "其中弱"))
    print("-" * 90)
    for r in rep["projects"]:
        print("%-24s %6d %8d %8d %8d %12d %7d %7d%s" % (
            r["project"], r["rows"], r["covered"], r["none_marked"],
            r["unmarked"], r["independent"], r["strong"], r.get("weak", 0),
            "" if r["has_expect_block"] else "   [无 expect 段]"))
    print("-" * 90)
    print("%-24s %6d %8d %8d %8d %12d %7d %7d" % (
        "合计", t["rows"], t["modelled"], t["none"], t["missing"],
        t["independent"], t["strong"], t.get("weak", 0)))
    n = t["rows"] or 1
    print()
    print("独立预期覆盖率: %d/%d = %.1f%%（强 %d = %.1f%% / 弱 %d = %.1f%%）；"
          "显式 !none %d；未建模 %d；伪独立 %d"
          % (t["independent"], t["rows"], 100.0 * t["independent"] / n,
             t["strong"], 100.0 * t["strong"] / n,
             t.get("weak", 0), 100.0 * t.get("weak", 0) / n,
             t["none"], t["missing"], t["pseudo"]))
    print("说明：弱预期 = 标签 `contract`（契约值）或 `driversrc`（转录自被测驱动实现，"
          "只证『没变』不证『仍对』）；二者判别力真实但强度远低于 handcalc/spec/golden。")


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--root", default=None, help="810_driver_test 根目录")
    ap.add_argument("--case", default=None, help="<工程>:<case>")
    ap.add_argument("--session", default=None, help="指定归档会话目录 reports/<date>-<idx>")
    ap.add_argument("--case-dir", default=None, help="直接指定归档 case 目录")
    ap.add_argument("--audit", action="store_true", help="全库覆盖率审计")
    ap.add_argument("--json", action="store_true", help="输出 JSON")
    ap.add_argument("--out", default=None, help="把 JSON 结果写到该文件")
    args = ap.parse_args(argv)

    root = Path(args.root) if args.root else Path(__file__).resolve().parent.parent

    if args.audit:
        rep = audit(root)
        if args.json:
            print(json.dumps(rep, ensure_ascii=False, indent=2))
        else:
            _print_audit(rep)
        if args.out:
            Path(args.out).write_text(json.dumps(rep, ensure_ascii=False, indent=2),
                                      encoding="utf-8")
        return 0

    if not args.case:
        ap.error("需要 --case <工程>:<case> 或 --audit")
    project, _, case = args.case.partition(":")
    cdir = Path(args.case_dir) if args.case_dir else find_case_dir(
        root, project, case or "case_default", args.session)
    if cdir is None:
        print(f"找不到归档: {project}:{case}（用 --session / --case-dir 指定）",
              file=sys.stderr)
        return 2
    rep = check_case(project, case, cdir, root=root)
    if args.json:
        print(json.dumps(rep, ensure_ascii=False, indent=2))
    else:
        _print_case(rep)
    if args.out:
        Path(args.out).write_text(json.dumps(rep, ensure_ascii=False, indent=2),
                                  encoding="utf-8")
    return 1 if rep["stats"].get("conflict") else 0


if __name__ == "__main__":
    raise SystemExit(main())
