/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * spid_test —— pfr_tpm TPM SPI slave 驱动用例（drivers/spid/spid_linkedsemi.c）。
 *
 * 纯 ztest：每项自带前置（cold_reset / init_spid_registers），不依赖其他用例的
 * 执行顺序或残留状态。
 *
 * ⚠️ 实例地址一律取自 DT 节点（`DT_NODELABEL(tpm_spis1/2)`），**不使用**模块
 *    sample 硬编码的 `SPID2_BASE = 0x4000e000`（与 alias `spid`→tpm_spis1 不一致，
 *    roadmap §7.2 S-3）。
 */

#include <zephyr/kernel.h>
#include <zephyr/ztest.h>
#include <zephyr/device.h>
#include <zephyr/irq.h>          /* irq_is_enabled()：interrupt_register 项的 INFO 观测 */
#include <zephyr/drivers/spi.h>
#include <zephyr/sys/sys_io.h>
#include <zephyr/sys/util.h>
#include <zephyr/logging/log.h>
#include <string.h>

#include "ztest_glue.h"
#include "spid_test.h"

#include <spid_linkedsemi.h>
#include <reg_spid.h>

LOG_MODULE_REGISTER(spid_test_case, LOG_LEVEL_INF);

/*
 * 寄存器中"非保留可写位"的掩码：`init_spid_registers()` 写 0x11（FIFO 模式）
 * / 0x13（CRB 模式），两值仅 bit1 不同。为避免把保留位的回读差异误判为失败，
 * 断言一律用该掩码（0x13）做位级比较，同时用 ZT_INFO 上报原始值。
 */
#define SPID_TPM_CFG_MODE_MASK 0x13u
#define SPID_TPM_CFG_CRB     0x13u
#define SPID_TPM_CFG_FIFO    0x11u

/* -------------------------------------------------------------------------- */
/* 设备 / 寄存器定位                                                           */
/* -------------------------------------------------------------------------- */

static const struct device *const spid_devs[SPID_INST_NUM] = {
	DEVICE_DT_GET(DT_NODELABEL(tpm_spis1)),
	DEVICE_DT_GET(DT_NODELABEL(tpm_spis2)),
};

/*
 * 基址取自 DT。方法：与 spid_devs 同序，但用 DT 的 reg 值直接列出 —— 保持
 * "地址来自 DT" 的约束（改动 dtsi 地址时这里必须跟着变，编译器不会静默通过）。
 */
static const uintptr_t spid_bases[SPID_INST_NUM] = {
	DT_REG_ADDR(DT_NODELABEL(tpm_spis1)),
	DT_REG_ADDR(DT_NODELABEL(tpm_spis2)),
};

const struct device *spid_dev(int idx)
{
	return (idx >= 0 && idx < SPID_INST_NUM) ? spid_devs[idx] : NULL;
}

uintptr_t spid_reg_base(int idx)
{
	return (idx >= 0 && idx < SPID_INST_NUM) ? spid_bases[idx] : 0;
}

/* -------------------------------------------------------------------------- */
/* 用例                                                                        */
/* -------------------------------------------------------------------------- */

ZTEST(spid, device_ready)
{
	for (int i = 0; i < SPID_INST_NUM; i++) {
		const struct device *dev = spid_dev(i);

		zassert_not_null(dev, "tpm_spis%d device pointer is NULL", i + 1);
		zassert_true(device_is_ready(dev), "tpm_spis%d not ready", i + 1);
		zassert_not_equal(spid_reg_base(i), 0u, "tpm_spis%d reg addr", i + 1);
	}

	/* 两个实例的基址必须不同（防止 DT 节点配错成同一地址） */
	zassert_not_equal(spid_reg_base(0), spid_reg_base(1),
			  "tpm_spis1/tpm_spis2 must map to different addresses");

	ZT_INFO("device_ready", "spis1=%#lx,spis2=%#lx",
		(unsigned long)spid_reg_base(0), (unsigned long)spid_reg_base(1));
}

