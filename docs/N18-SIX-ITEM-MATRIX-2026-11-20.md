# N18 六项验收矩阵（2026-11-20）

状态：12 跑抓帧全部完成（11 跑批 + 1 跑控制，全部 `rc=0`），六项验收逐项有可复核读数。
证据目录 `evidence/n18-six-items/`（读数、像素差、md5、裁切图）；大 trace（18–33 MB/份）不入库，留在 `.scratch/n18/`。
行号按当前工作树：HEAD `fb6236df`，`native/src/main.cpp` 带未提交的 N18-B 补丁（`git diff --stat` = 21 insertions(+), 78 deletions(-)）。
本矩阵闭合 `docs/N18-FIRSTPERSON-VISIBILITY-2026-11-20.md:160` §6 的交叉组合缺口，并回答 `docs/N18-VIEWMODEL-POSE-SURVEY-2026-11-19.md:88` §7 的两条未决。

## §0 六项结论

| # | 项 | 结论 | 一句话证据 |
|---|----|------|-----------|
| 1 | 长枪 / 短武器正对照 | PASS | 同 tick 2000、同一手臂合成体，只换 `--viewmodel-weapon-model`：`c_sniperrifle` materials 2/2/2/2、vertices 23061、meshRadius 33.83 → `c_machete` 1/1/1/1、3405、18.37；像素差 4876 px，bbox 与 body off 完全重合 |
| 2 | 左右手 | PASS | `hand=left`、companion `attachment=weapon_bone_L`、pass `alignment=weapon_bone_L`、drawn 2341；grip 只差 0.01；像素差 12322 px 覆盖双手区 |
| 3 | 武器切换 | PASS | tick 2600 rifle(734, 208) → tick 2660 machete(810, 201, CTFClub)；trace 里 path 在 `scene: clock tick=2634` 处换；vmAnim 28→6；bodypaths 直方图 8×c_machete + 2326×c_sniperrifle |
| 4 | 开火 / reload 动画 | 构造式 PASS；录像内无自然触发 | `--viewmodel-apply-animation` → `applied=1 skinning=1 boneTableCoverage=58/58`；`--viewmodel-sequence 29` → `sequence=29` 对 `entitySequence=28`、`frame=50/51`、539 px 差异。11 跑里序列从不变化（步枪 28、砍刀 6） |
| 5 | 缺失回退 | PASS | 缺 vm：`resourceStatus=incomplete(mdl=0,vvd=0,vtx=0,mesh=0)` + `attachmentStatus=absent`，武器体照旧，帧与基线逐字节相同（0 px）；缺 body：`fallback=buildFailed` + `weaponReason=viewmodel was not found`，帧与 body-off 逐字节相同（同 bbox 4447 px）；两跑 `rc=0` 无崩溃 |
| 6 | 自身隐藏 | PASS | POV 11 跑一律 `removed=1 indices=9 candidates=320 submitted=322`（隐藏集变化时才打印，每份 trace 3 行）；STV 控制跑 `removed=1` 零次；body off 不改变隐藏；PMA 全 PASS |

## §1 配方与钉死的输入

### 1.1 二进制与录制

- 可执行：`native/build-nmake/tf2_demo_native.exe`（N18-B 工作树构建）。
- POV 录制：`E:/demo测试/pl_upward_f12/POV/autorecord_2026-08-08_21-43-59.dem`（本机唯一一份 pl_upward_f12 POV，大小 146,853,666 B、sha256 `14b4943a8d7e866677091bb5a4843882bfed1105bc5ef27d955bbc4cd3ad00e8`，与 `weapon-body-check.sh:114-117` 同一份；本批**没有**重算 sha，见 §5）。
- STV 录制（负控）：`E:/demo测试/pl_upward_f12/STV/a9630c6557468b24f0b8c2e7dab38ed8_match-20261030-2145-pl_upward_f12.dem`。
- TF root：`D:/SteamLibrary/steamapps/common/Team Fortress 2/tf`。

### 1.2 每跑公共参数（逐字，来自 `.scratch/n18-batch.sh`）

```
TF2_NATIVE_TRACE="$ROOT/$OUT/$name.trace" timeout 600 "$EXE" \
  --tf-root "$TF" --audio-device "" --hidden-window --audio-disabled \
  --demo "$demo" --settings-file "$OUT/iso/$name.ini" --ui-settings-file "$OUT/iso/ui.ini" \
  --capture-frame "$OUT/$name.bmp" --capture-tick "$tick" --metrics-file "$OUT/$name.csv" \
  --viewmodel-fov 90 "$@"
```

