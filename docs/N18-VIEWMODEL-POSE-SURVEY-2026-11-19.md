# N18 ViewModel pose 勘察（第 8 轮 G9 交付，已核对落盘）

* 基线：`work/integrate @ 28aba482`（`native/src/main.cpp` 16,090 行）
* 来源：Gemini 第 8 轮交付 `gemini_round8_deliverable_2026_11_19.md` 第一部分（G9）
* 落盘方式：逐行核对后写入；**交付原文有 3 处与代码不符，已在 §0 修正，正文按代码事实重写**。

## 0. 核对修正（读正文前先读这里）

1. **行号错 1 处**：交付写 `native/src/main.cpp:14812`（`state.enabled`），实测在
   **`native/src/main.cpp:14834`**：`state.enabled = viewModelMeshBufferPath == drawnRequest.path;`。
   这就是交付自己怀疑的 +22 偏移（G8 那轮 +22 行改动造成的整体下移），成立。
2. **`status=missing` 的归因要换**：交付说命中「`viewmodel.cpp:449` 默认状态在前期检查早退」或
   「`:550` 采样失败」。pinned POV 录制实测命中的是最前面那条实体检查——
   `native/src/viewmodel.cpp:464-467`：
   `if (!input.entityPresent) { outcome.reason = "no viewmodel entity owned by the view entity"; return outcome; }`
   它在 binding 检查与采样之前返回，`outcome.status` 保持结构体默认的 `Missing`，
   所以 `reason` 原文是 `no viewmodel entity owned by t...`（trace 里被截断打印）。
3. **构图那条已经做完了**：交付 §4 第 1 条（把 distance/rightOffset/downOffset 改成屏幕 NDC 构图）
   **已由 commit `7250df6c` 实现**。交付读到的 `native/src/native_renderer_viewmodel.cpp:542-554`
   是旧值（`rightOffset = radius * 0.30f`、`downOffset = -radius * 0.22f`）；
   现值是 `kViewModelFramingNdcX = 0.40f` / `kViewModelFramingNdcY = -0.62f` +
   `framingTanHalfFov` / `framingScreenSign`，`distance` 仍是 `radius * 2.2f`。
   落盘时按「已完成」记，不再列为待办。

核对的锚点（全部与交付一致，除上述第 1 条）：
`native/src/main.cpp:14846-14847`（`resolveViewModelAnimationInput` 调用）、`:14848`（`state.animationStatus = viewModelAnimationStatusName(outcome.status)`）、
`:14860`（`const bool poseAvailable = !outcome.bindPose && !outcome.skinMatrices.empty();`）、`:14861`（`if (viewModelIdentityBones)`）、`:14871-14872`（`else if (poseAvailable && viewModelApplyAnimation && renderer.uploadViewModelBones(...))`）；
`native/src/viewmodel.cpp:446-449`、`:550`、`:556-559`、`:567`、`:573-576`（唯一把 `bindPose` 置 false 并填 `skinMatrices` 的路径）；
`native/src/entity_model.cpp:439`、`:454-466`、`:133-138`、`:318`、`:747`；`native/src/entity_overlay.cpp:247`；`main.cpp:14964`/`:14976`（两行 `scene: vmAnim`）。

## 1. `poseAvailable=0` 的原因链

* `outcome` 由 `native/src/main.cpp:14846-14847` 产出，实现在 `native/src/viewmodel.cpp:446-578`。
* `ViewModelAnimationOutcome` 默认 `bindPose = true`、`skinMatrices` 空（`native/src/viewmodel.cpp:449`）。
* 唯一能翻成「可用」的落点在 `native/src/viewmodel.cpp:573-576`：
  `outcome.status = ViewModelAnimationStatus::Ok;` / `outcome.bindPose = false;` / `outcome.skinMatrices = sampled.skinMatrices;`
* 因此只要在它之前的任何一处返回（无实体 / 无序列 / `binding == nullptr` / 采样失败 / 骨数不匹配），
  `bindPose=true` 与 `skinMatrices.empty()` **同时成立**，`:14860` 必为 false，`:14871` 落到 `:14880` 的 else，保持 bind pose 并逐帧打印 `vmAnimApplied=0`。

## 2. `scene: vmAnim ... status=missing`

* `native/src/main.cpp:14848` 把 `outcome.status` 字符串化进 `state.animationStatus`，由 `:14964`（每实体）与 `:14976`（summary）打印。
* `Missing` 的来源有两类，pinned 屏录命中的是第一类：
  1. **无 ViewModel 实体**（`native/src/viewmodel.cpp:464-467`，先于一切检查返回，`reason` 具名）；
  2. 采样失败被翻译成 `Missing`（`native/src/viewmodel.cpp:556-559`：非 `BoneMismatch`/`TooManyBones` 一律 `Missing`）。
