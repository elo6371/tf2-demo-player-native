# N19（实例4-P2）：世界模型四层预算门禁在 candidate/e5 上实跑

- 卡片：T105-E5 实例4 P2/N19（角色武器/音频/导出线），执行者 card-char
- 仓库：D:/TF2_Native_Worktrees/t105-e5，分支 candidate/e5
- 时间：2026-11-16 00:44:00–00:45:01（锁窗口 00:45–01:10 内），机器锁 owner=card-char
- 产品 exe：native/build-nmake/tf2_demo_native.exe
  sha256 `e61c7fd9e5dba5167463f493937b23be799c96bcdd35bee7e0970d747a3bb078`，3,025,408 B，mtime 2026-11-15 22:58:31
  （跑前跑后同一 sha；本次没有重新编译，没有改任何产品源码）
- demo：a9630c6557468b24f0b8c2e7dab38ed8_match-20261030-2145-pl_upward_f12.dem
  sha256 `7e047aff1f7743915e355eac907a95dc3d981aa0fae5591b6b15346fb77dc4a3`
- 门禁脚本：仓库根 attach-weapon-budget-check.sh，sha256 `d2e6e442f3d37521659af69264b821d1927a02c900e9bb440b50a58c3cc17391`
- 日志：D:/_shared/EXEC-2026-11-15/INST4/p2-n19/attach-budget-run.log
- 工件：D:/_shared/EXEC-2026-11-15/INST4/p2-n19/attach-budget/（frame.bmp / frame.trace / frame.log）

## 0. 三句话结论（三件不同的事，分开写）

1. **编译**：本次没有编译。没有改产品源码，exe sha 跑前跑后一致。
2. **门禁跑成功**：28 条断言 0 失败，末行 `ATTACH-WEAPON-BUDGET=PASS`，锁从 ACQUIRED 到 RELEASED 干净，无 STALE-LOCK 回收。
3. **像素**：本门禁不主张逐模型像素正确（见 §6）；它主张的是四层账目、具名供给、零拒绝、零 E_OUTOFMEMORY，以及抓帧路径在工作。

## 1. N19 要回答什么

N19 原文要求：先把现有容量常量/池盘点清楚，针对**真实**的越界/OOM/实例 key 冲突做**最小**修复与受限输入控制；
不是把常量放大；CPU 分配、索引容量、GPU 预算、生命周期要分开；拒绝必须具名；有效输入仍然可用。

所以本次判据是四问：

- 容量常量是否仍然原值（64 / 96）？—— 若被放大，这个方案就是错的。
- 受限输入是否真的生效（有东西被延后）？—— 若 deferred=0，门禁是空转。
- 该给的模型是否**按名字**供给、且没有任何一个被拒绝？
- 有效输入是否仍可用（这帧真的抓出来了吗）？

## 2. 第一次跑为什么"假失败"（根因：行尾，不是产品）

第一次跑（本日 00:0x，同一门禁）日志里是：

    attach-weapon-budget-check.sh: line 101: --demo: command not found
    line 99: --tf-root / 100: --settings-file / 102: --capture-frame
    line 103: --window-size / 104: --debug-camera / 105: --player-animation 同上
    rc=0 frame=no，frame.trace 33,962,017 B（残篇），frame.log 空

根因：脚本整份是 CRLF（196 个 CR），HEAD blob 是 LF。反斜杠续行后紧跟 CR，bash 不认续行，
每个续行变成一条独立命令（每行一个 "command not found"）；最后一行是纯重定向，于是 **rc=0、无帧、账目全空**。
产品没有失败，门禁本身没跑起来。

（该主机 core.autocrlf=true，根目录 *.sh 检出即 CRLF；产线的 .gitattributes `/*.sh eol=lf` 还没落地。）

## 3. 最小修复（两处，均不碰产品源码）

1. 行尾归一：`sed -i 's/\r$//' attach-weapon-budget-check.sh`，CR=0。归一后文件内容与 HEAD blob
   （`52f614ac06f32380c3748a31ce0616859bfbd4824653bdbeeb25667990a34460`）逐字节一致，`git diff --quiet` rc=0。
2. EOL 守卫（D93）：`cd "$(dirname "$0")"` 之后加 9 行——`grep -q $'\r' "$0"` 命中就 FATAL + exit 2。
   这条守卫把"CRLF 静默空跑并报出看似真实的结果"变成"按名字失败"。`bash -n` 通过。

修复前 sha256 `f630f581…`（全仓 git grep 无引用，未被 pin）→ 修复后 `d2e6e442…`（本次实跑用的就是它）。

## 4. 实跑读数（同一帧）

命令（门禁自己拼的，逐字抄自日志头）：

    native/build-nmake/tf2_demo_native.exe --tf-root D:/SteamLibrary/steamapps/common/Team Fortress 2/tf \
      --hidden-window --audio-disabled --settings-file <tmp>/settings.ini --ui-settings-file <tmp>/ui.ini \
      --demo D:/TF2_Demo_Player/testdata/demos/a9630c65…pl_upward_f12.dem \
      --capture-tick 30001 --playback-speed 32 --capture-frame <OUT>/frame.bmp \
      --window-size 1280x720 --debug-camera 1707.0 -125.1 152.8 4.1 0.0 \
      --player-animation --player-weapon-attach on

门禁侧包裹：`bash tools/lockrun.sh card-char -- env T85_ATTACH_OUT=D:/_shared/.../p2-n19/attach-budget bash attach-weapon-budget-check.sh`
（OUT 显式覆盖，未写 evidence/，避免 tracked 目录变脏；D71 风险点。）