- `--settings-file` / `--ui-settings-file` 指到 `.scratch/n18/iso/` 的每跑副本：产品在正常退出时会写 settings.ini 与 ui.ini，门禁与取证都不许改动机器配置或被跟踪证据（同 `weapon-body-check.sh:96-103` 的约定）。
- **每跑都必须给 `--viewmodel`**。整个 `gpu: viewmodel` 读数族只在 viewmodel pass 打开时存在，而 pass 由 G8 latch 决定（`native/src/main.cpp:4560-4578`，`viewModelRequested = !commandViewModel.empty()` 在 `:8391`）：不给 `--viewmodel` 且没设 `TF2_NATIVE_VIEWMODEL_PATH` 时 pass 根本不开。`stv-control` 就是这条负控 —— 它的 companion / path / vmanim / pass 四段全空（`evidence/n18-six-items/readings.txt:151-159`）。
- `--viewmodel-fov 90` 钉平 viewmodel pass 的 FOV，免得读数里的 `fov=90.0 settingsFov=90.0` 随每跑 settings 副本漂移。

### 1.3 12 跑

| 跑 | `--capture-tick` | 录制 | 额外参数 |
|----|------|------|---------|
| `body-rifle` | 2000 | POV | `--viewmodel $VM --viewmodel-weapon-model $RIFLE` |
| `body-rifle-off` | 2000 | POV | 同上 + `--viewmodel-weapon-body off` |
| `body-machete` | 2000 | POV | `--viewmodel $VM --viewmodel-weapon-model $MACHETE` |
| `body-rifle-left` | 2000 | POV | `--viewmodel $VM --viewmodel-weapon-model $RIFLE --viewmodel-hand left` |
| `body-rifle-anim` | 2000 | POV | `--viewmodel $VM --viewmodel-weapon-model $RIFLE --viewmodel-apply-animation` |
| `body-rifle-seq29` | 2000 | POV | 同上 + `--viewmodel-sequence 29` |
| `missing-vm` | 2000 | POV | `--viewmodel models/weapons/v_models/v_does_not_exist.mdl` |
| `missing-body` | 2000 | POV | `--viewmodel $VM --viewmodel-weapon-model models/weapons/c_models/c_does_not_exist.mdl` |
| `sw-before` | 2600 | POV | `--viewmodel $VM` |
| `sw-after` | 2660 | POV | `--viewmodel $VM` |
| `stv-control` | 2000 | STV | 无 `--viewmodel`（负控） |
| `body-default` | 2000 | POV | `--viewmodel $VM`（控制跑，`.scratch/n18-control.sh`） |

`$VM = models/weapons/v_models/v_scattergun_scout.mdl`、`$RIFLE = models/weapons/c_models/c_sniperrifle/c_sniperrifle.mdl`、`$MACHETE = models/weapons/c_models/c_machete/c_machete.mdl`。

### 1.4 tick 为什么是 2000 / 2600 / 2660

- 这份 POV 在 **clock tick 2634** 换武器（§2.4 的 trace 行），所以 2000 落在**步枪段**、2660 落在**砍刀段**，2600/2660 这一对夹住切换。
- 门禁 `weapon-body-check.sh` 的 `CAPTURE_TICK=3000`（`:148`）落在砍刀段，钉的是 `EXPECT_WEAPON_ENTITY=810 / EXPECT_WORLD_MODEL_INDEX=201 / EXPECT_WEAPON_CLASS=CTFClub`（`:136-139`）与 `EXPECT_WEAPON_VIEW_MODEL_INDEX=1032`（`:145`）。本矩阵 tick 2000 的链是**步枪**（`entity=734 worldModelIndex=208 path=c_sniperrifle`），两套链不要混读（§5 第 6 条）。

## §2 逐项读数

读数族与发射点（工作树）：

| 读数族 | 发射点 |
|--------|--------|
| `gpu: viewmodel request` | `native/src/main.cpp:8785` |
| `gpu: viewmodel grip` | `native/src/main.cpp:11900` |
| `gpu: viewmodel weaponBodyReading` | `native/src/main.cpp:11873` |
| `gpu: viewmodel pass order` | `native/src/main.cpp:11788` |
| `gpu: viewmodel weaponBodyMaterial` | `native/src/main.cpp:11919` |
| `gpu: viewmodel companion` | `native/src/main.cpp:14598` |
| `scene: weaponBody` | `native/src/main.cpp:14779` |
| `scene: vmAnim`（逐帧 `:14945`，汇总 `:14957`） | `native/src/main.cpp:14945` |
| `scene: cameraFilter` | `native/src/main.cpp:12894` |
| `scene: clock` / `scene: eye` | `native/src/main.cpp:12257 / :12425` |
| `scene: PLAYER-MODEL-ASSERT` | `native/src/main.cpp:14218` |
| `spectate: summary` | `native/src/main.cpp:15812` |