ZTEST(spid, cold_reset_state)
{
	const struct device *dev = spid_dev(0);
	const uintptr_t base = spid_reg_base(0);

	zassert_ok(spid_linkedsemi_cold_reset(dev), "cold_reset failed");

	/* cold_reset 收尾：SPID_INTR_ENABLE = 0xff（spid_linkedsemi.c:143） */
	zassert_equal(sys_read32(base + SPID_INTR_ENABLE), 0xffu,
		      "SPID_INTR_ENABLE must be 0xff after cold_reset");

	/* 第二个实例同样成立（两实例各自有独立寄存器窗） */
	{
		const struct device *dev2 = spid_dev(1);
		const uintptr_t base2 = spid_reg_base(1);

		zassert_ok(spid_linkedsemi_cold_reset(dev2), "cold_reset (spis2) failed");
		zassert_equal(sys_read32(base2 + SPID_INTR_ENABLE), 0xffu,
			      "SPID_INTR_ENABLE (spis2) must be 0xff");
	}

	ZT_INFO("cold_reset_state", "intr_enable=%#x",
		sys_read32(base + SPID_INTR_ENABLE));
}

ZTEST(spid, cold_reset_idempotent)
{
	const struct device *dev = spid_dev(0);
	const uintptr_t base = spid_reg_base(0);

	/* 时钟 off → reset toggle → clock on → pinctrl 的完整路径重复执行
	 * （spid_linkedsemi.c:98-146）应保持同一终态，且不返回错误 */
	for (int i = 0; i < 3; i++) {
		zassert_ok(spid_linkedsemi_cold_reset(dev), "cold_reset round %d", i);
		zassert_equal(sys_read32(base + SPID_INTR_ENABLE), 0xffu,
			      "INTR_ENABLE after round %d", i);
	}
}

ZTEST(spid, init_register_modes)
{
	const struct device *dev = spid_dev(0);
	const uintptr_t base = spid_reg_base(0);
	uint32_t fifo_cfg;
	uint32_t crb_cfg;
	uint32_t before;

	zassert_ok(spid_linkedsemi_cold_reset(dev), "cold_reset failed");

	/* INTF_FIFO_MODE → SPID_TPM_CFG = 0x11（spid_linkedsemi.c:88） */
	init_spid_registers(dev, INTF_FIFO_MODE);
	fifo_cfg = sys_read32(base + SPID_TPM_CFG);
	zassert_equal(fifo_cfg & SPID_TPM_CFG_MODE_MASK, SPID_TPM_CFG_FIFO,
		      "FIFO mode cfg=%#x (expect low bits %#x)", fifo_cfg, SPID_TPM_CFG_FIFO);

	/* INTF_CRB_MODE → SPID_TPM_CFG = 0x13（spid_linkedsemi.c:90） */
	init_spid_registers(dev, INTF_CRB_MODE);
	crb_cfg = sys_read32(base + SPID_TPM_CFG);
	zassert_equal(crb_cfg & SPID_TPM_CFG_MODE_MASK, SPID_TPM_CFG_CRB,
		      "CRB mode cfg=%#x (expect low bits %#x)", crb_cfg, SPID_TPM_CFG_CRB);

	/* 两种模式的差异位就是 bit1 */
	zassert_not_equal(fifo_cfg & BIT(1), crb_cfg & BIT(1),
			  "interface mode must be encoded in SPID_TPM_CFG bit1");

	/* 非法模式（既非 FIFO 也非 CRB）：只 printk 报错，**寄存器保持不变**
	 * （spid_linkedsemi.c:91-93 的 else 分支） */
	before = sys_read32(base + SPID_TPM_CFG);
	init_spid_registers(dev, 0x7f);
	zassert_equal(sys_read32(base + SPID_TPM_CFG), before,
		      "invalid interface mode must leave SPID_TPM_CFG unchanged");

	/* 复位回 CRB（产品默认接口模式） */
	init_spid_registers(dev, INTF_CRB_MODE);

	ZT_INFO("init_register_modes", "fifo_cfg=%#x,crb_cfg=%#x", fifo_cfg, crb_cfg);
}

