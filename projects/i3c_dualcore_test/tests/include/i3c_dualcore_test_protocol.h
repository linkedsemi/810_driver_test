/*
 * Copyright (c) 2025 LinkedSemi
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef I3C_DUALCORE_TEST_PROTOCOL_H
#define I3C_DUALCORE_TEST_PROTOCOL_H

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Inter-core command protocol.
 *
 * Controller issues all commands as private writes to target address 0x32.
 * First byte = opcode, remaining bytes = payload.
 *
 * opcode 选用可打印 ASCII：逻辑分析仪上能直接认出命令含义，
 * 且不会与 I3C CCC 命令码（direct ENEC=0x80 / DISEC=0x81 等）混淆。
 */
#define CMD_TEST_START         'S'	/* 0x53 */
#define CMD_TEST_END           'E'	/* 0x45 */
#define CMD_WRITE_DATA         'W'	/* 0x57 */
#define CMD_SET_READ_LEN       'L'	/* 0x4C */
#define CMD_TRIGGER_IBI        'I'	/* 0x49 */
#define CMD_TRIGGER_HJ         'H'	/* 0x48 */
#define CMD_RESET_TARGET_STATE 'R'	/* 0x52 */
#define CMD_PRINT_SUMMARY      'P'	/* 0x50 */
#define CMD_RUN_TGT_NEG        'N'	/* 0x4E：让 target 执行 IBI 负路径自检 */
#define CMD_AASA_PROBE         'A'	/* 0x41：SETAASA/SETDASA 测试的新地址探针写，
					 * target 收到时快照 SDYNADDR 寄存器（此刻
					 * 能被寻址本身即证明地址已切换） */
#define CMD_INDEXED_READ       'X'	/* 0x58：索引读——下一字节是索引，target 按
					 * 索引预填 tgt_tx_buf[idx..idx+len-1] 返回 */
#define CMD_INDEXED_READ_DELAY 'Y'	/* 0x59：延时索引读——后随 2 字节（索引 + 延时档位），
					 * target 收到延时字节后先 spin 延时再预填，用于实测
					 * "写段 data 到读段欠载"的可用准备窗口（P1→T-bit）。
					 * 延时档位单位 50ns（0-255 → 0-12.75us）。 */
#define CMD_INDEXED_READ_DELAY_LEN 'Z'	/* 0x5A：延时长度索引读——后随 3 字节
					 * （索引 + 延时档位 + 读长度），target 收到
					 * 长度字节（=写段最后字节）后 spin 延时再预填，
					 * 读段按 len 返回。FIFO 深 16，>16 由 TXSEND
					 * 中断 read_processed_cb 续发——测跨 FIFO 深度
					 * 边界的回复能力。长度 0 → 256（单字节约定）。 */

/* write_read 写阶段的占位字节：不参与解析，仅用于产生 restart 前的写段。
 * 读长度统一由 CMD_SET_READ_LEN 告知 target。 */
#define CMD_DUMMY_BYTE         0xA5

/* SETAASA/SETDASA 测试地址约定：
 * - target 静态地址（cpu1_i3c_target/app.overlay 的 static-address，
 *   写进 SCONFIG.SADDR，硬件据此响应 SETDASA/SETAASA）
 * - SETDASA 测试分配的动态地址（故意 ≠ 静态地址 ≠ 0x32，证明地址可变） */
#define I3C_TARGET_STATIC_ADDR 0x5AU
#define I3C_SETDASA_NEW_ADDR   0x45U

/* SETAASA/SETDASA 探针写负载（'A' 之后跟 4 字节 magic），
 * target 侧校验 write_data 前 4 字节等于它。 */
#define AASA_PROBE_MAGIC_0     0xDE
#define AASA_PROBE_MAGIC_1     0xAD
#define AASA_PROBE_MAGIC_2     0xBE
#define AASA_PROBE_MAGIC_3     0xEF

/* Result codes carried by CMD_TEST_END. */
#define TEST_RESULT_PASS  0
#define TEST_RESULT_FAIL  1
#define TEST_RESULT_SKIP  2

/*
 * Target address used by both controller and target.
 */
#define I3C_TARGET_ADDR 0x32U

/*
 * Test IDs shared by controller and target.
 *
 * The order must match i3c_test_names[] below.
 */
