# Gemini 只读任务包（2026-11-20）

用途：把下面每个 brief 整段复制给外部 Gemini，做只读分析/设计评审，结果贴回来由我们落地。
统一约束（每个 brief 都适用）：

- 只读分析，**不要**写产品代码补丁；不要假设能访问我们的机器或 Git 历史。
- 结论必须带**依据**：原文行号、数值、公式推导；没有依据的判断标「待证」。
- 输出 Markdown，先给「结论（≤5 行）」，再给证据表 / 风险 / 可执行下一步。
- 用中文写。

---

## G1. N12/N19 长片 seek 陈旧度：独立复核

**背景**：我们的 demo 播放器按 96 行稀疏检查点存档；上游 TF2-Demo-Analyser 用定宽 1,088 B/帧、MAX_FRAMES=150_000 的帧缓存。

**已有结论（待你复核，不要直接接受）**：
1. 我们的 1 个采样单位是一份全量检查点 8,701,402 B，不是 1,088 B；150k 份 ≈ 1.30 TB，所以「150k 帧」不能按帧数搬。
2. 96 行档案恒定约 835 MB、与片长无关；我们不存在长片 OOM。
3. 真正的长片缺陷是 seek 陈旧度 ≈ 片长/192：三份 demo 的 gap_median ≈ span/96；76 min POV 实测陈旧 24 s。
4. 建议引入字节预算 `maxArchiveBytes`（C1），相机轨/弹体轨禁止帧数上限（C4），不引入 MAX_FRAMES/frame_at。

**需要你做的**：
- 复核第 1、3 条的算术：8,701,402 × 96 是否 ≈ 835 MB；span/96 与「陈旧度 span/192」的关系是否自洽（给出推导）。
- 指出第 4 条的风险：字节预算触顶后先丢什么、会不会让 seek 更陈旧；给出「预算 + 陈旧度上界」双约束的取舍建议。
- 若认为应该引入帧数上限，给出反例与代价。

**附带材料**：无（上面的数字已足够；可选附我们审计报告 .scratch/n12-n19-audit/report.md）。

---

## G2. N21(b) WAV cue / loop_start：最小实现方案

**背景**：我们的 WAV 解析只认 `fmt ` 和 `data`，其余 chunk 一律跳过（audio_resource.cpp:122）；结构体没有 loop 字段；播放侧是一次性 waveOutWrite（audio_player.cpp:231，CALLBACK_NULL）。上游 TF2-Demo-Analyser 用 Source 的 loop_start/cue 语义让循环音效从 cue 点回绕。

**需要你产出**：
1. RIFF `cue ` chunk 与 `smpl` chunk 的精确布局：`dwSampleOffset` 在 chunk 内的字节偏移（相对 chunk data 起点），以及 sampler chunk 的循环字段（start/end/loop count）偏移；给出 32 字节对齐与 chunk padding 的注意点。
2. 播放侧最小改法：在 waveOutWrite 一次性路径上如何实现「到 loop_end 回到 loop_start」——重新排期 buffer 的推荐方案（双缓冲/定时器/回调）与线程安全边界；说明为什么不能只改解析不改播放。
3. 回退策略：没有 cue 时按 chunk 缺失处理；loop 点越界/为 0 时的防御。
4. 验收清单（真实带 cue 的 loop 音效怎么证明它真的回绕了，而不是被当一次性播完）。

**附带材料**：audio_resource.cpp、audio_resource.h、audio_player.cpp（粘贴相关函数即可）。

---

## G3. N16 关闭标签页的 GPU closing 队列：设计契约

**背景**：我们是 D3D11 单进程多视图（多个标签页各自持有 GPU 资源：纹理、顶点/索引缓冲、render target）。当前关闭标签页时直接释放资源；上游用 closing 队列把「逻辑关闭」与「GPU 资源回收」解耦。

**需要你产出**：
1. 设计契约：ownership 归属、延迟回收的入队/出队时机、与帧边界（present/fence）的关系、如何证明没有 use-after-free。
2. 队列元素应携带什么（设备指针、资源列表、引用计数、帧号、关闭原因），以及设备丢失（device lost）时如何整体清空。
3. 两种实现路线对比：引用计数式（shared_ptr 自定义 deleter）vs 显式队列 + 帧栅栏；推荐一种并说明为什么。
4. 测试矩阵：连续开关 100 次、切换分辨率、设备丢失恢复、关闭当前正在渲染的标签页。

**附带材料**（可选）：我们的 renderer 头文件与多视图管理代码摘要。

---

## G4. N27 离线片段导出兜底：规格

**背景**：我们已有离线导出路径；上游兜底是「ffmpeg 可用→MP4（H.264/AAC），否则→800px 宽 GIF」。