* 与 §1 是**同一条链**，不是两条独立缺陷：`status == Missing` 直接阻断 `:573-576`，必然导出 `poseAvailable=0`。
* 负控读法：`reason=no viewmodel entity owned by the view entity` 说明「demo 里没有该 view 实体的 ViewModel 实体」，
  而不是「pose 采样算错了」——修法在输入侧（POV 录制把第一人称数据放在 `c_*` 手臂模型 + 世界武器实体上），
  不在蒙皮管线里。

## 3. `weaponBody` 的 `noViewModelEntity` 拒绝

* 字符串化：`native/src/entity_model.cpp:439`（`case ViewModelWeaponBodyRefusal::NoViewModelEntity: return "noViewModelEntity";`）。
* 判定：`native/src/entity_model.cpp:454-466` 的 `findViewModelWeaponBodyInput`——
  `if (viewModelEntityIndex < 0 || static_cast<std::size_t>(viewModelEntityIndex) >= statesByIndex.size())` 立即拒绝。
* 成因：`viewModelEntityIndex` 来自上游 `findViewModelAnimationInput`；demo 没有有效 `CTFViewModel` 实体
  （或 `native/src/entity_model.cpp:318-324` 在 class 表里找不到 ViewModel 类）时传入 -1，于是 weapon body 查找连试都不试。
* 与 §1/§2 同源：三条读数（`vmAnim status=missing`、`weaponBody refusal=noViewModelEntity`、`poseAvailable=0`）
  是**同一个输入缺失**的三个出口，改一个不会让另外两个变绿。

## 4. 第一人称构图：现在是什么

* 计算在 `native/src/native_renderer_viewmodel.cpp`（入口 `drawViewModel`，独立清深度），
  常量 `kViewModelFramingNdcX = 0.40f`、`kViewModelFramingNdcY = -0.62f`，
  由 `framingTanHalfFov` 把「目标 NDC 位置」换成视空间偏移，`distance = radius * 2.2f` 保留。
* 投影在 `:575-583`（`viewModelState_.fov`、`nearPlane = max(radius * 0.05f, 0.01f)`、水平翻转）。
* 判据可读数：`main.cpp:11845` 那组 pass 打印里的 `centre viewSpace=/clip=/ndc=`；
  matcha 帧实测 `ndc=(0.391,-0.605) onScreen=1`，落在右下象限且未被近裁剪面切到——这就是 NDC 构图要的证据。
* 支撑绘制的契约点：`main.cpp:14834`（`state.enabled`）、`:14849`、`:14866`（注释里点名 `viewmodel-contract-check.sh` 首次运行）。

## 5. 不应该动什么

* 世界 pass 跳过 `v_*` 与 ViewModel 类：`native/src/entity_model.cpp:133-138`（`isViewModelPath`）、`:318`（`isViewModelClassName`）、`:747`（`viewModelSkipped`）、`native/src/entity_overlay.cpp:247`。放宽会让世界出现浮空枪。
* 独立清深度与恒等蒙皮：`native/src/native_renderer_viewmodel.cpp` 的清深度点与 `main.cpp:14861` `if (viewModelIdentityBones)`。

## 6. 本轮之后的状态变更（交付写作时不存在）

* `7250df6c`：NDC 构图入库（见 §0 第 3 条）。
* **N18-B（进行中，未提交）**：POV 录制里摄像机眼就是玩家自己的眼（`scene: eye ... reason=applied`），
  世界 pass 把该玩家的身体画在同一个 origin 上，于是狙击手的帽子/头/躯干把画面右侧包住。
  第一版补丁把隐藏写在帧尾的相机决策处，**实测无效**：候选列表在它之前就被过滤完了，
  而暂停的 demo 不会再有下一个 scene tick，两帧字节与修前完全相同。
  正确位置是 `g_scenePovEye` 解析处（`native/src/main.cpp:12403-12410` 一带），
  条件 = 非 `--debug-camera` + `CameraMode::FirstPerson` + `haveObserverView` + `entityIndex >= 0` + `offsetApplied`。
* 另有一条已证伪的旧结论：`--viewmodel-weapon-body off` 对那块巨大几何**没有任何像素影响**，
  它是世界 pass 画的身体，不是 viewmodel 武器体。武器体在画面里只是手心里那支细枪，尺寸正常。

## 7. 未决

* `viewmodel.cpp` 的 `Missing` 是否该在「有 `c_*` 手臂模型」时换成一条具名状态（而不是与「真的没有实体」共用 `Missing`）——影响的是可读性，不是画面。
* POV 的世界武器实体（`models/weapons/c_models/c_sniperrifle/c_sniperrifle.mdl`）在隐身本体之后是否仍停在摄像机前，要等 N18-B 抓帧确认。
