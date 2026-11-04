/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Telemetry sender of the cpu2 perf peer, exported so the integrated image
 * (integrated/sets/itest_all/cpu2) can relay its own result lines to the cpu1
 * console through the same channel (perf_ch9).
 *
 * Each call becomes one line on cpu1's console, prefixed with
 * PERF_TEST:CTRL: by integrated/sets/itest_all/cpu1/src/perf_telemetry.c.
 */
#ifndef PERF_PEER_TELE_H
#define PERF_PEER_TELE_H

/* Send one line (no trailing newline needed) to the cpu1 relay. Blocks up to
 * ~1 s while the mbox channel is busy; drops the line if it stays busy. */
void tele_line(const char *fmt, ...);

#endif /* PERF_PEER_TELE_H */
