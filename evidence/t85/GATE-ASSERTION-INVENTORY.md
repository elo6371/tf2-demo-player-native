# T85 P0 门禁断言清单（outline-check.sh）

计数命令（lead m00911 指定，报告必须带命令）：

    grep -cE '^[[:space:]]*assert_(eq|ne|ge|le|lt|contains)\b' outline-check.sh

- HEAD（e15577b / integration/t79-final）:outline-check.sh = 102
- 本 worktree 当前 = **173**（102 → 167 是预算账目 + attach 覆盖账；167 → 173 是 §8.6 的帧尺寸组 4 条
  + 层 C 的 submitted 账 2 条，见 B6）

本文件回答两个问题：**新加了哪些断言（读哪个 trace 字段、判什么）**，**退役/改写了哪些老断言（它们原来代表哪个读数）**。
所有行号都指本 worktree 的当前 outline-check.sh；老断言行号指 `git show HEAD:outline-check.sh`。

## A. 退役/改写的老断言

| 老行号 | 老断言 | 它原来代表 | 为什么退役/改写 |
|---|---|---|---|
| HEAD:860 | `assert_eq "world pass model uploads unchanged by the overlay" ${WORLD_MODELS_UPLOADED} 59` | 在 snakewater outline 夹具上，overlay 的 9 个 class 模型没有挤掉世界 pass 的 59 个模型 | 59 是**这段录像的偶然值**（候选集换个 demo 就变），钉死它等于把巧合当契约；而且它只覆盖"overlay 不抢"这一个方向，看不到"目标模型被拒"。改写成 A/B 账目：候选=准入+延后、built+failed=admitted、uploaded=prepared（见 B 区 1045–1058） |
| HEAD:1850 | `assert_ge "cap 64: the old limit REFUSES class meshes (this is what 96 buys)" ${CAP64_REJECTED} 1` | 把 kEntityModelMeshLimit 打到 64 时，9 个 class mesh 里有 >=1 个被 E_OUTOFMEMORY 拒掉——这就是 96 买来的东西 | 它是**把缺陷当对照**：class 模型被拒正是本轮要消灭的行为，不能留成绿断言；而且锚点 `kEntityModelMeshLimit = 96` 已改名迁到 .cpp，原样保留会让 patch_in 直接 MUTATION-INVALID 退出 3。换成"容量缩到 24 的边界变异"（见 B 区 2136–2145） |
| HEAD:1859 | `assert_eq "cap 96: every prepared class mesh is uploaded" ${CAP96_REJECTED} 0` | 96 的容量下 9 个 class mesh 全部上传 | 语义保留，读数来源从 overlay 自报切到层 B 账目：`budget: meshTable capacityRefusals==0` + overlay `rejected==0`（B 区 2167） |
| HEAD:1860 | `assert_eq "cap 96: uploaded count matches prepared" ${CAP96_UPLOADED} ${CAP96_PREPARED}` | uploaded==prepared（没有静默丢） | 语义保留、扩到三层：`uploaded==prepared`（1058）+ `resident<=capacity`（1111）+ `每一 slot 有具名去向`（1120） |

## B. 新增/改写的断言

### B1. 世界模型具名交付段（assert_world_fixture，跑 Highlander STV pl_upward_f12 夹具）

| 行 | 断言名 | 读的字段 | 判据 |
|---|---|---|---|
| 232 | named world models: the upward fixture demo is the pinned file | `sha256sum ${T85_WORLD_DEMO}` | == 7e047aff…dc4a3（夹具自证身份，缺失即红，不 skip） |
| 233 | named world models: the upward map is the installed pl_upward_f12 BSP | `stat -c%s ${TF}/maps/pl_upward_f12.bsp` | == 25252306 |
| 245 | named world models: the upward multiply-held-weapons run rc | 进程返回码 | == 0 |
| 246 | named world models: the run captured a frame | `stat -c%s ${prefix}.bmp` | >= 1 |
| 255 | named world models: c_flamethrower's world mesh was supplied (it used to OOM) | trace 里 `attach: mesh result=supplied path=models/weapons/c_models/c_flamethrower/c_flamethrower.mdl` 的行数 | >= 1 |
| 256 | named world models: c_shogun_warhorn's world mesh was supplied (it used to OOM) | 同上，warhorn 路径 | >= 1 |
| 257 | named world models: no refusal line names any of the three | `attach: mesh result=refused` 且命名三者之一的行数 | == 0 |
| 258 | named world models: E_OUTOFMEMORY 0x8007000E never appears in the trace | trace 里 `0x8007000E` 行数 | == 0 |
| 276 | named world models: the run saw a candidate set | `budget: prepare` 的 candidates | >= 1 |
| 277 | named world models: every candidate is admitted or deferred | admitted+deferred | == candidates |
| 278 | named world models: the prepare budget is 64 (not raised) | capacity | == 64（明确钉住"没靠加常数"） |
| 282 | named world models: the budget actually binds (some candidates are deferred) | deferred | >= 1（证明这段录像上预算真的紧；见 §6 变异读数） |
| 283 | named world models: no admitted model failed to build | failed | == 0 |
| 284 | named world models: the table never exceeds its bound | meshTable resident | <= capacity |
| 285 | named world models: the table refused nothing at the shipped bound | capacityRefusals | == 0 |
| 286 | named world models: no resident mesh was evicted at the shipped bound | evictions | == 0 |
| 288 | named world models: the instance list has slots | `budget: instances` slots | >= 1 |
| 289 | named world models: the accepted instances fit the instance list | accepted | <= slots |

