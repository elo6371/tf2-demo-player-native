# `pl_upward_f12` 地图与 HUD 验收

日期：2026-10-02（Asia/Shanghai）

## 地图来源

- 来源：`https://fastdl.serveme.tf/maps/pl_upward_f12.bsp`
- 本地路径：`testdata/pl_upward_f12.bsp`
- 文件大小：25,252,306 bytes
- SHA256：`0D9FE5486023DF49F0E6D4DA43AC3E777BAEE87F1C588933DC16750CB7C51677`
- Demo：`0f55b29be497e7ba673a1a888b252a78_matcha-20260808-1349-pl_upward_f12.dem`

## 实测

`scripts/character-render-check.ts` 使用真实 Demo、下载的同版本 BSP 和本地 TF2 资源运行：

- 7/7 帧 `glError=0`
- 自由镜头、第三人称跟随、玩家 POV 均成功渲染
- 角色绘制计数：`2186 / 14012 / 14816 / 44030 / 42215 / 31270 / 28765`
- 7 个跟随目标中 7 个在 20 秒内产生有效相机位移，观察者目标保持不动
- POV 与第三人称眼高差验证通过：`followHeight 90`，实际差值 `15`，收敛 `true`

`scripts/hud-theme-check.mjs` 使用 `pl_upward_f12 -> cp_snakewater_final1` 连续切换运行：

- `pl_upward_f12 · 推车` / `dataset=payload`
- `cp_snakewater_final1 · 5CP` / `dataset=fiveCp`
- RED/BLU 队名、玩家名、职业图标、血量和 Über 读数存在
- HUD 隐藏/恢复、`显示 HUD` 按钮、Alt 交互和主题导入验收通过

## 判定

精确 `pl_upward_f12.bsp` 已补齐，地图几何与 HUD 模式已在真实样本上验证。角色材质仍是当前 WebGL 适配质量，不能等同于 TF2 原生渲染；投射物瞬时实体正文和 Demo 游戏音效时序仍未闭环。
