# 2026-11-15 第二窗口报告（09:36 → 10:40）：N12 覆盖率收复、gate4 两处缺陷、N13 第一步落地

仓库 `D:/TF2_Native_Worktrees/t105-e5`，分支 `candidate/e5`。窗口开始时 HEAD `ca8a55a`，
结束时 HEAD `4bf830a`，工作树只剩负责人的 `.gitattributes` 与三份 `docs/PROJECT-*` 未提交项（未碰）。
证据根 `D:/_shared/EXEC-2026-11-15/`。

按用户 m04034 给的顺序执行：① N12 覆盖率收复 ② N13 构建 + A8 ③ gate4 负控修复重跑。

---

## 1. N12 覆盖率收复：A2/A3/B2 从 NOT VERIFIED 变 PASS

上一窗口 step3 的捕获集只有 `t20000/t8415/t2000` 三帧 POV + 一帧 STV 站立帧，因此 A2/A3/B2 无样本。
本轮在**同一 exe**（`90f781b5…`，3,003,392 B）上补采四帧，耗时 09:41 → 09:52:36（job `bash-44c1f8a6e09d4801`），
四帧全部 `rc=0`、`trace_state=ok`、bmp 均 2,582,406 B：

| 帧 | 耗时 | trace |
|---|---|---|
| `pov-t40000` | 284,830 ms | 28,677,297 B |
| `upward-pov-t39999` | 129,270 ms | 43,888,006 B |
| `upward-pov-t20000` | 91,642 ms | 22,252,763 B |
| `stv-t40000` | 125,749 ms | 27,864,481 B |

`N12_CAP=D:/_shared/EXEC-2026-11-15/N12-QA/capture-90f781b5 N12_SET=full90 python accept.py`
（日志 `N12-QA/full90-run2.log`，产物 `accept.json` 97,090 B + `accept-table.md` 40,003 B；
改前备份 `accept.json.pre-full90.bak`、`accept.step4.json.pre-full90.bak`、`accept.step4.md.pre-full90.bak`）：

- **N12-A POV eye = origin + viewOffset → PASS**，A1/A2/A3 全 PASS；
  eyes 行 `offsetZ=75.0001/75.0001/68.0001 reason=applied src=camera dz=+75.0000/+75.0000/+68.0000`。
- **N12-B STV crouch eye = target origin + viewOffset → PASS**：B2 `stv-t40000` PASS（45.000043 距 68.0 常量 23 u，有区分度）；
  B1 `stv-t20000` = **COINCIDENT / NOT VERIFIED**（oracle 68.000065 与常量 68.0 只差 0.0001 u，
  无法区分「读属性」与「用常量」，故既不计绿也不计红、不入分母）。
- **N12-C stale snapshot selection → BOUNDED-STALE**（B1/B2 的 STV 快照样本判定；本构建 POV 样本不读快照）。
- tick-align：A1 raw=-2543 demo=-340、A2 raw=-2274 demo=-73、A3 raw=-1571 demo=-298，branch=camera；B1/B2 branch=n/a。
- 控制项 N1a/b/c、N2a/b、N3、EX-1/2/4/5/6/7 全部按预期红/拒绝；**EX-3 death camera 未抬升 = PASS**
  （eye z 896.0000 = origin z 896.0000，`lifted-to-a-constant=False`）。
- B1/B2 的 eye line 在新构建上 `NOT APPLICABLE`，原因在 `accept.py:1134-1147`：eye line 位于
  `if (haveObserverView)`（`native/src/main.cpp:10146/:10156`），SourceTV 无 cmdinfo ⇒ 0 条 eye line
  （`stv-t40000` 有 2430 条 camera 行、0 条 eye 行），STV 相机走 `placeSpectatorView`
  （`native/src/main.cpp:638-691`，`kSpectatorEyeHeight=68.0f` 在 `:657`），不经过 `resolvePovEye`。

**本轮修掉的 accept.py 假红**（`D:/_shared/EXEC-2026-11-15/N12-QA/accept.py`，rev `ab2e349a3c00`）：
full90 首跑报 `EX3 eye line / viewEntity: oracle 1 vs product 10 → FAIL`。根因在 `:1235`：非 pov 角色一律用
`ENTITY['stv_signon']`（=1），death 角色也吃到这个值，而 EX3 是 POV demo（entity 10）。
修法 `_expect = ENTITY[_ent] if _mode == 'death' else ENTITY['stv_signon']` 并加注释说明 STV 分支保留该值
只是因为它不可观测。重跑后 **EX3 eye line ent=10 PASS**，六条 eye line 全 PASS/NOT APPLICABLE。

---

## 2. gate4（`animation-drive-states-check.sh`）：两处缺陷修好，四条负控首次可判

第一轮 gate4 已 `rc=0 / ANIMATION-DRIVE-STATES=PASS`（4002 s、9 次产品运行、`READINGS=PASS`），
但**自带的四条负控是 NOT VERIFIED**，本轮查出两个独立原因并都修掉。