ZTEST(spid, instance_isolation)
{
	const struct device *dev1 = spid_dev(0);
	const struct device *dev2 = spid_dev(1);
	const uintptr_t base1 = spid_reg_base(0);
	const uintptr_t base2 = spid_reg_base(1);
	uint32_t cfg1;

	zassert_ok(spid_linkedsemi_cold_reset(dev1), "cold_reset spis1");
	zassert_ok(spid_linkedsemi_cold_reset(dev2), "cold_reset spis2");

	/* 两实例写不同接口模式：互不影响（各自独立寄存器窗） */
	init_spid_registers(dev1, INTF_FIFO_MODE);
	init_spid_registers(dev2, INTF_CRB_MODE);

	cfg1 = sys_read32(base1 + SPID_TPM_CFG);
	zassert_equal(cfg1 & SPID_TPM_CFG_MODE_MASK, SPID_TPM_CFG_FIFO,
		      "spis1 must keep FIFO mode");
	zassert_equal(sys_read32(base2 + SPID_TPM_CFG) & SPID_TPM_CFG_MODE_MASK,
		      SPID_TPM_CFG_CRB, "spis2 must keep CRB mode");

	/* 只复位 spis1，spis2 的配置不应被改动 */
	zassert_ok(spid_linkedsemi_cold_reset(dev1), "cold_reset spis1 again");
	zassert_equal(sys_read32(base2 + SPID_TPM_CFG) & SPID_TPM_CFG_MODE_MASK,
		      SPID_TPM_CFG_CRB, "spis2 cfg must survive spis1 cold_reset");

	ZT_INFO("instance_isolation", "spis1=%#x,spis2=%#x",
		sys_read32(base1 + SPID_TPM_CFG), sys_read32(base2 + SPID_TPM_CFG));
}

/*
 * 白盒观测视图：驱动 `struct spid_linkedsemi_data`
 *   = { spid_callback_t cb; void *user_data; const struct device *dev; uint32_t data; }
 * （spid_linkedsemi.c:32-37）。该结构**未**在 spid_linkedsemi.h 导出、也无 getter，
 * 而驱动 `register_callback` 是**纯指针落地且恒返回 0**（:66-77）——
 * 只看返回值是恒真断言。按**同一布局**复刻视图后直接读 `dev->data` 字段，
 * 使"回调真的落地"可证伪：布局若漂移，读到的不会是我们的函数指针 ⇒ FAIL（响亮，
 * 不会假 PASS），届时同步该视图即可。
 */
struct spid_data_view {
	spid_callback_t cb;
	void *user_data;
	const struct device *dev;
	uint32_t data;
};

static void spid_precise_cb(const struct device *dev, uint32_t callback_idx,
			    void *user_data, void *drv_data)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(callback_idx);
	ARG_UNUSED(user_data);
	ARG_UNUSED(drv_data);
}

ZTEST(spid, callback_register)
{
	const struct device *dev = spid_dev(0);
	struct spid_data_view *view = (struct spid_data_view *)dev->data;

	/* 先清空，保证随后"读到非 NULL"确由本项注册造成 */
	view->cb = NULL;
	view->user_data = NULL;

	/* 注册非 NULL 回调 + 非 NULL user_data：两者都必须落到 dev->data。
	 * 驱动正确时必过；cb 被丢弃 / 写错位置 / 只改返回值时必不过。 */
	zassert_ok(spid_linkedsemi_register_callback(dev, 0, spid_precise_cb, (void *)dev),
		   "register callback on tpm_spis1");
	zassert_equal_ptr(view->cb, spid_precise_cb,
			  "cb did not land in dev->data (got %p)", view->cb);
	zassert_equal_ptr(view->user_data, dev,
			  "user_data did not land in dev->data (got %p)", view->user_data);

	/* 覆盖注册 NULL：必须清掉旧值（驱动 :73-74 无条件赋值） */
	zassert_ok(spid_linkedsemi_register_callback(dev, 0, NULL, NULL),
		   "register NULL callback");
	zassert_is_null(view->cb, "cb must be cleared by a NULL registration");

	/* 两个实例各自独立；`callback_idx` 完全不被校验（驱动忽略该参数） */
	for (int i = 0; i < SPID_INST_NUM; i++) {
		const struct device *d = spid_dev(i);
		struct spid_data_view *v = (struct spid_data_view *)d->data;

		zassert_ok(spid_linkedsemi_register_callback(d, 0xffffffff, spid_precise_cb,
							     (void *)d),
			   "register on tpm_spis%d", i + 1);
		zassert_equal_ptr(v->cb, spid_precise_cb,
				  "tpm_spis%d cb did not land", i + 1);
		zassert_equal_ptr(v->user_data, d,
				  "tpm_spis%d user_data did not land", i + 1);
	}
}

