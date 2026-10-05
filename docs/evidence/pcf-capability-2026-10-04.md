# PCF 能力状态（2026-10-04）

新增 `pcf_capability_probe` 只读检查 loose/VPK 资源，不解析粒子算子或材质。

```powershell
cmake --build native/build --preset windows-release --config Release --target pcf_capability_probe
native/build/Release/pcf_capability_probe.exe `
  'D:\SteamLibrary\steamapps\common\Team Fortress 2\tf'
```

真实输出：

```text
pcf loose_count=0 vpk_status=opened vpk_pcf_count=135 first_entry=particles/bigboom.pcf first_prefix_hex=3c 21 2d 2d 20 64 6d 78 20 65 6e 63 6f 64 69 6e dmx_header_candidate=1 system_markers=1 operator_markers=6 header_validated=false parsed=false rendered=false mapping=false
```

首个 PCF 的前缀是 ASCII `<!-- dmx encodin...`（十六进制输出见上），探针将其
标记为 `dmx_header_candidate=1`，并观察到 `system_markers=1`、`operator_markers=6`。
这只说明文件前缀和少量文本标记像文本化 DMX；当前工程没有经过验证的 PCF/DMX schema、
算子解析器或材质绑定，因此不把该前缀当作完整格式验证，也不宣称 PCF
粒子已渲染。

当前能力分层：

- 资源存在：`Available`（VPK 索引可列出 135 个 PCF）。
- 头部验证：`Unknown`。
- PCF/DMX 结构解析：`Unavailable`。
- 粒子算子与材质采样：`Unavailable`。
- Renderer PCF 绘制：`Unavailable`；现有 CPU 几何/颜色 fallback 独立运行。
