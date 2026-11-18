# 五门禁真实 verdict（第二轮，2026-11-15 01:00）

执行者 `gateverdict2`；原始报告 `D:/_shared/EXEC-2026-11-15/LEAD/gate-verdicts2.md`（sha256 见提交说明）；证据根 `D:/_shared/EXEC-2026-11-15/gateverdict2/`。本轮全程 hostlock 包裹、零 tracked 写入（本文件由我入库）。

## §0 口径（先读这一节）

本轮把五道门禁里**两道**推进到可以声称绿，但**没有一道是"全门禁绿"**：

| 门禁 | 本轮读数 | 可以声称 | 不可以声称 |
| --- | --- | --- | --- |
| #1 `class-pose-family-check.sh` | 未重跑（沿用第一轮 `rc=0`） | 第一轮真绿 | 本轮复跑过 |
| #2 `player-animation-check.sh` | **`rc=0` / `PLAYER-ANIMATION-CHECK=PASS` / 67 s** | 重建 probe 后的这一次运行真绿；`E` 段读数真的来自两处修复 | 源码一动它就会因 STALE-BINARY 重新变红 |
| #3 `playback-error-paths-check.sh` | 未重跑 | 第一轮的红是**本机缺录制**造成的环境假红 | 已修 / 已绿 |
| #4 `animation-drive-states-check.sh` | **`rc=0` / `ANIMATION-DRIVE-STATES=PASS` / 4002 s / 9 次产品运行** | 产品读数真绿；`[ -e ]` 修复让门禁真的驱动了产品 | 门禁自带的四条负控（原因串为空，见正文） |
| #5 `viewmodel-animation-check.sh` | 未重跑 | 同 #3，环境假红 | 已修 / 已绿 |

**两条宿主级教训都要带着读这轮结论**：① 本机 `[ -d ]` 对 TF 根为假、`[ -e ]` 为真（教训十）；② 嵌套算术不展开（教训九，已由 `f74ce5f` 批量清扫 14 处）。第二轮的 gate#4 之所以能从第一轮的 `exit 2` 走到真跑 9 次产品，正是因为 `569c67e` 把 `:136` 的 `[ -d ]` 换成了 `[ -e ]`。

## §0.1 我额外核过的事实

- gate#2 的正控读数：`lines=8 bad_move=0 bad_idle=0 bad_parse=0`、`E demo movingTicks=39`、`movingAdvanced=26`；新增两条负控（`movingTicks` 为空 / `12x`）都 `rc=1` 并打出具名 FAIL + 我要求的那条非整数护栏。
- gate#4 的 9 次运行 **全部 `rc=0`、帧 2,582,406 B**，`A default == legacy` 逐字节相同（md5 `6f6309c0…`）；跑完 55 个 `native/build-nmake` 二进制**逐字节未变**，产品 exe 仍是 `90f781b5…`。
- gate#4 门禁自带负控：四条注入都产出了**指向注入本身症状**的 FAIL（`no trace line names state=flinch` / `never names state=flinch` / `crouch layer did not reach the frame…byte-identical` / `no animdsum line`），但因为门禁 `:445` 用 `$(grep -m"$N"E …)` 拼原因串、本机 grep 报 `invalid argument '1E' for '-m'`，**原因字段是空的** ⇒ 按严口径记 NOT VERIFIED。这条 `grep` 粘连是**门禁脚本自身的缺陷**，未在今晚修（改门禁会让这轮 verdict 的出处变味），列为明日待办。

## §0.2 与第一轮的差异

| | 第一轮（`304813d`） | 第二轮 |
| --- | --- | --- |
| 真绿 | 仅 #1 | #1（沿用）+ **#2** + **#4** |
| 环境假红 | #3 #4 #5 | #3 #5（未重跑） |
| 修过的门禁缺陷 | #2 的嵌套算术、#4 的 `[ -d ]` | 新增实测：两处修复都**被读数证明**（#2 的 `movingAdvanced=30/26`、#4 的 9 次真跑） |
| 负控 | #1 有效 | #2 两条有效；#4 四条证据指向有效但原因串为空 ⇒ NOT VERIFIED |

---
# Gate re-verification round 2 — gateverdict2

