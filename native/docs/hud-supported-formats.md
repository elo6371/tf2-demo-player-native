# T51 — What a custom TF2 HUD import may safely support

Status: **the support list is this document**. It is not a claim of Source HUD
compatibility; it is the exact set of files, keys and coordinate forms the
import accepts, and the exact set of reasons it refuses everything else.
Owner: T51 (`task/t51-hud`). Baseline `6b71aeb`.

Read it as a contract: `native/include/hud_import.h` implements this list, and
`hud-check.sh` asserts it. If the three disagree, the disagreement is a bug in
whichever moved last.

## 1. What a TF2 HUD actually is, measured on this machine

The installed corpus used for the measurements below is the m0rehud-5.8 custom
HUD at `tf/custom/m0rehud-5.8`, plus the shipped `tf/resource` tree:

| part of a HUD | files | what it needs |
|---|---|---|
| panel layout | `resource/ui/*.res` | VGUI `EditablePanel` documents in Source KeyValues |
| scheme/colours | `resource/clientscheme.res`, `resource/colors.res` | the same KeyValues shape, referenced by `#base` |
| fonts | `resource/fonts/*.ttf`, `resource/fonts.res` | a font rasteriser, `$font` entries, `1`/`2` pt definitions |
| images/crosshairs | `materials/vgui/**`, `resource/*.res` `image` keys | a VMT/VTF decode chain plus VGUI's image scaling rules |
| scripts | `scripts/*.nut`, `scripts/hudlayout.res` | a Squirrel VM and the game's own globals |

Measured on that corpus: 102 `.res` documents, 23 of which open with `#base`,
and 5313 coordinate values — the value of `xpos`, `ypos`, `wide`, `tall` or
`zpos` — whose first character is either a digit/sign (4903), `c` (168), `f`
(133), `r` (88), `p` (18) or `o` (3); every value is one of those six forms.
No document in it uses a `%` suffix or an `s` (scaled) coordinate, but both
are legal VGUI and both are handled below. The counting rule is stated because
these numbers are a contract: count the five geometry keys across the 102
documents and these are the numbers, so a later reader can reproduce them
instead of taking them on trust.

**What the 102 documents actually do through the parser** (`hud_probe` reads the
whole directory, not a sample; the readings are in `evidence/hud/probe.txt`):

| outcome | count | why |
|---|---|---|
| imported as a layout | **71** | no `#base`, every panel's coordinates resolvable |
| refused, `unsupported-include` | **23** | they open with `#base` and would render with holes |
| refused, `unsupported-coordinate` | **7** | nothing in the document can be placed. Three (`hudalert`, `huddemomancharge`, `hudteamswitch`) are laid out with `cs-0.5` — a centred coordinate carrying the `s` (scaled) suffix, which needs Source's base resolution and which this grammar deliberately cannot express — and every panel in them is one of those. Four (`hudhealthaccount`, `huditemeffectmeter_action`, `huditemeffectmeter_killstreak`, `hudkillstreaknotice`) are control and colour configuration files: their only panel carries no `xpos`/`ypos`/`wide`/`tall` at all, so there is no rectangle to resolve |
| refused, `missing-root-panel` | **1** | `huddamageaccount_last.res` is an include *fragment*: `"Resource/UI/HudDamageAccount.res" { }`, an empty document that only exists to be `#base`d by `huddamageaccount.res`. Refusing it is correct. |

So the honest headline is **71 of 102 (70%)** of an installed custom HUD's
documents, and **that is the support scope, not "Source HUD compatible"**. The
eight refusals that are not includes are all one defect each — a scaled-centre
coordinate, a document with no geometry, or a fragment — and every one of them
is named rather than silently skipped.

