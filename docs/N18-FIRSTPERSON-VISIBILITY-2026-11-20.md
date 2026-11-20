# N18 第一人称可见性总设计（2026-11-20）

状态：核对并改写自 Gemini 第 9 轮 G12 交付（`C:/Users/Administrator/Downloads/gemini_round9_deliverable_2026_11_20.md` 第一部分），
基线 `28aba48274a9f33da2d9c6ee9053dd0ec6e6f02b`（HEAD `native/src/main.cpp` 16,068 行）。
本工作树另有 N18-B 未提交补丁，故本文同时给出 HEAD 行号与工作树行号；`native/src/native_renderer_viewmodel.cpp`、`native/src/entity_model.cpp` 本轮未改动，行号一致。

## §0 与交付的差异（核对结论）

1. **交付 §1「并非同一变换源」不成立**。手臂与武器体在结构上用的是**同一条 hand 链**：两处都是
   `multiply34(handTransform, attachmentLocal)`，再乘一个「把自身 AABB 中心搬到共享 target」的平移。
   真实机制是**两半各自按自己的 AABB 中心对齐到同一点**（`c_sniper_arms` 的中心不是掌心、`c_sniperrifle` 的中心不是握把），
   所以「枪看着离手」的残差来自 AABB 中心与真实握持点不重合，而不是两套变换源。详见 §1。
2. **交付 §4 把收敛层放在世界候选过滤循环（`main.cpp:12872-12903`）是无效位置**。该循环在本帧**眼解析之后**才跑，
   写在里面只能影响下一个 scene tick；暂停 demo 没有下一个 tick。本轮实测证明：决策必须写在眼解析块
   （工作树 `main.cpp:12407-12426`）里，过滤循环只读。详见 §4。
3. **交付 §3「ent 734 可能被世界 pass 画成双份」在本轮 POV 帧上不成立**（实测单份）；但它引出的风险是真的，
   判据应改成「按录制实测世界候选里有没有武器实体」，而不是静态猜测。详见 §3。
4. 交付自报的不确定项 3（`:14667` vs `:14687` 差 20 行）解释正确：`:14667` 是关闭文案、`:14687` 是
   `viewModelWeaponStats.lastReason = weaponBody.reason;`，两者都真实存在，差别只是任务书按工作树口径写了 +20。

## §1 手里的枪是谁画的、用的什么变换

**谁画**：`Renderer::drawViewModel`（`native/src/native_renderer_viewmodel.cpp:465`）里，武器体是**第二份 draw request**，
由 `viewModelWeaponVertexBuffer_` 绘制（`native_renderer_viewmodel.cpp:812-817` 的 `enabled` / mesh 就绪判断），
整遍由 `native/src/native_renderer_frame.cpp:2031` 的 `drawViewModel(paneWidth, paneHeight);` 调到。
产品口径见 `main.cpp:3218-3229`：*the static weapon body, the second half of the first-person composite: the model the
weapon entity's own `m_iWorldModelIndex` names, drawn as its own draw request next to the arms*；默认 `on` 在 `main.cpp:3357`
（`bool viewModelWeaponBodyEnabled = true;`），CLI 解析 `main.cpp:4358` / `4367`，关闭文案 `main.cpp:14667`。

**手臂的链**（`native_renderer_viewmodel.cpp:528-579`）：

```cpp
const std::array<float, 12> hand = viewModelState_.handTransform;
const std::array<float, 12> attachmentHand = multiply34(hand, attachment);   // :528-529
const float centerX = attachmentHand[0] * cx + ... + attachmentHand[3];      // :536-538（AABB 中心经同一条链）
const auto placement = translation34(targetX - centerX, targetY - centerY, targetZ - centerZ);  // :578
const auto world = multiply34(placement, attachmentHand);                     // :579
```

**武器的链**（`native_renderer_viewmodel.cpp:819-845`）：