- Window: 2026-11-14 23:42:19 → 2026-11-15 01:00:39 (+0800). Host: bash 5.3.20(1)-release (x86_64-pc-msys).
- Worktree: D:/TF2_Native_Worktrees/t105-e5 (branch candidate/e5, HEAD 68105a8). Writes ONLY under D:/_shared/EXEC-2026-11-15/gateverdict2/ (this file is the only file written outside it). No git add/commit, no tracked-file write; tools/audit-tree.sh after every run: "VERDICT: OK (no forbidden writes, line endings intact)".
- Method: each gate ran through my runner, which (a) takes /d/TF2_Native_Worktrees/_shared/hostlock.sh, (b) captures the gate's OWN exit status (exec-gate2.sh: rc=$?; the round-1 wrapper ended with a bare echo, which is where "TRUE rc" doubt came from), (c) hashes native/build-nmake/*.exe before/after, (d) records git status before/after. Gate output goes to each gate's official OUT env var, never into the repo.
- Host quirks that decide these verdicts: (1) this msys bash's builtin `[ -d ]` is FALSE on the TF root while `[ -e ]` is TRUE (`/usr/bin/test -d` TRUE, `[ -x ]` TRUE, ls/cd fine); (2) sed is WinuxCmd 1.2.1 (BRE differs); (3) grep mis-parses combined flags: `grep -m1E` → "invalid argument '1E' for '-m'".

## Verdict table

| gate | command (full text below) | start→end | TRUE rc | gate's own last line | negative controls | still env-blocked? | may claim | may not claim |
|---|---|---|---|---|---|---|---|---|
| #2 player-animation-check.sh | run-gate2.sh g2-player-animation player-animation-check.sh | 23:44:50→23:45:58 (67 s) | **0** | PLAYER-ANIMATION-CHECK=PASS | gate's own 1/6 controls: OK; plus my 2 named controls on the fixed guard (rc=1, exact FAIL lines) | no (probe rebuilt) | green in THIS run, after rebuild | nothing general; freshness detector itself not negative-controlled |
| #4 animation-drive-states-check.sh | run-gate2.sh g4-animation-drive-states animation-drive-states-check.sh (T84_KEEP_RUNS=1) | 23:53:56→01:00:39 (4002 s) | **0** | ANIMATION-DRIVE-STATES=PASS ... | **NOT VERIFIED** (4 built-in); my control A missing-TF: VERIFIED rc=2 | no (the :141 -e fix is what unblocked it) | main verdict green; 9 product runs really happened; -e fix works | that the 4 built-in controls proved anything |
| #3 playback-error-paths | not re-run this round | — | n/a | — | — | round-1 red = local missing recording (environment), NOT fixed, NOT contradicted | nothing | anything "fixed"/"green" |
| #5 viewmodel-animation | not re-run this round | — | n/a | — | — | round-1 red = same missing recording | nothing | anything "fixed"/"green" |
| #1 class-pose | carry-over from round 1, docs/GATE-VERDICTS-2026-11-15.md | — | 0 (round 1) | PASS (round 1) | — | no | true green as of round 1 | that it was re-run in round 2 |

## Gate 2 — player-animation-check.sh: TRUE green (scoped to this run)

- Command: `cd /d/TF2_Native_Worktrees/t105-e5 && PLAYER_ANIMATION_OUT=D:/_shared/EXEC-2026-11-15/gateverdict2/g2-player-animation/out bash /d/_shared/EXEC-2026-11-15/gateverdict2/run-gate2.sh g2-player-animation player-animation-check.sh`
- rc: RUNNER-RC=0, gate.rc=0, meta rc=0, elapsed 67 s, gate.log 269 lines sha256 5b91947b9d68bcbdd0cd0517dfa18f71b9ef440f1babd4cec6de9556664c2135, 235 "  OK " / 0 "  FAIL ".
- Probe: rebuilt at 23:44:32 (sha256 ba371d6112f20831c646cbfcad6d74cfdf0785975778b0906af07a5798a68fa7, 965120 B) because round 1 died on STALE-BINARY (probe 2026-11-13 23:17:27 older than native/tools/model_pose_probe.cpp). I replicated that staleness by hand before the rebuild (stale_from_deps=[native/tools/model_pose_probe.cpp], stale_header=[native/include/demo_header.h]); after it both lists are empty. In the run: `OK   BINARY-FRESH=1 probe=2026-11-14 23:44:32`.
- The :334-341 two-step fix is what the E section measures (the old one-liner emitted literal arithmetic text):
  - `animticks mode=synthetic ticks=61 movingTicks=31 movingAdvanced=30 idleTicks=30 idleAdvanced=0 maxMovingDelta=0.020000001 maxIdleDelta=0.000000000`
  - `OK   E synthetic movingAdvanced (baseline excluded) = 30 (>= 30)`
  - `OK   E demo movingTicks = 39 (>= 1)` / `OK   E demo movingAdvanced (one baseline per entity) = 26 (>= 26)`; demo line: `window=60000,62000 transitions=39 entities=13 movingTicks=39 movingAdvanced=26 idleTicks=0 idleAdvanced=0 staleTicks=1997`.
  - D55 reader: `lines=8 bad_move=0 bad_idle=0 bad_parse=0`.