enum i3c_test_id {
	I3C_TEST_DAA_DISCOVERY = 0,
	I3C_TEST_WRITE_8,
	I3C_TEST_WRITE_100,
	I3C_TEST_READ_32_DIRECT,
	I3C_TEST_WRITE_READ_1,
	I3C_TEST_WRITE_READ_15,
	I3C_TEST_WRITE_READ_16,
	I3C_TEST_WRITE_READ_17,
	I3C_TEST_WRITE_READ_32,
	I3C_TEST_IBI,
	I3C_TEST_HOT_JOIN,
	I3C_TEST_CCC_BASIC,
	I3C_TEST_WRITE_STOP_READ_1,
	I3C_TEST_WRITE_STOP_READ_15,
	I3C_TEST_WRITE_STOP_READ_16,
	I3C_TEST_WRITE_STOP_READ_17,
	I3C_TEST_WRITE_STOP_READ_32,
	I3C_TEST_READ_UNDERRUN_8_16,
	I3C_TEST_READ_UNDERRUN_16_32,
	I3C_TEST_NACK_BAD_ADDR,
	I3C_TEST_BACK_TO_BACK_8X8,
	I3C_TEST_BIG_256,
	I3C_TEST_CCC_RAPID_8,
	I3C_TEST_READ_MASTER_ABORT,
	I3C_TEST_IBI_NEG_PATHS,
	I3C_TEST_API_NEG_PATHS,
	I3C_TEST_MULTI_MSG_4,
	/* 主机写 1 字节（索引），从机 restart 后根据索引预填对应数据段返回：
	 * 验证 write_received_cb 改 tx_len/偏移后，write_requested_cb 能按新
	 * 参数预填（ISR 顺序重构：MATCHED 先于 RXPEND） */
	I3C_TEST_WRITE_INDEXED_READ_16,
	/* 延时索引读：测从机收到写段 data 后到读段欠载的可用准备窗口。
	 * controller 扫延时档位，target 收到写段后 spin 延时再预填，
	 * 临界档位（恰好不欠载的最大延时）即 P1→T-bit 实测窗口 */
	I3C_TEST_WRITE_DELAY_READ_16,
	/* 延时长度索引读：写段内容决定读长度（FIFO 深 16 临界 17/32/64），
	 * target spin 后 prefill 16B，>16 由续发路径补齐——验证跨 FIFO
	 * 深度边界的回复能力 + 续发不缩小可用准备窗口 */
	I3C_TEST_WRITE_DELAY_READ_LEN,
	/* read_underrun_stress：连续 8 轮短读（target 准备 8B，controller 读 16B），
	 * 压测 FC double-give 竞态 + 信号量计数泄漏 */
	I3C_TEST_READ_UNDERRUN_STRESS,
	/* multi_msg_short_read：4 段帧 W+R+W+R，末段 target 短发（8B/16B），
	 * 验证多消息帧末尾短读 + 恢复 */
	I3C_TEST_MULTI_MSG_SHORT_READ,
	/* short_read_then_nack：4 轮 [短读 → 伪造地址 NACK → 恢复读]，
	 * 验证跨帧 ERR+RXTGTEND 组合恢复 */
	I3C_TEST_SHORT_READ_THEN_NACK,
	/* SETAASA/SETDASA 放最后：测试会 RSTDAA 清动态地址再恢复，
	 * 若恢复失败只影响这两项自身，不波及其他测试 */
	I3C_TEST_SETAASA,
	I3C_TEST_SETDASA,
	I3C_TEST_COUNT,
};

static const char * const i3c_test_names[I3C_TEST_COUNT] = {
	[I3C_TEST_DAA_DISCOVERY] = "daa_discovery",
	[I3C_TEST_WRITE_8] = "write_8",
	[I3C_TEST_WRITE_100] = "write_100",
	[I3C_TEST_READ_32_DIRECT] = "read_32_direct",
	[I3C_TEST_WRITE_READ_1] = "write_read_1",
	[I3C_TEST_WRITE_READ_15] = "write_read_15",
	[I3C_TEST_WRITE_READ_16] = "write_read_16",
	[I3C_TEST_WRITE_READ_17] = "write_read_17",
	[I3C_TEST_WRITE_READ_32] = "write_read_32",
	[I3C_TEST_IBI] = "ibi",
	[I3C_TEST_HOT_JOIN] = "hot_join",
	[I3C_TEST_CCC_BASIC] = "ccc_basic",
	[I3C_TEST_WRITE_STOP_READ_1] = "write_stop_read_1",
	[I3C_TEST_WRITE_STOP_READ_15] = "write_stop_read_15",
	[I3C_TEST_WRITE_STOP_READ_16] = "write_stop_read_16",
	[I3C_TEST_WRITE_STOP_READ_17] = "write_stop_read_17",
	[I3C_TEST_WRITE_STOP_READ_32] = "write_stop_read_32",
	[I3C_TEST_READ_UNDERRUN_8_16] = "read_underrun_8_16",
	[I3C_TEST_READ_UNDERRUN_16_32] = "read_underrun_16_32",
	[I3C_TEST_NACK_BAD_ADDR] = "nack_bad_addr",
	[I3C_TEST_BACK_TO_BACK_8X8] = "back_to_back_8x8",
	[I3C_TEST_BIG_256] = "big_256",
	[I3C_TEST_CCC_RAPID_8] = "ccc_rapid_8",
	[I3C_TEST_READ_MASTER_ABORT] = "read_master_abort",
	[I3C_TEST_IBI_NEG_PATHS] = "ibi_neg_paths",
	[I3C_TEST_API_NEG_PATHS] = "api_neg_paths",
	[I3C_TEST_MULTI_MSG_4] = "multi_msg_4",
	[I3C_TEST_WRITE_INDEXED_READ_16] = "write_indexed_read_16",
	[I3C_TEST_WRITE_DELAY_READ_16] = "write_delay_read_16",
	[I3C_TEST_WRITE_DELAY_READ_LEN] = "write_delay_read_len",
	[I3C_TEST_READ_UNDERRUN_STRESS] = "read_underrun_stress",
	[I3C_TEST_MULTI_MSG_SHORT_READ] = "multi_msg_short_read",
	[I3C_TEST_SHORT_READ_THEN_NACK] = "short_read_then_nack",
	[I3C_TEST_SETAASA] = "setaasa",
	[I3C_TEST_SETDASA] = "setdasa",
};

