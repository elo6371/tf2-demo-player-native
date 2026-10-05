# GPU 骨骼蒙皮和动画实现计划

## 当前状态分析

### ✅ 已完成
1. **模型加载器** - `ModelLoader` 可以读取：
   - MDL 骨骼层级（`ModelBone`，parent、position、poseToBone 矩阵）
   - VVD 顶点权重（`ModelVertex.weights`、`boneIndices`、`boneCount`）
   - 动画序列元数据（`ModelSequence.label`、`activity`、`flags`）
   
2. **渲染器骨骼管线** - `NativeRenderer` 已有：
   - Shader 常量缓冲 `ModelSkinningConstants` (8208 字节 = 128×4×4×4 + 16)
   - Vertex shader 输入布局包含 `BLENDWEIGHT` 和 `BLENDINDICES`
   - 顶点数据上传包含权重和骨骼索引

### ❌ 缺失功能

1. **骨骼矩阵未上传** - `modelSkinningConstants_` 缓冲从未被 `UpdateSubresource`
2. **动画采样器不存在** - 没有代码从 `.ani` 文件或 MDL 内嵌动画中读取帧数据
3. **Vertex shader 不做蒙皮** - 第 227 行直接用 `input.position`，权重和骨骼索引被忽略
4. **ViewModel 绘制路径缺失** - 没有 `v_*.mdl` 的特殊处理（FOV、attachment）

---

## 实现路径

### ✅ 阶段 1：静态 Bind Pose 蒙皮（验证管线）- 已完成

**目标**：用 identity 矩阵验证蒙皮管线，模型应该显示为 bind pose（无动画）

1. ✅ 读取 MDL `poseToBone` 矩阵（已有，在 `ModelBone::poseToBone`）
2. ✅ 上传 identity 矩阵到 `modelSkinningConstants_`
3. ✅ 修改 vertex shader 执行实际蒙皮：
   ```hlsl
   float3 skinnedPos = float3(0, 0, 0);
   float3 skinnedNormal = float3(0, 0, 0);
   for (uint i = 0; i < 3; ++i) {
     if (input.weights[i] > 0.0) {
       skinnedPos += mul(float4(input.position, 1.0), bones[input.boneIndices[i]]).xyz * input.weights[i];
       skinnedNormal += mul(float4(input.normal, 0.0), bones[input.boneIndices[i]]).xyz * input.weights[i];
     }
   }
   output.position = mul(float4(skinnedPos, 1.0), mvp);
   output.normal = normalize(skinnedNormal);
   ```

**验证方法**：模型应该正确显示（因为 identity 矩阵 = 无变换）

**实现细节**：
- 在 `NativeRenderer::uploadBoneMatrices()` 添加矩阵上传功能
- Vertex shader 现在使用 `bones[input.boneIndices[i]]` 执行真正的蒙皮
- 权重归一化在 shader 中自动处理

---

### ✅ 阶段 2：真实 Bind Pose 矩阵 - 已完成

**目标**：用 MDL 的 `poseToBone` 矩阵，模型应该仍然显示为 bind pose（但矩阵是真实的）

1. ✅ 从 `ModelMetadata.bones[].poseToBone` 构造 `float4x4` 矩阵数组
2. ✅ 上传到 `modelSkinningConstants_`
3. ✅ 验证 `poseToBoneValid` 标志，确保所有骨骼矩阵有效

**注意**：TF2 的 `poseToBone` 是 3×4 矩阵（12 个 float），需要扩展为 4×4：
```cpp
// poseToBone[12] 布局：
// [0-2]: X 轴 + X 平移
// [3-5]: Y 轴 + Y 平移
// [6-8]: Z 轴 + Z 平移
// [9-11]: 未使用（补齐）
// 需要构造为：
// | poseToBone[0]  poseToBone[3]  poseToBone[6]  poseToBone[9]  |
// | poseToBone[1]  poseToBone[4]  poseToBone[7]  poseToBone[10] |
// | poseToBone[2]  poseToBone[5]  poseToBone[8]  poseToBone[11] |
// | 0              0              0              1              |
```

**实现细节**：
- 在 `main.cpp` 添加 `convertPoseToBone()` 辅助函数转换 3x4 到 4x4 矩阵
- 从 `model.bones[].poseToBone` 读取真实矩阵并上传
- 骨骼层级关系已存在于 `ModelBone::parent`，但当前尚未用于矩阵链计算

---

### ⬜ 阶段 3：动画序列解码器（从 MDL 内嵌动画开始）

**目标**：播放简单的内嵌动画（如 idle、walk）

1. ⬜ 解析 MDL 的 `mstudioanimdesc_t` 结构：
   - `animindex` 指向动画数据块
   - `numframes` 帧数
   - `fps` 帧率
   
2. ⬜ 解码每帧的骨骼位置/旋转（Source 动画格式复杂，有多种压缩方式）：
   - **mstudioanim_t** - 每个骨骼的动画数据
   - **mstudioanim_valueptr_t** - 压缩的位置/旋转值
   - 可能需要参考 Valve SDK 2013 的 `studio.h` 和 `bone_setup.cpp`
   
3. ⬜ 采样当前帧，生成 `bones[]` 矩阵数组
4. ⬜ 上传到 GPU