ZTEST(spid, interrupt_register)
{
	/*
	 * ⚠ **本项当前无判别力（如实标注，2026-09-18 F1 波）** ⚠
	 *
	 * 驱动 `interrupt_register` 只做 `dev_config->irq_config_func(dev)` 然后
	 * `return 0`（spid_linkedsemi.c:148-156），**恒返回 0** ⇒ 原有 `zassert_ok`
	 * 是恒真断言，测不出"中断是否真的使能 / IRQ 号是否连错"。
	 *
	 * 本轮**尝试过**的判别方向与**为何放弃**：
	 *   方向：断言 `irq_is_enabled(<main IRQ>)`。
	 *   事实：`irq_enable()` 在 RISC-V 上落到 `arch_irq_enable()` →
	 *     `riscv_clic_irq_enable()`
	 *     （`zephyr/soc/common/riscv-privileged/soc_common_irq.c:19-33`，
	 *      `CONFIG_RISCV_HAS_CLIC=y` 已在 .config 核实）；
	 *     对应的读回是 `arch_irq_is_enabled()` → `riscv_clic_irq_is_enabled()`。
	 *   但：本 SoC 的这两个函数由 `soc/linkedsemi/common/e906/irq_driver.c:34/:52`
	 *     实现，**内部都走 csi_vic_* 系列**（`csi_vic_enable_irq` /
	 *     `csi_vic_get_enabled_irq`，`modules/hal/linkedsemi/soc/rv32/csi2/include/
	 *     core/core_rv32.h:463/:521`），而这两个函数体**只在
	 *     `CONFIG_INTC_CLIC` / `CONFIG_INTC_CLIC_APLIC` / `CONFIG_INTC_APLIC`
	 *     之一为真时才有实现**，三个都假时 `csi_vic_get_enabled_irq` 返回
	 *     **未初始化的 `ret`**（UB）。本板 `.config` 里**三者皆未置位**
	 *     （只有 `CONFIG_RISCV_HAS_CLIC` / `CONFIG_XUANTIE_E906_CLIC`）。
	 * ⇒ 该读回值的语义**无法从仓库内确证**（可能是真实 CLIC 位，也可能是栈垃圾），
	 *   断言它会引入**假 FAIL 风险** —— 按本轮纪律"说不清依据的宁可留 INFO"，
	 *   **不加断言**，只把读回值经 ZT_INFO 上报，供 P6 实机一眼定论。
	 *
	 * 待父会话裁定：要么在 P6 实机用 INFO 值确认该读回是否可信后再升级为断言，
	 * 要么改从 `SPID_INTR_ENABLE` 寄存器侧找判据（驱动 cold_reset 写 0xff，
	 * 与本 API 无关，故不构成判据）。
	 */
	static const uint32_t spid_main_irq[SPID_INST_NUM] = {
		DT_IRQ_BY_NAME(DT_NODELABEL(tpm_spis1), main, irq),
		DT_IRQ_BY_NAME(DT_NODELABEL(tpm_spis2), main, irq),
	};

	for (int i = 0; i < SPID_INST_NUM; i++) {
		const struct device *dev = spid_dev(i);
		bool en_before;
		bool en_after;

		zassert_ok(spid_linkedsemi_cold_reset(dev), "cold_reset spis%d", i + 1);

		en_before = (irq_is_enabled(spid_main_irq[i]) != 0);
		zassert_ok(spid_linkedsemi_interrupt_register(dev),
			   "interrupt_register tpm_spis%d", i + 1);
		en_after = (irq_is_enabled(spid_main_irq[i]) != 0);

		/* 重复安装应保持同一成功语义（幂等） */
		zassert_ok(spid_linkedsemi_interrupt_register(dev),
			   "interrupt_register repeat tpm_spis%d", i + 1);

		ZT_INFO("interrupt_register",
			"inst=%d,irq=%u,en_before=%d,en_after=%d,en_repeat=%d",
			i + 1, spid_main_irq[i], (int)en_before, (int)en_after,
			(int)(irq_is_enabled(spid_main_irq[i]) != 0));
	}
}