```cpp
const std::array<float, 12> weaponAttachment = viewModelWeaponState_.attachmentValid
  ? viewModelWeaponState_.attachmentTransform : identity34();                 // :819-820
const std::array<float, 12> weaponAttachmentHand =
  multiply34(viewModelWeaponState_.handTransform, weaponAttachment);          // :821-822
const float weaponCenterX = weaponAttachmentHand[0] * wcx + ... ;             // :826-831
const auto weaponPlacement =
  translation34(targetX - weaponCenterX, targetY - weaponCenterY, targetZ - weaponCenterZ);  // :832-833
const auto weaponWorld = multiply34(weaponPlacement, weaponAttachmentHand);    // :834
const float weaponNear = std::max(weaponRadius * 0.05f, 0.01f);               // :840
const float weaponFar = distance + weaponRadius * 4.0f;                        // :841
XMConvertToRadians(viewModelWeaponState_.fov), aspect, weaponNear, weaponFar); // :842-843
```

**结论**：两半共用 `handTransform` 与同一个 `target`（`kViewModelFramingNdcX = 0.40f` / `kViewModelFramingNdcY = -0.62f`
在 `native_renderer_viewmodel.cpp:557-558` 解出的屏幕构图点），**武器确实挂在 hand 链上**——`attachmentTransform`
就是 `mstudioattachment_t::local`（attachment local -> 骨骼空间，构造在 `main.cpp:8462-8480` `viewModelDrawStateFor`，
注释 `:8456-8461`），乘在 `handTransform` 右侧。差别只有三处：各自 AABB 中心、各自 near/far、武器读自己的 `fov`。
`native_renderer_viewmodel.cpp:793-798` 的注释明说共享 target 是**有意设计**：
*The body shares the arms' placement point ... a second, independent placement formula would let the two drift apart as their radii differ.*

因此「枪看着不在手里」的机制是：把两半的 **AABB 中心**搬到同一点，等价于以各自的包围盒中心对齐，
而不是以握把/掌心对齐；AABB 中心离握把越远（长枪、带刺刀的枪越明显），错位越大。
要消除它，正确的最小改动是**别用 AABB 中心定位武器**，改用武器模型自己的 `weapon_bone` attachment 定位
（即去掉 `:826-834` 的 `translation34(target - weaponCenter)` 补偿，让 `weaponAttachmentHand` 直接就是世界矩阵，
必要时对它再乘一个从实测握持点得到的固定偏移），不是「把武器挂到手臂骨骼矩阵」——它已经挂着了。

## §2 为什么用 `c_` 体（数据决定，且设计如此）

- 数据决定：本条录制里 ViewModel 实体自己的 `m_nModelIndex` 命名的是 `models/weapons/c_models/c_sniper_arms.mdl`；
  武器体只能来自武器实体的 `m_iWorldModelIndex`（`entity_model.cpp:564-585`），路径 `models/weapons/c_models/c_sniperrifle/c_sniperrifle.mdl`。
  现代 TF2 的 VPK 里已没有 `v_sniperrifle.mdl`（实测 `status=failed ... reason=viewmodel was not found`），
  `v_` 路径不可作为修法。
- 设计如此：`--viewmodel-weapon-body` 的 CLI 注释（`main.cpp:3221-3229`）把武器体定义成第一人称合成的第二半；
  `--viewmodel-render companion`（`main.cpp:3218-3220`）画 ViewModel 实体自己的模型，两者是同一遍里的两份 draw request
  （`main.cpp:14629-14635` 的注释：*This is the second model the T2.5 ... its own mesh and its own draw request*）。
- 运行时挑选：`main.cpp:14679-14682` 调 `tf2::native::findViewModelWeaponBodyInput(...)`（`entity_model.cpp:454-466`），
  它按「ViewModel 实体 → `m_hActiveWeapon`/`m_hWeapon` → 武器实体 → `m_iWorldModelIndex` → modelprecache」解析，
  拒绝原因表在 `entity_model.cpp:436-449`（含 `WorldModelIndexZero`、`WorldModelNotDeclared`、`WorldModelIsArms`）。
  其中 `WorldModelIsArms` 这一条对应的正是历史上的真实缺陷：武器实体的 `m_nModelIndex` 命名的是**手臂合成体**，
  拿它当武器体会把一双手再画一遍——`entity_model.cpp:558-565` 原文：
  *A weapon carries two indices next to each other in the send table, and the WRONG one -- m_nModelIndex, which names the class's first-person arms composite -- still resolves to a real, drawable file. Drawing it would put a second pair of hands in front of the camera and every count-based reading would stay green, which is exactly what the world pass did before cd36db1.*

