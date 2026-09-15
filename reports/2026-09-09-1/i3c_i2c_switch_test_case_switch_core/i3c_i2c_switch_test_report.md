# i3c_i2c_switch_test 测试报告
- 日期: 2026-09-09 13:06  板卡: lsqsh_evb@2os  脚本: run_i3c_i2c_switch_tests.py
- 环境: 见 810_driver_test/README.md 外设接线记录（涉及外部路径的测试项在 COVERAGE.md 标注，CLAUDE.md §14）
- case 数: 1  总测试项: 10  通过: 1  失败: 9

## case_switch_core (FAIL)
| 测试项 | 预期 | 实际(INFO) | 结果 | 来源 | reason |
|--------|------|-----------|------|------|--------|
| i2c_nack_bad_addr | 访问 0x70 返回非 0（预期 NACK） | - | PASS | mem |  |
| i2c_rw_100 | 写 0x00..0x63，读 0xAA..0x109 模 256 | - | FAIL | mem |  |
| i2c_rw_16 | 写 0x00..0x0F，读 0xAA..0xB9 | - | FAIL | mem |  |
| i2c_rw_17 | 写 0x00..0x10，读 0xAA..0xBA | - | FAIL | mem |  |
| i2c_rw_1 | 写 1B 读 1B（0xAA） | - | FAIL | mem |  |
| i3c_baseline | 写 0x00..0x0F 16B；读 0xAA..0xB9 16B | - | FAIL | mem |  |
| pinctrl_states | state_count==2 且 states[1].id==PINCTRL_STATE_PRIV_START | - | FAIL | mem |  |
| switch_i2c_to_i3c | 切回后 DAA 重新发现 target，16B 写读一致 | - | FAIL | mem |  |
| switch_i3c_to_i2c | 切换后 i2c5↔i2c6 回连 16B 写读（target 侧 100ms 延时先行） | - | FAIL | mem |  |
| switch_stress | 5 轮全部通过（同 id 多轮失败累计） | - | FAIL | mem |  |

## 汇总

OVERALL: FAIL
