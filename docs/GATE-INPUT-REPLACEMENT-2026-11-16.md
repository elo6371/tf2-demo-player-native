# 语料替换：userinfo 门禁的 review1.dem —— 原录像找不到，改用 2026-11-15 的同图 SourceTV 录像

2026-11-16。起因：E1 第 3 轮里 `userinfo-check.sh` 报 `FATAL: missing input …/tf/demos/review1.dem`，红在这一行而不是断言。查清后三份语料里只有 review1.dem 真的缺。

## 1. 三份语料现在各自在哪（都验过，不是猜的）

| 门禁里的名字 | 文件 | 大小 | sha256 | 怎么确认是原文件 |
|---|---|---|---|---|
| `73` | `D:/TF2_Demo_Player/work/protocol-rescue-20261005/73.dem` | 106,397,681 B | `69f9a864c88c6cf885cf46068f0be590e2fbd47b8d8ef9bbb2e6c71bccebccce` | 用 `demo_open_probe --scan --userinfo` 跑出的 9 列 slot 行与 `evidence/t100-userinfo/golden/73.tsv` **逐字节相同**（25 行） |
| `prodcutscout` | `E:/demo测试/koth_product_final/STV/prodcutscout.dem` | 62,389,932 B | `378734bc25bb4d3746503b36f9602489b195a58ecb2932e01436bc670d63557e` | 同上，与 `golden/prodcutscout.tsv` 逐字节相同（25 行） |
| `review1` | 原录像 `review1.dem` 全盘没有：Steam `tf/demos`、`D:/TF2_Demo_Player`、`E:/demo测试`、`D:/_shared` 都搜过 | — | — | — |

`evidence/header-scan/header-scan.csv:1641` 记着原 `review1.dem` 的指纹：`SourceTV Demo, koth_ashville_final1, 1368.97 s, 91265 ticks`，`source-tv-coverage-check.sh:74` 记着它的字节数与包数 `82903524 | 91247`。这些是原件的属性，不是现在这份文件的。

## 2. 替换件（用户 2026-11-16 提供）

- 原始来源：`C:/Users/Administrator/Downloads/597450fbb345099739e824bfc762773f_match-20261115-1910-koth_ashville_final1.dem`
- 归档位置（按本仓既有约定 `E:/demo测试/<map>/STV/` 放好）：`E:/demo测试/koth_ashville_final1/STV/597450fbb345099739e824bfc762773f_match-20261115-1910-koth_ashville_final1.dem`
- 大小 64,122,700 B，sha256 `4edc6058e9b3de17b2a2f58bbd88e7dab149613d9798b012f69ad8ae10976b06`（复制后复核，与源文件一致）
- 头部读数：`recording=SourceTV map=koth_ashville_final1 ticks=69646 packets_scanned=69643 source_tv=1 malformed_packets=0 temp_failures=0 entity_failures=0`
- userinfo 表读数：`userinfo_create=1 userinfo_table_resets=0 userinfo_entries=44 userinfo_slots=25 userinfo_named=19 userinfo_samples=19 userinfo_failures=0 userinfo_unparsed=0 userinfo_text_slot_mismatches=0 userinfo_duplicate_userids=0 userinfo_layout_mask=1 userinfo_missing_userdata=6 userinfo_empty_names=6`
- 也就是说：表只建一次、无重置、无重复 userId、无解析失败、名字与槽一一对应——门禁那些断言要的性质它都有，**变的是槽里装的 19 个名字**（SourceTV、Mifune、SpicyFire、Raptor、Spam、Emierr_、Shraphyn、dbflower5、Komends jaar er jaeger、Sibola、Zz_Counter、dorje、ss、twitch.tv/azzietf2、يحس بالأورا、ぷ跑步机ぷ、Wolfy、fryboy (arrogant)、psychoboost），signon 前后的 tick 位置也从 35109 变成 29950。