附注：277/278/282 三条里，**282（deferred>=1）是这段录像特有的"预算确实在紧"证据**；回退优先级的变异下它是否仍红，实测数见 §6。

### B2. 主 outline 夹具上的四层账目

| 行 | 断言名 | 读的字段 | 判据 |
|---|---|---|---|
| 892 | attach: the third-person attach run rc | attach 专用产品运行返回码 | == 0（新增的第二次启动：`attach: meshSource` 只在 `--player-weapon-attach` 下打印） |
| 1045 | layer A: the prepare ledger line is present in the trace | `budget: prepare` 行数 | >= 1（先证明存在，再谈相等——防止两边都读成 `<none>` 的空相等） |
| 1046 | layer A: the prepare ledger read a candidate set (a number, not `<none>`) | candidates | >= 1 |
| 1047 | world pass published a model count (gpu: models done) | WORLD_MODELS_PREPARED | >= 1 |
| 1048 | layer A: the prepare budget is 64 (not raised -- a bound, not a moved cut) | capacity | == 64 |
| 1049 | layer A: every candidate is admitted or deferred (the ledger accounts for all of them) | admitted+deferred | == candidates |
| 1050 | layer A: admitted never exceeds the budget | admitted | <= capacity |
| 1056 | layer A: every admitted model is built or its failure is counted (no admission vanishes) | built+failed | == admitted |
| 1057 | layer A: no admitted world model failed to build | failed | == 0 |
| 1058 | layer A -> B: every prepared world model is uploaded (nothing silently dropped) | WORLD_MODELS_UPLOADED | == WORLD_MODELS_PREPARED |
| 1099 | layer B: the meshTable ledger line is present in the trace | 行数 | >= 1 |
| 1100 | layer B: the meshTable ledger names a capacity | capacity | >= 1 |
| 1101 | layer C: the instances ledger line is present in the trace | 行数 | >= 1 |
| 1102 | layer C: the instances ledger names a capacity | capacity | >= 1 |
| 1103 | layer D: the draw ledger line is present in the trace | 行数 | >= 1 |
| 1104 | layer D: the draw ledger names a capacity | capacity | >= 1 |
| 1105 | attach: the meshSource ledger line is present in the attach run's trace | 行数 | >= 1 |
| 1109 | attach: the mesh source actually supplied a held-weapon world model | supplied | >= 1 |
| 1110 | layer B: the cache bound is 96 (kept -- raising it is the rejected fix) | capacity | == 96 |
| 1111 | layer B: resident never exceeds the bound | resident | <= capacity |
| 1112 | layer B: no capacity refusal at the shipped bound (the held weapons fit) | capacityRefusals | == 0 |
| 1113 | layer B: no key was re-uploaded (an attach mesh is uploaded once) | reuploads | == 0 |
| 1114 | layer C: the instance bound is published as its own number | capacity | == 96 |
| 1115 | layer C: every draw's mesh exists (an evicted key never reaches the instance list) | missingMesh | == 0 |
| 1120 | layer C: every submitted slot is accepted or rejected for a named reason | accepted+missingMesh+deferredNotResident+badMesh+nonFinite+budget | == slots |
| 1123 | layer C: no submitted instance was dropped for a budget reason at the shipped bound | budget | == 0 |
| 1124 | layer D: no player body was dropped by the draw budget | droppedPlayers | == 0 |
| 1129 | layer D: player + prop + attach sub-budgets add up to the one draw capacity | playerBudget+propBudgetAfterAttach+attachReserved | == capacity |
| 1131 | layer D: no player draw exceeds its reserved budget | playerDrawn | <= playerBudget |
| 1132 | layer D: no attach draw exceeds its reserved budget | attachDrawn | <= attachReserved |
| 1133 | attach: no held-weapon world mesh was refused (the three non-target weapon models no longer OOM) | refused | == 0 |

