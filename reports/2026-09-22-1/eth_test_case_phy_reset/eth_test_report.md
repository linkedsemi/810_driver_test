# eth_test 测试报告

- 工程: eth_test
- case: case_phy_reset
- 日期: 2026-09-22 17:52:14
- 板级: lsqsh_evb@1os/lsqsh/cpu1
- 环境: 见 810_driver_test/README.md 外设接线记录（涉及外部路径的测试项在 COVERAGE.md 标注）
- 执行标记: UNKNOWN
- 内存兜底: 不可用（内存兜底未执行） — 内存兜底未执行（gdb/dump 失败、超时或符号解析失败）。
- 结果: FAIL (0/0)

> 证据缺失（非驱动失败）：5 项已声明测试项无确定结果（token 与内存兜底都没有值）—— eth_dev_ready、eth_phy_link_state、mdio_invalid_args、mdio_phy_id、mdio_reg_rw

## 测试项结果

| 测试项 | 结果 | 来源 | 期望 | 实测(INFO) | 失败原因 |
|---|---|---|---|---|---|

## 汇总

OVERALL: FAIL
