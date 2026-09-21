# 配对控制器的引脚让渡（两个 owner 抢一组 net）

## 现象

把一个"配对控制器"挪作他用（当作普通控制器 / 上游 master）后，**总线毫无波形**，
但驱动不报错、编译也不失败。症状伪装成"接线错 / 从设备不在位 / 器件坏"。

## 根因

模块层为某个"功能单元"（滤波器、SPIF 等）指定了**配对控制器**，并因此把该控制器的引脚
**默认划归功能单元所有**：

- 在配对关系的 dtsi 里给该控制器打上 `pinctrl-noinit`（引脚不初始化，归滤波器等）；
- 或让配对控制器保留**板级 `pinctrl-0`**，而功能单元的 master 态使用**另一组引脚**（自己的
  `pinctrl-N`）。

一旦该控制器被挪作他用，这层"引脚归别人"的假设就不再成立。**不解除不会有任何报错**，
只是 SCL/SDA（或 SCK/CS）停在上电默认态、或两个 owner 同时驱动同一 net。

## 判定方法

- 查配对关系：`grep -n "<配对者>.i2c = <&i2cN>"` / `<配对者>.spi = <&spiN>`（DT 固定，可证）。
- 查该控制器是否带 `pinctrl-noinit`，或是否仍持有 `pinctrl-0`（与功能单元 master 态引脚重叠）。
- 编译器/文档都不会提示；判据是"**引脚表重叠 + 属性存在**"，最终以实测波形确认。

## 处理

在本工程自己的 overlay 里显式解除（**改完删 build 全量重建**）：

```dts
&i2cN { status = "okay"; /delete-property/ pinctrl-noinit; };   /* I2C 侧 */
&spiN { pinctrl-0 = <>; pinctrl-names = "default"; };           /* SPI 侧 */
```

**角色判定必须分级**（同一个坑的认知面）：

- **"配对控制器"由 DT 可证**——引脚重叠、`<配对者>.i2c = <&i2cN>`，是**仓库事实**；
- **"上游 master"只有接线能定**——DT 里没有任何属性描述它（外接跳线到 `<配对者>.SCLI/SDAI`）。
- 两个角色的引脚关系不同，**不可互相推断**，更不可用后者的假设去否定前者。

⇒ 上游 master 做成 **DT alias**，用例一律 `DEVICE_DT_GET(DT_ALIAS(...))`，接线变更只改 overlay 一行。

## 关联

- KNOWN_ISSUES #83（pfr_tpm `pinctrl-noinit` + SPIF 配对 SPI `pinctrl-0` 两个实例）
- qsh-test-gen §1.8（外设使能类 overlay 模板要点）
- 本库 mcu 域 `pinmux-dual-state-electrical-residual.md`（**不同**问题：那条是电气属性残留，
  本条是引脚所有权）
