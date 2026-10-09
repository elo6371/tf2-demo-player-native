# Native 主线更新日志

## 2026-10-09：整合 P0 主循环修复与分层验收入口

### 已整合

- `native/src/main.cpp` 修复固定 1 ms 等待造成的主循环高频轮询。
- 标题和 UI 控件更新增加状态缓存；指标流增加等待和更新计数。
- 新增 `verify-fast.ps1`、`verify-entity.ps1`、`verify-render.ps1`、`verify-audio.ps1`，按子系统执行增量构建和定向探针。
- 新增根目录 `HANDOFF-2026-10-09.md`，作为所有接手对话的第一阅读入口。

### 未整合

- P1a 实体属性预检查只在 `D:\TF2_Native_Test` 的测试树通过，因主线实体模型版本存在语义差异，暂不直接复制源码。
- 完整 `verify-all.sh` 和证据目录继续保留在测试仓库，不进入产品主线。

### 记录的限制

- 目前主循环体自身约占 7 ms，等待修复收益可测但不会自动达到 120 FPS。
- 地图 512x512 材质图集上限、缺失 BSP、实体材质、动画/ViewModel、音频 tick 对齐和高级材质仍未完成。

## 2026-10-09：整合资源可达性诊断

### 已整合

- 新增 `resource_reachability_probe`：报告 VPK 是否打开、地图 BSP 是否可达、地图材质和 VTF 是否能解码，以及当前世界图集能覆盖的三角形数量。
- 新增 `vpk_query`：按归档和路径前缀统计资源条目，用于区分缺失资源和解析故障。
- 在 `native/CMakeLists.txt` 注册两个探针，保持窗口程序和正式协议/渲染源码不变。

### 验证结论

- 测试仓库的 `cp_snakewater_final1` 读数为 148 个地图材质、111 个可解码材质、7 个通过 512x512 限制，覆盖 30731/200000 个三角形。
- `koth_bagel_rc13` 的 BSP/材质缺失仍是本机资源前提，不应被帧抓取门禁误判为地图已经绘制。
- 资源探针通过只证明资源可达性；实体模型材质、骨骼动画、ViewModel 和世界高级材质仍未完成。

### 下一步

先在隔离分支量化放宽 512x512 世界图集上限后的覆盖率和显存成本，再决定图集分片或单材质纹理策略；完成后必须用真实地图帧抓取回归。

## 2026-10-08：P0/P1 候选合并审查

### 问题

- 旧实体报告把 SourceTV 失败归因于缺失 delta base 和 Preserve 分支终止语义。
- 主线仍未包含 `D:\TF2_Native_Test` 的 10 个实体/P1 候选提交。
- 观察目标虽然能解析，但直接驱动相机会使用过期 checkpoint，破坏 POV 画面。

### 复核证据

- 测试树基线 `7ab4e72` 的 `native/` 与主线 `d585af8` 的 `native/` 树对象一致。
- 测试树 `p0-entity-protocol` 的 9 份 Demo：`entity_failures=0`、`malformed_packets=0`、`unknown_message_packets=0`。
- Rust oracle 对照：9 份 Demo 的 enter 数、PacketEntities 包数、实体更新数逐值一致。
- Fixture：`58/58`；变异套件能将错误读数变红；P1 轨迹 oracle 为 `compared=40 mismatches=0`。

### 候选修复

- P0 `658ae69`：解码此前会放弃整包的消息类型。
- P0 `226d119`：按 `clientname` 修正 SourceTV/POV 分类。
- P0 `22dc465`：让索引状态和空输入判据能正确失败。
- P1 `8c6f06e`：从 `modelprecache` 解析模型路径。
- P1 `4868e7b`：缓存 VPK 目录，减少重复扫描。
- P1 `9316416`：补齐 install smoke probe 的 VPK 链接。
- P1 `6cb0ecf`：读取实体自身 Z 和确定性 origin。
- P1 `aa93926`：按 tick 间隙保留实体历史 checkpoint。
- P1 `cd36db1`：使用武器世界模型索引，避免绘制手臂模型。
- P1 `c0ff710`：解析观察模式/目标；拒绝未经新鲜度验证的相机跟随。

### 当前流程决定

这些源码候选尚未进入 `native-mvp`。下一次整合必须按 P0 → P1 分批进行，运行 `git merge-tree`、Release 全量构建、9 份 Demo、fixture 和 oracle 回归后再推送。判据脚本、证据和补丁不自动进入产品源码。

### 尚未解决

- SourceTV 外部语料边界仍需扩大；部分 `updateType==3`、trailer 和 soundprecache 分支未被当前语料区分。
- 观察目标槽位新鲜度和相机跟随未解决。
- 实体模型实际绘制、动画 D3D 接线、ViewModel 第一人称绘制、cubemap/Water、高级材质、Demo 音效时序和发布验收仍未完成。
