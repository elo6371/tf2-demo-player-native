# Native Release install smoke (2026-10-04)

The Release install was generated with CMake into a clean prefix. It contains
the native executable, the offline smoke probe, and the PowerShell checker; no
TF2 VPK or other game asset was packaged.

```text
-- Installing: .../install-smoke/tf2_demo_native.exe
-- Installing: .../install-smoke/native_install_smoke_probe.exe
-- Installing: .../install-smoke/install_smoke_probe.ps1
```

The probe ran without a TF2 root and without opening an audio device:

```text
install_executable=1 tf_root_missing_prompt=1 warp_fallback=1 feature_level=b100
```

The installed-directory checker passed system DLL checks for `d3d11.dll`,
`dxgi.dll`, `d3dcompiler_47.dll`, and `winmm.dll`:

```text
install_executable=1 tf_root_missing_prompt=1 warp_fallback=1 feature_level=b100
install_layout=ok tf2_assets_packaged=0
```
