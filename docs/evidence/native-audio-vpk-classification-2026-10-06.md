# Native VPK sound classification (2026-10-06)

The new `audio_vpk_classification_probe` enumerates WAV entries from the installed
`tf2_sound_misc_dir.vpk` and `tf2_sound_vo_english_dir.vpk`, classifies paths with
the same `SoundIndexTable` used by the scheduler, and parses each non-voice/world
sample through `readSoundResource`. It injects no playback sink and opens no audio
device.

This proves the resource side only. It does not claim that a Demo sound index was
observed or that a weapon/footstep/Uber event was classified from a Demo. That
remains blocked by the current branch's Demo open/scan failure and must be rerun
against a parser build whose `demo_open_probe` matches `DemoNetworkSummary`.

## Reproducible silent run

```text
cmake --build build --config Release --target audio_vpk_classification_probe
$env:TF_ROOT='D:/SteamLibrary/steamapps/common/Team Fortress 2/tf'
./build/Release/audio_vpk_classification_probe.exe
archives=2 wav_entries=2817 wav_parsed=1062 wav_failures=0 skipped_voice_or_world=1755
kind=weapon classified=879 parsed=879 example=sound/weapons/3rd_degree_hit_01.wav example=sound/weapons/3rd_degree_hit_02.wav example=sound/weapons/3rd_degree_hit_03.wav
kind=footstep classified=167 parsed=167 example=sound/misc/octosteps/octosteps_01.wav example=sound/misc/octosteps/octosteps_02.wav example=sound/misc/octosteps/octosteps_03.wav
kind=uber classified=16 parsed=16 example=sound/items/powerup_pickup_uber.wav example=sound/player/invulnerable_off.wav example=sound/player/invulnerable_on.wav
device_opened=0 status=pass
```