以下是逐字行，长行中间用 `…` 省略的字段只为排版，完整原文见 `evidence/n18-six-items/readings.txt` 与三个 `raw-*.txt`。

### 2.1 基线 `body-rifle`（tick 2000，步枪链）

```
gpu: viewmodel grip armsGrip=(-72.98,-26.10,-65.42) weaponGrip=(-72.98,-26.10,-65.42) residual=0.0000 armsCentreModel=(0.00,52.05,-1.80) weaponCentreModel=(0.76,6.25,14.08)
scene: weaponBody entity=734 class=CTFSniperRifle worldModelIndex=208 path=models/weapons/c_models/c_sniperrifle/c_sniperrifle.mdl refusal=none fallback=- reason=the weapon entity's own m_iWorldModelIndex names the weapon body observations=1791 named=1791 present=1791 refusals=none:1791
gpu: viewmodel weaponBodyReading armDrawCalls=1974 weaponDrawCalls=1974 weaponModelIndex=208 weaponModelPath=…/c_sniperrifle.mdl weaponMaterials=2/2/2/2 weaponFallback=- weaponVisible=1 weaponReason=drawn weaponEntity=734 weaponClass=CTFSniperRifle weaponMeshRadius=33.83 weaponVertices=23061 weaponClip=(2.889,-16.751,88.213,89.162) weaponMaterialsFailure=-
gpu: viewmodel companion path=…/c_sniper_arms.mdl status=ok meshReady=1 sequences=43 bones=58 attachment=weapon_bone reason=-
gpu: viewmodel request path=…/v_scattergun_scout.mdl status=ok resolution=3 companions=1 meshReady=1 attachments=5 valid=5 partial=0 invalid=0 alignment=weapon_bone fov=90.0 hand=right reason=-
scene: vmAnim entity=685 sequence=28 entitySequence=28 rate=1.31 cyclePresent=0 model=…/c_sniper_arms.mdl status=ok poseAvailable=1 applied=0 frame=30/31 … owner=9 viewEntity=9 modelIndex=998 drawnModel=…/c_sniper_arms.mdl entityModelMatchesDrawn=1 sequenceInRange=1 bindPose=0 sequenceOverride=-1
gpu: viewmodel pass order worldPass=1 viewModelPass=8 afterWorld=1 drawCalls=1974 vertices=9996 depthWrite=1 depthFunc=less_equal skinning=0 boneTableCoverage=0/0 animationInput=ok meshRadius=29.88 alignment=weapon_bone vmAnimInput=ok vmAnimApplied=0 drawnModel=…/c_sniper_arms.mdl … modelMismatch=0 boneMismatch=0 bindPose=0 ranges=2 boundSlots=2
scene: PLAYER-MODEL-ASSERT=PASS tick=2177 checked=11 indexResolved=0 noIndex=11 classFallback=11 mismatched=0 unresolved=0 firstMismatch=-
```

- `residual=0.0000`：`bip_hand_r` 到 `weapon_bone` 的距离为 0 —— 世界武器体与手臂用的是同一条 hand 链（`docs/N18-FIRSTPERSON-VISIBILITY-2026-11-20.md` §1）。
- `weaponClip=(2.889,-16.751,88.213,89.162)` 是武器体在屏幕上的裁剪盒（x, y, 宽, 高，像素口径），后文各项与它比位置。
- `PMA tick=2177` 不是 `--capture-tick`：PMA 打印的是 `resolvedSceneTick`（`main.cpp:14218`，server 空间），11 跑都是 2177 是因为末条 PMA 落在 `scene: clock tick=0` 附近，而该处 `serverTick=2177`（`.scratch/n18/sw-after.trace:489`）。PMA 只用于「有没有把人画错」，不用于计时。

### 2.2 项 1：长枪 → 短武器（只换 `--viewmodel-weapon-model`）

`body-machete` 与 `body-rifle` 的**实体与索引完全不变**（734 / CTFSniperRifle / 208），只有被画的模型变了：

