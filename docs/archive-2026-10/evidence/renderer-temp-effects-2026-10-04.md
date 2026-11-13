# Renderer TempEffect 可见标记证据（2026-10-04）

已解码的 `CTEExplosion` 与 `CTEFireBullets` 现在通过原生 renderer 的动态
LINELIST 缓冲区生成可见几何标记：

- FireBullets：origin 到 direction 的橙色方向线。
- Explosion：以 origin 为中心，按已解码 radius 生成 X/Y/Z 三轴标记线；
  缺少 radius 时使用固定小标记。

这条路径只使用纯色 `WorldVertex`，没有伪造粒子材质、VMT 或 VTF 替换。
Explosion 标记仅在 `event.tick == currentTick` 时上传，下一 tick 会被清空，
因此生命周期由 Demo tick 驱动。

## Probe

```powershell
cmake --build native/build --preset windows-release --config Release --target temp_effect_probe tf2_demo_native
native/build/Release/temp_effect_probe.exe
```

输出：

```text
temp_effect_firebullets accepted=1 events_before_clear=3 explosion_events=2 explosion_magnitude=80 explosion_scale=2 explosion_radius=128 explosion_wrong_class_rejected=1 wrong_class_rejected=1 nan_rejected=1 zero_shots_rejected=1 negative_tick_rejected=1 early_range=0 active_range=3 lifecycle_cleared=1 status=pass
```

Probe 验证 tick 区间过滤和 `clear()` 生命周期；渲染器接入只增加可见几何
标记，不代表粒子系统或材质替换已经实现。