### 2.1 `grep -m1E` 粘连 flag（负控原因串为空）

`:445` 原为 `grep -m1E --`，本机 grep 解析成 `-m 1E` ⇒ `grep: invalid argument '1E' for '-m'`，
四条负控的「原因」字段恒为空。改为 `grep -m 1 -E --`。

### 2.2 `--dry-run` 会截断已提交的 evidence（更严重）

`:153` 的 `: > "$OUT/gate.txt"` 位于参数解析之后、dry-run 早退之前 ⇒ 只跑一次 `--dry-run` 就会把
tracked 的 `evidence/t84-animdrive/animation-drive-states/gate.txt`（43 行官方记录）截成 22 行。
本轮先在 `git status` 里发现该文件是 M，`git checkout --` 还原（md5 `814ba78cb789d94e0b09cde4b8675c4e`），再改脚本：

- 所有 `tee -a "$OUT/gate.txt"` → `tee -a "$GATE_LOG"`（`:161/:162/:185/:187/:455/:487` 等）；
- `:153` 改为 `GATE_LOG="$OUT/gate.txt"` + `[ "$DRY_RUN" -eq 1 ] && GATE_LOG=/dev/null`，
  **必须放在 dry-run 分支之前**——分支前三行 header 也写文件，第一版漏了这点，md5 仍变；
- `--controls-only` 用独立 `$OUT/gate-controls.txt`，不覆盖完整记录；真正整轮跑才在分支后 `: > "$GATE_LOG"`。

验证：`bash -n` OK；`--dry-run` rc=0 且 evidence md5 前后都是 `814ba78c…` ⇒ `EVIDENCE-UNTOUCHED-BY-DRYRUN`。

### 2.3 新增 `--controls-only` 与四条负控实测

新增模式（`CONTROLS_ONLY=0` + case 分支；`:421-440` 复用守卫：九个 `$OUT/<name>.bmp/.trace` 必须存在，
否则 `FATAL: --controls-only but frame/trace missing: …` 并 `exit 2`；打印 `REUSED-OUT` 与 `reused exe:`）。
负控计数 `controls_run/controls_ok`，`:538-541` 那句恒印 `controls=skipped` 的假话改成真实分数
（原 `:491` `controls="${SKIP_NEG:+skipped}"` 在 `SKIP_NEG=0` 非空时恒为 skipped）。

实测：把 gate4 第一轮的 nine captures（bmp/csv/trace + gate.txt，约 1.1 G，不含 neg-*）拷到
`D:/_shared/EXEC-2026-11-15/gate4-controls/out`，跑 `T84_STATES_OUT=… bash animation-drive-states-check.sh --controls-only`
（job `bash-12ce9924529b46f8`，日志 `controls-only-run2.log`）：

```
  OK   negative control 1-flinch-renamed failed as required:   FAIL C no trace line names state=flinch
  OK   negative control 2-flinch-stripped failed as required:   FAIL B the states trace never names state=flinch (state lines x0, a_flinch frames x0)
  OK   negative control 3-crouch-as-legacy failed as required:   FAIL B the crouch layer did not reach the frame: states and legacy are byte-identical at tick 14765 (04dd5f99… vs 04dd5f99…)
  OK   negative control 4-dsum-stripped failed as required:   FAIL C no animdsum line in the cover run trace

  SUMMARY failures=0 controls=4/4
ANIMATION-DRIVE-STATES=PASS …
```

每条负控现在都给出**具名原因**，与注入内容一致；readings 仍是 20 个 OK、0 FAIL，`READINGS=PASS`。
另外补一条具名负控：`T84_STATES_OUT` 指向空目录 ⇒ `rc=2`，六行 `FATAL: --controls-only but …missing`，
只生成两个空日志文件、不启动任何进程（`gate4-controls/empty-out-neg.log`）。

**证据边界（报告必须写清）**：controls-only 模式不启动产品进程，复用 trace 由 exe `90f781b5…`
在 11-14/15 产出；脚本打印的 `reused exe:` 是当前构建路径，不是这些 trace 的生产者。
它证明的是「四条负控各自的检查逻辑能按预期红」，不是「负控在本次新构建上重放」。

---

## 3. N13 第一步：编译通过 + A8 全绿 + 已提交

实现规格 `docs/N13-COMMAND-STATE-MODEL-2026-11-15.md`，详细实现与读数见
`docs/N13-STEP1-IMPL-2026-11-15.md`。要点：

- 纯增量四文件：`native/include/target_identity.h`（新，127 行）、`native/src/main.cpp` +640/−0、
  `native/src/main_hud.cpp` +22/−0、`native/src/main_hud.h` +20/−0；零删除行，四个相机函数与既有 trace 格式串未动。
