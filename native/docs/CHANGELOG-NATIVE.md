# Native 主线更新日志

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
