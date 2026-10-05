# CPU 粒子 fallback 证据（2026-10-04）

原生 renderer 现在从已解码的 `ProjectileTimelineEvent` 生成受限 CPU 粒子快照：

- `CTEExplosion`：短生命周期爆炸点线段，半径来自已解码 `radius`，缺失时用
  固定小半径。
- `CTEFireBullets`：沿已解码方向生成短尾迹线段。
- 生命周期：只接受 `event.tick <= currentTick` 且不早于 `currentTick - 8`。
- 上限：最多 4096 个 `WorldVertex`，超出后停止收集。
- 输出：复用 renderer 动态 LINELIST 顶点缓冲区，纯色几何，不读取 PCF，
  不生成或替换任何材质。

## 验证

`temp_effect_probe` 验证了事件时间区间和清理：

```text
early_range=0 active_range=3 lifecycle_cleared=1 status=pass
```

该实现是几何/颜色 fallback，不代表完整 PCF 粒子系统、粒子算子或材质采样
已经实现。