- 首次编译两处真错（代码从未编译过）：① `gen-build-stamp.cmake:293` **D-90 REFUSED -- untracked compile inputs**
  点名 `include/target_identity.h` ⇒ `git add` 后重试；② `main.cpp:1221` C2065（前一文件先用后定义）与
  `main.cpp:4060` **C1061 块嵌套太深**——根因不是新增 per-frame 块，而是 CLI `else-if` 梯子：
  四个 `--follow-*` 分支把 117 层链顶过 MSVC 上限（`main.cpp:3440-3443` 早已记载此上限）。
  修法：新增 `followConsumeArgument(LPWSTR*, int, int&)`（`main.cpp:1295-1356`）在梯子前消费
  （`main.cpp:3557-3559`），梯子回到 HEAD 的 530 行 / 117 rung，逐字节 `IDENTICAL: True`。
  **梯子又没余量了**：下一个 flag 必须也放梯子前。
- 构建：`bash build-target.sh tf2_demo_native` exit 0（10:01:58），exe `6d15c566cf34…`、3,010,048 B。

### A8 判据的修正（本轮最重要的设计结论）

A8 原写成「同一条 trace 逐字节不变」。**实测这条判据对任何构建都不可满足**：同一 demo / tick /
`--playback-speed 8` 两次运行，step-3 参照（exe `90f781b5`，11-14 23:10）75,799 行、`povQueries=1429`、
242.9 s、12.4 MB；本 exe（`6d15c566`，11-15 10:09）154,899 行、`povQueries=2932`、163.1 s、25.3 MB。
行数随帧率浮动，与代码无关。仓里早已有规则 `docs/N12-ACCEPTANCE-2026-11-15.md:398`：
「断言不得回读 povApplied 的整数行数做等价比较（每次运行帧数不同）；按 tick 归键比对」。
A8 因此按 tick 归键执行（`D:/_shared/EXEC-2026-11-15/N13-QA/a8/a8-check.sh`，`--capture/--analyze/--all/--capture-one`）：

| # | 断言 | 读数 |
|---|---|---|
| A8.1 | `scene: eye row` 按 `reqTick`，ref vs 无 flag | 387 共同 tick、**0 不同行**（ref 1429 行 / new 2908 行） |
| A8.2 | `scene: eye` 按 `(originTick,offsetTick)` | 387 共同 key、**0 不同行** |
| A8.3 | 帧 vs step-3 帧 | md5 `334fe0a81e2505d986f3be8dde6c4e09` 相同 |
| A8.4 | `--follow-target 9` 不改画面 | 同一 md5 |
| A8.5 | `--follow-target 9` 不改任何归键 eye 行 | 742 共同 tick、**0 不同行** |
| A8.6 | 行种类不增不减 | 85 vs 85，种类集合相同（数字掩码后比较） |
| A8.7 | `follow:` 行数 | 无 flag **0** / `--follow-target 9` **5** |
| A8.8 | 相机优先级被具名 | `cameraEffect=pov-eye-wins` 观察到 |
| A8.9 | `demo_open_probe --scan` / `--scan --camera-track` | `8007b92b…`(3171 B) / `3ef091d5…`(3763 B)，与 N12 §17.4 基线逐字节相同 |

⇒ `N13-A8=PASS`。样例 follow 行：

```
follow: target=9 serial=0 src=manual pinned=1 valid=1 state=ok reason=ok nameKnown=1 nameSource=userinfo
        nameReason=ok name="Sinister Mark" tick=2177 cameraEffect=pov-eye-wins pinAccepted=0
```

**这一步不做什么**（设计如此）：相机优先级未改，POV demo 里选人仍不改画面（`cameraEffect=pov-eye-wins`）；
`serial` 无数据源（0=unknown，`gone-index-reused` 编码但不可达）；A1–A7 命令语义未验（需第二步 harness）。

---

## 4. 提交

| 提交 | 内容 |
|---|---|
| `4bf830a` | N13 第一步四文件 + gate4 两处修复 + `docs/N13-STEP1-IMPL-2026-11-15.md` 95 行 + N-STATUS 更新；7 文件 +963/−9 |

提交前 `bash tools/audit-tree.sh` → `VERDICT: OK`（无禁止写入、行尾完好）。
负责人未提交的 `.gitattributes`、`docs/PROJECT-NEXT-TASKS-2026-11-14.md`、
`docs/PROJECT-ROADMAP-HANDOFF-2026-11-12.md`、`docs/PROJECT-HANDOFF-2026-11-14.md` 全程未碰、未纳入。

---

## 5. 仍未做（明日）

1. **12 道清扫过的门禁复跑**（算术清扫改了 12 个脚本，只用 `--selftest`/仓扫/回放验证过，没跑真实整轮）。
2. N14/N15 → N16 → N27 导出实现 → N17–N20 → N28/N29/N30。
3. N13 第一步的 A1–A7 命令语义验收；N13 第二步（相机优先级单独裁决）。
4. B1 `stv-t20000` 的 COINCIDENT 样本只能靠换帧（更高/更低的下蹲目标）解决。
5. `.gitattributes` 的行尾策略实施等待负责人确认。