### B3. 预算边界的负向控制（96 → 24 缩容，是 outline-check `--mutation` 的一部分）

| 行 | 断言名 | 读的字段 | 判据 |
|---|---|---|---|
| 2136 | budget boundary: the product reports the capacity it was built with | 缩容版 `budget: meshTable` capacity | == 24 |
| 2137 | budget boundary: the cache never exceeds its capacity | resident | <= 24 |
| 2138 | budget boundary: the policy EVICTS instead of overfilling | evictions | >= 1 |
| 2142 | budget boundary: every eviction names the model key it took (no anonymous drop) | `budget: eviction key=` 行数 | >= 1 |
| 2143 | budget boundary: the named eviction is a model path, not an index | 该行含 `key=models/` | 相等（文本） |
| 2145 | budget boundary: the named eviction says it unpublished and will re-supply the path | 该行含 `action=unpublish,resupply` | 相等（文本） |
| 2163 | shipped budget: capacity is the number the source declares | 恢复版 capacity | == 96 |
| 2164 | shipped budget: nothing was evicted at the shipped capacity | evictions | == 0 |
| 2165 | shipped budget: no capacity refusal at the shipped capacity | capacityRefusals | == 0 |
| 2166 | shipped budget: no instance lost a missing mesh | missingMesh | == 0 |
| 2167 | shipped budget: every prepared class mesh is uploaded | overlay rejected | == 0 |
| 2168 | shipped budget: uploaded count matches prepared | overlay uploaded | == prepared |

### B4. mutate.sh case m14（行为变异，长期防线）

一次构建 + 一次 `outline-check.sh --world-only` 产品启动。patch 的锚点是 prepare 准入的
`std::stable_sort` 比较体（本 worktree main.cpp:3825-3828），把它换成先到先得；断言：

- `OUTLINE-CHECK=FAIL (--world-only`（must_refuse：退出码非 0 且文本命中）
- 至少 1 条 `FAIL named world models:` 具名行（计数 >= 1）——退出码本身分不清"预算耗尽"和"demo 没载入"
- `deferred >= 1` 仍然成立（证明 defer 计数器不是只在排序下成立）

这条 case 的直接目的：**只把 96 抬到 160/192 的构建过不了它**——抬容量不改排序，目标集合照样被先到先得挤掉。

### B5. attach 覆盖账 + 实例拒绝拆分（assert_world_fixture，lead m01475 / m01484）

这 8 条回答"before supplied=23 vs after supplied=13 是不是覆盖缩水"以及"rejected=37 是不是 37 个坏实例"。
全部读 Highlander STV pl_upward_f12 夹具（pyro，tick 30001）那一轮 trace；行号是本 worktree 当前 outline-check.sh。

| 断言行 | 断言名 | 读的 trace 字段 | 谓词 |
|---|---|---|---|
| 347 | attach reports its request accounting | `attach: coverage requested=` 行数 | >= 1 |
| 348 | every attach request left through a named exit (coverage balances) | 每行 requested == resolvedFromWorld+uploaded+refused+retrySkipped+unresolvedPath | 违例行数 == 0 |
| 349 | the hand's weapon world models are covered | uploaded + resolvedFromWorld | >= 1 |
| 352 | coverage ledger agrees with the mesh-source ledger | coverage.uploaded vs meshSource.supplied | 相等 |
| 369 | nothing that should exist is missing its mesh | budget: instances 的 rejected=missingMesh:N | == 0 |
| 370 | deferred instances are counted as deferred, not dropped | rejected=deferredNotResident:N | >= 1 |
| 371 | the frame has weapon-holding bodies | scene: weaponAttach held | >= 1 |
| 372 | every weapon-holding body got its world mesh | scene: weaponAttach drawn vs held | 相等 |