## 3. 门禁怎么找输入（改的是解析，不是断言）

`userinfo-check.sh` 原来把三个路径硬写成 `$DEMO_DIR/<name>.dem`，文件不在就直接 FATAL。现在按名字解析，三条规则依次是：
1. 环境变量 `TF2_USERINFO_DEMO_<NAME>`（`TF2_USERINFO_DEMO_73`、`TF2_USERINFO_DEMO_REVIEW1`、`TF2_USERINFO_DEMO_PRODCUTSCOUT`）；
2. Steam 安装目录 `<DEMO_DIR>/<name>.dem`（默认仍是 `D:/SteamLibrary/steamapps/common/Team Fortress 2/tf/demos`，可用 `TF2_USERINFO_DEMO_DIR` 覆盖）；
3. 本文件里登记的三条归档路径（上表）。

解析结果在第一条断言之前用一行 `DEMO=<name> path=<…> source=<env|steam|archive> sha256=<…>` 打印出来，所以「这一轮用的是哪份文件、它的字节是不是原来那份」永远在输出里，不存在悄悄换输入。**断言、阈值、语料条数、退出码一条都没改**；`OUT` 仍可用 `TF2_USERINFO_OUT` 指到树外。

## 4. 金标准快照随之重录（这是替换的直接后果，必须写明）

`evidence/t100-userinfo/golden/review1.tsv` 是「解码器在这份录像上的输出」的快照，换录像就必须重录，否则它比对的是另一份文件的名字表。新快照 = 25 行、18 个有名槽 + 7 个空槽，取自上面那份替换件的 `--userinfo-at 0,2000,60000,600000,99999999` 读数。

这不是把红改绿：它把「review1 这条语料」重新钉到一份真实存在的录像上，钉的方式和另外两份**完全一样**（同样是探针输出、同样 9 列、同样排序），并且这次替换写在这里、写在门禁头注释里、写在提交信息里。

## 5. 这次替换没有证明 / 没有做的事

- 没有证明这份替换件与原 `review1.dem` 在**任何**统计意义上等价：地图相同、录制方式相同（SourceTV）、表结构性质相同，但人数（19 vs 18）、userdata 长度、tick 时间线都不同。
- 没有动 `source-tv-coverage-check.sh:74` 里 `review1.dem|82903524|91247` 那一行：那条 gate 的规矩是「行与文件不符就是 gate 在工作」，要不要跟着换语料是另一件事，单独判断。
- 没有把文件复制进 Steam 安装目录（沿用之前 `SUNSHINE.dem` 的处理），也没有改任何断言阈值。
## 6. 跑的结果（不是推断）

`bash tools/lockrun.sh lead-userinfo -- env TF2_USERINFO_OUT=D:/_shared/EXEC-2026-11-16/INST2/userinfo-repin bash userinfo-check.sh`

- `RESULT=PASS`，`ASSERTIONS=191`，`GATE-RC=0`，日志 `D:/_shared/EXEC-2026-11-16/INST2/userinfo-repin/gate2.log`
- 三条 `DEMO=` 身份行：`review1 … source=archive sha256=4edc6058…`、`prodcutscout … sha256=378734bc…`、`73 … sha256=69f9a864…`
- 三条 `… : slot rows match the recorded snapshot` 全绿（golden 与新录像一致，另两条仍与原 golden 逐字节相同）
- 产品 exe 全程 `26bff4361874a5f1…`（未重编）；被重建的只有探针 `demo_open_probe.exe`（`sha256=168e8d9f5ed7d0bd06ba6a7a7376cb150259bb9b7e19793a66cb75d1320fdc8f`），因为门禁先报了 `STALE-BINARY`（`native/src/animation_binding.cpp`、`animation_clock.cpp` 比探针新）。

## 7. 顺带发现并修掉的一处同类缺陷（D93 那一类：行尾 CR 让按字节比对的断言假红）

