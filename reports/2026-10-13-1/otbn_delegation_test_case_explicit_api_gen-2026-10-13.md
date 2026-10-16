# otbn_delegation_test — 新增 case `case_explicit_api`（生成 + 编译）2026-10-13

**范围**：本轮**只生成 + 双核 pristine 编译**；板子物理离线（CKLink 不在、板载双串口缺席），
**未上板**。所有"上板预期"均为**预测**，板子恢复后需证伪。

**结论（一句话）**：新 case `case_explicit_api` 已入库并双核编译零错误；它相对 `case_default`
**只新增 1 个真运行时项 `t16d`**（同镜像转发桩存活证），其余 24 项是**同一份测试体同名的重复覆盖**，
仅把 client 的调用目标从 `mbedtls_ecdsa_*` 换成 `mbedtls_ls_otbn_ecdsa_*` —— 因为转发桩是 1 行
`return`，被执行的代码基本同一条，**功能独立价值有限**（详见 §3 诚实性）。

---

## 1. 被测假设的实证核对（都在源码里坐实，非引用任务书）

被测源码 `zephyr/soc/linkedsemi/lsqsh/otbn/mbedtls/mbedtls_otbn_crypto_mbox.c`（13:43 修订版）：

| 任务书假设 | 实证 | 位置 |
|---|---|---|
| 三个代理函数体改名为 `mbedtls_ls_otbn_ecdsa_genkey/_sign/_verify` | ✅ | `.c` L158 / L239 / L311 |
| 原 `mbedtls_ecdsa_genkey/sign/verify` 保留为薄转发 | ✅（各 1 行 `return mbedtls_ls_otbn_ecdsa_*(...)`） | `.c` L384 / L390 / L397 |
| 新头文件声明 3 接口 + 2 个 init | ✅ | `mbedtls_otbn_crypto_mbox.h` L38 / L45 / L63 / L78 / L94 |
| 目录在 `zephyr_include_directories(mbedtls/.)` 内 | ✅ | `otbn/CMakeLists.txt` |

**对任务书的一处更正**：任务书称"白名单外曲线的 `-EIO` 早退路径是**显式名独有的分支**"——
**不成立**。`mbedtls_ecdsa_genkey` 转发桩直达同一函数体（`.c` L387），故 t10 通过**两个名字都能**
命中同一条 `-EIO`（`.c` L181）。即该负例不是显式名独有，两 case 都覆盖它。

---

## 2. 交付物与实现

### 2.1 新增/修改文件

| 文件 | 动作 | 说明 |
|---|---|---|
| `tests/src/otbn_delegation_test.c` | 改（**共享体**） | 顶部加 `DELEG_GENKEY/SIGN/VERIFY` 重定向宏（默认 = `mbedtls_ecdsa_*`；`OTBN_DELEG_EXPLICIT_API` + CLIENT = 显式名）；16 处调用点改走宏；末尾**新增** `t16d`（`#ifdef OTBN_DELEG_EXPLICIT_API`） |
| `cpu2_controller_otbn_delegation_test/CMakeLists.txt` | 改（只增） | `if(OTBN_DELEG_EXPLICIT_API) target_compile_definitions(...)` |
| `cpu1_target_otbn_delegation_test/CMakeLists.txt` | 改（只增） | 同上 |
| `tests/case_explicit_api/test.sh` | 新增 | 三模式；两核均传 `-DOTBN_DELEG_EXPLICIT_API=1` |
| `tests/case_explicit_api/{controller,target}.{conf,overlay}` | 新增 | 无额外 Kconfig/DT 差异 |
| `tests/cases.yaml` | 改 | 登记 `- case_explicit_api` |
| `tests/run_test.py` / `tests/scripts/dump_results.py` | 改 | 各同步 `TEST_NAMES_BY_CASE["case_explicit_api"]`（25 项） |
| `tests/COVERAGE.md` | 改 | case 清单 + 矩阵（t16d 行）+ expect 块 |

未改 `regression/cases.yaml`（工程级已登记）、未改 `case_default` 目录、未改其余 4 个 case 目录。
`git checkout/restore/stash/reset` 全程未用。

### 2.2 设计要点

- **两核都传 `-DOTBN_DELEG_EXPLICIT_API=1`**：重定向宏的 guard 是
  `defined(OTBN_DELEG_EXPLICIT_API) && defined(CONFIG_..._CLIENT)`，故**服务端仍用 `mbedtls_ecdsa_*`**
  （服务端镜像压根没有显式入口）。两核都传是为了让 `t16d` 在**双核都注册**（同名校验要求），
  服务端该项 `ztest_test_skip()`。
- **`t16d` 的 guard 用 `#ifdef OTBN_DELEG_EXPLICIT_API`**：保证 `case_default` 的 24 项**一项不增**
  （它的构建不传该宏）。
