# `from B import f` 之后 patch `A.f` 不生效 —— 补丁要打在**定义模块**上

## 现象
给"看起来就是那个函数"的属性打猴补丁（`A.f = stub` / `A.allocate_session_dir = stub`），
运行结果却是**旧行为**：要么补丁像没执行，要么调用方仍走原实现。排查时容易误判成
"补丁没跑到 / 有缓存 / 有第二份实现"，在调用方模块里反复加打印，越查越远。

## 根因
`from B import f` 在 **import 时**把 `f` 绑定成 B 的**当前对象**，存进 A 自己的模块命名空间 ——
A 里那一格与 B 里那一格是**两个独立的名字槽**。函数体写在 B 里时，它按**自己模块的全局命名
空间**（`B.__dict__`）解析 `f` ⇒ 改 `A.f` 影响不到 B 内已有的引用。
**函数体内的 import 同理**：若调用方写的是
`def _run_all(): ... from report_dir import allocate_session_dir; ...`，该绑定在**调用时**才建立
⇒ 想改它的行为只能改 `report_dir.allocate_session_dir`。

## 判定
1. 分别问：这个名字**定义**在哪个模块？**调用点**从哪个模块取？（含"函数体内 import"这种形式）
2. 调用方是 `from X import f`（含函数内 import）⇒ patch 目标 = **X（定义模块）**，不是调用方模块。
3. 自检：patch 后**读回该属性确认是新对象**；若症状是"没生效"而非"报错"，几乎总是 patch 错了模块。
4. 静态筛查：AST 找 `from X import f` 且文件内存在对 `f` 的 `Call` ⇒ 那些 call 的 patch 目标必是 X。

## 处理
- 一律 patch **定义模块**的属性：`import report_dir; report_dir.allocate_session_dir = stub`
  —— **不是** `run_all.allocate_session_dir = stub`。
- 调用方用 `import X; X.f(...)` 的写法天然可 patch；`from X import f` 的写法对调用方**不透明可 patch**。
- 复用/测试类脚本要"接管"某个函数时，先确认它被 import 进来的形态，再决定 patch 哪个模块。

## 关联
- 810 实例（G-RUN-1）：复跑要绑定既有会话目录，只 patch `run_all.allocate_session_dir` **不生效**，
  实测另建了新会话目录（落到 `-3`）；须 patch `report_dir` 模块属性。见 `qsh-test-run` §4.2。
- 同属"后台/复跑"族的操作陷阱：`bash-bg-cd-subshell.md`。