第一次跑（旧探针、修 CR 之前）只有一条红：`FAIL fixtures: good rows are the two players that were written`。读产物发现 probe 在这个 host 上把 fixture 行按 CRLF 写，而门禁写进 `fixture-good-want.txt` 的期望是 LF，`assert_same_file` 是逐字节比对 → 两行内容完全一样却判 FAIL。语料那三条 golden 当时是绿的，说明只有 fixture 输出那条路径带 CR。

处理：在**读入侧**统一 `tr -d '\r'`（fixture 行与语料行两处），**比较的值与断言一字未改**；和 card-arch 在 `minimap-check.sh:92` 修的那处（提交 `5a5f3023`）是同一类。修完 191 条断言全绿。

## 8. source-tv 覆盖门禁的输入现状（2026-11-16 13:05 普查；本节内容未提交，等裁决）

- 9 个钉住的录像里，Steam 安装目录 `tf/demos`（现存 2063 个文件）只剩 `proc2.dem`；按精确字节数在全机搜过（Steam demos、`E:/demo测试`、`D:/TF2_Demo_Player`、`D:/_shared`、`C:/Users/Administrator`、两个回收站），能读到的只有 5 个：
  - `73.dem` → `D:/TF2_Demo_Player/work/protocol-rescue-20261005/73.dem`（106,397,681 B / 112,175 包 / `69f9a864…`）
  - `SUNSHINE.dem` → `E:/demo测试/cp_sunshine/STV/SUNSHINE.dem`（82,759,239 B / 120,929 / `7b8db6b4…`）
  - `proc2.dem` 仍在默认目录（38,541,519 B / 61,544）
  - `prodcutscout.dem` → `E:/demo测试/koth_product_final/STV/prodcutscout.dem`（62,389,932 B / 88,307 / `378734bc…`）
  - `review1.dem` 槽位 = 本文 §1 的替换录像（64,122,700 B / 69,643 / `4edc6058…`）
- 四个彻底找不到（全机没有任何文件是钉住的字节数）：`gullyscout.dem` 76,066,801 B、`processdemo.dem` 81,960,416 B、`review12.dem` 80,261,474 B、`review44.dem` 53,569,778 B。
- 同图候选（已实测，均为 SourceTV、`index_state=ok`、entity_failures=0、malformed=0、`unknown_message_types=<none>`、delta_base_unavailable=0）：
  - `E:/demo测试/cp_gullywash_f9/STV/gully4454.dem` 81,660,813 B / 120,922 包（对 gullyscout 槽）
  - `E:/demo测试/cp_process_f12/STV/1338f5de…matcha-20260927-1312-cp_process_f12.dem` 90,995,077 B / 120,920（对 processdemo 槽）
  - `E:/demo测试/cp_sultry_b8a/STV/f63a7a20…matcha-20260703-1442-cp_sultry_b8a.dem` 85,506,321 B / 140,020（对 review44 槽）
  - `review12.dem`（cp_reckoner）**没有**同图 STV 录像：全机无 `*reckoner*` 文件，header 索引里那些 cp_reckoner 行都是已经消失的 POV 自动录像。
