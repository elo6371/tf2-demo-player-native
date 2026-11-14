# TRIAGE — `animation_viewmodel_probe` 自检 `loop=false`（任务 F2-2a）

- worktree: `D:/TF2_Native_Worktrees/t105-e5`，分支 `candidate/e5`，HEAD `94e7612`
- 探针源码: `native/tools/animation_viewmodel_probe.cpp`（517 行）
- 被测产品代码: `native/src/animation_decoder.cpp`
- 本次运行产物（未入库）: `.scratch/loopfalse/probe-stdout-tfroot.txt`、`probe-stderr-tfroot.txt`、
  `probe-stdout.txt`（无 `TF_ROOT`）、`probe-stderr.txt`

---

## 结论

**(a) 探针自检的夹具（fixture）缺陷** —— `loopAndOnce()` 用 `studioShell(1, 1, 1)` 造了一个**只有一根骨骼、
而这一根骨骼就是 skeleton root** 的合成模型；产品代码按设计（`animation_decoder.cpp:289-306`，commit
`2024a526`）对 root 骨强制返回 bind 变换、完全不下采样动画通道，于是该夹具在所有帧上产出的
`modelPosition` 恒等于 bind 位置，最后一条断言 `animation_viewmodel_probe.cpp:144`
`!samePositions(start.modelPosition, last.modelPosition)` **在任何输入下都不可能成立**。

一句话：探针期望的语义（`STUDIO_LOOPING` 时回绕、否则夹取）是**对的**；产品对 loop 标志的解析也是
**对的**（`realLoop=true` 已独立证明）；错的是夹具让"第 0 帧 vs 第 3 帧位置必须不同"这件事在结构上不可观测。

---

## 证据链

### 1. 探针侧：`loopAndOnce()` 到底断言什么

`animation_viewmodel_probe.cpp:101-145`。

| 行 | 代码行为 |
|---|---|
| 102 | `auto bytes = studioShell(1, 1, 1);` → **1 根骨骼**、1 个 animdesc、1 个 seqdesc |
| 29-55 | `studioShell`：`boneAt=408`，第 `i` 根骨骼在 `408+i*216`；`i==0` 的 `parent` 写 `-1`（第 46 行），`i>0` 写 `i-1`。**bone 0 必然是 root，且骨骼数=1 时模型里没有任何非 root 骨** |
| 44-47 | bone 0：`name="root"`、`parent=-1`、`position=(0,0,0)`（`bone+32`）、`rotation=(0,0,0,1)`（`bone+56` 写 w=1） |
| 103-105 | animdesc @624：`fps=10`(+8)、`frameCount=4`(+16)、`animIndex=size-624`(+56) |
| 106-117 | 动画块 @`data`：`bytes[data]=0`（**bone 0**）、`bytes[data+1]=0x04`（`kAnimPos`）、`next=0`、通道 0 相对偏移 6、run `valid=4 total=4`、样本 `0/100/200/300` |
| 118 | `bindSequence(bytes, 0, "loop", 1, 0)` → seqdesc `flags(+12)=1` = `STUDIO_LOOPING` |
| 124-130 | loop 断言：`start=frame 0`、`wrapped=tick 4 → frame 0`、`again` 同值；要求三者 `modelPosition` 完全相同 |
| 132-133 | `onceBytes` 把 `flags(+12)` 改成 `0` → 非 loop |
| 138-139 | `last=tick 3 → frame 3`、`clamped=tick 100 → frame 3` |
| 140-143 | 要求两次 once 采样帧号都是 3 且位置相同 |
| **144** | **`!samePositions(start.modelPosition, last.modelPosition)` —— 要求 loop 的 frame 0 与 once 的 frame 3 位置不同** |

夹具/期望值来源：全部由该函数自己构造（`studioShell` + 上述字节写入），**不读 VPK、不读磁盘**。
期望值来源是 mdl 语义：`flags & 0x0001` 时时间轴回绕，否则夹到末帧。

### 2. 产品侧：这些值是怎么算出来的