**需要你产出**：
1. 兜底规格：探测 ffmpeg 的方式（PATH/内置二进制/版本下限）、超时与取消、进度上报、失败回退顺序。
2. 输出参数建议：MP4 的码率/关键帧间隔/像素格式；GIF 的宽度 800、帧率上限、调色板与抖动；两者的体积上限。
3. 失败模式表：编码器缺失、磁盘满、输入帧率为 0、音频缺失、路径含非 ASCII/空格。
4. 验收清单：如何用一条命令证明「MP4 分支」与「GIF 分支」都被真实走到。

**附带材料**：无（按通用规格写；我们落地时再对齐）。

---

## G5. N17 Step 1 解码假设的独立复核（优先级最高）

**背景**：我们正在给动画解码器补 TF2 的 2D pose 网格（9 格 3×3 移动、12 格 4×3 瞄准、少量一维）。当前实现只读混合序列的第 0 个 int16 单元，因此 9 格永远播西南格、瞄准矩阵永远播 straight_up。

**我们的读取计划（待你逐条证伪）**：
- 结构体（修正版）：序列描述是 `mstudioseqdesc_t`（212 B）：`numblends`（+56）、`animindexindex`（+60，相对本记录起点的 int32 字节偏移，指向 `int16` 网格）、`groupsize`（+68/+72）、`paramindex`（+76/+80）、`paramstart/paramend`（+84..+96）；+32..+52 是 bbmin/bbmax；`weightlistindex` 在 +156。**每个 blend 单元 = 单个 int16**（`mstudioanimdesc_t` 下标），不是 3 元组。
- `mstudioanimdesc_t`（100 B）：`numframes` 在 +16（int32）、`animindex` 在 +56；pose-param 表在 `studiohdr_t` 的 +300/+304（条目 20 B：name +0 / flags +4 / start +8 / end +12 / loop +16）。
- 扁平化（统一公式）：`k = y*groupsize[0] + x`，`x = paramindex[0]`（横向：move_y / body_yaw），`y = paramindex[1]`（前后/纵向：move_x / body_pitch）。9 格与 12 格共用，无需分族。
- `weight_list` 是逐骨骼 `float` **连续权重**（实测有 0.5/0.4/0.75 等），不是二值掩码；应先按 2D 网格采样整层 pose，再按 `weight_list[bone]` 加权叠加。
- 归一化来源：`move_x` = 速度在 view yaw 前向的投影、`move_y` = 右向投影（Source x=前、y=右）；aim pitch 用 `pos = (90 − clamp(−eyePitch,−45,90))/45`；aim yaw 恒 0 → 中列（当前身体朝向=视线，待 Step 5 解耦）。

**需要你做的**：
1. 对照 TF2 mdl v48 的 anim_desc 布局（mstudioanimdesc_t）逐字段核对上面的偏移与类型；指出错的偏移、单位（int16 vs uint8）、对齐。
2. 判断「9 格与 12 格扁平化方向相反」是否成立；给出上游实现依据或反证。
3. 检查 `weight_list` 的语义（逐骨骼参与掩码 vs 归一化权重）与我们「先做掩码再做 pitch」的顺序是否合理。
4. 指出 Step 1 最小可证伪实验：只解元数据、不采样，如何用 39/28/6 的计数与四角名字证明解码正确。

**附带材料**：native/src/animation_decoder.cpp、native/include/animation_decoder.h、.scratch/n17-posegrid-plan.md（如能附）。若不能附文件，请明确哪些结论只能「待证」。

---

## G6. N18 P2 像素对照的验收设计（可选，第二位评审）

**背景**：第一人称武器挂点目前走 attachment 链，残差按构造为 0；上游用同名骨骼合并。P1 已离线证明 `weaponWorld = armsWeaponBoneWorld` 无偏移（权重恒等式 max|Σw−1|=5.96e-8、根骨刚性误差 ≤1.5e-5）。

**需要你产出**：`--viewmodel-weapon-attach=attachment|bone` A/B 的同 tick 像素对照方案——如何选 tick、如何构造「只换挂点、其它全同」的对照、判定阈值（枪口到手臂 weapon_bone 距离 ≤12 之外还要什么）、失败时如何区分「配对错误」与「D(t) 时变」。

**附带材料**：.scratch/n18-bonemerge/report.md、p1-weight-identity.md。

---

### 交付回来后我这边怎么用
- G1/G5 先看：G5 若指出偏移错，立刻停 N17 Step 1 施工回来核对；G1 决定 N12/N19 是否改方案。
- G2/G3/G4 进队列，等 native 写入窗口（N17 → N18 P2 → N21 (a)）之后排。
- G6 不阻塞施工，只用来收紧 P2 验收。