- 门禁改动（未提交）：输入解析通用化（`REPLACED`/`REPLACED_SHA`/`REPLACED_NOTE` + A6 的 sha 钉，`demo_path()` 同时供给 collect 与断言）、新鲜度改成「mtime 新 **且** dump 的 `index_tail_bytes` 等于该行 size」（否则 `git checkout` 还原出来的旧 dump 会被当成新读数）、缺文件不再覆盖已有 dump、W2 下限提成常量 `W2_FLOOR=895000`（pin 895,939）、mutation 期望显式写成「每行一红 + 工作计数红」。
- 实测结果：正向 29 OK / 6 FAIL（4 个 missing input + W1 5/9 + W2 452,598 < 895,000），`SOURCE-TV-COVERAGE=FAIL`；负控 `MUTATION-CAUGHT=PASS`（2/2，per_demo_reds=9、work_reds=2）。日志 `D:/_shared/EXEC-2026-11-16/INST5/source-tv/gate-positive4.log`、`gate-mutation4.log`；证据目录已还原干净，`tools/audit-tree.sh` = `VERDICT: OK`。
- 未提交的原因：要先裁决（补这 4 个文件 / 换同图录像 / 缩减钉住语料）。不提交任何绿色声明。
- 消息类型覆盖实测（用 9 份 dump 的 `message_type_histogram` 做并集）：全体 9 个录像共 21 种；**5 个存活语料本身就已经覆盖这 21 种**，4 个丢失的录像没有独占类型（`only in lost4 = none`）；三个同图候选各 18/18/20 种，与存活 5 个的并集仍是 21。也就是说：无论裁到 5 个还是换 3 个同图录像，**消息类型这一层都不会丢场景**；覆盖损失只体现在包数/实体量上（452,598 vs 895,939）。

同图候选与丢失槽位的吞吐对照（全部来自同一探针的 dump，不是估算）：

| 槽位 | packets | enter | leave | delete | max_active | model_precache |
|---|---|---|---|---|---|---|
| 丢失 gullyscout | 120,929 | 14,745 | 1,953 | 11,651 | 597 | 1,165 |
| 候选 gully4454 | 120,922 | 13,718 | 1,919 | 10,825 | 594 | 1,169 |
| 丢失 processdemo | 120,904 | 17,277 | 2,140 | 12,473 | 627 | 1,209 |
| 候选 cp_process_f12 | 120,920 | 15,891 | 2,115 | 11,996 | 639 | 1,213 |
| 丢失 review44 | 80,591 | 12,699 | 1,332 | 8,237 | 897 | 1,410 |
| 候选 cp_sultry_b8a | 140,020 | 20,466 | 2,405 | 14,139 | 900 | 1,410 |
| 丢失 review12 | 120,917 | 16,975 | 2,056 | 12,121 | 698 | 1,263 |

同图候选的实体进入/离开/删除、并发上限与模型预缓存量都和丢失槽位同量级（sultry 那对连 `model_precache=1,410` 都一致），所以替换不是"降级覆盖"，只是换一段同图不同局的录像；`review12`（cp_reckoner）没有候选，只能补文件或从覆盖集移除。

## 9. 2026-11-16 13:55 裁决落地：三个同图替换件登记、review12 转 BLOCKED-INPUT、包数口径统一

> §8 是裁决前的现场记录，其中 `W2_FLOOR=895000`、"29 OK / 6 FAIL"、"未提交原因=等裁决" 三处**已被本节取代**，保留原样不追改，作为决策依据留档。

用户裁决（2026-11-16）：三份替换件按对应关系替换，登记完整路径 / SHA-256 / 地图 / 录制类型 / 实测包数 / 替换原因，保留解析质量断言，单独提交；`review12.dem` 移出可执行集、登记为 BLOCKED-INPUT；当前集报 8/8，历史九份目标仍是 8/9 已验证，不得写成"原覆盖全恢复"；覆盖数字统一口径，W1 按登记活动集，W2 按核实后的覆盖要求重订并说明依据，**不许改成约数下限换绿**；拿到 cp_reckoner STV 后作为新增恢复任务验收，不追溯修改本次证据。

### 9.1 三个替换件（与本轮 review1 替换同一处理方式）

