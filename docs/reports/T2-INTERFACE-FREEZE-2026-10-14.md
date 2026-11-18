# T2 interface freeze (2026-10-14)

Task: water surface and ViewModel advanced materials (Phong / Bump / Selfillum).
Worktree: `D:\TF2_Native_Worktrees\world-material-effects`
Baseline: `7c24bda` (origin/p0-entity-protocol)

## Why freeze before implementing

T1 froze its two entry points the same way, for the same reason: a reading that is
defined after the code that produces it is a reading that gets bent to fit the
code. Everything below is a contract and nothing below it is.

The freeze is **additive**. No existing struct field changes meaning, no existing
function changes signature, and no existing trace field is renamed. The reason is
the constraint in the task: T1's skybox drawing and VIS clipping are off limits,
and `world-material-check.sh` / `viewmodel-material-check.sh` pin their readings.
A frozen field that moved would break both gates for no gain.

## Existing interfaces measured (not assumed)

| Question | Measured answer |
|---|---|
| water faces / triangles | `map.waterFaces` (18 on cp_snakewater_final1), `triangle.water` (36) |
| world draw flow | `uploadWorldGeometry` builds one buffer for every triangle, water included |
| water texture | **none** — no water material is read and no water texture is bound anywhere |
| ViewModel slot table | `resolveViewModelMaterialTable` → `ViewModelMaterialSlot.{phong,bump,selfIllum}` = declared only |
| range build | `main.cpp:2305 viewModelDrawRangesFor` → `materialKey / translucent / alphaTest` |
| base texture cache | `viewModelMaterialTextures_` keyed by normalised vtf path, hit returns `false` |
| shader constants | `ViewModelConstantsData{mvp,tint,skinInfo,material}` = 112 B; only `material.xy` used |
| ViewModel shader | `viewModelPixelSource`, `t0` = albedo, one fixed light direction, no phong/bump/selfillum |
| VMT reader | `VmtMaterial.fields` carries **every** raw scalar key, so `$phongexponent` etc. are readable without touching `vmt_material.cpp` |

## The additive contract

### Water (`world_material.h`)

Five state words, because the task asks for them and because "declared" and
"parsed" are different facts: a VMT that exists is declared, a VMT that exists
and resolved a texture is parsed.

```
WorldWaterSurfaceState { Unavailable, Declared, Parsed, Active, Fallback }
```

Three entry points, all pure (no GPU, no file I/O), so the probe drives the same
code the product drives:

```
WorldWaterDrawReading evaluateWaterDrawRequest(const WorldWaterDrawRequest&, std::vector<WorldWaterVertex>& out)
void worldWaterWarpUv(float u, float v, double phase, float amplitude, float& outU, float& outV)
WorldWaterParams measureWaterParams(const std::string& materialText)
```

`worldWaterWarpUv` is the one function whose contract is worth spelling out,
because the three things the task demands of it are properties rather than
values:

1. **bounded** — `outU` may never leave `[u - amplitude, u + amplitude]`, for any
   phase and any input in `[0,1]`.
2. **NaN-free** — a non-finite input or amplitude returns the input unchanged
   rather than poisoning the sample.
3. **repeatable at a fixed phase** — `f(u,v,p) == f(u,v,p)` always, and
   `f(u,v,0) != f(u,v,p)` for the phases the product actually uses. The second
   half is what makes `water-on == water-repeat` a statement about the warp
   rather than about a constant.

### ViewModel advanced materials (`viewmodel_material.h`)

One reader per effect, each returning its own six-field report, and one range-level
judge. The reader and the judge are separated because they answer different
questions: the reader says what the resource declared, the judge says what the
draw did with it.

```
struct ViewModelEffectReport {          // the six the task names
  State state; bool declared, parsed, uploaded, applied; bool fallback; std::string reason;
};
struct ViewModelPhongReading  : report + { float exponent, boost; float fresnel[2]; float tint[3]; }
struct ViewModelBumpReading   : report + { std::string normalMapPath; bool resolved, uploaded, reused; }
struct ViewModelSelfillumReading : report + { bool maskDeclared; std::string maskPath; float tint[3]; }

ViewModelPhongReading    readViewModelPhong(const VmtMaterial&, const std::string& basePath);
ViewModelBumpReading     readViewModelBump(const VmtMaterial&, const std::string& basePath);
ViewModelSelfillumReading readViewModelSelfillum(const VmtMaterial&, const std::string& basePath);
```

Applied ranges are counted per effect by the renderer and read back through one
new getter (`viewModelMaterialSampleState`) rather than by extending
`WorldMaterialSampleState`. T1's struct and T1's getter are frozen; a new struct
next to them cannot move a field T1's gate pins.

## Scope discipline

`native_renderer.cpp` and `main.cpp` are touched, which the task allows with
justification. The rule followed here:

* the skybox pass, the pvs clip and the world vertex buffer are **not edited** —
  water is an additional pass over an additional buffer, so turning water on or
  off cannot change what the world pass draws and cannot move
  `drawnTriangles + hiddenTriangles == inputTriangles`;
* every edit is a new field, a new function or a new branch, never a changed
  value in an existing one.

`world-material-check.sh` and `viewmodel-material-check.sh` are re-run unchanged
after every step that touches the product. The proof that skybox/VIS did not
regress is those two gates staying green, not a paragraph.