```
scene: weaponBody entity=734 class=CTFSniperRifle worldModelIndex=208 path=…/c_machete/c_machete.mdl refusal=none fallback=- reason=the weapon entity's own m_iWorldModelIndex names the weapon body observations=1932 …
gpu: viewmodel weaponBodyReading armDrawCalls=2160 weaponDrawCalls=2160 weaponModelIndex=208 weaponModelPath=…/c_machete.mdl weaponMaterials=1/1/1/1 weaponFallback=- weaponVisible=1 weaponReason=drawn weaponEntity=734 weaponClass=CTFSniperRifle weaponMeshRadius=18.37 weaponVertices=3405 weaponClip=(1.182,-20.681,92.783,93.089) weaponMaterialsFailure=-
gpu: viewmodel weaponBodyMaterial ranges=1 texturedRanges=1 fallbackRanges=0 boundSlots=1 keys=materials/models/weapons/v_machete/v_machete.vtf … override=1 slotOverride=-1
gpu: viewmodel grip armsGrip=(-72.98,-26.10,-65.42) weaponGrip=(-72.98,-26.10,-65.42) residual=0.0000 armsCentreModel=(0.00,52.05,-1.80) weaponCentreModel=(-0.04,2.61,8.89)
```

判据全部落在「同一个武器实体，换了一个模型」上：`weaponMaterials 2/2/2/2 → 1/1/1/1`、`weaponVertices 23061 → 3405`、`weaponMeshRadius 33.83 → 18.37`、`weaponCentreModel (0.76,6.25,14.08) → (-0.04,2.61,8.89)`、材质键从 `c_sniperrifle.vtf` 换成 `v_machete.vtf`。像素差 4876 px（§3），bbox 与 body off 完全相同 —— 即被换掉的正是那把直立的长枪，位置没动。

### 2.3 项 2：左右手

```
gpu: viewmodel request … hand=left drawn=2341 reason=drawn
gpu: viewmodel grip armsGrip=(-72.97,-26.10,-65.42) weaponGrip=(-72.97,-26.10,-65.42) residual=0.0000 …
gpu: viewmodel companion path=…/c_sniper_arms.mdl status=ok meshReady=1 sequences=43 bones=58 attachment=weapon_bone_L reason=-
gpu: viewmodel pass order … drawCalls=2341 … alignment=weapon_bone_L …
gpu: viewmodel weaponBodyReading armDrawCalls=2341 weaponDrawCalls=2341 weaponModelIndex=208 … weaponMaterials=2/2/2/2 … weaponMeshRadius=33.83 weaponVertices=23061 weaponClip=(3.163,-16.162,89.536,90.475)
```

换手只换挂点：`attachment=weapon_bone` → `weapon_bone_L`、`alignment=weapon_bone_L`，`residual` 仍 0.0000；grip 的 x 只差 0.01（`-72.98 → -72.97`）。武器体本身没变（208 / 23061 / 33.83），像素差 12322 px 落在双手区（§3）。

### 2.4 项 3：切换（tick 2600 → 2660）

两跑读数对比（同一录制，只挪 `--capture-tick`）：

| 字段 | `sw-before`（2600） | `sw-after`（2660） |
|------|------|------|
| `scene: eye` `rowOrigin` | 1199.4156,-2097.7678,251.8828 | 1097.6080,-2104.6831,288.0312 |
| `rowOffsetZ` | 75.0001 | 75.0001 |
| `scene: weaponBody` | 734 / CTFSniperRifle / 208 / c_sniperrifle | 810 / CTFClub / 201 / c_machete |
| `gpu: viewmodel weaponBodyReading` | 208 / 2/2/2/2 / 33.83 / 23061 | 201 / 1/1/1/1 / 18.37 / 3405（entity=810 CTFClub，末条读数） |
| `scene: vmAnim` | sequence=28 entitySequence=28 frame=30/31 | sequence=6 entitySequence=6 rate=1.00 frame=16/60 |
| `gpu: viewmodel grip` | (-82.00,-56.29,-35.82) residual=0.0000，weaponCentreModel=(0.76,6.25,14.08) | (-12.96,27.41,-52.34) residual=0.0000，weaponCentreModel=(-0.04,2.61,8.89) |
| `spectate: summary` | hiddenInstances=2264 … deaths=0 hidden=1 lastdeath=-1 | hiddenInstances=2326 … deaths=1 handovers=1 hidden=8 lastdeath=4837 |
| `PLAYER-MODEL-ASSERT` | PASS tick=2177 checked=11 | PASS tick=4837 checked=7 |

切换点在 trace 里是**一条 clock 行 + 紧邻的第一条新实体行**：

```
198558.6 scene: clock tick=2634 entryTick=2634 serverTick=4837 serverTickKnown=1        # .scratch/n18/sw-after.trace:178023
        scene: weaponBody entity=810 class=CTFClub worldModelIndex=201 path=…/c_machete/c_machete.mdl   # :178087
```

直方图交点（`bodypaths`）：`8 × c_machete` 与 `2326 × c_sniperrifle`，合计 2334 = `scene: weaponBody … observations=2334`；`vmanim_seqs` 同样是 `2326 × 28/28` 加 `8 × 6/6`。所以切换不是「模型换了但实体没换」，也不是「实体换了但动画没换」。