| 槽位 | 原文件（已丢失） | 替换件路径 | SHA-256 | 实测包数 | 地图 |
|---|---|---|---|---|---|
| `gullyscout.dem` | 76,066,801 B / 120,929 包 / cp_gullywash_f9 | `E:/demo测试/cp_gullywash_f9/STV/gully4454.dem` | `3d9c3e64a0e94e618bbfa2075467f32722db4d82ee52f9ea1e9e88e093ece16d` | 120,922 | cp_gullywash_f9 |
| `processdemo.dem` | 81,960,416 B / 120,904 包 / cp_process_f12 | `E:/demo测试/cp_process_f12/STV/1338f5de79ea343682c17dd514c980d9_matcha-20260927-1312-cp_process_f12.dem` | `81ceb1afa4c54ad5fdf14201b1f1a9049c183d11aa70c6ed431a69ebf9f73b94` | 120,920 | cp_process_f12 |
| `review44.dem` | 53,569,778 B / 80,591 包 / cp_sultry_b8a | `E:/demo测试/cp_sultry_b8a/STV/f63a7a2040379d7d6a8414f76bbb81dc_matcha-20260703-1442-cp_sultry_b8a.dem` | `032bbd5ab719012cc2a7b4858f0a5d4e1ba882b4cdae1d86f2b966a3578c4374` | 140,020 | cp_sultry_b8a |

三件都实测过（同一探针 `native/build-nmake/entity_protocol_probe.exe`）：录制类型 SourceTV（header 与流内字段一致）、`index_state=ok` 且 `index_tail_bytes` 等于文件大小、`entity_failures=0 malformed_packets=0 unknown_message_packets=0 delta_base_unavailable=0`、`unknown_message_types=<none>`。文件大小与实测包数已写进门禁的 `DEMOS` 行（`gullyscout.dem|81660813|120922|cp_gullywash_f9`、`processdemo.dem|90995077|120920|cp_process_f12`、`review44.dem|85506321|140020|cp_sultry_b8a`），路径与 sha256 在 `REPLACED` / `REPLACED_SHA` / `REPLACED_NOTE` 三张表里，并由 A6 逐个钉住——**替换件被换掉或改名，门禁立刻红**。

替换原因统一是：钉住的录像在本机不存在（§8 的全机普查，按精确字节数无一命中），而同图 SourceTV 录像在盘上存在且解析质量同为 0 失败；这不是恢复原文件，是换一段同图不同局的录像。

### 9.2 review12.dem（cp_reckoner）：BLOCKED-INPUT，不是替换

- 原文件 80,261,474 B / 120,917 包 / `cp_reckoner`，全机没有同图 STV 录像（`*reckoner*` 零命中）。
- 门禁新增 `BLOCKED_INPUTS` 表登记 `review12.dem|80261474|120917|cp_reckoner` 与 `BLOCKED_NOTE`，启动与汇总都会打印；它**不在** `DEMOS` 行里，也不参与 W1/W2 的分子，因此不会被误读成"读过"。
- 若日后补到同图 STV，按"新增恢复任务"验收：新登记一条、重跑正负控、单独提交；**不追溯修改本轮证据**。

### 9.3 包数口径（三个数字是三件事，不许混用）

| 数字 | 含义 |
|---|---|
| 917,543 | 原始九份录像的目标包数（历史目标） |
| 895,939 | review1 槽换成 2026-11-15 录像后、九行的旧 pin（−21,604，−2.35%） |
| **834,460** | **本轮活动八行的精确 pin** = 112175+120929+120922+61544+120920+88307+69643+140020 |

W1 = `demos_read == 8`（活动行数）；W2 = 实测包数之和达到 **834,460 精确 pin**（不是约数下限）。m06794 明确不许靠下调约数下限换绿，所以 ±任何一行的包数变动都会红。历史目标仍记作 8/9 已验证，PASS 行会同时打印 `active=8/8 blocked=1` 与 historical 说明。

### 9.4 门禁脚本改动清单（`source-tv-coverage-check.sh`）

