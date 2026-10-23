/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * One-off diagnostic shell command: enqueue 200 deferred log messages from the
 * shell thread. The shell thread is also the consumer of the shell log backend,
 * so while this command runs it cannot drain that queue.
 *
 * Guarded by CONFIG_I2CF_LOG_FLOOD (default n): not compiled into any normal
 * case. Used by the log-backend pacing measurement only.
 */

#include <zephyr/kernel.h>
#include <zephyr/shell/shell.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(log_flood, LOG_LEVEL_INF);

#define LOG_FLOOD_COUNT 200

/* Long-message variant for the length-control run (L5): formatted text is
 * ~94 chars ("flood msg 199 " + 80 a's), close to the I2CF ERR line.
 */
#if defined(CONFIG_I2CF_LOG_FLOOD_LONG)
#define LOG_FLOOD_FMT "flood msg %d " 	"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
#else
#define LOG_FLOOD_FMT "flood msg %d"
#endif

static int cmd_log_flood(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(sh);
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	for (int i = 0; i < LOG_FLOOD_COUNT; i++) {
		LOG_ERR(LOG_FLOOD_FMT, i);
	}

	return 0;
}

SHELL_CMD_REGISTER(log_flood, NULL, "flood 200 deferred log msgs", cmd_log_flood);

/* Diagnostic-only variant: measures the loop duration itself (cycles) and
 * prints it directly through the shell transport (bypassing the log queue).
 * Used once to decide whether the LOG_ERR loop or the output path paces it.
 */
static int cmd_log_flood_timed(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);
	uint32_t t0 = k_cycle_get_32();

	for (int i = 0; i < LOG_FLOOD_COUNT; i++) {
		LOG_ERR(LOG_FLOOD_FMT, i);
	}

	uint32_t t1 = k_cycle_get_32();
	uint64_t us = (uint64_t)(t1 - t0) * 1000000ULL /
		      (uint64_t)CONFIG_SYS_CLOCK_HW_CYCLES_PER_SEC;

	shell_print(sh, "flood loop done: cycles=%u us=%llu", t1 - t0,
		    (unsigned long long)us);
	return 0;
}

SHELL_CMD_REGISTER(log_flood_timed, NULL, "timed flood 200 deferred log msgs",
		   cmd_log_flood_timed);
