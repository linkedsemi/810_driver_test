# ztest_unit_test 链接序 = 字典序（非源码序，含尾下划线坑）

## 现象
按"ztest 注册的源码顺序"假设第 N 项，导致与运行顺序 / GDB 内存兜底结果**错位**（张冠李戴）——
全 PASS 时潜伏，出现 FAIL 才暴露。

## 根因
链接器对 `ztest_unit_test` 段 `SORT_BY_NAME` → 注册顺序按**测试名字典序**，不是源码序。
坑中坑：`TYPE_SECTION_ITERABLE` 给段名追加**尾下划线**，`SORT_BY_NAME` 比较完整段名——数字字符
`'0'-'9'`(0x30-0x39) 排在 `'_'`(0x5F) 之前，裸名字典序可能与真实链接序**相反**（如 st_sha3 vs
st_sha384：链接序 sha384 在前，plain nm 名字典序却显示 sha3 在前）。

## 判定 / 处理
- 每工程用 `nm --numeric-sort <elf> | grep ztest_unit_test`（按地址序 = 链接序），或对照
  `zephyr.map` 的 `._ztest_unit_test.static.*` 段序核实；
- **勿用 plain `nm` 名字典序输出**（会被尾下划线欺骗）；
- `TEST_NAMES`（dump_results.py）/ glue 记账 idx 按链接序对齐并注释 SORT_BY_NAME；
- **增删测试项后重新核对**。

## 关联
- 810_driver_test CLAUDE.md §10.27；dump_results.py 的 TEST_NAMES 注释