| 读数 | 值 |
|---|---|
| run rc / frame / trace | 0 / yes / 10,100,518 B |
| attach supplied / refused / named-refusals / E_OUTOFMEMORY | 13 / 0 / 0 / 0 |
| 具名 supplied | c_flamethrower=1，c_shogun_warhorn=1，c_sapper_xmas=0（该 tick 未被持有，门禁自己也这么注） |
| layer A 准备预算 | capacity=64 candidates=98 admitted=64 built=64 deferred=34 failed=0 |
| layer B mesh 表 | capacity=96 resident=86 evictions=0 capacityRefusals=0 |
| layer C 实例表 | missingMesh=0 deferredNotResident=41 |
| layer D 本帧绘制 | droppedPlayers=0 weaponAttach-overBudget=0 |
| 世界模型 | prepared=64 uploaded=64 |
| 优先级 admitted | player:9 weapon:43 overlay:0 prop:12 firstPersonOnly:0 |
| 优先级 deferred | player:0 weapon:0 overlay:0 prop:25 firstPersonOnly:9 |
| 抓帧尺寸 | 1264x681（= --window-size 1280x720 的 client 区，门禁断言值） |

## 5. 28 条断言（全 PASS）的分组与"为什么能防住错误修复"

- 身份类：demo sha、attach 账目存在、layer A/B 账目存在。
- 缺陷签名类：supplied≥1、flamethrower 与 warhorn **按名字**供给≥1、三模型都不出现在拒绝行、E_OUTOFMEMORY=0。
- 防"放大常量"：**layer A capacity 仍然是 64**、**layer B capacity 仍然是 96**、B resident≤cap。
- 受限输入真的生效：**deferred≥1（本帧 34）**——若预算实际没绑定，这条会红。
- 优先级真的起作用：player 身体 admitted≥1（9）、手持武器世界模型 admitted≥1（43），且两者 deferred 都是 0。
- 生命周期/实例：layer C missingMesh=0、layer D droppedPlayers=0、held weapon 想要 slot 的都拿到了（0 缺失）、overBudget=0。
- 有效输入仍可用：rc=0、frame=yes、抓帧尺寸等于 pinned window 的 client 尺寸。
- 末行：`ATTACH-WEAPON-BUDGET=PASS`（ledger-recon UNANCHORED 清单 8.5-5 的债锚，拼写未漂移）。

## 6. 与 P0 结项说明的关系（不要把两件事混起来）

- P0 的"同帧像素不变"（sha256 `d297340f…`，a3c93d57 ↔ t85-budget）是**那次 before/after** 的证据；本文没有重复该实验，也不主张像素。
- P0 说明 §7.4：`--capture-tick` 是**播过去再回夹**，不是 seek；trace 里的 tick 是播放域。所以本门禁的"抓帧"只能证明抓帧路径与尺寸，不能推出"用户在该 tick 看到的画面"。
- P0 说明 §7.5：exe sha 不是源码身份（同源码多次构建 sha 不同）。本次 exe 是基线冻结件，跑前跑后一致只证明"没换二进制"。
- P0 说明 §7.6：模型"被供给/被准备"有读数；"在像素里被画出来"只有实体覆盖账，无逐模型像素证据。本文同样只到这层。
- P0 §6 里 `attach-weapon-budget-check.sh | 已写、未跑（等合并树）` 这一行，**本次已关闭**（在 candidate/e5 上跑通并 PASS）。

## 7. 工件与哈希

| 文件 | 大小 | sha256 |
|---|---|---|
| frame.bmp | 2,582,406 B（24bpp 1264x681，BMP 头自洽：54+1264*681*3） | `dfaf5500999ef013190b550839d24cb5edc653fe988b126e87d74f262cb36053` |
| frame.trace | 10,100,518 B | `a3b046b871a35c5e5ea4f44ac41975a4c6386578447d0140728e27827edf87c6` |
| frame.log | 0 B（hidden-window 下正常） | — |
| attach-budget-run.log | 3,865 B | 在 _shared，未入库 |

两次 exe sha 前后一致；跑后 `git status --porcelain -- evidence native/tests/fixtures` 为空，lockrun 未报 TRACKED-DIRTY。

## 8. 未验证 / 开放项

1. **负控未跑**：P0 的 m14 变异（把 A 层优先级改回先到先得，要求具名红）本次没有跑；本文不声称负控通过。
2. `c_sapper_xmas` 在该 tick 未被持有（0 是合法值，不是失败）；它只在"被持有时必须按名字供给"的意义上被覆盖。
3. outline-check 整链、third-person-weapon-check、player-body-appearance-check、两进程同帧确定性：仍未验证（属合并树/其它卡）。
4. `.gitattributes` 的 `/*.sh eol=lf` 尚未落地（arch-ci 工作区里有未提交改动，不是我的写域）。落地前，EOL 守卫是唯一的"按名字失败"防线。
5. 本卡下一个节点：P3a/N20（Pyro 火焰 → Medic 治疗束 → 流血），见 docs/N20 相关记录。

## 9. 交接（给 lead / ledger-recon）

- 产出 token：`ATTACH-WEAPON-BUDGET=PASS`（28/28），落在 _shared 日志末行；ledger-recon 8.5-5 的债锚可结。
- 提交物（本文件 + 脚本 EOL 守卫 11 行），未动产品源码、未动 capacity 常量、未写 evidence/。