- **无需 CMake 换源文件**：复用 `case_throughput` 的 `EXTRA_CMAKE_ARGS` 透传口（`#70`），
  只加一段默认关闭的 `target_compile_definitions`（对既有 case 零影响）。

---

## 3. 诚实性：本 case 相对 `case_default` 到底新增了什么

| 类别 | 项 | 相对 case_default |
|---|---|---|
| **新增（真运行时价值）** | `t16d_mbedtls_and_explicit_agree` | ✅ 唯一新项：**同一 client 镜像里**先 `mbedtls_ecdsa_genkey`（转发桩）再 `mbedtls_ls_otbn_ecdsa_genkey`（显式），**两者都要成功**。这是**运行期**证明转发桩活着；既有 case 都是"同一份输入只走一条路"，任何 case 都没有同镜像双名对照。 |
| **新增（编译期价值）** | 应用侧 `#include "mbedtls_otbn_crypto_mbox.h"` | ✅ 证明新头文件在应用 TU 可用、三接口的声明与定义名/签名一致（编译期）。 |
| **新增（链接期价值）** | client 镜像**直接引用**显式符号 | ✅ 反汇编可见 `jal <mbedtls_ls_otbn_ecdsa_*>`（§4）。证明新公开 API 真实存在且被调用。 |
| **重复（同名同断言）** | `t01..t16c, t17, t19, t21, t80, t88, t89` = **24 项** | ⚠️ **与 `case_default` 逐项同名同断言**；唯一差别是 client 侧调用目标从 `mbedtls_ecdsa_*` 改为 `mbedtls_ls_otbn_ecdsa_*`。因转发桩是 **1 行 `return`**，被执行的代码路径基本相同 ⇒ **这 24 项对 `case_default` 的功能覆盖是重复的**。 |

**我的判断**：`case_explicit_api` 的**独立价值集中在 `t16d` 一项 + 编译/链接期证明**；
其余 24 项属"同覆盖、换入口名"的重复。**不是为了凑覆盖而造** —— 但若目标只是"覆盖度增量"，
它的边际价值确实有限；它的主要意义是**合同/接口层面的验收**（新显式入口 + 新头文件对外可用、
转发桩在运行期确实活着），而非新增功能路径。

**一处需要任务方复核的设计取舍**：`t16d` 在服务端只能 `SKIP`（服务端镜像无显式入口）。
810 双核判据要求"同名项双核 PASS"，此处服务端为 SKIP（判据把它当**确定结果**、不计分母、
不判死 case）。这是本工程既有先例（`case_dead_channel` 客户端功能项同样 SKIP）。若任务方
希望"零 SKIP"，则 `t16d` 无法纳入双核同名判据体系——**本设计是可行性与判据约束下的折中**。

---

## 4. 编译证据（本轮硬证据）

### 4.1 双核 pristine 编译零错误

`bash tests/case_explicit_api/test.sh`（= build-only 模式，`-p always` 全量）：
- controller：`[1289/1289]` → `Linking C executable zephyr.elf` → 打印 launch.json；
- target：`[1301/1301]` → `Linking C executable zephyr.elf`；
- 全日志 `grep -iE "error:|FAILED:|ninja: error|undefined reference|Build failed"` **零命中**。

### 4.2 真重编译（mtime）与产物

```
src/otbn_delegation_test.c              14:29:05
cpu2_controller_.../CMakeLists.txt      14:29:09
case_explicit_api/build/controller/zephyr/zephyr.elf   14:31:11   ← 晚于源
case_explicit_api/build/target/zephyr/zephyr.elf       14:32:40   ← 晚于源
```
（`-p always` 全量重建，非命中旧产物。）

### 4.3 client 镜像**确实调用** `mbedtls_ls_otbn_ecdsa_*`（nm + objdump）

```
CONTROLLER(client) ELF:
  10003f1a T mbedtls_ls_otbn_ecdsa_genkey
  1000405a T mbedtls_ls_otbn_ecdsa_sign
  100041e8 T mbedtls_ls_otbn_ecdsa_verify
  10004384 T mbedtls_ecdsa_genkey            ← 转发桩仍在（t16d 引用它）

TARGET(server) ELF:
  100d185c T mbedtls_ecdsa_genkey
  100d1546 T mbedtls_ecdsa_sign
  100d16ea T mbedtls_ecdsa_verify            ← 无任何 mbedtls_ls_otbn_ecdsa_*（符合设计）
```
- 全反汇编中 `mbedtls_ls_otbn_ecdsa` 出现 **47 次**（矩阵项直接 `jal` 显式名）。
- **`t16d` 反汇编（client，逐条坐实"同镜像双名"）**：
  ```
  _otbn_deleg_t16d_mbedtls_and_explicit_agree_wrapper:
    ... jal 10004384 <mbedtls_ecdsa_genkey>            ← 转发桩
    ... jal 10003f1a <mbedtls_ls_otbn_ecdsa_genkey>    ← 显式
    ... seqz + z_zassert（两 rc 都必须 0）+ mbedtls_ecp_check_pubkey
  ```