## §3 本地玩家的武器实体（ent 734）会不会被世界 pass 画成双份

**代码事实**：

- 世界候选的隐藏集只有「相机/调度器隐藏的那个玩家实体」，不含它持有的武器：
  `main.cpp:12881-12885` `const bool hiddenForCamera = (g_spectateCameraHiddenEntity >= 0 && ...) || (g_paneSplit && g_pane1CameraHiddenEntity >= 0 && ...)`。
- `placeSpectatorView`（`main.cpp:1250` 定义、`:1347` `g_spectateCameraHiddenEntity = targetIndex;`）只写玩家实体。
- `entity_model.cpp:747` 的 `instance.viewModelSkipped = isViewModelPath(request.modelPath);`（`:133-138`、`:318`）只跳过 `v_*`，
  `c_*` 不跳过；`findViewModelWeaponBodyInput` 只服务 viewmodel pass，不删世界候选。
- `--player-weapon-attach` 的注释（`main.cpp:3230-3241`）承认世界 pass 会画武器实体：
  *`off` is the product default and the A/B baseline: the world pass keeps drawing each weapon entity where the demo says it sits ... `on` replaces that standalone instance with the held one -- never both.*

**本轮实测量（POV pinned 帧，`--viewmodel-fov 90 --viewmodel-sequence 1 --viewmodel-apply-animation`）**：只有一份。

- `gpu: viewmodel pass order worldPass=1 viewModelPass=8 ... drawCalls=1 vertices=9996 ... drawnModel=...c_sniper_arms.mdl`，
  `gpu: viewmodel weaponBodyReading armDrawCalls=1 weaponDrawCalls=1 ... weaponEntity=734 weaponMeshRadius=33.83 weaponVisible=1`：
  手臂 1 次、武器体 1 次，都在 viewmodel pass。
- 世界 pass：`budget: instances capacity=96 submitted=67 accepted=67 ... rejected=missingMesh:0,...`，
  整份 trace 里 `models/weapons/` 只出现在 `gpu: viewmodel` / `scene: vmAnim` 行，**没有任何世界侧的武器网格行**。
- 画面佐证：把 ent 9 的自身身体隐藏后（§4），帧里是世界上唯一的一对手臂 + 一支枪；若世界 pass 也在 ent 734 的
  origin（≈摄像机眼）画一份手臂合成体，那会是一双贴脸的巨手——不存在。

**结论**：本条录制**没有双份**；风险是潜在的（某条录制的武器实体若带可解析的 `m_nModelIndex`，世界 pass 会在演示位置
再画一份）。**判据**：不要静态推演，直接读世界 trace 有没有该 ent 的网格/实例行；有才加 `ownerEntity` 过滤，
并同时给 `scene: cameraFilter` 增加 `hiddenWeapons=<n> weaponIndices=<csv>` 读数。

## §4 判据收敛层（实测：只能写在眼解析块，不能写在过滤循环）

**为什么不能写在过滤循环**：`main.cpp:12872-12903` 的候选循环每个 scene tick 跑一次，读的是**上一刻**写好的
`g_spectateCameraHiddenEntity`。把赋值写在循环内部（或写在其后的帧尾相机决策块 `main.cpp:15202` 一带）只能影响下一个 tick；
`--capture-tick 0 --start-paused` 的帧只有一次过滤，写晚了就是**零效果**——2026-11-20 两次尝试都因此字节不变
（`md5 5c5e874af1d46b848b211d0c3636f3f3` 修前修后相同，`scene: cameraFilter removed=0 indices=- hidden0=-1`）。

**正确位置（已实现，未提交）**：眼解析块末尾，`if (haveObserverView) { cameraEyeX = g_scenePovEye.eye[0]; ... }`
（工作树 `main.cpp:12402-12406`）之后，工作树 `main.cpp:12407-12426`：

```cpp
if (!g_debugCamera
    && g_cameraMode.mode() == tf2::native::CameraMode::FirstPerson
    && haveObserverView && g_scenePovEye.entityIndex >= 0
    && g_scenePovEye.offsetApplied) {
  g_spectateCameraHiddenEntity = static_cast<std::int32_t>(g_scenePovEye.entityIndex);
} else {
  g_spectateCameraHiddenEntity = -1;
}
```