整帧像素差在这里**不能用来隔离切换**：两跑相机位置不同（eye 差约 100 单位），sw-before→sw-after 858961 px 覆盖全帧。切换的证据链是 trace 的实体/path/vmAnim 变化 + 项 1 同 tick 的单项正对照（唯一变量是模型文件）。

### 2.5 项 4：开火 / reload

11 跑里 `scene: vmAnim` 的 `sequence` 从不变化（步枪段始终 28、砍刀段始终 6，`applied=0`）—— 这份录像没有自然触发开火或 reload 的窗口，所以只能构造式取证：

```
# --viewmodel-apply-animation
scene: vmAnim entity=685 sequence=28 entitySequence=28 rate=1.31 cyclePresent=0 … poseAvailable=1 applied=1 frame=30/31 …
gpu: viewmodel pass order … drawCalls=2273 vertices=9996 … skinning=1 boneTableCoverage=58/58 animationInput=bones:58 … vmAnimApplied=1 …
# --viewmodel-apply-animation --viewmodel-sequence 29
scene: vmAnim entity=685 sequence=29 entitySequence=28 rate=1.31 … applied=1 frame=50/51 … sequenceOverride=29
gpu: viewmodel pass order … drawCalls=2308 … skinning=1 boneTableCoverage=58/58 … sequenceOverride=29 vmAnimApplied=1 …
vmanim_seqs:  1968 29/28
```

- `applied=1` + `skinning=1` + `boneTableCoverage=58/58` 证明**动画输入真的接进了蒙皮**（58 根骨全有表项），不是「开关打开了但没人读」。
- `sequence=29` 对 `entitySequence=28` + `sequenceOverride=29` 证明 CLI 序列覆盖生效（序列号是输入覆盖，实体自己的序列没变）；`frame=50/51` 与基线的 `30/31` 是两条不同序列自己的播放位置。
- 像素上 `body-rifle-anim → body-rifle-seq29` 是 539 px，bbox (826..903, 388..515)，正是枪/手区；`body-rifle → body-rifle-anim` 是 13628 px，bbox (756..937, 388..582) —— 蒙皮把手臂与武器一起改了姿态。
- CLI 自我描述（`native/src/main.cpp:4392-4409` 一带）：`--viewmodel-sequence` 是 INPUT OVERRIDE；`--viewmodel-apply-animation` 的帮助说钉子打在录像上、打开它本身不改变任何可测的东西（真正改变画面的是序列覆盖）。
- **结论按「构造式 PASS / 录像内无自然触发」记**：判据（输入覆盖能到达蒙皮与像素）成立；但「开火时 viewmodel 会自己播开火序列」这件事在这份录像上**没有观测证据**，不写成 PASS。

### 2.6 项 5：两条缺失回退

缺 vm（改 `--viewmodel` 路径，武器体不动）：

```
viewModelPath=…/v_does_not_exist.mdl resourceStatus=incomplete(mdl=0,vvd=0,vtx=0,mesh=0) attachmentStatus=absent sequenceCount=0 fov=90.0 settingsFov=90.0 hand=right drawn=1630 reason=drawn
scene: weaponBody entity=734 class=CTFSniperRifle worldModelIndex=208 path=…/c_sniperrifle.mdl refusal=none fallback=- reason=the weapon entity's own m_iWorldModelIndex names the weapon body observations=1623 …
gpu: viewmodel weaponBodyReading armDrawCalls=1630 weaponDrawCalls=1630 weaponModelIndex=208 weaponModelPath=…/c_sniperrifle.mdl weaponMaterials=2/2/2/2 weaponFallback=- weaponVisible=1 weaponReason=drawn …
```

命令给的 viewmodel 模型缺失时是**软失败**：资源三件套 `mdl=0,vvd=0,vtx=0,mesh=0` 全缺 → `resourceStatus=incomplete`、`attachmentStatus=absent`、`sequenceCount=0`；画面仍出（`drawn=1630`，比基线 1974 少 —— 少了那份画不出来的 command viewmodel），**武器体照旧 drawn**，帧与 `body-rifle` 逐字节相同（0 px）。这反证了武器体不来自 `--viewmodel`：它的模型来自武器实体自己的 `m_iWorldModelIndex`。

缺 body（改 `--viewmodel-weapon-model` 路径）：