实测（evidence/t85/upward-pyro/coverage.trace，402 帧）：548 行覆盖账、逐行平衡违例 0；
最后一行 requested=5988 resolvedFromWorld=5975 uploaded=13 refused=0 retrySkipped=0 unresolvedPath=0；
missingMesh=0、deferredNotResident=37；held=13、drawn=13。

两条为什么不是恒真（这是本轮被 lead 反复要求的地方）：

- 348 的平衡是**每行**都算的，不是只看最后一行：任何一个新加的早退/新加的拒绝分支只要忘了记账，
  或者把 uploaded 加两次，都会让某一行不平衡。它挡的是"路径悄悄消失"。
- 370 的 deferred>=1 读的是层 A 的推迟在层 C 的可见后果：如果哪天有人把层 A 的预算放大到装得下全部候选
  （或把 deferred 的实例直接丢掉不计数），这条会变红；它同时证明 37 这个数字的来源是预算而不是缺陷。
- 352 让两份自报账（coverage 与 meshSource）互相校验：同一轮里两个数不一致说明其中一行是陈旧的。

注意口径：`attach: coverage` 的计数是**整轮累计**（402 帧、每帧 13~17 次请求），
所以 349 的 >=1 只证明"覆盖存在"，不证明"每帧都覆盖"；每帧覆盖由 371/372 的 held==drawn 负责。

### B6. 帧尺寸与层 C 提交数（最后一组离线改动，未编译）

167 → 173 的 6 条。前 4 条对应本轮的 --window-size 与 capture: frame 打印；后 2 条是层 C 的口径修正。

| 断言行 | 断言名 | 读的 trace 字段 | 谓词 |
|---|---|---|---|
| 1085 | the speed=1 capture and the speed=$SPEED capture have the SAME frame size | 两个 trace 各自的 capture: frame width=.. height=..（取最后一行） | 两者相等（WxH） |
| 1270 | layer C: the instances ledger names how many instances were submitted | budget: instances 的 submitted= 是否存在 | >= 1 |
| 1293 | layer C: every submitted instance is accepted or refused for a named reason | accepted + missingMesh + deferredNotResident + badMesh + nonFinite + budget vs submitted | 相等（每个提交都有具名去向） |
| 1299 | layer C: every accepted instance wrote at least one instance row | slots vs accepted | slots >= accepted（每实体至少一行；一行 = 一个 material run） |
| 1323 | frame size: every capture of this lease printed its frame size | $OUT/*.trace 里 capture: frame 去重后的尺寸个数 | >= 1 |
| 1324 | frame size: every capture in this lease came out at ONE size | 同上 | == 1（一轮只有一个尺寸） |
| 1325 | frame size: the captured frame is the size of the window it was created at | 上面的尺寸 vs window: created ... client=WxH | 相等（自报尺寸与窗口自洽） |

口径说明（这段是这次 FAIL 的根因，写下来防止再犯）：

- **层 C 的 slots 不是实体数**。native/src/native_renderer_entity.cpp:588 的
  entityModelInstanceSlotCount_ = packed.size() 是 instance ROW 数，一个实体按 material run 拆成多行。
  实测 2026-11-07：accepted=95 slots=236 rejected=deferredNotResident:1（snakewater/fixture 段）
  与 accepted=59 slots=170（pl_upward 夹具）—— 两次都说明 accepted+deferred==96==容量，账本来就是平的，
  红的是我拿 236 去比 96。
- **submitted 必须由产品给**：实例表在满 96 之后 continue，所以"提交了多少"不能从 slots 反推；
  现在 budget: instances 里带 submitted=%zu（main.cpp 的 draws.size()），门禁只跟它比。
- **条数统计命令**（报告必须带）：grep -cE '^[[:space:]]*assert_(eq|ne|ge|le|lt|contains)\b' outline-check.sh
  → HEAD 102、当前 173。

### B7. 新门禁脚本 attach-weapon-budget-check.sh 的末行 token（ledger-recon 8.5-5）

该脚本（未被 verify-all 登记，按 lead m00816 的选项 (a) 其断言已折进 outline-check.sh 的
assert_world_fixture()；删除它的前提是 outline 在合并树跑绿）末行固定打印
ATTACH-WEAPON-BUDGET=PASS 或 ATTACH-WEAPON-BUDGET=FAIL，供 debt-claim 锚定；token 必须是最后一行。
它的具名断言里包含优先级那组：player 被准入 >=1、weapon 被准入 >=1、player 被 defer ==0、weapon 被 defer ==0，
以及"预算在这段录像上真的紧（deferred >= 1）"。