- Added negative control (my copy, 2 hunks: fixed cd + parameterised synthetic ticks line; tracked gate untouched):
  - empty movingTicks → rc=1, `FAIL E synthetic movingTicks = <none>, expected >= 1` and the new guard `FAIL E synthetic movingTicks is not a non-negative integer: ''` (gate.log:239-240).
  - "12x" → rc=1, `FAIL E synthetic movingTicks = 12x, expected >= 1` and `FAIL E synthetic movingTicks is not a non-negative integer: '12x'` (gate.log:239-240). Both end PLAYER-ANIMATION-CHECK=FAIL.
- May claim: on this host, in this run, with a freshly rebuilt probe, gate 2 is green; the two-step arithmetic yields 30; the non-integer guard rejects both empty and 12x. May not claim: any cross-host/general result; the freshness detector was NOT negative-controlled (would need a stale probe, i.e. a tracked-source touch, which the brief forbids).

## Gate 4 — animation-drive-states-check.sh: main verdict TRUE green; built-in negative controls NOT VERIFIED

- Command: `cd /d/TF2_Native_Worktrees/t105-e5 && T84_STATES_OUT=D:/_shared/EXEC-2026-11-15/gateverdict2/g4-animation-drive-states/out T84_KEEP_RUNS=1 bash /d/_shared/EXEC-2026-11-15/gateverdict2/run-gate2.sh g4-animation-drive-states animation-drive-states-check.sh` (run as a background host job; it outlived several 10-min program budgets).
- rc: RUNNER-RC=0, gate.rc=0, meta rc=0, elapsed 4002 s (66.7 min), gate.log 56 lines sha256 51fc660b9add3e1990464c8045684f08e575a25928b4ee64c70db56b1be4ab54, 20 "  OK " / 0 "  FAIL ".
- Last line: `ANIMATION-DRIVE-STATES=PASS default==legacy byte-identical with the T61 lines intact, the states trace names state=flinch and plays a_flinch sequence frames while the legacy trace names neither, states crouch frame differs from legacy at the same tick and repeats byte-identically, crouch/crouchwalk/flinch each named with needle+reason and each reached by the per-frame counters, four negative controls failed for their own reasons`
- 9 product runs, every one rc=0, 2582406 B bmp: default 16780, legacy-crouch 14765, legacy 16780, states-flinch 16780, states-flinch-repeat 16780, states-crouch 14765, states-crouch-b 14800, states-cover 20000 (COVER_DEMO), early 12000.
- READINGS=PASS: `A default frame == --animation-drive legacy frame (md5 6f6309c05c2135d6dd6a51287326df15)`; `B crouch layer reached the frame: states 3c522ad02483c8e80ac6a1412db30489 != legacy 04dd5f99e00e2bcc81e85dafcce2e15c at tick 14765`; `C peak over 10568 animdsum frames: crouch=2 crouchwalk=2(tick=11355) flinch=7 flinchStarts=7(tick=12016) fallback=10`.
- The :141 fix is what unblocked it: with the real TF root, builtin `[ -d ]`=FALSE but `[ -e ]`=TRUE (probe run this session), so round 1 exited 2 before any reading; this run passed the check and drove the product 9 times.
- My control A (copy, exactly 3 lines changed: cd, TF, verify-iso path; sha256 6885c8357f8c7fe8c21bf6f8bab4fbf61e5052d5c0fcc42735d3bb36b06b3a1c): `FATAL: missing TF root: D:/_shared/EXEC-2026-11-15/gateverdict2/neg/no-such-tf-root-does-not-exist`, rc=2, OUT directory never created, hostlock released. So after the -e fix a genuinely missing root is still refused with exit 2 — verified.
- The gate's own four controls (1-flinch-renamed, 2-flinch-stripped, 3-crouch-as-legacy, 4-dsum-stripped): recorded NOT VERIFIED. Reason: line 445 prints the reason with `$(grep -m1E -- "$expect" "$dir/check.txt")`, which this host's grep reads as `-m 1E` → `grep: invalid argument '1E' for '-m'`; every "failed as required:" line therefore ends with an empty reason. The verdict path itself is separate and did work (:429-430 writes check.txt, :433 `grep -q 'FAIL'`, :441 `grep -qE -- "$expect"`), and I extracted the raw matched lines with the corrected form `grep -m 1 -E`:
  - neg-1 `FAIL C no trace line names state=flinch`
  - neg-2 `FAIL B the states trace never names state=flinch (state lines x0, a_flinch frames x0)`
  - neg-3 `FAIL B the crouch layer did not reach the frame: states and legacy are byte-identical at tick 14765 (04dd5f99e00e2bcc81e85dafcce2e15c vs 04dd5f99e00e2bcc81e85dafcce2e15c)`
  - neg-4 `FAIL C no animdsum line in the cover run trace`
  Each line names the injected mutation's own symptom and matches the gate's own expect regex (:458, :468, :476, :484). The evidence points to the controls working, but because the gate's own display path crashed and the controls were NOT re-run with the flags split, they stay NOT VERIFIED (lead ruling). Fix for a later round: line 445 `grep -m1E` → `grep -m 1 -E`.
