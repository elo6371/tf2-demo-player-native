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