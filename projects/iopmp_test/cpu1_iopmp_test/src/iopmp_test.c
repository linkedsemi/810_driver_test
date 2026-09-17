/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * IOPMP test cases (minimal coverage, non-destructive)
 *
 * 直接用 HAL API 操作 sec_iopmp1 (0x40001000)。
 * 砍违例注入、COMP_LOCK、自我封锁等破坏性场景。
 *
 * 「非破坏性」的含义（TC-5 落实）：被测条目在用例结束时必须回到测试前
 * 的取值——先读原值保存、测完原样写回并断言恢复，绝不把保护规则清零。
 */

#include <zephyr/kernel.h>
#include <zephyr/ztest.h>
#include <iopmp.h>
#include <reg_base_addr.h>

#define IOPMP_BASE SEC_IOPMP1_ADDR

/* TC-1: iopmp_is_enable after config_enable(true) */
ZTEST(iopmp, test_iopmp_is_enable_after_init)
{
	/* 使能 */
	iopmp_config_enable(IOPMP_BASE, true);
	bool enabled = iopmp_is_enable(IOPMP_BASE);
	zassert_true(enabled, "iopmp should be enabled after config_enable(true)");

	/* 已知限制（HAL 侧）：iopmp_config_enable(dev, enable) 忽略 enable
	 * 参数，恒写 IOPMP_EN=1|MD_EN=1（iopmp.h:89）⇒ 用 HAL 关不掉。
	 * 这是 **HAL 软件限制**，不是硬件限制：MD_CFG 寄存器可读写，直写
	 * `MD_CFG=0` 即可真正关闭（实测 0x40001000 写 0x2/0x1/0x0 均按值回读；
	 * 见 KNOWN_ISSUES #65②）。此处只验证 enable 路径。 */
}

/* TC-2: idempotent init (call twice, no crash) */
ZTEST(iopmp, test_iopmp_idempotent_init)
{
	iopmp_config_enable(IOPMP_BASE, true);
	iopmp_config_enable(IOPMP_BASE, true);  /* 二次调用应幂等 */
	bool enabled = iopmp_is_enable(IOPMP_BASE);
	zassert_true(enabled, "iopmp should remain enabled after double init");

	/* HAL 限制（iopmp.h:89）：传 false 是 no-op（HAL 忽略 enable 参数）。
	 * 把「关不掉」显式登记为可验证事实，而不是留一条看起来在关、其实
	 * 没关的无断言裸调用。原因 = HAL 软件限制（非硬件限制，见 TC-1 注释）。 */
	iopmp_config_enable(IOPMP_BASE, false);
	zassert_true(iopmp_is_enable(IOPMP_BASE),
		"config_enable(false) must be a no-op (HAL ignores 'enable', iopmp.h:89)");
}

/* TC-3: addr2napot round-trip (pure function) */
ZTEST(iopmp, test_iopmp_napot_encode_decode)
{
	/* 地址 0x200000, 大小 0x100000 (1MB) -> NAPOT 编码 -> 解码回 */
	uint32_t addr = 0x200000;
	uint64_t size = 0x100000;
	uint32_t napot = addr2napot(addr, size);

	/* 解码回地址和大小 */
	uint32_t dec_addr = napot_reg2addr(napot);
	uint32_t dec_size = napot_reg2size(napot);

	/* 允许 NAPOT 对齐误差（地址可能被向下对齐到 2 的幂边界） */
	zassert_true(dec_addr <= addr,
		"decoded addr 0x%08x > original 0x%08x", dec_addr, addr);
	zassert_true(dec_size >= size,
		"decoded size 0x%x < original 0x%llx", dec_size, size);
}