ZTEST(spid, intr_state_clear_idiom)
{
	const struct device *dev = spid_dev(0);
	const uintptr_t base = spid_reg_base(0);
	uint32_t state;

	zassert_ok(spid_linkedsemi_cold_reset(dev), "cold_reset failed");

	/* 驱动 ISR 以 "写 0xff 到 SPID_INTR_STATE" 清中断（spid_linkedsemi.c:63），
	 * 即假定该寄存器为 W1C。本项固化该假设：置位后写 0xff 应读回 0。
	 * 若读回非 0 ⇒ 该寄存器是普通 RW（驱动的清中断写法无效），属需上报的发现。 */
	state = sys_read32(base + SPID_INTR_STATE);
	sys_write32(0xff, base + SPID_INTR_STATE);
	zassert_equal(sys_read32(base + SPID_INTR_STATE), 0u,
		      "SPID_INTR_STATE must be W1C (driver ISR writes 0xff to clear)");

	ZT_INFO("intr_state_clear_idiom", "idle_state=%#x,after_w1c=%#x", state,
		sys_read32(base + SPID_INTR_STATE));
}

ZTEST(spid, register_stability)
{
	const struct device *dev = spid_dev(0);
	const uintptr_t base = spid_reg_base(0);
	uint32_t cap, didvid, rid, sts, access0;

	zassert_ok(spid_linkedsemi_cold_reset(dev), "cold_reset failed");
	init_spid_registers(dev, INTF_CRB_MODE);

	/* 存在性判据：时钟/复位生效后，TPM 侧只读寄存器不应回读全 0 或全 1
	 * （未上电/未时钟的 IP 通常回读 0x00000000 或 0xffffffff）。 */
	cap = sys_read32(base + SPID_TPM_CAP);
	zassert_not_equal(cap, 0xffffffffu, "SPID_TPM_CAP reads all-ones (IP not clocked?)");
	zassert_not_equal(cap, 0u, "SPID_TPM_CAP reads zero (IP not responding?)");

	didvid = sys_read32(base + SPID_TPM_DID_VID);
	rid = sys_read32(base + SPID_TPM_RID);
	sts = sys_read32(base + SPID_TPM_STATUS);
	access0 = sys_read32(base + SPID_TPM_ACCESS_0);

	/* 连续两次读一致 ⇒ 总线可稳定访问 */
	zassert_equal(sys_read32(base + SPID_TPM_CAP), cap, "SPID_TPM_CAP not stable");
	zassert_equal(sys_read32(base + SPID_TPM_DID_VID), didvid,
		      "SPID_TPM_DID_VID not stable");

	/* 命令地址 FIFO 空时 cmdaddr_notempty（bit0）应为 0（清空后无主控下发） */
	zassert_equal(sts & BIT(0), 0u, "SPID_TPM_STATUS.cmdaddr_notempty must be 0 when idle");

	ZT_INFO("register_stability",
		"cap=%#x,didvid=%#x,rid=%#x,status=%#x,access0=%#x",
		cap, didvid, rid, sts, access0);
}