- header 重写：输入解析现状、三件替换 + review12 BLOCKED、8/8 与 8/9 的区别、"Accounting" 一节把 917,543 / 895,939 / 834,460 并列解释；cost 行改八份/628 MB/~834k 包；`--mutation` 说明改为"每活动行一红，工作计数红单列"。
- `DEMOS` 8 行（删 review12，三条替换值按实测填）；`REPLACED` / `REPLACED_SHA` / `REPLACED_NOTE` 各 +3 条。
- 新增 `BLOCKED_INPUTS` / `BLOCKED_NOTE`，启动打印 active/blocked 与每行 override。
- `W2_FLOOR=895000` → `W2_PIN=834460`，W2 断言与 mutation 工作计数都改用它。
- PASS 行：`SOURCE-TV-COVERAGE=PASS (active=8/8 blocked=1 packets=834460 pin=834460 assertions_ok=N)` 并附一行 historical 8/9 说明。
- 断言本体（A1–A6、W1）与退出码语义未动。

### 9.5 本轮没做 / 不能声称的

- **正负控已实跑（14:56，lockrun owner=lead-sourcetv）**：正向 rc=0、49 ok / 0 fail、`SOURCE-TV-COVERAGE=PASS (active=8/8 blocked=1 packets=834460 pin=834460)`；负控 rc=0、`MUTATION-CAUGHT=PASS`。完整读数、证据 dump 更新与「没有证明什么」的边界见 §10。
- cp_reckoner STV 已由用户提供并归档（`E:/demo测试/cp_reckoner/STV/546392288740b01f46035778bbc3b9ed_match-20261115-2106-cp_reckoner.dem`，75,843,581 B，sha256 `6736334f4657172cef3d5b5d547b6b29f5a40d4918cf9ba7bd347f0c7e6aa541`），但**尚未接入本门禁**，按裁决作为新增恢复任务处理。
- 不声称"Source TV 覆盖恢复到九份"：活动集是八份，历史目标 8/9。

## 10. 实跑结果（2026-11-16 14:56，lockrun owner=lead-sourcetv，`--refresh` 全量重解）

- 命令：`bash tools/lockrun.sh lead-sourcetv -- bash source-tv-coverage-check.sh --refresh`，同一窗口接着跑 `--mutation`。日志：`D:/_shared/EXEC-2026-11-16/INST5/source-tv/gate-positive5.log`、`gate-mutation5.log`。
- 正向 rc=0：`demos_read=8 packets_scanned=834460 entity_failures=0 assertions_ok=49 assertions_fail=0`；汇总 `SOURCE-TV-COVERAGE=PASS (active=8/8 blocked=1 packets=834460 pin=834460 assertions_ok=49)`；并打印 `coverage: active set 8/8; historical 9-recording target 8/9 verified (review12.dem BLOCKED-INPUT)`。启动行逐条打印 8 个 input override 与 1 条 blocked input。
- 负控 rc=0：P1 `assertions_fail=8`、P2 `assertions_fail=8`，`MUTATION-CAUGHT=PASS (2/2 caught: per_demo_reds=8 work_reds=0 red_rows=16)`；A6 对每个替换槽逐条核 sha256（review1 `4edc6058…`、review44 `032bbd5a…` 等）。
- 证据更新（`--refresh` 的直接后果，随本次提交）：`review1.dem.txt` 91,247 包 / 82,903,524 B → 69,643 包 / 64,122,700 B；`gullyscout.dem.txt`、`processdemo.dem.txt`、`review44.dem.txt` 改为三个替换件的实测读数（120,922 / 120,920 / 140,020 包）；`review12.dem.txt` 保持不动（BLOCKED-INPUT，历史读数留档）。
- lockrun 打印 `LOCKRUN=TRACKED-DIRTY … evidence changed during this run`：门禁写自身证据目录所致，属预期，不是 D40/D49 违规（回落点只有 evidence/source-tv-coverage）。
- 本节证明：活动八份语料端到端零失败、包数精确命中 834,460 pin、扰动逐行被捕获、替换件身份未漂移。**没有证明**：九份覆盖已恢复（历史 8/9）、cp_reckoner 已接入（未接入，按新增恢复任务）、其他门禁或语料的情形。