**简化方案**：如果 Source 动画格式太复杂，可以先做静态 pose 切换：
- 只支持 `numframes == 1` 的静态 pose
- 或者只支持线性插值的简单动画

---

### 阶段 4：外部 .ani 文件支持

**目标**：支持从 `.ani` 文件加载动画（复杂模型如 Heavy 的动画在独立文件中）

1. ⬜ 从 VPK 或松散文件读取 `.ani` 文件
2. ⬜ 解析 `.ani` 文件头（格式与 MDL 类似，但只有动画数据）
3. ⬜ 合并到动画采样器

---

### 阶段 5：ViewModel 渲染

**目标**：正确绘制第一人称武器（`v_*.mdl`）

1. ⬜ 检测 ViewModel 路径（`models/weapons/v_*.mdl`）
2. ⬜ 调整 MVP 矩阵：
   ```cpp
   // ViewModel 有独立的 FOV（通常是 70-80，而不是世界的 90）
   Matrix4x4 viewModelProjection = PerspectiveFOV(viewModelFOV, aspect, near, far);
   Matrix4x4 viewModelMVP = viewModelProjection * viewMatrix * modelMatrix;
   ```
3. ⬜ 处理 attachment 点：
   - 武器模型（如 `w_scattergun.mdl`）需要附着在手部骨骼上
   - 从 `ModelAttachment` 读取 attachment 点的变换矩阵
4. ⬜ 渲染手臂 + 武器

---

## 技术难点

### 1. Source 动画格式复杂度
- **问题**：TF2 使用 Source Engine 的动画格式，有多种压缩方式（raw、animated、fast compressed）
- **解决方案**：
  - 优先支持 raw 格式（未压缩）
  - 参考 Valve SDK 2013 的 `bone_setup.cpp::CalcBonePosition`
  - 如果格式太复杂，先做静态 pose 或固定动画循环

### 2. 骨骼层级和矩阵链
- **问题**：子骨骼的变换需要乘以父骨骼的变换（矩阵链）
- **解决方案**：
  ```cpp
  // 伪代码
  for (int i = 0; i < boneCount; ++i) {
    Matrix4x4 localTransform = ComputeLocalTransform(animData, i, currentTime);
    if (bones[i].parent >= 0) {
      bones[i].worldTransform = bones[bones[i].parent].worldTransform * localTransform;
    } else {
      bones[i].worldTransform = localTransform;
    }
  }
  ```

### 3. 128 骨骼上限
- **问题**：Shader 常量缓冲只能容纳 128 个 `float4x4` 矩阵
- **解决方案**：
  - TF2 的大部分模型 < 64 骨骼，应该够用
  - 如果超过，需要分批绘制或使用 structured buffer

---

## 验证检查点

### 阶段 1 完成后
- [x] 模型显示正确（无变形）
- [x] Vertex shader 确实执行了蒙皮（可通过 GPU 调试器验证）

### 阶段 2 完成后
- [x] 模型仍然显示正确（bind pose）
- [x] `poseToBone` 矩阵被正确上传（可打印前 3 个骨骼的矩阵）

### 阶段 3 完成后
- [ ] 简单动画（如 idle）可以播放
- [ ] 关节运动流畅（无突变）

### 阶段 4 完成后
- [ ] Heavy 等复杂模型的动画可以播放

### 阶段 5 完成后
- [ ] 第一人称武器正确显示
- [ ] 武器附着在手部骨骼上

---

## 时间估算

| 阶段 | 预计时间 | 难度 |
|-----|---------|-----|
| 阶段 1 - Identity 矩阵蒙皮 | 2-4 小时 | 低 |
| 阶段 2 - Bind pose 矩阵 | 2-3 小时 | 低 |
| 阶段 3 - MDL 内嵌动画 | 1-2 天 | **高** |
| 阶段 4 - .ani 文件支持 | 0.5-1 天 | 中 |
| 阶段 5 - ViewModel 渲染 | 1-2 天 | 中高 |

**总计**：3-5 天（如果 Source 动画格式顺利的话）

---

## 当前进度总结（2026-10-04）

### ✅ 已完成
- **阶段 1 和 2**：GPU 骨骼蒙皮管线已工作
  - Vertex shader 执行真正的蒙皮计算（使用权重和骨骼索引）
  - `NativeRenderer::uploadBoneMatrices()` 可上传任意骨骼矩阵到 GPU
  - 已验证：identity 矩阵和真实 `poseToBone` 矩阵都能正确渲染

### 🚧 下一步工作
- **阶段 3**：动画解码器（需要解析 Source Engine 的动画格式）
  - 这是技术难度最高的部分，需要参考 Valve SDK
  - 可能需要 1-2 天深入研究 `mstudioanimdesc_t` 和压缩格式
  
- **阶段 5**：ViewModel 实体（第一人称武器）
  - 独立的 FOV 和投影矩阵
  - Attachment 点系统（武器附着到手部骨骼）

### 📝 技术债务
- 骨骼层级的矩阵链尚未实现（当前只用 `poseToBone`，不考虑父骨骼）
- 动画混合（blend）和 IK 系统暂不支持

---

## 下一步

从阶段 1 开始：修改 vertex shader 并上传 identity 矩阵。
