# i2c_pure_test 测试报告
- 日期: 2026-09-02 10:55  板卡: lsqsh_evb@2os  脚本: run_i2c_pure_tests.py
- 环境: 见 810_driver_test/README.md 外设接线记录（涉及外部路径的测试项在 COVERAGE.md 标注，CLAUDE.md §14）
- case 数: 1  总测试项: 1  通过: 0  失败: 1

## case_pure_i2c_400k (FAIL)
| 测试项 | 预期 | 实际(INFO) | 结果 | 来源 | reason |
|--------|------|-----------|------|------|--------|
| i2c_wave | 100 轮 ×（写 16B 0x00..0x0F + 读 16B 0xAA..0xB9）全部匹配；轮间 100ms 静默（示波器单次触发抓拍）；master INFO 上报 rounds=100,delay_ms=100,rate_khz=400；slave INFO 上报 writes/bytes/reads 计数 | CTRL:bytes=1600, reads=1701, writes=100 | FAIL | mem |  |

## 汇总

OVERALL: FAIL