/*
 * 测试分块开关：置 0 可在编译期裁掉对应测试，减少一次性跑出的波形数量，
 * 便于示波器/逻辑分析仪上定位某个测试的波形。
 *
 * 只影响 controller 侧注册的 ZTEST 数量；target 侧是纯命令驱动
 * （收到哪个测试的 CMD_TEST_START/END 就跑哪个校验），无需改动。
 * 修改本头文件后 test.sh 会同时重编双核，两端天然一致。
 *
 * 注意：ccc_basic 末尾的 DISEC(EVT_ALL) 会关闭 target 的 IBI/HJ 事件，
 * 若单独分块跑 ibi/hot_join 而不跑 ccc_basic，行为不受影响；
 * 若一起跑，controller 会在 ibi/hot_join 测试前重新 ENEC。
 */
/*
 * 测试开关：一个测试项一个宏，1 = 注册运行，0 = 直接不跑。
 * 只影响 controller 侧注册的 ZTEST 数量；target 侧是纯命令驱动
 * （收到哪个测试的 CMD_TEST_START/END 就跑哪个校验），无需改动。
 * 修改本头文件后 test.sh 会同时重编双核，两端天然一致。
 *
 * 注意：ccc_basic 末尾的 DISEC(EVT_ALL) 会关闭 target 的 IBI/HJ 事件，
 * 若单独分块跑 ibi/hot_join 而不跑 ccc_basic，行为不受影响；
 * 若一起跑，controller 会在 ibi/hot_join 测试前重新 ENEC。
 */
/* 全量测试开关 */
#define I3C_TEST_EN_CCC_BASIC          1
#define I3C_TEST_EN_DAA_DISCOVERY      1
#define I3C_TEST_EN_WRITE_8            1
#define I3C_TEST_EN_WRITE_100          1
#define I3C_TEST_EN_READ_32_DIRECT     1
#define I3C_TEST_EN_WRITE_READ_1       1
#define I3C_TEST_EN_WRITE_READ_15      1
#define I3C_TEST_EN_WRITE_READ_16      1
#define I3C_TEST_EN_WRITE_READ_17      1
#define I3C_TEST_EN_WRITE_READ_32      1
#define I3C_TEST_EN_IBI                1
#define I3C_TEST_EN_HOT_JOIN           1
#define I3C_TEST_EN_WRITE_STOP_READ_1  1
#define I3C_TEST_EN_WRITE_STOP_READ_15 1
#define I3C_TEST_EN_WRITE_STOP_READ_16 1
#define I3C_TEST_EN_WRITE_STOP_READ_17 1
#define I3C_TEST_EN_WRITE_STOP_READ_32 1
#define I3C_TEST_EN_READ_UNDERRUN_8_16  1
#define I3C_TEST_EN_READ_UNDERRUN_16_32 1
#define I3C_TEST_EN_NACK_BAD_ADDR       1
#define I3C_TEST_EN_BACK_TO_BACK_8X8    1
#define I3C_TEST_EN_BIG_256             1
#define I3C_TEST_EN_CCC_RAPID_8         1
#define I3C_TEST_EN_READ_MASTER_ABORT   1
#define I3C_TEST_EN_IBI_NEG_PATHS       1
#define I3C_TEST_EN_API_NEG_PATHS       1
#define I3C_TEST_EN_MULTI_MSG_4         1
#define I3C_TEST_EN_WRITE_INDEXED_READ_16 1
#define I3C_TEST_EN_WRITE_DELAY_READ_16   1
#define I3C_TEST_EN_WRITE_DELAY_READ_LEN  1
#define I3C_TEST_EN_READ_UNDERRUN_STRESS  1
#define I3C_TEST_EN_MULTI_MSG_SHORT_READ  1
#define I3C_TEST_EN_SHORT_READ_THEN_NACK  1
#define I3C_TEST_EN_SETAASA             1
#define I3C_TEST_EN_SETDASA             1

#ifdef __cplusplus
}
#endif

#endif /* I3C_DUALCORE_TEST_PROTOCOL_H */