One caveat on the "71", because it is the number most likely to be over-read: a
document is refused as a whole only when nothing in it can be placed. When at
least one panel resolves, the document is **imported and the unresolvable panels
are dropped and counted**, so "71 imported" is not "71 documents drawn
completely". Measured over the same corpus, from the `corpus.importedPartial`,
`corpus.panelsKept` and `corpus.panelsRefused` readings in
`evidence/hud/probe.txt`:

| reading | value |
|---|---|
| documents imported | **71 of 102** |
| ... of those, documents that dropped at least one panel | **55 of 71** |
| panels placed | **800** |
| panels dropped | **511** |

So the honest unit for the support scope is the PANEL: **800 of 1311 panels
(61%)** of this installed HUD get a rectangle, and 55 of the 71 documents that
open are drawn only in part. Two of those lose a single `wide "o1"`
(`pvprankpanel.res`, `ingamequeuestatus.res`); others lose most of their
controls. Counting documents hides that; counting panels does not.

Two tokenizer facts the corpus taught, both now covered by fixtures:

- HUD documents carry **quoted empty strings** (`"labelText" ""`) on nearly every
  text panel. A tokenizer that drops empty tokens turns the next key into this
  key's value and misaligns every pair after it; that failure was measured here
  (19 of the first 24 files imported) and fixed by keeping quoted empties.
- A **bare prefix letter** with no number (`"xpos" "c"`) is a typo, not a zero
  offset. It is refused as malformed; `"c0"` is how you say "centred".

### 1.1 The layout documents are the only safe part

A `.res` document is **data**: a tree of quoted strings and numbers. It cannot
execute anything, it cannot reference a network resource, and its keys are
closed-form. That is why it is the one part this feature imports.

Fonts, images and scripts are not data in that sense: a `.ttf` is a parser
attack surface with a rasteriser behind it, a `.vtf` is a container format, and
a `.nut` is a program. This build imports **none** of them and says so per key
(see 3.2): a document's `"font"` and `"image"` values are read, counted and
reported, and the panel is drawn as a solid block. That is a visible, honest
degradation, not a silent one.

## 2. The supported subset, exactly

### 2.1 Document shape

```
"Resource/UI/Name.res"     <- the document name, first token, mandatory
{
    "RootPanelName"
    {
        "controlname" "EditablePanel"    <- and any of the keys in 2.2
        ...
        "ChildPanelName"                 <- nesting IS supported
        {
            ...                           <- relative to the parent's rect
        }
    }
}
```

| accepted | detail |
|---|---|
| quoted and bare tokens | `"xpos" "0"` and `xpos 0` both parse |
| `//` and `/* */` comments | unterminated comment => refused |
| nested panels | a child's coordinates are relative to its parent's resolved rectangle |
| up to 512 panels | a larger document is refused by `too-many-panels` |
| nesting up to 16 | the real corpus reaches depth 7; a deeper document is refused by `nesting-too-deep` |
| duplicate panel names | counted (`duplicateNames`), not fatal |

### 2.2 Panel keys

| key | support |
|---|---|
| `xpos`, `ypos`, `wide`, `tall` | resolved (see 2.3) |
| `zpos` | read and kept; panels are drawn in document order, so a HUD that
  relies on zpos for overlap draws in the wrong order. Reported, not guessed |
| `visible` | `0`/`1`/`true`/`false`; an invisible panel is **not drawn and owns no pixels**, and a hidden panel's children are hidden with it (visibility is inherited, like Source's). The geometry is still resolved, so a hidden panel is a legal parent for a visible descendant once it is shown |
| `enabled` | read and reported; this build has no input handling and no VGUI runtime, so a disabled panel is still drawn. Reading it and pretending otherwise would be the kind of half-true support this document exists to avoid |
| `controlname`, `fieldname` | read, recorded, never dispatched: this build has no VGUI runtime |
| `labelText` | read and reported; **not rendered** — this build rasterises no font, so the panel is a bordered block. `%Token%` interpolation is not implemented either |
| `font`, `textAlignment`, `image` | read and reported per panel; **not rendered** (see 2.4) |
| every other key | counted in `unknownKeys`; a document's unknown-key count is reported so "we ignored 40 keys" is a reading |