| 行 | 代码行为 |
|---|---|
| `animation_decoder.cpp:23` | `constexpr int kLoopingFlag = 0x0001;` |
| `:498` | `readAt(bytes, size, offset + 12, sequence.flags);` → 与 `mstudioseqdesc_t` 的 `+12 flags` 一致 |
| `:502` | `sequence.looping = (sequence.flags & kLoopingFlag) != 0;` → 本夹具 `flags=1` ⇒ `looping=true` |
| `:222-235` | `selectFrame()`：looping 时 `fmod(cursor, frameCount)`；否则 `<0` 夹 0、`>frameCount-1` 夹末帧 |
| `:572-573` | `cursor = tick*fps/tickRate`，`frame = selectFrame(cursor, frameCount, sequence.looping)` |
| `:613-626` | 逐条走骨骼链，调 `readBoneFrame` |
| **`:289-306`** | **`if (bone.parent < 0) { position = bone.position; rotation = bone.rotation; return true; }` —— root 骨直接返回 bind 变换，`record`/`frame` 一个字节都不读** |
| `:307-351` | 非 root 骨才解析 `kAnimPos/kAnimRot/...` 并叠加通道值 |
| `:641-646` | `composeLocalToModel()` 把 local 变换合成到 `modelPosition` |

对本夹具的实测推算（tickRate=10、fps=10）：
`start`: tick 0 → cursor 0.0 → frame 0（`:223-224`）
`wrapped`: tick 4 → cursor 4.0 → `fmod(4,4)=0` → frame 0 ✔（`:128` 通过，且这本身就证明 loop 标志被正确解析：若按 once 处理会夹到 frame 3）
`last`: tick 3 → frame 3 ✔；`clamped`: tick 100 → 夹到 3 ✔

但**帧号不同、位置必然相同**：唯一骨骼是 bone 0（root），`:289-306` 在第 0/3 帧都返回同一个
`bone.position=(0,0,0)`；`composeLocalToModel`（`:383-387`，`parent<0` 直接取 local）也原样透传。
⇒ `start.modelPosition == last.modelPosition` 恒为真 ⇒ `:144` 恒为假 ⇒ `loopAndOnce()` 返回 `false`。

### 3. 为什么只可能是 `:144`

`:121/135`（表结构解析）、`:127/140/141`（采样状态）、`:129/130/143`（同帧比较）都是**与帧无关**的
量：本 shell 的表结构已被同一文件的 `malformed`/`compressed` 子用例（`malformed=true`、`compressed=true`）
证明可解析；采样路径对 root 骨不读任何通道，不会失败；同帧比较必然相等。唯一随帧变化的是
`modelPosition`，而它在本夹具里被 `:289-306` 钉死。因此 `loop=false` 只能由 `:144` 产生。

（说明：本轮禁止构建，因此这是"排除全部与帧无关的分支"后的唯一解，不是对二进制插桩的直接观测。）

### 4. 历史因果（解释"以前没发现"）

- `loopAndOnce()` 与整个探针自 2026-10-06 的导入提交 `7ab4e72` 起**一行未改**（`git blame` 101-145 全部指向 `7ab4e72`）。
- `animation_decoder.cpp:289-306` 的 root 分支由 **`2024a526`（2026-11-04，`fix(anim): drive a skeleton root from its bind transform`）** 新引入；
  该提交之前的 `readBoneFrame` 没有 root 特判，bone 0 的 `kAnimPos` 通道会被采样（0/100/200/300），
  `:144` 因而可满足。
- 首次记录到 `loop=false` 的时间是 2026-11-13（`docs/F2-2-PROBE-SWEEP-2026-11-13.md:48`），
  **恰在 `2024a526` 之后**。夹具写完 → 产品改了 root 语义 → 夹具变成空断言。
- 该 root 分支是有意为之（注释里写明 T62 的 spiked-blob/拖到地面的回归），并且不在本任务范围内：
  它给真实模型带来的收益由同一探针的 `bindPose/scout/soldier/motion/section/realLoop` 全 true 佐证。

### 5. 权威依据：正确的期望值是什么

1. **mdl 字段**：`mstudioseqdesc_t` 的 `+12` 是 `flags`，`STUDIO_LOOPING = 0x0001`。仓库内有独立仪器的
   逐字节确认：`T2.5-MAPPING-RECON-2026-10-12.md:83-85`（`+4` label、`+8` activity 名、`+12` flags、`+16` 数值 activity）。
   探针写 `flags=1` 表示 loop、`flags=0` 表示 once，**写法正确**；产品读 `+12` 再 `&1`，**也正确**。