- **`t16d` 反汇编（server，证实 SKIP）**：`j ztest_test_skip`。
- **`t01` 对照（证明两 case 真的走了不同入口）**：
  - `case_default` client t01 → `jal 10004262 <mbedtls_ecdsa_genkey>`（转发桩）
  - `case_explicit_api` client t01 → `jal 10003f1a <mbedtls_ls_otbn_ecdsa_genkey>`（显式）

### 4.4 三份项清单一致（qsh-test-validate §1.10-n）

固件 `ZTEST` 数 25（含 t16d）== `run_test.py` 25 == `dump_results.py` 25，且顺序与
`nm --numeric-sort` 链接序**逐项相符**（t16d 落在 t16c / t17 之间）。

### 4.5 `case_default` 未回归（共享体改动的爆炸半径检查）

`case_default` 双核 `west build -p auto` 增量重建成功（各 14 步，仅重编改动的 TU + 重链）：
- item 数仍 **24**，`t16d` **不存在**；
- t01 wrapper 仍 `jal mbedtls_ecdsa_genkey`（默认宏展开为原名，行为不变）。

---

## 5. 上板预期（**预测，均未验证**，供板子恢复后证伪）

`case_explicit_api` 的 client 除调用入口名外与 `case_default` 同源同断言，服务端镜像与
`case_default` 服务端**逐字节等价**（除 `t16d` 的 skip 分支）⇒ **预测逐项结果 = `case_default`
的既有结果**（COVERAGE.md §1）：**OVERALL = FAIL**（产品缺陷族未修）。

| 项 | client(cpu2) | server(cpu1) | 预测 |
|---|---|---|---|
| t01–t16c（24 项） | PASS（经显式名） | PASS（本地） | **PASS**（16c 双核） |
| **t16d** | PASS（转发桩 + 显式各一次 + 公钥合法） | SKIP | **SKIP**（不计分母） |
| t17_sign_bad_d_zero | 无显式 FAIL token 则 MISSING | 无 FAIL token 则 MISSING | 依产品缺陷：**d=0 被接受 ⇒ rc=0 ⇒ 该项 FAIL**（= 既有缺陷） |
| t88_sign_bad_d_ge_n | 同上 | 同上 | 同上（**d≥n 被接受**） |
| t19 / t89（坏 Q） | panic 风险 | panic 风险 | **内核 panic ⇒ 套件被截断**（既有缺陷的次生后果） |
| t21_verify_wrong_keypair | — | panic 风险 | 同上 |
| t80_proxy_responds | 3 s 内返回 | — | **PASS**（server 应答，同 case_default） |

**证伪指引**：若上板后 `t16d` 不是"client PASS / server SKIP"，或 t01–t16c 出现非 PASS，
**先怀疑预测而非硬件** —— 预测依据是"转发桩 1 行 ⇒ 与 case_default 同路径"这一推断，
虽有反汇编支持，但**未经运行期验证**。

---

## 6. 我**未**验证的东西（显式标注）

1. **未上板**：无任何运行期结果；`t16d` 的 client PASS / server SKIP、以及"逐项 = case_default"
   全部是**预测**。
2. **运行期等价性**：只证明反汇编层"调用了显式名"；**未证明**显式名与转发桩在运行期
   返回逐位相同的结果（`t16d` 只断言两者各自 rc=0 且公钥合法，**未**比较两次输出的具体值）。
3. **服务端 `-DOTBN_DELEG_EXPLICIT_API=1` 的副作用**：仅验证"未引入显式符号 + t16d skip +
   item 数 25"；未穷举该宏在服务端是否还有其它编译期分支受影响（guard 设计上应无）。
4. **`case_dead_channel` / `case_no_server` 的共享体改动未重编验证**：预期同样零影响（默认宏展开
   为原名），但**只重编了 `case_default`** 作为爆炸半径代表。
5. **`.md`/`.json` 报告渲染**：本轮无 run，未产出回归报告；未验证 `t16d` 的 SKIP 在
   `judge.evaluate_case` 下的报告形态（按代码走 SKIP 路径，**未实测**）。

---

## 7. 留档

- 编译日志：`810_driver_test/tmp/otbn_explicit_build.log`
- 新 case 构建产物：`tests/case_explicit_api/build/{controller,target}/zephyr/zephyr.elf`
