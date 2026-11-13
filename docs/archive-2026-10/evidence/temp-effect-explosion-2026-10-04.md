# CTEExplosion 到 TempEffectTimeline 证据（2026-10-04）

本轮接入 `demo_header.cpp` 已实际解码的字段：

- `m_vecOrigin` -> `TempEffectEvent.origin`
- `m_iMagnitude` -> `magnitude`
- `m_iScale` -> `scale`
- `m_iRadius` -> `radius`

同时接受 `CTETFExplosion` 和 `CTEExplosion` 两个已存在的 class 名。没有
接入粒子名、材质或渲染器；字段值保持原始整数，未对负数做未经协议证明的
语义裁剪。

## Probe

```powershell
cmake --build native/build --preset windows-release --config Release --target temp_effect_probe
native/build/Release/temp_effect_probe.exe
```

输出：

```text
temp_effect_firebullets accepted=1 events=3 entity=7 weapon=13 seed=99 explosion_events=2 explosion_magnitude=80 explosion_scale=2 explosion_radius=128 explosion_wrong_class_rejected=1 wrong_class_rejected=1 nan_rejected=1 zero_shots_rejected=1 negative_tick_rejected=1 status=pass
```

这验证了 FireBullets 与 Explosion 的数据生命周期、字段保留和边界拒绝，
但不代表任何粒子或材质已经渲染。
