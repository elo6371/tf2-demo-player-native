# P3b 声音账务拆分（task B），2026-11-16

Status: **实现 + 读数**。改的是**计数语义**（设计性丢弃 vs 解码失败），不是 4096 上限、不是解码行为。
主树产品 exe 全程未重建未替换：`75dae10e60740cd2c3e780ef68c72eb100e6f4221f8c234227a907a99d0607ab`
（3,044,864 B，mtime 2026-11-16 14:14:35）。

## 结论

`sound_failures` 以前是「设计性丢弃 + 真解码中断」的和：pl_upward_f12 上 `20923 = 20921 + 2`。
现在丢弃按原因单独计（`sound_dropped_*`），`sound_failures` 只计 payload 已捕获却中断的消息。
四个 demo 上解码相关读数（`sound_decoded` / `sound_matches` / `sound_misses`）改前改后完全相同，
变的是账，不是解码结果。

## 1. 改动

| 位置（task B 之后） | 内容 |
|---|---|
| `native/include/demo_header.h:823` | `enum class SoundDropReason { PayloadTooLarge, MessageCap }` |
| `native/include/demo_header.h:827-829` | `rawSoundMessagesDropped` 保留为总数，新增 `...PayloadTooLarge` / `...MessageCap` |
| `native/src/demo_header_sound.cpp:28` | `recordDroppedSoundMessage()`：唯一记录丢弃的地方，总数与原因一起递增 |
| `native/src/demo_header_sound.cpp:71` | capture 分支加 `bool captured`；丢弃走 `recordDroppedSoundMessage()` 后 skip，不再进解码循环 |
| `native/src/demo_header_sound.cpp:100` | 解码循环加 `captured &&` 守卫：丢弃消息整条跳过，记 0 failure |
| `native/tools/demo_open_probe.cpp:855-856` | 扫描行新增 `sound_dropped_payload_too_large=` / `sound_dropped_message_cap=` |

上限条件与常数原样保留：`payloadBits < 64u * 1024u && summary.rawSoundMessages.size() < 4096u`。

**为什么行为不变**：被丢弃的消息 payload 为空，旧代码进入解码循环后第一次 bit 读必失败 → `break`，
一条事件也不产出。现在只是显式跳过同一条消息。所以 `sound_decoded`、`sound_matches`、`sound_misses`
在四个 demo 上改前改后逐项相同（见 §2）。

## 2. 读数（改前 / 改后，同一 worktree、同一构建树、同一 demo）

probe 改前 sha256 `2c398e6868f294640eafb1b6247c0e3b008150e5b3ab4aea99575849c63688cb`（15:24 构建），
改后 `314ab6b201d6dfb0a5dde61c6f931795e3c87eb7bbefd53d08f50b9e01e0eabe`（15:32 构建；之后仅注释
再改，重建 sha 不变）。命令：`demo_open_probe.exe --scan <demo>`。

| demo | messages | dropped 前 | failures 前 | dropped 后 | payload_too_large | message_cap | failures 后 | decoded（前后同） |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| koth_bagel_rc13 | 15114 | 11018 | 11018 | 11018 | 0 | 11018 | 0 | 5234 |
| pl_upward_f12 | 25017 | 20921 | 20923 | 20921 | 0 | 20921 | 2 | 5528 |
| cp_snakewater_final1 | 12402 | 8306 | 8308 | 8306 | 0 | 8306 | 2 | 5292 |
| koth_bagel_rc11 | 22382 | 18286 | 18288 | 18286 | 0 | 18286 | 2 | 5244 |

改前 failures 恰好等于 dropped + {0, 2, 2, 2}（11018+0 / 20921+2 / 8306+2 / 18286+2），与「每条丢弃
在空 payload 上制造 1 次假失败」的机理逐条对上；改后该差值消失。

四条恒等式（脚本逐条打印）：
- I1 `dropped == payload_too_large + message_cap`；
- I2 `messages - dropped == 4096`（丢弃存在时恰好饱和，四个 demo 全满足）；
- I3 `failures <= 4096`（真失败只能来自已捕获消息，每条最多 1 次）；
- I4 有丢弃时 `failures < dropped`（旧混计会让它 >=，这是回归守卫）。

## 3. 4096 上限没动；64 KiB 分支当前不可达

- 上限条件与常数未改。
- `sound_dropped_payload_too_large` 在四个 demo 上恒 0。这不是「没测到」，而是**结构上不可能**：
  `payloadBits` 在可靠路径按 8 bit 读、非可靠路径按 16 bit 读（`readSounds` 开头），最大值 65535
  < 65536 = 64 KiB。要让这一类计数上涨必须改线格式（字段加宽），换输入 demo 做不到。
- 能上涨的只有 `message_cap`：它等于「第 4097 条起被丢弃的消息数」，四个 demo 分别 11018 / 20921 /
  8306 / 18286。
