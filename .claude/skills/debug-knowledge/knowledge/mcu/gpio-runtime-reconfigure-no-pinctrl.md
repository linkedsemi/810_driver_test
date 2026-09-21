# 运行期反复改 GPIO 方向/上下拉不必过 pinctrl（但抢不回被复用占住的脚）

## 现象

需要把一个 GPIO 在运行期反复切换"输入+内部上拉" ↔ "推挽输出低"（扫描/邻接探测类用例）。
按常规思路会先设计"每根脚一个静态 pinctrl 状态"，一算要写上百个 pinctrl child node。

## 根因（其实不需要）

某些 SoC 的 GPIO 驱动 `gpio_pin_configure()` 是**两个独立 switch + 直接写寄存器**：
方向（`io_cfg_output/io_cfg_input`）与上下拉（`io_pull_write`）各走一条路径，
**全驱动没有任何 `pinctrl_apply_state()`** —— 脚的身份来自实例 reg 基址，与 pinctrl 无关。
`GPIO_INPUT|GPIO_PULL_UP`、`GPIO_INPUT|GPIO_PULL_DOWN`、`GPIO_OUTPUT_LOW` 都直接生效。

## 判定方法

- `grep -n pinctrl <gpio 驱动>.c`：若只命中 EXTI/IRQ 节点引用（而非 `pinctrl_apply_state`）
  ⇒ 该驱动的方向/上下拉不经过 pinctrl。
- 反向确认：驱动里是否**只改方向、不改 pinmux**；若如此，则下一条限制成立。

## 处理

- **不必造 pinctrl 状态**（避免 DT 爆炸），直接 `gpio_pin_configure()` 运行期切换。
- **必须记住的两条限制**：
  1. 该驱动**不会**把被外设复用功能占走的脚抢回来（只改方向不改 pinmux）⇒ 想让一根脚能被
     GPIO 探，前提是**没有外设 pinctrl 把脚占住**（扫描类工程做法：一个被测外设都不使能）。
  2. 端口使能是另一回事：SoC 里每个 GPIO 端口节点可能默认 `status = "disabled"`，
     **用哪个端口就得在 overlay 显式使能哪个端口**，否则 `device_is_ready(port)` 失败、
     设备静默 not ready。
- 另：binding 里声明了但驱动里**应用段被注释掉**的属性（如 drive-strength、`input-1v8-enable`）
  是"**声明着却静默失效**"——只记录，不当作可用能力。

## 关联

- KNOWN_ISSUES #81（GPIO 端口默认全 disabled）、#86（扫描类缺还原自检）
- qsh-test-gen §1.2（复用收敛段的反向提示）、§1.0（外设引脚落在哪个端口）
- 本库 mcu 域 `peripheral-capability-evidence.md`（"声明存在 ≠ 生效"同族）