2. **真实数据的独立反证**：同一次运行 `realLoop=true`（`animation_viewmodel_probe.cpp:361-374`）——
   它在真实 `scout.mdl` 的 76 骨模型上找一条 `sequence.looping && animDesc>=0 && frameCount>=2` 的序列，
   用 `tick = frameCount` 采样并要求 `start.frame==0 && wrapped.frame==0 && 位置相同`。若产品把
   `LOOP_` 标志解析错或 `selectFrame` 不回绕，这里会是 `frame == frameCount-1` 且位置不同。
   该用例通过 ⇒ **产品的 loop 语义在真实数据上是正确的**，排除 (b)。
3. **不依赖资产的证明**：无 `TF_ROOT` 运行时（`.scratch/loopfalse/probe-stdout.txt`）`loop` 仍为 `false`，
   其余依赖资产的标志全 false。合成用例完全不碰 VPK ⇒ 排除 (c)（不是数据/输入缺失）。

⇒ 正确的期望值是"loop 回绕、once 夹取"，探针并没有把 once/loop 写反；
出错的只是**让这一差别可观测的夹具**（需要一根非 root 骨）。

---

## 最小修复 diff（仅改探针文件，改完须重编译）

思路：把合成骨架从"只有 root"改成"root + 一根子骨"，动画块指向子骨（`bytes[data]=1`），
从而 `kAnimPos` 通道真正参与 `modelPosition`。为不改动其它 3 个用例，`bindSequence` 增加一个
**默认参数**（默认值就是原来的 724 硬编码）。

```diff
--- a/native/tools/animation_viewmodel_probe.cpp
+++ b/native/tools/animation_viewmodel_probe.cpp
@@ -61,9 +61,8 @@
-void bindSequence(std::vector<std::uint8_t>& bytes, int sequence, const char* label, int flags, int anim) {
-  const std::size_t sequenceAt = 408u + 216u + 100u;
+void bindSequence(std::vector<std::uint8_t>& bytes, int sequence, const char* label, int flags, int anim,
+    std::size_t sequenceAt = 408u + 216u + 100u) {
   const auto offset = sequenceAt + static_cast<std::size_t>(sequence) * 212u;
```

精确 old / new 行（按 1-based 行号，逐行）：

- **L61-L63**
  - old:
    ```
    void bindSequence(std::vector<std::uint8_t>& bytes, int sequence, const char* label, int flags, int anim) {
      const std::size_t sequenceAt = 408u + 216u + 100u;
      const auto offset = sequenceAt + static_cast<std::size_t>(sequence) * 212u;
    ```
  - new:
    ```
    void bindSequence(std::vector<std::uint8_t>& bytes, int sequence, const char* label, int flags, int anim,
        std::size_t sequenceAt = 408u + 216u + 100u) {
      const auto offset = sequenceAt + static_cast<std::size_t>(sequence) * 212u;
    ```

- **L102-L108**
  - old:
    ```
      auto bytes = studioShell(1, 1, 1);
      writeAt<float>(bytes, 624 + 8, 10.0f);
      writeAt<std::int32_t>(bytes, 624 + 16, 4);
      writeAt<std::int32_t>(bytes, 624 + 56, static_cast<std::int32_t>(bytes.size() - 624));
      const auto data = bytes.size();
      bytes.resize(data + 20, 0);
      bytes[data] = 0;
    ```
  - new:
    ```
      auto bytes = studioShell(2, 1, 1);
      const std::size_t animAt = 408u + 2u * 216u;
      const std::size_t sequenceAt = animAt + 100u;
      writeAt<float>(bytes, animAt + 8, 10.0f);
      writeAt<std::int32_t>(bytes, animAt + 16, 4);
      writeAt<std::int32_t>(bytes, animAt + 56, static_cast<std::int32_t>(bytes.size() - animAt));
      const auto data = bytes.size();
      bytes.resize(data + 20, 0);
      bytes[data] = 1;
    ```

- **L118**
  - old: `  bindSequence(bytes, 0, "loop", 1, 0);`
  - new: `  bindSequence(bytes, 0, "loop", 1, 0, sequenceAt);`

- **L133**
  - old: `  writeAt<std::int32_t>(onceBytes, 408 + 216 + 100 + 12, 0);`
  - new: `  writeAt<std::int32_t>(onceBytes, sequenceAt + 12, 0);`

