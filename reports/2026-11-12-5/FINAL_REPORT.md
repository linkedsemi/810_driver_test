# 全工程回归报告（WSL）

- 会话: `wsl_environment/reports/2026-11-12-5`
- 日期: 2026-11-13 19:22:56
- 串口: TGT/安全核=COM31，CTRL/业务核=COM14
- DebugServer: 172.24.48.1:1025

## 结论: **ALL PASS**

- 共 75 个 case：PASS 75，FAIL 0，SKIP 0

| 工程 | case | 结果 | 通过/总数 | 报告/日志 |
|---|---|---|---|---|
| adc_temper_test | case_default | PASS | - | [报告](adc_temper_test_case_default/adc_temper_test_report.md) [日志](logs/adc_temper_test_case_default.log) |
| crypto_api_test | case_default | PASS | 44/44 | [报告](crypto_api_test_case_default/crypto_api_test_report.md) [日志](logs/crypto_api_test_case_default.log) |
| dma_test | case_default | PASS | - | [报告](dma_test_case_default/dma_test_report.md) [日志](logs/dma_test_case_default.log) |
| dma_test | case_destructive | PASS | - | [报告](dma_test_case_destructive/dma_test_report.md) [日志](logs/dma_test_case_destructive.log) |
| flash_test | case_default | PASS | - | [报告](flash_test_case_default/flash_test_report.md) [日志](logs/flash_test_case_default.log) |
| iopmp_test | case_default | PASS | - | [报告](iopmp_test_case_default/iopmp_test_report.md) [日志](logs/iopmp_test_case_default.log) |
| pwm_test | case_default | PASS | - | [报告](pwm_test_case_default/pwm_test_report.md) [日志](logs/pwm_test_case_default.log) |
| qspi_flash_test | case_default | PASS | - | [报告](qspi_flash_test_case_default/qspi_flash_test_report.md) [日志](logs/qspi_flash_test_case_default.log) |
| qspi_flash_test | case_no_calib | PASS | - | [报告](qspi_flash_test_case_no_calib/qspi_flash_test_report.md) [日志](logs/qspi_flash_test_case_no_calib.log) |
| spi_test | case_default | PASS | 29/29 | [报告](spi_test_case_default/spi_test_report.md) [日志](logs/spi_test_case_default.log) |
| spif_test | case_default | PASS | 24/24 | [报告](spif_test_case_default/spif_test_report.md) [日志](logs/spif_test_case_default.log) |
| spi_spif_test | case_default | PASS | 54/54 | [报告](spi_spif_test_case_default/spi_spif_test_report.md) [日志](logs/spi_spif_test_case_default.log) |
| spid_test | case_default | PASS | 10/10 | [报告](spid_test_case_default/spid_test_report.md) [日志](logs/spid_test_case_default.log) |
| i2cf_test | case_default | PASS | 18/18 | [报告](i2cf_test_case_default/i2cf_test_report.md) [日志](logs/i2cf_test_case_default.log) |
| i2cf_test | case_enum_matrix | PASS | 19/19 | [报告](i2cf_test_case_enum_matrix/i2cf_test_report.md) [日志](logs/i2cf_test_case_enum_matrix.log) |
| i2cf_test | case_scan_burst | PASS | 19/19 | [报告](i2cf_test_case_scan_burst/i2cf_test_report.md) [日志](logs/i2cf_test_case_scan_burst.log) |
| trng_test | case_default | PASS | 24/24 | [报告](trng_test_case_default/trng_test_report.md) [日志](logs/trng_test_case_default.log) |
| otbn_test | case_default | PASS | 52/52 | [报告](otbn_test_case_default/otbn_test_report.md) [日志](logs/otbn_test_case_default.log) |
| otbn_keygen_test | case_default | PASS | 27/27 | [报告](otbn_keygen_test_case_default/otbn_keygen_report.md) [日志](logs/otbn_keygen_test_case_default.log) |
| otbn_selftest | case_default | PASS | 30/30 | [报告](otbn_selftest_case_default/otbn_selftest_report.md) [日志](logs/otbn_selftest_case_default.log) |
| rtc_ls_test | case_default | PASS | 10/10 | [报告](rtc_ls_test_case_default/rtc_ls_test_report.md) [日志](logs/rtc_ls_test_case_default.log) |
| rtc_timer_ls_test | case_default | PASS | 15/15 | [报告](rtc_timer_ls_test_case_default/rtc_timer_ls_test_report.md) [日志](logs/rtc_timer_ls_test_case_default.log) |
| mbedtls_test | case_sw_baseline | PASS | 35/35 | [报告](mbedtls_test_case_sw_baseline/mbedtls_test_report.md) [日志](logs/mbedtls_test_case_sw_baseline.log) |
| mbedtls_test | case_otbn_ecc | PASS | 45/45 | [报告](mbedtls_test_case_otbn_ecc/mbedtls_test_report.md) [日志](logs/mbedtls_test_case_otbn_ecc.log) |
| mbedtls_test | case_hw_otbn_mix | PASS | 49/49 | [报告](mbedtls_test_case_hw_otbn_mix/mbedtls_test_report.md) [日志](logs/mbedtls_test_case_hw_otbn_mix.log) |
| mbedtls_test | case_hw_all | PASS | 42/42 | [报告](mbedtls_test_case_hw_all/mbedtls_test_report.md) [日志](logs/mbedtls_test_case_hw_all.log) |
| mbedtls_test | case_otbn_hash | PASS | 46/46 | [报告](mbedtls_test_case_otbn_hash/mbedtls_test_report.md) [日志](logs/mbedtls_test_case_otbn_hash.log) |
| mbedtls_test | case_otbn_rsa | PASS | 45/45 | [报告](mbedtls_test_case_otbn_rsa/mbedtls_test_report.md) [日志](logs/mbedtls_test_case_otbn_rsa.log) |
| mbedtls_test | case_otbn_all | PASS | 48/48 | [报告](mbedtls_test_case_otbn_all/mbedtls_test_report.md) [日志](logs/mbedtls_test_case_otbn_all.log) |
| mbedtls_test | case_otbn_sha256_hw_sm3 | PASS | 48/48 | [报告](mbedtls_test_case_otbn_sha256_hw_sm3/mbedtls_test_report.md) [日志](logs/mbedtls_test_case_otbn_sha256_hw_sm3.log) |
| mbedtls_test | case_sw_hw_mix | PASS | 40/40 | [报告](mbedtls_test_case_sw_hw_mix/mbedtls_test_report.md) [日志](logs/mbedtls_test_case_sw_hw_mix.log) |
| mbedtls_test | case_all_on_max | PASS | 54/54 | [报告](mbedtls_test_case_all_on_max/mbedtls_test_report.md) [日志](logs/mbedtls_test_case_all_on_max.log) |
| sec_iwdg_cpu1_test | case_non_destructive | PASS | - | [报告](sec_iwdg_cpu1_test_case_non_destructive/sec_iwdg_cpu1_test_report.md) [日志](logs/sec_iwdg_cpu1_test_case_non_destructive.log) |
| sec_iwdg_cpu1_test | case_boundary | PASS | - | [报告](sec_iwdg_cpu1_test_case_boundary/sec_iwdg_cpu1_test_report.md) [日志](logs/sec_iwdg_cpu1_test_case_boundary.log) |
| i2c_pure_test | case_pure_i2c_400k | PASS | - | [报告](i2c_pure_test_case_pure_i2c_400k/i2c_pure_test_report.md) [日志](logs/i2c_pure_test_case_pure_i2c_400k.log) |
| i2c_pure_test | case_pure_i2c_alt_100k_400k | PASS | - | [报告](i2c_pure_test_case_pure_i2c_alt_100k_400k/i2c_pure_test_report.md) [日志](logs/i2c_pure_test_case_pure_i2c_alt_100k_400k.log) |
| i3c_i2c_switch_test | case_switch_core | PASS | - | [报告](i3c_i2c_switch_test_case_switch_core/i3c_i2c_switch_test_report.md) [日志](logs/i3c_i2c_switch_test_case_switch_core.log) |
| i3c_i2c_switch_test | case_no_bus_init | PASS | - | [报告](i3c_i2c_switch_test_case_no_bus_init/i3c_i2c_switch_test_report.md) [日志](logs/i3c_i2c_switch_test_case_no_bus_init.log) |
| i3c_dualcore_test | case_mixed_1m_12m5 | PASS | - | [报告](i3c_dualcore_test_case_mixed_1m_12m5/i3c_dualcore_test_report.md) [日志](logs/i3c_dualcore_test_case_mixed_1m_12m5.log) |
| i3c_dualcore_test | case_mixed_1m_6m25 | PASS | - | [报告](i3c_dualcore_test_case_mixed_1m_6m25/i3c_dualcore_test_report.md) [日志](logs/i3c_dualcore_test_case_mixed_1m_6m25.log) |
| i3c_dualcore_test | case_mixed_400k_12m5 | PASS | - | [报告](i3c_dualcore_test_case_mixed_400k_12m5/i3c_dualcore_test_report.md) [日志](logs/i3c_dualcore_test_case_mixed_400k_12m5.log) |
| i3c_dualcore_test | case_mixed_400k_1m | PASS | - | [报告](i3c_dualcore_test_case_mixed_400k_1m/i3c_dualcore_test_report.md) [日志](logs/i3c_dualcore_test_case_mixed_400k_1m.log) |
| i3c_dualcore_test | case_mixed_400k_6m25 | PASS | - | [报告](i3c_dualcore_test_case_mixed_400k_6m25/i3c_dualcore_test_report.md) [日志](logs/i3c_dualcore_test_case_mixed_400k_6m25.log) |
| i3c_dualcore_test | case_pure_i3c_12m5 | PASS | - | [报告](i3c_dualcore_test_case_pure_i3c_12m5/i3c_dualcore_test_report.md) [日志](logs/i3c_dualcore_test_case_pure_i3c_12m5.log) |
| i3c_dualcore_test | case_pure_i3c_6m25 | PASS | - | [报告](i3c_dualcore_test_case_pure_i3c_6m25/i3c_dualcore_test_report.md) [日志](logs/i3c_dualcore_test_case_pure_i3c_6m25.log) |
| otbn_delegation_test | case_default | PASS | - | [报告](otbn_delegation_test_case_default/otbn_delegation_test_report.md) [日志](logs/otbn_delegation_test_case_default.log) |
| otbn_delegation_test | case_dead_channel | PASS | - | [报告](otbn_delegation_test_case_dead_channel/otbn_delegation_test_report.md) [日志](logs/otbn_delegation_test_case_dead_channel.log) |
| otbn_delegation_test | case_no_server | PASS | - | [报告](otbn_delegation_test_case_no_server/otbn_delegation_test_report.md) [日志](logs/otbn_delegation_test_case_no_server.log) |
| otbn_delegation_test | case_throughput | PASS | - | [报告](otbn_delegation_test_case_throughput/otbn_delegation_test_report.md) [日志](logs/otbn_delegation_test_case_throughput.log) |
| otbn_delegation_test | case_throughput_iso_proxy | PASS | - | [报告](otbn_delegation_test_case_throughput_iso_proxy/otbn_delegation_test_report.md) [日志](logs/otbn_delegation_test_case_throughput_iso_proxy.log) |
| otbn_delegation_test | case_throughput_iso_local | PASS | - | [报告](otbn_delegation_test_case_throughput_iso_local/otbn_delegation_test_report.md) [日志](logs/otbn_delegation_test_case_throughput_iso_local.log) |
| otbn_delegation_test | case_throughput_iso_local_hwrng | PASS | - | [报告](otbn_delegation_test_case_throughput_iso_local_hwrng/otbn_delegation_test_report.md) [日志](logs/otbn_delegation_test_case_throughput_iso_local_hwrng.log) |
| otbn_delegation_test | case_entropy_iso | PASS | - | [报告](otbn_delegation_test_case_entropy_iso/otbn_delegation_test_report.md) [日志](logs/otbn_delegation_test_case_entropy_iso.log) |
| otbn_delegation_test | case_seq_soak | PASS | - | [报告](otbn_delegation_test_case_seq_soak/otbn_delegation_test_report.md) [日志](logs/otbn_delegation_test_case_seq_soak.log) |
| otbn_delegation_test | case_explicit_api | PASS | - | [报告](otbn_delegation_test_case_explicit_api/otbn_delegation_test_report.md) [日志](logs/otbn_delegation_test_case_explicit_api.log) |
| otbn_delegation_test | case_alt_via_proxy | PASS | - | [报告](otbn_delegation_test_case_alt_via_proxy/otbn_delegation_test_report.md) [日志](logs/otbn_delegation_test_case_alt_via_proxy.log) |
| otbn_delegation_test | case_rsa_delegation | PASS | - | [报告](otbn_delegation_test_case_rsa_delegation/otbn_delegation_test_report.md) [日志](logs/otbn_delegation_test_case_rsa_delegation.log) |
| otbn_delegation_test | case_slot | PASS | - | [报告](otbn_delegation_test_case_slot/otbn_delegation_test_report.md) [日志](logs/otbn_delegation_test_case_slot.log) |
| app_iwdg_cpu2_test | case_non_destructive | PASS | - | [报告](app_iwdg_cpu2_test_case_non_destructive/app_iwdg_cpu2_test_report.md) [日志](logs/app_iwdg_cpu2_test_case_non_destructive.log) |
| app_iwdg_cpu2_test | case_quick_enable | PASS | - | [报告](app_iwdg_cpu2_test_case_quick_enable/app_iwdg_cpu2_test_report.md) [日志](logs/app_iwdg_cpu2_test_case_quick_enable.log) |
| app_iwdg_cpu2_test | case_quick_invalid | PASS | - | [报告](app_iwdg_cpu2_test_case_quick_invalid/app_iwdg_cpu2_test_report.md) [日志](logs/app_iwdg_cpu2_test_case_quick_invalid.log) |
| mbox_test | case_mbox_basic | PASS | 5/5 | [报告](mbox_test_case_mbox_basic/mbox_test_report.md) [日志](logs/mbox_test_case_mbox_basic.log) |
| mbox_test | case_mbox_big_crc | PASS | 4/4 | [报告](mbox_test_case_mbox_big_crc/mbox_test_report.md) [日志](logs/mbox_test_case_mbox_big_crc.log) |
| mbox_test | case_mbox_consumers | PASS | 12/12 | [报告](mbox_test_case_mbox_consumers/mbox_test_report.md) [日志](logs/mbox_test_case_mbox_consumers.log) |
| mbox_test | case_mbox_invalid | PASS | 7/7 | [报告](mbox_test_case_mbox_invalid/mbox_test_report.md) [日志](logs/mbox_test_case_mbox_invalid.log) |
| mbox_test | case_mbox_rtc_sync | PASS | 4/4 | [报告](mbox_test_case_mbox_rtc_sync/mbox_test_report.md) [日志](logs/mbox_test_case_mbox_rtc_sync.log) |
| mbox_test | case_mbox_stress | PASS | 3/3 | [报告](mbox_test_case_mbox_stress/mbox_test_report.md) [日志](logs/mbox_test_case_mbox_stress.log) |
| mbox_test | case_mbox_fifo_full | PASS | 2/2 | [报告](mbox_test_case_mbox_fifo_full/mbox_test_report.md) [日志](logs/mbox_test_case_mbox_fifo_full.log) |
| mbox_test | case_layout_check_on | PASS | 5/5 | [报告](mbox_test_case_layout_check_on/mbox_test_report.md) |
| mbox_test | case_layout_check_off | PASS | 5/5 | [报告](mbox_test_case_layout_check_off/mbox_test_report.md) |
| perf_test | case_default | PASS | - | [报告](perf_test_case_default/perf_test_report.md) [日志](logs/perf_test_case_default.log) |
| perf_test | case_layout_check_on | PASS | - | [报告](perf_test_case_layout_check_on/perf_test_report.md) |
| perf_test | case_layout_check_off | PASS | - | [报告](perf_test_case_layout_check_off/perf_test_report.md) |
| emmc_test | case_block_driver | PASS | - | [报告](emmc_test_case_block_driver/emmc_test_report.md) [日志](logs/emmc_test_case_block_driver.log) |
| emmc_test | case_block_pio | PASS | - | [报告](emmc_test_case_block_pio/emmc_test_report.md) [日志](logs/emmc_test_case_block_pio.log) |

## 失败项

- 无