```
scene: weaponBody entity=734 class=CTFSniperRifle worldModelIndex=208 path=…/c_does_not_exist.mdl refusal=none fallback=buildFailed reason=the weapon entity's own m_iWorldModelIndex names the weapon body observations=1805 named=1805 present=1805 refusals=none:1805
gpu: viewmodel weaponBodyReading armDrawCalls=1845 weaponDrawCalls=0 weaponModelIndex=208 weaponModelPath=…/c_does_not_exist.mdl weaponMaterials=0/0/0/0 weaponFallback=buildFailed weaponVisible=0 weaponReason=viewmodel was not found weaponEntity=734 weaponClass=CTFSniperRifle weaponMeshRadius=0.00 weaponVertices=0 weaponClip=(0.000,0.000,0.000,0.000) weaponMaterialsFailure=-
gpu: viewmodel grip … weaponGrip=(0.00,0.00,0.00) residual=-1.0000 … weaponCentreModel=(0.00,0.00,0.00)
```

硬失败（`fallback=buildFailed`）：`weaponVisible=0`、`weaponDrawCalls=0`、`weaponVertices=0`、`weaponMeshRadius=0.00`、`weaponMaterials=0/0/0/0`，grip 退化成 0 且 `residual=-1.0000`（没有武器可对齐）。画面只剩手臂（`armDrawCalls=1845`），帧与 `body-rifle-off` 逐字节相同（md5 都是 `a14f685f7008cc78bc0306ae6a783ccd`）。两跑都 `rc=0`、PMA=PASS。

### 2.7 项 6：自身隐藏

隐藏读数只在**隐藏集变化**时打印（`scene: cameraFilter`，`main.cpp:12894`），所以每份 POV trace 只有 3 行：

```
132048.7 scene: cameraFilter removed=1 indices=9 candidates=320 submitted=322 hidden0=9 hidden1=-1
149947.8 scene: cameraFilter removed=0 indices=- candidates=321 submitted=322 hidden0=-1 hidden1=-1
150009.8 scene: cameraFilter removed=1 indices=9 candidates=320 submitted=322 hidden0=9 hidden1=-1
```

- 11 个 POV 跑全部是 `removed1=2 removed0=1 hidden0_9=2`（即上面三行的形态）；`stv-control` 是 `removed1=0 removed0=1 hidden0_9=0`（第三人称 STV 从来不隐藏录制者自己）。
- `candidates 320 vs 321` 的差值正是被隐藏的 ent 9（与 `docs/N18-FIRSTPERSON-VISIBILITY-2026-11-20.md:169` 记的同一现象）。
- `body-rifle-off` 仍是 `removed1=2 removed0=1`：**相机自藏与武器体开关互不影响**（这正是 §4.1 要闭合的交叉组合）。
- PMA 全部 PASS（11 跑 `tick=2177 checked=11`、`sw-after` `tick=4837 checked=7`、`stv-control` `tick=12765 checked=18`，`mismatched=0 unresolved=0`）。
- 四张 POV 裁切帧里都看不到录制者自己的身体；`spectate: summary` 里那些 `hiddenInstances=` 是**尸体规则**、`hidden=` 是 director 的 `hiddenDead_`，都不是相机自藏，不能拿来当本项判据。

## §3 像素证据

帧规格：12 张 BMP 全是 1264×681、24bpp、2,582,406 B。md5（`evidence/n18-six-items/bmp-md5.txt`）：

| 跑 | md5 |
|----|-----|
| `body-rifle` | 66d61c7bfdcb792ee22c2ca4fe3032d0 |
| `body-default` | 66d61c7bfdcb792ee22c2ca4fe3032d0 |
| `missing-vm` | 66d61c7bfdcb792ee22c2ca4fe3032d0 |
| `body-rifle-off` | a14f685f7008cc78bc0306ae6a783ccd |
| `missing-body` | a14f685f7008cc78bc0306ae6a783ccd |
| `body-machete` | 84e82034a5162315da24c4592ba8899f |
| `body-rifle-left` | 7d0b5b4becb20dffbfa67749fd564465 |
| `body-rifle-anim` | 70a8a4c209bd133aa77867b62c16b182 |
| `body-rifle-seq29` | cf185e3ce746576b9cf0dbd6b93c822c |
| `sw-before` | 704e8f82aa86f74c0f6aac87d1e390e4 |
| `sw-after` | fd7c6b85d941842deb88a626cda8eb30 |
| `stv-control` | c26418d692d00d640cd67f0265f48e35 |

两两相等不是巧合：`missing-vm == body-rifle`（缺的 viewmodel 模型不影响画面）、`missing-body == body-rifle-off`（两条路径都让武器体不画，画面必然同形）。

像素差（`bmp-diff-bbox.py`，左上原点，`evidence/n18-six-items/diffs.txt`）：

