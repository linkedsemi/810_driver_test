/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * 循环波形发生器入口（示波器抓取 read/write 事务用）。
 * 仅 CONFIG_I2CF_WAVE_GEN=y 时编入。
 */

#ifndef I2CF_WAVE_H_
#define I2CF_WAVE_H_

/* 进入永不返回的循环波形发生器；返回 void 契约上不返回。 */
void i2cf_wave_gen_run(void);

#endif /* I2CF_WAVE_H_ */