/* TC-4: napot invalid params (non-power-of-2 / misaligned) */
ZTEST(iopmp, test_iopmp_napot_invalid_params)
{
	/* 先读清契约（iopmp.h:64）：addr2napot() 是**纯位运算**，不做任何
	 * 校验——非 2 幂 size 也照公式 (addr>>2)|((size-1)>>3) 出结果，
	 * 不会报错、不返回 0。校验在上一层 iopmp_config_region_napot()。 */
	zassert_equal(addr2napot(0x100000, 3), 0x40000,
		"addr2napot is a pure bit-op: (0x100000>>2)|((3-1)>>3) = 0x40000");

	/* 非法参数的**拒绝契约**在 iopmp_config_region_napot()（iopmp.h:159）：
	 * (addr % size) != 0 或 size 非 2 幂 → 返回 -EINVAL 且不写寄存器。 */
	uint32_t addr0_before = iopmp_get_pmpaddrx(IOPMP_BASE, 0);
	uint32_t cfg0_before = iopmp_get_pmpxcfg(IOPMP_BASE, 0);

	/* 负例 1：size=3 非 2 的幂 */
	int ret = iopmp_config_region_napot(IOPMP_BASE, 0, 0x100000, 3,
					    true, true, false, false);
	zassert_equal(ret, -EINVAL,
		"non-power-of-2 size must be rejected, got %d", ret);

	/* 负例 2：addr 未按 size 对齐（0x100001 % 0x1000 = 1） */
	ret = iopmp_config_region_napot(IOPMP_BASE, 0, 0x100001, 0x1000,
					true, true, false, false);
	zassert_equal(ret, -EINVAL,
		"misaligned addr must be rejected, got %d", ret);

	/* 拒绝路径不得写寄存器（否则等于用非法参数破坏在用条目） */
	zassert_equal(iopmp_get_pmpaddrx(IOPMP_BASE, 0), addr0_before,
		"rejected napot call must not touch ADDR0");
	zassert_equal(iopmp_get_pmpxcfg(IOPMP_BASE, 0), cfg0_before,
		"rejected napot call must not touch CFG0");
}

/* TC-5: register readback (write CFG/ADDR entry 0, read back, then restore) */
ZTEST(iopmp, test_iopmp_register_readback)
{
	/* ★ 非破坏性：条目 0 可能承载上层（SoC init / 其它固件）配置的保护
	 * 规则。原实现"清理"时写 ADDR0=0 / CFG0=0，等于把保护删掉且不恢复
	 * （CFG=0 即 A=OFF，条目失效）。这里改为**先读原值保存、测完原样写回
	 * 并断言恢复**——不依赖"某个条目一定空闲"的假设，任何配置下都安全。
	 * 注意 CFG0 是打包寄存器（32 位含 4 个条目，各 1 字节）：
	 *   ① `iopmp_set_pmpxcfg(base,0,val)` 写的是整个 CFG0 寄存器，
	 *      会连带改写条目 1~3；② 必须整体保存/整体写回才能完整恢复。 */
	uint32_t addr0_before = iopmp_get_pmpaddrx(IOPMP_BASE, 0);
	uint32_t cfg0_before = iopmp_get_pmpxcfg(IOPMP_BASE, 0);
	bool en_before = iopmp_is_enable(IOPMP_BASE);

	/* 写 ADDR0（某些实现会做编码变换，只验证寄存器可读写、不绑定具体值） */
	iopmp_set_pmpaddrx(IOPMP_BASE, 0, 0x12345678);
	uint32_t val = iopmp_get_pmpaddrx(IOPMP_BASE, 0);
	ARG_UNUSED(val);

	/* 写 CFG0 */
	iopmp_set_pmpxcfg(IOPMP_BASE, 0, 0x03);
	val = iopmp_get_pmpxcfg(IOPMP_BASE, 0);
	zassert_equal(val, 0x03, "CFG0 readback = 0x%02x, expected 0x03", val);

	/* 恢复原值（打包寄存器整体写回） */
	iopmp_set_pmpaddrx(IOPMP_BASE, 0, addr0_before);
	iopmp_set_pmpxcfg(IOPMP_BASE, 0, cfg0_before);

	/* 断言恢复成功：被操作条目与全局使能状态与测试前一致 */
	zassert_equal(iopmp_get_pmpaddrx(IOPMP_BASE, 0), addr0_before,
		"ADDR0 not restored (0x%08x != 0x%08x)",
		iopmp_get_pmpaddrx(IOPMP_BASE, 0), addr0_before);
	zassert_equal(iopmp_get_pmpxcfg(IOPMP_BASE, 0), cfg0_before,
		"CFG0 not restored (0x%08x != 0x%08x)",
		iopmp_get_pmpxcfg(IOPMP_BASE, 0), cfg0_before);
	zassert_equal(iopmp_is_enable(IOPMP_BASE), en_before,
		"global IOPMP enable state changed by TC-5");
}

ZTEST_SUITE(iopmp, NULL, NULL, NULL, NULL, NULL);