| 对 | 差异像素 | bbox |
|----|---------|------|
| `body-rifle → body-rifle-off` | 4447 | (617..695, 283..486) 79×204 |
| `body-rifle → body-machete` | 4876 | (617..695, 283..486) 79×204 |
| `body-rifle → missing-body` | 4447 | (617..695, 283..486) 79×204 |
| `body-rifle → body-rifle-left` | 12322 | (617..938, 283..582) 322×300 |
| `body-rifle → body-rifle-anim` | 13628 | (756..937, 388..582) 182×195 |
| `body-rifle-anim → body-rifle-seq29` | 539 | (826..903, 388..515) 78×128 |
| `body-rifle → missing-vm` | 0 | 逐字节相同 |
| `sw-before → sw-after` | 858961 | (0..1263, 0..680) 全帧（相机移动） |

裁切图（`evidence/n18-six-items/`）：

- `crop-weapon-rifle.png` / `crop-weapon-rifle-off.png` / `crop-weapon-left-hand.png` / `crop-weapon-machete.png`：同一区域 x=540 y=220 w=480 h=440，直接看得到「直立长枪 / 空手 / 左手持枪 / 砍刀」四种形态；body off 后长枪整支消失、双手保留。
- `crop-zoom-rifle.png` / `crop-zoom-rifle-off.png`：x=590 y=260 w=130 h=250 的 3× 放大，看清枪托在下、枪管向上、带瞄具的那一支就是武器体。
- `crop-anim-vs-seq29.png`：x=700 y=340 w=310 h=300 左右并排（基线姿态 vs 序列 29），差异即 539 px 那一片。

## §4 对既有文档两处缺口的闭合与一处更正

### 4.1 `N18-FIRSTPERSON-VISIBILITY-2026-11-20.md:170`（交叉组合）

原文：「`--viewmodel-weapon-body off` 的 A/B 帧已抓（md5 `a9efb2db6738302646113c68cdf23c6f`），但与相机隐藏补丁的交叉组合（隐藏 on + body off）尚未单独抓。」

本轮 `body-rifle-off` 就是这一格：POV + 第一人称（隐藏 on）+ `--viewmodel-weapon-body off`。读数是 `filter: removed1=2 removed0=1 hidden0_9=2` —— 隐藏照旧 `removed=1 indices=9`，与 body 开关无关；像素差 4447 px 只落在武器区（617..695, 283..486），身体区不变。**两条机制正交，交叉组合闭合。**

### 4.2 `N18-VIEWMODEL-POSE-SURVEY-2026-11-19.md:90`（Missing 具名化）

原文：`viewmodel.cpp` 的 `Missing` 是否该在「有 `c_*` 手臂模型」时换成一条具名状态。

本轮读数把两种缺失分开了，但**改的还是武器体侧的文案，不是 `viewmodel.cpp` 的 `Missing`**：

- 缺 vm 模型：`resourceStatus=incomplete(mdl=0,vvd=0,vtx=0,mesh=0)` + `attachmentStatus=absent` + `sequenceCount=0` —— 具名到「资源缺哪几件」。
- 缺 body 模型：`fallback=buildFailed` + `weaponReason=viewmodel was not found` + `weaponVertices=0`。
- 真没有实体：`scene: weaponBody entity=-1 … refusal=noViewModelEntity fallback=noViewModelEntity reason=no ViewModel entity was selected`（`.scratch/g7/cap/frame1-on.trace:604`）。

三者已能区分 → 建议的**可读性**目标在武器体这一侧达成；`viewmodel.cpp` 的 `Missing` 本轮未触碰，该条**保持开放**。

### 4.3 `N18-VIEWMODEL-POSE-SURVEY-2026-11-19.md:91`（世界武器实体在隐身本体之后）

原文：「POV 的世界武器实体（`models/weapons/c_models/c_sniperrifle/c_sniperrifle.mdl`）在隐身本体之后是否仍停在摄像机前，要等 N18-B 抓帧确认。」

答案：**是。** tick 2000 的 POV 帧里，录制者本体被隐藏（`removed=1 indices=9`）的同时，武器体照常 drawn（`weaponVisible=1 weaponDrawCalls=1974`、`weaponClip=(2.889,-16.751,88.213,89.162)` 在屏内）；把它关掉（`body off`）画面少 4447 px，就是这支枪。**两者互不干扰。**

### 4.4 更正：旧 frame1 A/B 的「body off 无像素影响」是空对照

`N18-VIEWMODEL-POSE-SURVEY-2026-11-19.md:85-86` 记过一条「已证伪的旧结论：`--viewmodel-weapon-body off` 对那块巨大几何没有任何像素影响」。复核两份旧 trace 后，这个「逐字节相同」不成立为证据：