判据语义：第一人称 + demo 眼解析成功 + 眼确实用 entity 的 `viewOffset` 抬起来（`offsetApplied`）= 摄像机站在那个玩家头里，
该玩家的身体不能被从内部画出来。**这与 spectate 侧一直以来的规则相同**（`placeSpectatorView` 成功后写 `= targetIndex`）。
第三人称 / `FreeLook` / `--debug-camera` 的镜头在玩家之外，必须看到身体，所以落 `-1`。

**验证读数（修后）**：`scene: cameraHide decided=9 mode=0 demoView=1 debugCam=0 entity=9 offsetApplied=1 haveObserverView=1`、
`scene: cameraFilter removed=1 indices=9 candidates=320 submitted=322 hidden0=9 hidden1=-1`、`anim: ent=9` 行消失、帧 `md5 3bf37d4f48c4ffb61660aaf3ee181ac9`。
同一隐藏集已被 overlay 侧的 `main.cpp:14441-14447` 读取（`overlay: corpseFilter`），所以**一处决策、三方一致**：
世界候选、overlay 轮廓、pane1 并集（`g_pane1CameraHiddenEntity`，工作树 `:15304`，HEAD `:15282`）。

**收敛层建议**：把「pane 的相机站在谁里面」做成一个函数（输入 mode / demo 眼 / debug 相机 / pane 归属，输出 ent 或 -1），
三处调用或读取同一结果；当前实现已把决策点唯一化在眼解析块，过滤循环与 overlay 只读全局，不要再各自判定。

## §5 最小改动清单与不该动什么

已完成（本轮，未提交）：

1. 工作树 `main.cpp:12407-12426`：第一人称 demo 眼 → `g_spectateCameraHiddenEntity = entityIndex`；其余一律 `-1`。
2. 原帧尾相机决策块的 `else { g_spectateCameraHiddenEntity = -1; }` 改成空壳注释（Nothing to decide here），指向上面这块。

待定（按实测再决定）：

3. 武器实体隐藏（`ownerEntity == hiddenPlayer` 的可见武器）——**本帧不需要**（§3）；只有在某条录制上量到世界侧武器实例才加。
4. 枪与手的贴合（§1 末尾）：改武器体的定位输入（用 `weapon_bone` 而非 AABB 中心），不动手臂侧公式。

不该动：NDC 构图常量（`7250df6c` 已完成，`native_renderer_viewmodel.cpp:557-558`）、手臂蒙皮/材质路径、
`placeSpectatorView` 的语义、`entity_model.cpp:133/:318/:747` 与 `entity_overlay.cpp:247` 的 `v_*` 跳过逻辑，
也不要靠类名去猜 `v_` 路径（VPK 里没有）。

## §6 仍未闭合

- **两条负控已抓（2026-11-20）**，都按预期不隐藏身体：
  - `--pane-mode 0 third`（解析 `main.cpp:3888-3898`，`pane == 0` → `g_cameraMode.setMode(ThirdPerson)`）：
    `scene: camera mode=third-person ... src=demo`、`cameraFilter removed=0 indices=- candidates=321 submitted=322 hidden0=-1 hidden1=-1`，
    帧 `md5 27d22722f3444a0d3d181320410a24d7`，画面里红袖、手持步枪的身体可见。
  - `--debug-camera 321 -2679 290 0 285.87`：`scene: camera mode=free-look src=debug`、`cameraFilter removed=0 ... hidden0=-1`，
    帧 `md5 3439914e577045614e48ad5064ae7079`，甲板上整具身体可见（另有第一人称手臂合成体照常绘制，与可见性无关）。
  - 两份帧：`.scratch/g7/cap/pov-neg-third.png`、`.scratch/g7/cap/pov-neg-debugcam.png`。
  注意 `candidates` 在隐藏帧是 320、不隐藏是 321，差值正是被隐藏的 ent 9。
- `--viewmodel-weapon-body off` 的 A/B 帧已抓（md5 `a9efb2db6738302646113c68cdf23c6f`），但与相机隐藏补丁的交叉组合（隐藏 on + body off）尚未单独抓。