- `controls=skipped` vs "four negative controls failed for their own reasons": :491 builds `controls="${SKIP_NEG:+skipped}"`, and SKIP_NEG is SET (0), so the `:+` expansion always prints "skipped" even when the controls ran. Four OK lines and four neg-*/check.txt directories exist, so they did run; "controls=skipped" is a cosmetic misreport, and the final PASS sentence is the accurate one.
- Integrity: all 55 native/build-nmake binaries byte-identical before/after the 66-min run (bins-before.sha256 == bins-after.sha256; tf2_demo_native.exe sha256 90f781b54c96ac88cbef2502afd06a159b9d643d01579bddf85cff94a97551df). Other agents changed tracked sources during the window (native/src/main.cpp, native/src/main_hud.cpp, native/src/main_hud.h, hud-live-check.sh, map-geometry-lighting-check.sh, player-model-check.sh, release-rebuild.sh, t2-visual-matrix.sh, userinfo-check.sh, viewmodel-*.sh, vmt-nesting-check.sh, world-material*.sh; untracked native/include/target_identity.h, tools/check-nested-arith.sh) — none of it is mine, and animation-drive-states-check.sh itself is unchanged (sha256 b0107ef8cbad306755c4374bf5c5054f0dca9d6b4edca1bbd9f16b7f02206974, same as at create time).

## Evidence index (all under D:/_shared/EXEC-2026-11-15/gateverdict2/)

- g2-player-animation/: out/ (14 files: class-*.txt x10, demo-ticks.txt, matcher-selftest.txt, entity-id-selftest.txt, negative/), gate.log, meta.txt, timing.txt, hostlock-*.txt, git-before/after.txt.
- g2-neg-b-empty/, g2-neg-b-nonint/: gate.log + meta.txt (NEG_SYNTHETIC_TICKS recorded) for the two added controls.
- g4-animation-drive-states/: gate.log (56 lines), meta.txt (rc=0, 4002 s), out/ (9 runs + 4 neg-*/check.txt), bins-before/after.sha256, git-before/after.txt.
- g4-neg-a/: neg-a.log, meta.txt (rc=2, out_dir_created=no).
- neg/: build-neg-a.sh, build-neg-b.sh, g4-neg-a-missing-tf.sh, g2-neg-b-empty.sh, g2-neg-b-nonint.sh, neg-a.diff.
- run-gate2.sh, exec-gate2.sh, run-build2.sh. (rerun-checker.sh is a partial attempt that produced no usable output; it is NOT used as evidence.)

## Limits

- These verdicts are host-specific: they hold for this machine (WinuxCmd sed/grep, msys bash with the -d anomaly) at these timestamps.
- Gate 2's green depends on the rebuilt probe; a source/header touch will make it red again by design.
- Gate 4's green is a product/readings green; its built-in negative-control layer is not verified on this host.
- Nothing here says the repository as a whole is green: gates #1/#3/#5 are outside this round's re-run scope (#1 carried over, #3/#5 still environment-red from round 1).
