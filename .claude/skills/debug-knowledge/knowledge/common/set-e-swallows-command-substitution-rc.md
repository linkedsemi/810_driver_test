# `set -e` 下"命令替换 + 事后查 `$?`"是死写法 —— 赋值那一步就终止脚本

## 现象
脚本里写：

```bash
raw="$(timeout 8 find … 2>/dev/null)"
rc=$?
if [ "$rc" = "124" ]; then echo "[WARN] 已跳过"; continue; fi
```

预期：命令超时时走"跳过"分支。实际：超时时**整条脚本当场退出**（本例 rc=124），
`[WARN] 已跳过` 一行**永远打不出来**，`if [ "$rc" … ]` 分支**不可达**。
把该函数前后加 `set +e` / `set -e`（**唯一变量**）对照，行为翻转 ⇒ 根因是 `set -e`。
**典型误判**：交互式 smoke（未开 `set -e`）看到"超时分支正确跳过"，就以为真实脚本没问题。

## 根因
`set -e`（errexit）在**任何简单命令**返回非零时终止脚本。bash 对"**只由赋值构成、
末位是命令替换**"的简单命令，**取命令替换的退出码**作为整条赋值命令的退出码。
于是 `raw="$(timeout … cmd)"` 在 `cmd` 超时（`timeout` 返回 124）时，**这条赋值本身
就是一条失败命令** ⇒ `set -e` 在**赋值处**终止脚本，**后面的 `rc=$?` 根本轮不到执行**。
⇒ 在 `set -e` 脚本里，"命令替换 + 事后查 `$?`"整体是**死代码**。

## 判定
1. **静态扫**：任何 `var=$(…)`（尤其套 `timeout` / `find` / `grep` / `diff` 等**会非零退出**的
   命令）后面紧跟 `$?` 或 `rc=$?` 判断的地方，只要所在脚本开了 `set -e`，即命中。
2. **复现**：把该命令的**阈值调到一个必触发的极小值**（如把超时设成 `0.001s`）跑**真实入口脚本**，
   看它是"打印跳过"还是"整条退出 + 非零 rc"。
3. **反证（唯一变量）**：同函数前后加 `set +e` / `set -e`，行为翻转 ⇒ 确认是 `set -e`。
4. **验证纪律**：验证必须跑**真正开了 `set -e` 的那份脚本**；用交互式 shell / 未开 `set -e`
   的 smoke 验证会得到假"通过"。

## 处理
要捕获失败码，三选一（都保留 `errexit` 语义）：

```bash
# ① 用 if 吃掉退出码（推荐）
if ! raw="$(cmd)"; then rc=$?; …; fi
# ② `||` 显式接住
rc=0; raw="$(cmd)" || rc=$?
# ③ 只在赋值那几行临时关掉 errexit
set +e; raw="$(cmd)"; rc=$?; set -e
```

**别**写 `raw="$(cmd)"; rc=$?` —— 在 `set -e` 下 `rc=$?` 是死代码。

## 关联
- 810 实例（G-O24-1）：`scripts/test_common.sh::warn_if_build_stale()` 的外层
  `timeout ${STALE_SCAN_TIMEOUT_S}` 在 `set -e`（各 `test.sh` 顶部 / `test_common.sh:32`）下
  曾是死代码 —— 超时时整条 `test.sh` 退 rc=124 而非"跳过"（见 `KNOWN_ISSUES.md` #107 / `qsh-test-run` §4.2）。
- 方法学：`embedded-debug` §6（坑沉淀准则）；"看起来没问题其实没生效"同族见
  `python-from-import-patch-target.md`、`probe-scan-selfcheck-invariants.md`。
