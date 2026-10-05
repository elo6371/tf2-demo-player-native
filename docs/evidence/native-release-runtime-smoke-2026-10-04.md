# Native Release runtime smoke (2026-10-04)

The existing installed-directory check passed before the runtime checks:

```text
install_executable=1 tf_root_missing_prompt=1 warp_fallback=1 feature_level=b100
install_layout=ok tf2_assets_packaged=0
```

The local `73.dem` sample was started with `--start-paused --audio-device 0`
and the real TF root for five seconds. The process stayed alive and was then
terminated by the probe harness:

```text
demo73_start_paused_alive=1 exit=-1
```

The same demo was started with a deliberately missing TF root for four seconds.
It stayed alive, showing the missing-resource startup path does not crash:

```text
missing_tf_root_alive=1 exit=running
```

The standalone WARP probe already passed with feature level `b100`. After both
runtime checks, the process inventory reported:

```text
native_processes_after_checks=0
```

This is a smoke result, not a heap or GPU leak proof; no crash or lingering
native process was observed.