- 所以「让丢弃/失败两类分别涨的输入」这一问分两半：MessageCap 类有真实输入（上表）；PayloadTooLarge
  类在当前线格式下**不存在可构造输入**。代码保留该分支，作为字段加宽后的守卫，并在注释里写明不可达。

## 4. 产品是否真走这条受限 payload 路径：是，读数对得上

代码链（未改动文件，行号稳定）：
- `native/src/main.cpp:4439` `parseDemoHeaderFile(commandDemo, demoHeader)`；`:4442`
  `scanKnownDemoMessages(commandDemo, demoIndex, demoNetworkSummary)`；
- `native/src/demo_header.cpp:180` 消息 type 17 → `readSounds(bits, summary, entryTick)`，
  受限 payload 逻辑就在这条路径里；
- `native/src/main.cpp:7168` `buildSoundTimeline(demoNetworkSummary.decodedSoundEvents, ...)`，
  产品实际消费解码事件；
- `native/src/main.cpp:7309-7312` 产品 TITLE 行打印 `sounds=`（消息数）`soundevents=`
  `sounddecoded=` `sounddecodefail=`。

既有产品运行时日志（**非本次重建**）与 probe 改前读数逐项相同：

| demo | 产品日志 | 产品读数 | probe 改前 |
|---|---|---|---|
| koth_bagel_rc13 | 主树 `evidence/camera-ui/e2e.driver.txt` TITLE 行 | sounds=15114 soundevents=19289 sounddecoded=5234 sounddecodefail=11018 | messages=15114 sounds=19289 decoded=5234 failures=11018 |
| cp_snakewater_final1 | `D:/_shared/t62-freeze4/material.log`（`demo=bb841c6d…snakewater`） | sounddecodefail=8308 | failures=8308 |
| koth_bagel_rc11 | `D:/_shared/t85-assets/d22b/camera-ui-runtime/e2e.driver.txt` | sounddecodefail=18288 | failures=18288 |

（命名注意：probe 的 `sounds=` 是 `soundEventCount`，产品 TITLE 的 `sounds=` 是
`soundMessageCount`；对齐时 probe `sounds=` ↔ 产品 `soundevents=`。）

这是「产品跑过并报告同一个计数器」的运行时证据，不是从代码推断。本次**没有重建也没有运行产品**，
所以「产品在新语义下打印 2」这件事尚无产品读数——合并后重跑产品抓图才会有。没有这一步，不声称
「产品账务已通过」。

## 5. 断言表述改了哪些（不是放宽）

- 旧 P3b A2 `20923 - 20921 = 2`（`docs/CARD-CHAR-P3B-P4-CHECKLIST-2026-11-16.md` §1）：现在只作为
  **修复前混计的证据**保留，不再是期望读数；新期望是 `sound_failures = 2`、`sound_dropped = 20921`、
  `0 + 20921 = 20921`。
- 新恒等式 I1–I4 见 §2，由 `native/tools/sound_accounting_verify.sh` 逐条打印 PASS/FAIL。
- 阈值一个没动：4096 上限、`sound_decoded`、matches/misses、旧 A1/A3/A4 全部原样。改的只是
  「failure」这个词的所指。

## 6. 复跑

```
bash native/tools/sound_accounting_verify.sh native/build-nmake/demo_open_probe.exe <demo.dem>
```

打印一行读数 + I1–I4，全绿才 `VERDICT: PASS`。四个 demo 的输出见证据目录 `verify/`。

## 7. 不声称什么

- 不声称「没有丢音频」：4096 条上限仍按设计丢弃消息；这一步只把丢弃与失败分开计。
- 不声称产品功能/听感通过：没有产品重建、没有产品运行、没有试听。
- 不声称 N27 产品导出入口存在：产品日志里仍是 `export=unavailable(native-encoder-missing)`。
- 不抬高上限（lead 明确要求保留 4096）。

## 8. 证据与二进制

- 证据根：`D:/_shared/EXEC-2026-11-16/INST3/taskB/`（`build/`、`scan/before/`、`scan/after/`、`verify/`）。
- worktree：`D:/TF2_Native_Worktrees/t105-e5-n27a`，分支 `work/card-char-n27a`，base `997e945e`。
- 构建：`bash /d/TF2_Native_Worktrees/_shared/hostlock.sh run card-char bash build-target.sh demo_open_probe`，
  改前/改后各一次，均 `rc=0`。
- 主树产品 exe 构建前后各核对一次，均为 `75dae10e60740cd2c3e780ef68c72eb100e6f4221f8c234227a907a99d0607ab`。
- 四个 demo 的 sha256 记录在 `scan/demos.sha256`；pl_upward_f12 为
  `7e047aff1f7743915e355eac907a95dc3d981aa0fae5591b6b15346fb77dc4a3`。