修复后的几何自洽性（供复核）：
`studioShell(2,1,1)` → `boneAt=408`、`animAt=840`、`sequenceAt=940`、`size=1152`；
`animIndex = 1152-840 = 312` ⇒ `dataOffset = 840+312 = 1152 = data` ✔（与改前的 936 同构）。
bone 1：`parent=0`、`position=(1,0,0)`、`positionScale=(1,1,1)`、`rotation=(0,0,0,1)`；
通道 0 在 frame 0/3 读 `data+12` / `data+18` = 0 / 300 ⇒ `modelPosition` = `(1,0,0)` / `(301,0,0)`，
`:144` 成立；`wrapped`（tick 4，loop）仍为 frame 0 ⇒ `:129` 成立；`clamped` 仍为 frame 3 ⇒ `:142-143` 成立。

### 修复后必须重跑的命令与预期

```bash
cd D:/TF2_Native_Worktrees/t105-e5
bash build-target.sh animation_viewmodel_probe          # 必须重编译，旧 exe 不含改动
TF_ROOT="/d/SteamLibrary/steamapps/common/Team Fortress 2/tf" \
  ./native/build-nmake/animation_viewmodel_probe.exe; echo "rc=$?"
```

预期 stdout（单行，关键处）：

```
selfTest=true parent=true loop=true compressed=true malformed=true fov=true missingCompanions=true bindPose=true scout=true soldier=true motion=true section=true realLoop=true deterministic=true viewmodel=true motionSequence=AttackStand_PRIMARY bones=76 anims=1012 ...
rc=0
```

必须看到的行级变化：`loop=false` → `loop=true`、`selfTest=false` → `selfTest=true`、`rc=1` → `rc=0`；
**`realLoop=true`、`bones=76 anims=1012`、`motionSequence=AttackStand_PRIMARY` 三项不得变化**（真实模型断言不许被削弱）。

---

## 结论不能证明什么

- 不能证明**产品在所有真实序列上的 loop 都正确**。`realLoop` 只抽中**一条**序列（找到第一条满足条件的即 return），
  它证明的是"该序列 + 该解码路径"，不是全部 1012 条 animdesc。
- 不能证明 root-bone bind 策略（`2024a526` / `:289-306`）在**视觉**上是对的。本轮未抓帧、未跑 `tf2_demo_native.exe`。
- 不能证明位置/旋转**数值**正确。探针自己输出的 `confidence=bit-layout; Euler order and position scale are not visually verified`
  仍然成立：本任务只验证"帧是否可区分"，不验证数值正确性。
- 不能证明 sectioned anim（`section=true` 那条路径）或外部 anim block 情况下的回绕行为。
- 不能证明"修复后的夹具适用于任意骨架形状"。它只证明该合成夹具能区分第 0 帧与第 3 帧。
- 不能证明 `:144` 是本函数**实际**的返回点是通过插桩观测得到的——本轮禁止构建，该结论由第 3 节的
  穷举排除法得出；修复后重跑是补齐这一步的最小动作。
- 不能证明仓库其它探针不受同类"夹具随产品语义漂移而失效"问题影响（本任务未扫其它 51 个探针）。

---

## 结果

```
Result: PASS
Files changed: docs/TRIAGE-LOOPFALSE-2026-11-14.md (new); 运行产物 .scratch/loopfalse/*.txt (untracked, 未入库)
What this proves:
  - `loop=false` 属于 (a) 夹具缺陷：`loopAndOnce()` 的 `studioShell(1,1,1)` 只有 root 骨，
    而 `animation_decoder.cpp:289-306` 对 root 骨强制 bind 变换（commit 2024a526 引入），
    使 `animation_viewmodel_probe.cpp:144` 的位置差异断言恒为假；产品的 loop 标志解析
    (`:23/:498/:502`) 与 `selectFrame` 回绕 (`:222-235`) 与 mdl `STUDIO_LOOPING=0x0001` 一致，
    并由 `realLoop=true` 在真实模型上反证。给出 4 处精确 old/new 的最小修复与重跑判据。
What this does not prove:
  - 未重编译、未验证修复后 `loop=true`；未验证全部真实序列的 loop；未验证 root-bind 策略的
    视觉正确性与任何采样数值；未插桩确认 `:144` 为实际返回点（由穷举排除得出）。
```