### 2.3 Coordinates

| form | example | meaning | scaled by `hud.scale`/dpi? |
|---|---|---|---|
| pixels | `0`, `-175`, `+12` | offset from the parent's left/top | yes |
| centred | `c95`, `c-175` | `(parent - self)/2 + v` | yes, on `v` only |
| right/bottom | `r10`, `r-4` | `parent - self - v` | yes, on `v` only |
| fraction | `f0`, `f0.5` | `parent * v` | **no**: it is already relative |
| percent | `50%`, `-25%` | `parent * v / 100` | **no** |
| `o` parent-offset | `o3` | **refused** `unsupported-coordinate`: needs the live tree |
| `p` proportional | `p0.5` | **refused** `unsupported-coordinate`: needs Source's design resolution (18 in the corpus) |
| `s` scaled | `s0.5` | **refused** `unsupported-coordinate`: needs Source's base resolution |

The scale-only-pixel-anchors rule is the reason a 200% HUD grows in place
instead of walking off the screen: a panel anchored with `f0.5` stays at half
the screen, and a panel anchored at `-175` becomes `-350`.

### 2.4 Explicitly NOT supported

| feature | why not | what the user sees |
|---|---|---|
| `#base` includes | 23 of the 102 corpus documents use one; resolving it means a second document load with its own path-safety question, and a partially-included HUD renders with holes | `unsupported-include`, with the count of directives |
| fonts and font metrics | needs a rasteriser and Source's scheme lookup | `"font"` read and reported; the panel draws as a block |
| images and crosshairs | needs the VMT/VTF chain plus VGUI scaling | `"image"` read and reported; the panel draws as a block |
| `labelText` token expansion | `%Health%` is game state this build would have to fabricate | the raw text is drawn in the preview |
| animations, `pincorner`, `pin_to_sibling` | a real VGUI layout engine | counted as unknown keys |
| `.nut` scripts | a Squirrel VM | extensions are refused before the file is read as a layout |
| `.vpk` HUD archives | the HUD is imported as loose files under a user-chosen folder | a `.vpk` is refused as `unsupported-extension` |

## 3. Safety rules

### 3.1 Path rules (every one is a named refusal)

The import never trusts a path from the settings file or from the document.
`sanitizeHudRelativePath()` applies the textual rules and `resolveInsideRoot()`
applies the filesystem rule on top:

| input | refusal |
|---|---|
| `""` | `empty-path` |
| `C:/x`, `/x`, `\\server\share`, `\\?\C:\x` | `absolute-path` |
| `C:x` (drive-relative) | `drive-relative` |
| any component `..` or `.` | `parent-traversal` |
| `name:stream` (NTFS ADS) | `alternate-stream` |
| component ending in `.` or space | `trailing-dot-or-space` |
| any byte < 0x20 or 0x7f | `control-character` |
| `CON`, `NUL`, `COM1`..`COM9`, `LPT1`..`LPT9` (stem before the first dot) | `reserved-device-name` |
| over 240 characters | `path-too-long` |
| a resolved path outside the imported root | `not-inside-root` |

`not-inside-root` is checked component-wise on canonical paths, so a
reparse point, a `..` that survived, or a root of `C:/hud` with a candidate of
`C:/hud2/x` are all caught rather than sharing a string prefix.

### 3.2 Content rules

| rule | refusal |
|---|---|
| not a regular file (directory, device, reparse target that is not a file) | `not-a-regular-file` |
| over 256 KiB, checked on the reported size **and** on the bytes actually read | `too-large` |
| UTF-16 BOM, or any byte sequence that is not valid UTF-8 | `invalid-utf8` |
| a NUL byte inside the text | `invalid-utf8` |
| a UTF-8 BOM | accepted and stripped; reported as `hadBom` |