ZTEST(spid, master_send_tpm_frame)
{
	/*
	 * 外部: tpm_spis1（MISO PI04 / MOSI PI05 / SCK PI06 / CSN PI07）与 SoC 侧
	 *       SPI master（overlay 选 `spi1`，PA00/PA01/PA02 + cs gpioa[0]）之间
	 *       需**外部跳线**（4 线对连）。
	 *
	 * ⚠ **判据只有主机侧一条（对从端零判据），如实标注（2026-09-18 F1 波）** ⚠
	 *
	 * 现判据：master 侧 `spi_transceive()` 返回 0 —— **真断言**（跳线/引脚复用/
	 *   CS/时钟任一坏掉都会 FAIL），但它**只证明主机事务发出去了**；
	 *   **spid 从端驱动收发全坏也不影响它 PASS**。
	 *
	 * 为何不给从端加断言（本轮已逐项查证，**结论：仓库内无法确证**）：
	 *   - `SPID_TPM_STATUS`（0x808）/ `SPID_TPM_CMD_ADDR`（0x830）在
	 *     `reg_spid.h:72/:82` **只有偏移与一行描述，无任何位域定义**；
	 *     "bit0 = cmdaddr_notempty" 仅见于 sample 注释
	 *     （`modules/lib/pfr_tpm/samples/spid/src/main.c:42-44`），非寄存器手册。
	 *   - 要让从端锁存命令地址所需的 **SPI 帧格式（CRB 事务形态）在仓库内无出处**：
	 *     驱动只把 `SPID_TPM_CFG` 写 0x11/0x13（FIFO/CRB，仅 bit1 之差，
	 *     `spid_linkedsemi.c:82-96`），驱动从不触碰 `SPID_CMD_FILTER_*` /
	 *     `SPID_CMD_INFO_*`，sample 也**从不构造/发送帧**（只轮询状态）。
	 *   - `SPID_INTR_STATE` 是否 W1C 亦无文档；本文件 `intr_state_clear_idiom` 的
	 *     "写 0xff 读回 0" 无法区分 W1C 与"该寄存器未实现恒 0"。
	 *   ⇒ 本项**保留 INFO（status/cmd_addr 实测值）**，不对从端断言 —— 编一个
	 *     期望值只会造出**假 FAIL**（比空壳更糟）。帧格式确认后可按
	 *     `tests/COVERAGE.md` 的口径升级为"cmdaddr_notempty 置起 + CMD_ADDR 比对"。
	 */
	const struct device *dev = spid_dev(0);
	const uintptr_t base = spid_reg_base(0);
	const struct device *master = DEVICE_DT_GET(DT_ALIAS(spidmaster));

	/* 占位帧：1 字节 TPM 寄存器地址 + 4 字节数据（CRB 事务形态，**未确认**） */
	static const uint8_t tx_buffer[] = { 0x00, 0x00, 0x00, 0x00, 0x00 };
	static uint8_t rx_buffer[sizeof(tx_buffer)];
	const struct spi_config spi_cfg = {
		.operation = SPI_OP_MODE_MASTER | SPI_WORD_SET(8) | SPI_TRANSFER_MSB,
		.frequency = MHZ(1),
		.cs = { .gpio = GPIO_DT_SPEC_GET(DT_ALIAS(spidmaster), cs_gpios) },
	};
	const struct spi_buf tx_buf = { .buf = (void *)tx_buffer, .len = sizeof(tx_buffer) };
	const struct spi_buf_set tx = { .buffers = &tx_buf, .count = 1 };
	struct spi_buf rx_buf = { .buf = rx_buffer, .len = sizeof(rx_buffer) };
	const struct spi_buf_set rx = { .buffers = &rx_buf, .count = 1 };
	int ret;
	uint32_t sts;

	zassert_true(device_is_ready(master), "SPI master (alias spidmaster) not ready");

	zassert_ok(spid_linkedsemi_cold_reset(dev), "cold_reset failed");
	init_spid_registers(dev, INTF_CRB_MODE);
	spid_linkedsemi_interrupt_register(dev);

	memset(rx_buffer, 0, sizeof(rx_buffer));
	ret = spi_transceive(master, &spi_cfg, &tx, &rx);
	zassert_ok(ret, "TPM frame transceive failed (ret=%d); 外部跳线到位?", ret);

	sts = sys_read32(base + SPID_TPM_STATUS);
	ZT_INFO("master_send_tpm_frame", "status=%#x,cmd_addr=%#x,frame_len=%u",
		sts, sys_read32(base + SPID_TPM_CMD_ADDR), (uint32_t)sizeof(tx_buffer));
}

ZTEST_SUITE(spid, NULL, NULL, NULL, NULL, NULL);
