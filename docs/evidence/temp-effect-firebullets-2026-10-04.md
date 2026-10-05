# CTEFireBullets 到 TempEffectTimeline 证据（2026-10-04）

本轮只接入 `demo_header.cpp` 已实际解码的 `CTEFireBullets` 字段：

- `m_vecOrigin` -> `TempEffectEvent.origin`
- `m_vecAngles` -> `TempEffectEvent.direction`
- `m_iPlayer` -> `entityIndex`
- `m_iWeaponID` -> `weaponId`
- `m_iSeed` -> `seed`

`m_iMode` 和 `delayRaw` 没有被转换为额外语义；协议 shot count 未解码，
所以单个 TempEntity 事件固定记录为 `shots=1`，不代表协议中的射击数量。

## Probe

```powershell
cmake --build native/build --preset windows-release --config Release --target temp_effect_probe
native/build/Release/temp_effect_probe.exe
```

输出：

```text
temp_effect_firebullets accepted=1 events=1 entity=7 weapon=13 seed=99 wrong_class_rejected=1 nan_rejected=1 zero_shots_rejected=1 negative_tick_rejected=1 status=pass
```

Probe 覆盖了错误 class、NaN 坐标、零 shots 和负 tick；所有非法变体均被拒绝。
本轮没有猜测未知 SendProp，也没有接入粒子或爆炸材质渲染。
