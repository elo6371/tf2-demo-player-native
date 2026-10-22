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
and 4240 coordinate strings whose first character is either a digit/sign
(4200), `c` (168), `f` (133), `r` (88), `p` (18) or `o` (3). No document in the
corpus uses a `%` suffix or an `s` (scaled) coordinate, but both are legal
VGUI and both are handled below.

**What the 102 documents actually do through the parser** (`hud_probe` reads the
whole directory, not a sample; the readings are in `evidence/hud/probe.txt`):

| outcome | count | why |
|---|---|---|
| imported as a layout | **71** | no `#base`, every panel's coordinates resolvable |
| refused, `unsupported-include` | **23** | they open with `#base` and would render with holes |
| refused, `unsupported-coordinate` | **7** | `hudalert`, `huddemomancharge`, `hudhealthaccount`, `huditemeffectmeter_action`, `huditemeffectmeter_killstreak`, `hudkillstreaknotice`, `hudteamswitch` — `o`-prefixed coordinates |
| refused, `missing-root-panel` | **1** | `huddamageaccount_last.res` is an include *fragment*: `"Resource/UI/HudDamageAccount.res" { }`, an empty document that only exists to be `#base`d by `huddamageaccount.res`. Refusing it is correct. |

So the honest headline is **71 of 102 (70%)** of an installed custom HUD's
documents, and **that is the support scope, not "Source HUD compatible"**. The
eight refusals that are not includes are all one defect each — an `o`
coordinate, or a fragment — and every one of them is named rather than
silently skipped.

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
| `visible`, `enabled` | `0`/`1`/`true`/`false`; an invisible panel is not drawn and owns no pixels |
| `controlname`, `fieldname` | read, recorded, never dispatched: this build has no VGUI runtime |
| `labelText` | read and shown in the preview as a marker; `%Token%` interpolation is not implemented |
| `font`, `textAlignment`, `image` | read, counted in `unknownKeys`-style readings, **not rendered** |
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

The 256 KiB cap is 20x the largest `.res` in the corpus (46 KiB,
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

`hud-check.sh` asserts:

1. the probe's reading set (paths, coordinates, resolution, refusals, corpus);
2. a real product run imports a known-good fixture HUD and draws it, with the
   drawn rectangles matching the probe's resolved rectangles;
3. a malformed fixture and a missing fixture are both refused with their named
   reason, and the product keeps rendering the world;
4. **two captures at the same camera and tick, one with the HUD and one
   without, are byte-identical outside the HUD union rectangle** — the
   assertion the task calls "HUD changes must not modify the world/model
   rendering underneath";
5. reset and uninstall leave every fixture user file byte-identical;
6. `--mutation` reintroduces two defects (a bad HUD accepted; the HUD pass
   perturbing the world) and requires the gate to go red, then restores the
   sources byte-for-byte (`cmp`) and re-verifies.

What it does **not** prove, and must not be read as proving: that any
particular third-party HUD looks like it does in Team Fortress 2. It proves
that the panels this build understands land where its own documented rules put
them, and that everything else is refused by name.