- `.scratch/g7/cap/frame1-on.trace`：24 条 `gpu: viewmodel` 行，但 `:604` 是 `scene: weaponBody entity=-1 … fallback=noViewModelEntity reason=no ViewModel entity was selected`，`:759` 的 `weaponBodyReading` 是 `weaponFallback=noViewModelEntity weaponVisible=0` —— **on 跑根本没画武器体**。
- `.scratch/g7/cap/frame1-off.trace`：`gpu: viewmodel` 行 **0 条** —— off 跑没有 `--viewmodel`，整个 pass 没开。

两帧相同是结构性的（都没有武器体可画），不能推出「body off 不影响画面」。本轮在武器体真 drawn 的帧上重做：4447 px。旧结论里**成立**的部分是「那块巨大几何是身体、不是武器体」—— 本轮 body off 的差异只落在细长的枪区（79×204），身体区一个像素没动。

## §5 局限

1. **计数类读数跨跑不可比**：末条读数不保证同一 tick（各跑 trace 长度不同，`observations / drawn / hiddenInstances` 分别 1623–2334 不等）。跨跑只比较不变量（模型路径、实体、类、worldModelIndex、materials、vertices、meshRadius、开关、直方图）。
2. **trace 不入库**：每份 18–33 MB，证据目录只有抽取文本；要复核逐帧行为得按 §6 重跑。
3. **本批没有重算 demo 的 size/sha**：§1.1 的 sha 来自 `weapon-body-check.sh:114-117` 的钉死值；只有跑门禁时才现场校验。
4. **开火 / reload 无自然触发**：本录像无窗口，项 4 只到「输入覆盖能到达蒙皮与像素」。
5. **sw-before/after 的整帧差不能隔离切换**：相机移动约 100 单位，858961 px 是全帧；切换证据靠 trace + 同 tick 正对照。
6. **tick 2000 段（步枪 734/208）与门禁 tick 3000 段（砍刀 810/201/CTFClub）是两条链**，本矩阵的读数口径只对前者，别拿门禁值套本矩阵的帧。
7. **PMA 的 tick 是 server 空间**（`resolvedSceneTick`），不是 `--capture-tick`；数值 2177/4837/12765 只说明末条 PMA 落在哪里。
8. **单帧证据**：每跑一张帧，不含时间序列；「切换瞬间有没有闪一帧旧模型」这类问题本矩阵答不了。
9. **裁切是一次性 PIL 调用**，没有脚本入库；区域与放大倍数记在 §3，可照着重做。
10. **没有跑 `weapon-body-check.sh` 本身**：本矩阵是取证，不是门禁；门禁的断言若要吸收这些读数，需要单独改脚本（本轮未做）。

## §6 复现

```
# 11 跑 + 读数 + 8 组像素差（日志 .scratch/n18-batch.log，末尾 N18-BATCH-DONE）
bash .scratch/n18-batch.sh

# 控制跑（body-default）+ 2 组差（日志 .scratch/n18-control.log，末尾 N18-CONTROL-DONE）
bash .scratch/n18-control.sh

# 单跑（与脚本里 body-rifle 等价）
ROOT=$(cygpath -m "$(pwd)"); OUT=.scratch/n18
TF2_NATIVE_TRACE="$ROOT/$OUT/x.trace" timeout 600 native/build-nmake/tf2_demo_native.exe \
  --tf-root "D:/SteamLibrary/steamapps/common/Team Fortress 2/tf" \
  --audio-device "" --hidden-window --audio-disabled \
  --demo "E:/demo测试/pl_upward_f12/POV/autorecord_2026-08-08_21-43-59.dem" \
  --settings-file "$OUT/iso/body-rifle.ini" --ui-settings-file "$OUT/iso/ui.ini" \
  --capture-frame "$OUT/x.bmp" --capture-tick 2000 --metrics-file "$OUT/x.csv" \
  --viewmodel-fov 90 --viewmodel models/weapons/v_models/v_scattergun_scout.mdl \
  --viewmodel-weapon-model models/weapons/c_models/c_sniperrifle/c_sniperrifle.mdl

# 读数（strip 时间戳，按族取末行/直方图）
bash .scratch/n18-readings.sh body-rifle body-rifle-off

# 像素差（读法，不是门禁；恒 exit 0）
python bmp-diff-bbox.py .scratch/n18/body-rifle.bmp .scratch/n18/body-rifle-off.bmp

# 裁切示例（区域与倍数见 §3）
python -c "from PIL import Image; im=Image.open('.scratch/n18/body-rifle.bmp').convert('RGB'); im.crop((540,220,1020,660)).save('evidence/n18-six-items/crop-weapon-rifle.png')"
```

证据文件清单见 `evidence/n18-six-items/README.md`。