The 256 KiB cap is 5.4x the largest `.res` in the corpus (48 225 bytes,
`mainmenuoverride.res`), so it constrains nothing real and bounds what a
hand-made file can make the parser do.

### 3.3 The rule that keeps user files safe

**The import never writes to, moves, or deletes anything under the imported
root.** Applying a HUD writes five keys into `ui.ini` and nothing else;
resetting clears those five keys. The imported directory is read-only to this
feature, which is why "reset" and "uninstall" cannot delete a user's HUD. The
gate asserts this with a fixture directory of user files whose bytes are
compared before and after both operations.

## 4. Where the settings live

Per the shared T50/T51 contract (`native/docs/camera-ui-contract.md` 1.3), the
`[hud]` section of `%LOCALAPPDATA%\TF2 Demo Player\ui.ini`:

| key | type | default | meaning |
|---|---|---|---|
| `hud.enabled` | int 0/1 | 0 | draw the HUD at all |
| `hud.root` | string | empty | absolute directory the user imported |
| `hud.layout` | string | empty | the document, relative to `hud.root` |
| `hud.scale` | int 50..200 | 100 | the user's HUD scale |
| `hud.preview` | int 0/1 | 1 | show the preview panel when the HUD is enabled |

Nothing outside `hud.*` is written by this feature.

## 5. What the gate proves, and what it does not

`hud-check.sh` asserts, in this order:

1. the probe's reading set (path rules, coordinate grammar, resolution
   arithmetic, every refusal by name, and the installed corpus);
2. a real product run imports a known-good fixture HUD and draws it: the
   renderer's drawn count equals the document's **visible** count (not its
   resolved count — the fixture hides one panel on purpose so the two numbers
   differ), and the renderer's own drawn region equals the resolver's union
   rectangle *for the same frame*, which is what makes that rectangle usable as
   an alibi boundary below;
3. the three `hud.*` hotkeys are present in the shared key table the PRODUCT
   built at startup, with their canonical chords — read from a run's trace, not
   from two source files compared with each other;
4. **the measurement is deterministic before it is used**: two HUD-off captures
   must be byte-identical before any pixel is compared. If they are not, the
   comparison below is reported NOT VERIFIED with an owner and a next step,
   because a difference between two frames that are not reproducible cannot be
   attributed to the HUD;
5. **HUD on vs HUD off, same camera and same tick, are byte-identical outside
   the HUD union rectangle** — the task's "a HUD change must not modify the
   world/model rendering underneath" — together with the mirror assertion that
   the HUD did change pixels inside the rectangle, so the first one cannot pass
   vacuously;
6. the same comparison at 200% scale, against that scale's own union rectangle;
7. six refusals executed in the product (malformed, `#base` include,
   unsupported coordinate, `..` traversal, missing file, oversized file), each
   with its own token and each still rendering a world frame; plus a
   partially-unsupported document as the CONTROL, which must import, keep five
   panels and count exactly one refused — otherwise "refused" could be a
   validator that refuses everything;
8. import-then-reset, and uninstall, leave the fixture user directory identical:
   the directory-entry set, the directory set (including an empty directory) and
   the bytes of every file;
9. `--mutation` reintroduces one defect at a time and requires THIS gate to go
   red, then restores each source byte-for-byte (`cmp`) and re-verifies with a
   rebuilt product: (a) the `#base` refusal disabled, so a bad HUD is accepted;
   (b) the HUD pass drawing full-screen quads, so the HUD perturbs the world;
   (c) `hud.toggle` never registered, so the key table is incomplete;
   (d) `resetToDefault()` deleting the imported directory, so reset destroys
   user files. A mutation is only scored against a green forward run: the suite
   refuses to start when the forward assertions are already red.

What it does **not** prove, and must not be read as proving: that any
particular third-party HUD looks like it does in Team Fortress 2. It proves
that the panels this build understands land where its own documented rules put
them, and that everything else is refused by name.

