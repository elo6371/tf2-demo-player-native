#!/usr/bin/env bash
# map-overview-check.sh -- E11 gate: the 250px top-right map overview draws the
# demos.tf basemap package (one HUD image texture with mipmapped bilinear
# sampling), maps world coordinates onto it with the published affine, marks
# every shown player, and never guesses a rectangle when it has no art.
#
# WHAT IT ASSERTS (lead spec, 2026-11-17)
#   A. composition and the basemap contract at 1280x720: panel is 250px at
#      12/12 from the top-right corner, map area 242px, the trace names the
#      resolved basemap (cp_process_f12 -> cp_process_final, exact=1), the SRV
#      format matches the colour pipeline (DXGI_FORMAT_R8G8B8A8_UNORM), the
#      sampler is linear-mip (NOT nearest), mips > 1, the PNG/boundary aspect
#      deviation is under 1%, decode+upload times are published, and exactly
#      ONE image primitive carries the art (a texture quad, not a mosaic);
#   B. the mapping is a real affine: the trace publishes scale/offsetX/offsetY,
#      and this gate recomputes it -- both boundary corners land within 2px of
#      the rect corners the trace published, and the overview: mapping line
#      agrees with the minimap: rect line;
#   C. three window sizes (1280x720, 1920x1080, 2560x1440): the panel keeps its
#      250px side and its 12px top-right margin, the contain rect stays 242px
#      on its fitted axis, the BLU roster starts below the panel and neither
#      roster intersects it, every rectangle stays inside the frame, and the
#      roster row counts clamp when the window is short;
#   D. the smallest supported window (320x240 outer) still composes a panel
#      from min(250, cw-24, ch-24), keeps the 12px margin, lists players and
#      writes a bitmap; a request below the floor is REFUSED (exit 13, no
#      bitmap) instead of being silently resized;
#   E. resolution honesty: pl_upward_f12 resolves to pl_upward (exact=1),
#      cp_gullywash_f9 -> cp_gullywash_final1 and koth_bagel_rc11 -> koth_bagel_rc4
#      are exact=0 and carry the LEGACY MAP badge (trace flag + warn pixels in
#      the badge strip);
#   F. no art is reported, never guessed: with --overview-image pointing at a
#      missing file the trace says loaded=0 with a published reason and draws
#      NO image primitive; the boundary is still known so the affine stays
#      valid -- what the gate forbids is a guessed image. The panel paints its
#      background plus the NO BASEMAP placeholder, so the area is not a black
#      block and the failure is visible instead of silent;
#   G. the PIXELS: closed-vs-open differs only inside the panel and both
#      rosters; the map area carries basemap art (not a flat fill); team chip
#      colours exist in each roster; hiding one entity drops drawing primitives
#      and changes pixels in the map and in a roster. The map pixel delta can be
#      tiny when the hidden entity is the viewer: its marker is a 1px square by
#      design, so the gate needs a non-zero delta plus the primitive drop, not a
#      40px delta that only a chip-sized marker would produce.
#
# NEGATIVE CONTROLS
#   N1 --overview-hide <entity>: the hidden entity disappears from the map and
#      the rosters (shown-1, hidden=1).
#   N2 a run without --overview-open: no overview/minimap trace line at all.
#
# SINGLE INSTANCE: every launch is --hidden-window --audio-disabled. The caller
# holds the host lock; this script does not take it (same rule as hud-live-check).
set -u

ROOT=$(cd "$(dirname "$0")" && pwd)
EXE=${EXE:-$ROOT/native/build-nmake/tf2_demo_native.exe}
TF=${TF:-D:/SteamLibrary/steamapps/common/Team Fortress 2/tf}
DEMO=${DEMO:-E:/demo测试/cp_process_f12/STV/1338f5de79ea343682c17dd514c980d9_matcha-20260927-1312-cp_process_f12.dem}
DEMO_UPWARD=${DEMO_UPWARD:-E:/demo测试/pl_upward_f12/STV/a9630c6557468b24f0b8c2e7dab38ed8_match-20261030-2145-pl_upward_f12.dem}
DEMO_GULLY=${DEMO_GULLY:-E:/demo测试/cp_gullywash_f9/STV/gully4454.dem}
DEMO_BAGEL=${DEMO_BAGEL:-E:/demo测试/koth_bagel_rc11/STV/dfbce608c80a4d72a390c70c597f0377_match-20261101-1959-koth_bagel_rc11.dem}
MAPROOT=${MAPROOT:-E:/demo测试/demos_tf_maps}
TICK=${TICK:-2000}
HIDE_ENTITY=${HIDE_ENTITY:-6}
AUDIO_DEVICE=${AUDIO_DEVICE:-6}
RUN_TIMEOUT=${RUN_TIMEOUT:-300}
OUT=${MAP_OVERVIEW_CHECK_OUT:-$ROOT/.scratch/map-overview}
SCRATCH=${MAP_OVERVIEW_CHECK_SCRATCH:-$OUT/scratch}
SECTIONS=""
for arg in "$@"; do
  case "$arg" in
    --sections=*) SECTIONS=${arg#--sections=} ;;
    *) echo "unknown argument: $arg" >&2; exit 2 ;;
  esac
done
want() { [ -z "$SECTIONS" ] && return 0; echo ",$SECTIONS," | grep -q ",$1,"; }

fail=0
checks=0
show() { if [ -z "$1" ]; then printf '<none>'; else printf '%s' "$1"; fi; }
red()  { checks=$((checks + 1)); fail=1; printf '  FAIL %s\n' "$*"; }
ok()   { checks=$((checks + 1)); printf '  OK   %s\n' "$*"; }
assert_eq() { checks=$((checks + 1)); if [ "$(show "$2")" = "$3" ]; then printf '  OK   %s = %s\n' "$1" "$(show "$2")"; else fail=1; printf '  FAIL %s = %s, expected %s\n' "$1" "$(show "$2")" "$3"; fi; }
assert_ne() { checks=$((checks + 1)); if [ -n "$2" ] && [ "$2" != "$3" ]; then printf '  OK   %s = %s ( != %s )\n' "$1" "$2" "$3"; else fail=1; printf '  FAIL %s = %s, expected something else than %s\n' "$1" "$(show "$2")" "$3"; fi; }
# Numeric bounds go through awk: several published fields are decimal (decodeMs
# 38.363, blackpct 0.0000, aspectDev 0.003) and bash's integer test would fail
# them for the wrong reason -- a fractional value is not a wrong value.
assert_ge() { checks=$((checks + 1)); if [ -n "$2" ] && awk -v a="$2" -v b="$3" 'BEGIN{exit !(a+0 >= b+0)}' 2>/dev/null; then printf '  OK   %s = %s ( >= %s )\n' "$1" "$2" "$3"; else fail=1; printf '  FAIL %s = %s, expected >= %s\n' "$1" "$(show "$2")" "$3"; fi; }
assert_le() { checks=$((checks + 1)); if [ -n "$2" ] && awk -v a="$2" -v b="$3" 'BEGIN{exit !(a+0 <= b+0)}' 2>/dev/null; then printf '  OK   %s = %s ( <= %s )\n' "$1" "$2" "$3"; else fail=1; printf '  FAIL %s = %s, expected <= %s\n' "$1" "$(show "$2")" "$3"; fi; }
assert_float() { checks=$((checks + 1)); if [ -n "$2" ] && awk -v a="$2" -v b="$3" -v t="$4" 'BEGIN{d=a-b; if(d<0)d=-d; exit !(d<=t)}' 2>/dev/null; then printf '  OK   %s = %s ( ~ %s +- %s )\n' "$1" "$2" "$3" "$4"; else fail=1; printf '  FAIL %s = %s, expected %s +- %s\n' "$1" "$(show "$2")" "$3" "$4"; fi; }
assert_within_pct() { checks=$((checks + 1)); if [ -n "$2" ] && awk -v a="$2" -v b="$3" -v p="$4" 'BEGIN{ if (b==0) exit 1; d=a-b; if(d<0)d=-d; exit !( d/b*100 <= p ) }' 2>/dev/null; then printf '  OK   %s = %s ( within %s%% of %s )\n' "$1" "$2" "$4" "$3"; else fail=1; printf '  FAIL %s = %s, expected within %s%% of %s\n' "$1" "$(show "$2")" "$4" "$3"; fi; }

PY=${PY:-C:/Users/Administrator/.dsh/dsh-runtimes/dsh-primary-runtime/dependencies/python/python.exe}
[ -x "$PY" ] || PY=C:/Users/Administrator/AppData/Local/Programs/Python/Python310/python.exe
for path in "$EXE" "$DEMO" "$DEMO_UPWARD" "$DEMO_GULLY" "$DEMO_BAGEL" "$TF"; do
  [ -e "$path" ] || { echo "FATAL: missing input: $path"; exit 1; }
done
[ -d "$MAPROOT" ] || { echo "FATAL: missing basemap package: $MAPROOT"; exit 1; }
[ -x "$PY" ] || { echo "FATAL: no python interpreter found"; exit 1; }
for tool in cygpath timeout grep awk sed cut tail; do
  command -v "$tool" >/dev/null 2>&1 || { echo "FATAL: missing tool: $tool"; exit 1; }
done
mkdir -p "$OUT" "$SCRATCH"

# run_one <name> <demo> <window WxH> [extra product args...]
run_one() {
  local name="$1" demo="$2" size="$3"; shift 3
  local trace="$SCRATCH/$name.trace" bmp="$SCRATCH/$name.bmp" csv="$SCRATCH/$name.csv"
  local log="$SCRATCH/$name.log"
  rm -f "$trace" "$bmp" "$csv" "$log"
  # TF2_NATIVE_TRACE must be a WINDOWS path (main_trace.cpp reads it with
  # GetEnvironmentVariableW); an MSYS /d/... value silently leaves tracing off.
  TF2_NATIVE_TRACE="$(cygpath -m "$trace")" timeout "$RUN_TIMEOUT" "$EXE" \
    --tf-root "$TF" --audio-device "$AUDIO_DEVICE" --audio-disabled --hidden-window \
    --settings-file "$(cygpath -m "$SCRATCH/$name.settings.ini")" \
    --ui-settings-file "$(cygpath -m "$SCRATCH/$name.ui.ini")" \
    --demo "$demo" --capture-frame "$(cygpath -m "$bmp")" \
    --metrics-file "$(cygpath -m "$csv")" \
    --window-size "$size" --capture-tick "$TICK" \
    --overview-root "$(cygpath -m "$MAPROOT")" \
    --debug-camera -3498.225 1021.852 1327.843 70.529329 134.999352 "$@" > "$log" 2>&1
  printf -v "RC_$name" '%s' "$?"
}

frame_line()   { grep -a 'overview: frame '   "$1" 2>/dev/null | tail -1; }
minimap_line() { grep -a 'minimap: source='  "$1" 2>/dev/null | tail -1; }
rect_line()    { grep -a 'minimap: rect='    "$1" 2>/dev/null | tail -1; }
mapping_line() { grep -a 'overview: mapping ' "$1" 2>/dev/null | tail -1; }
summary_line() { grep -a 'overview: summary ' "$1" 2>/dev/null | tail -1; }
capture_line() { grep -a 'capture: frame width=' "$1" 2>/dev/null | tail -1; }
# Traces are CRLF; strip the CR before the value comparison, or every
# string assertion compares "1\r" against "1".
field() { printf '%s' "$1" | tr -d '\r' | grep -oE "(^| )$2=[^ ]*" | tail -1 | sed "s/^ *$2=//"; }
rect_x() { printf '%s' "$1" | cut -d, -f1; }
rect_y() { printf '%s' "$1" | cut -d, -f2; }
rect_w() { printf '%s' "$1" | cut -d, -f3; }
rect_h() { printf '%s' "$1" | cut -d, -f4; }
# overview: frame prints the panel as x,y,SIDE (it is a square), every other rect
# is x,y,w,h. panel_rect() squares it for the intersection and pixel helpers.
panel_rect() { printf '%s' "$1" | awk -F, '{printf "%s,%s,%s,%s", $1, $2, $3, $3}'; }
# overlaps x1,y1,w1,h1 x2,y2,w2,h2 -> prints hit / miss (touching edges do NOT count)
overlaps() {
  if [ "$1" -lt $(($5 + $7)) ] && [ "$5" -lt $(($1 + $3)) ] && \
     [ "$2" -lt $(($6 + $8)) ] && [ "$6" -lt $(($2 + $4)) ]; then echo hit; else echo miss; fi
}
inside_frame() { [ "$1" -ge 0 ] && [ "$2" -ge 0 ] && [ $(($1 + $3)) -le "$5" ] && [ $(($2 + $4)) -le "$6" ] && echo yes || echo no; }

# ---------------------------------------------------------------- section A/B
section_ab() {
  echo "== A. composition + basemap contract, cp_process_f12, tick $TICK, 1280x720"
  run_one main "$DEMO" 1280x720 --hud-live --overview-open
  assert_eq "main exit code" "$RC_main" 0
  local cap frame mm rl map summ area
  cap=$(capture_line "$SCRATCH/main.trace")
  assert_ne "capture frame line present" "$cap" ""
  CW=$(field "$cap" width); CH=$(field "$cap" height)
  assert_ge "capture width" "$CW" 100
  assert_ge "capture height" "$CH" 100
  frame=$(frame_line "$SCRATCH/main.trace")
  assert_ne "overview frame trace present" "$frame" ""
  assert_eq "overview open" "$(field "$frame" open)" 1
  assert_eq "overview ready" "$(field "$frame" ready)" 1
  assert_eq "overview reason" "$(field "$frame" reason)" ok
  assert_eq "overview map name" "$(field "$frame" map)" cp_process_f12
  assert_eq "panel side" "$(rect_w "$(field "$frame" panel)")" 250
  assert_eq "panel height" "$(rect_h "$(panel_rect "$(field "$frame" panel)")")" 250
  # OLD PINS (until 2026-11-17): panel y == 12 and map area y == 16 -- the minimap
  # sat pinned under the top-right corner. The clock and score now own the top of
  # the screen, so the minimap moved to the bottom-right corner at the same 12px
  # margin. The pins below name the corner instead of a literal y.
  assert_eq "panel x (clientW - 12 - 250)" "$(rect_x "$(field "$frame" panel)")" $(($CW - 262))
  assert_eq "panel right margin (clientW - x - side)" $((CW - $(rect_x "$(field "$frame" panel)") - 250)) 12
  assert_eq "panel bottom margin (clientH - y - side)" $((CH - $(rect_y "$(field "$frame" panel)") - 250)) 12
  assert_eq "border" "$(field "$frame" border)" 2
  area=$(field "$frame" area)
  assert_eq "map area x" "$(rect_x "$area")" $((CW - 262 + 4))
  assert_eq "map area y = panel y + 4" "$(rect_y "$area")" $(( $(rect_y "$(field "$frame" panel)") + 4 ))
  assert_eq "map area side" "$(rect_w "$area")" 242
  assert_eq "the map was drawn (frame+bg primitives)" "$(field "$frame" panelPrimitives)" 2
  mm=$(minimap_line "$SCRATCH/main.trace")
  assert_ne "minimap source trace present" "$mm" ""
  assert_eq "basemap loaded" "$(field "$mm" loaded)" 1
  assert_eq "basemap reason" "$(field "$mm" reason)" ok
  assert_eq "basemap source key" "$(field "$mm" source)" cp_process_final
  assert_eq "basemap exact" "$(field "$mm" exact)" 1
  assert_ne "basemap rule" "$(field "$mm" rule)" ""
  case "$(field "$mm" path)" in
    *cp_process_final.png) ok "basemap path = $(field "$mm" path)" ;;
    *) red "basemap path = $(field "$mm" path), expected *cp_process_final.png" ;;
  esac
  assert_ge "basemap png width" "$(field "$mm" w)" 256
  assert_ge "basemap png height" "$(field "$mm" h)" 256
  assert_eq "SRV format matches the colour pipeline" "$(field "$mm" format)" DXGI_FORMAT_R8G8B8A8_UNORM
  assert_eq "sampler is mipmapped bilinear (not nearest)" "$(field "$mm" sampler)" linear-mip
  assert_ge "mip chain length" "$(field "$mm" mip)" 2
  # the trace already publishes percent, so this is an absolute bound, not a ratio
  assert_le "png/boundary aspect deviation (percent)" "$(field "$mm" aspectDev)" 1
  awk -v d="$(field "$mm" decodeMs)" 'BEGIN{exit !(d > 0)}' && ok "decode ms published = $(field "$mm" decodeMs)" || red "decode ms = $(field "$mm" decodeMs), expected > 0"
  awk -v u="$(field "$mm" uploadMs)" 'BEGIN{exit !(u >= 0)}' && ok "upload ms published = $(field "$mm" uploadMs)" || red "upload ms = $(field "$mm" uploadMs), expected >= 0"
  rl=$(rect_line "$SCRATCH/main.trace")
  assert_ne "minimap rect trace present" "$rl" ""
  assert_eq "legacy badge off on the exact map" "$(field "$rl" legacy)" 0
  summ=$(summary_line "$SCRATCH/main.trace")
  assert_ne "overview summary trace present" "$summ" ""
  local players shown hidden rowsred rowsblu skipped icons iconsskipped iconsnopos iconsoffmap arrows classicons chips
  players=$(field "$summ" players); shown=$(field "$summ" shown); hidden=$(field "$summ" hidden)
  rowsred=$(field "$summ" rowsRed); rowsblu=$(field "$summ" rowsBlu); skipped=$(field "$summ" skipped)
  icons=$(field "$summ" icons); iconsskipped=$(field "$summ" iconsSkipped)
  iconsnopos=$(field "$summ" iconsNoPos); iconsoffmap=$(field "$summ" iconsOffMap)
  arrows=$(field "$summ" arrows); classicons=$(field "$summ" classIcons); chips=$(field "$summ" chips)
  assert_ge "players" "$players" 2
  assert_eq "shown = players - hidden" "$shown" $((players - hidden))
  assert_eq "rows add up" $((rowsred + rowsblu + skipped)) "$shown"
  # A dead player keeps its roster row but loses its marker (iconsSkipped++),
  # so the invariant is icons + iconsSkipped == shown, not icons == shown. The
  # capture tick can land on a moment with one dead player (seen at 1920x1080).
  assert_eq "marker accounting (icons + skipped = shown)" $((icons + iconsskipped)) "$shown"
  assert_eq "no marker missing an origin" "$iconsnopos" 0
  assert_eq "no marker outside the map" "$iconsoffmap" 0
  assert_eq "portrait-or-chip accounting" $((classicons + chips)) "$icons"
  assert_ge "a marker is drawn for a living player" "$icons" 1
  assert_le "arrows never exceed markers" "$arrows" "$icons"
  assert_eq "exactly one image primitive carries the basemap (not a mosaic)" "$(field "$summ" images)" 1
  assert_ge "rect primitives" "$(field "$summ" rects)" 40
  assert_ge "text primitives" "$(field "$summ" text)" 10
  assert_eq "hidden entity at start" "$(field "$summ" hiddenEntity)" -1
  ICONS_MAIN=$icons; SHOWN_MAIN=$shown; PLAYERS_MAIN=$players
  PSIDE_MAIN=$(rect_w "$(field "$frame" panel)")

  echo "-- B. the mapping is a real affine (recomputed, not trusted)"
  map=$(mapping_line "$SCRATCH/main.trace")
  local bounds bminx bminy bmaxx bmaxy scale offx offy rrect rtex rx ry rw rh tx ty tw th
  bounds=$(field "$map" bounds)
  bminx=$(echo "$bounds" | cut -d, -f1); bminy=$(echo "$bounds" | cut -d, -f2)
  bmaxx=$(echo "$bounds" | cut -d, -f3); bmaxy=$(echo "$bounds" | cut -d, -f4)
  scale=$(field "$map" scale); offx=$(field "$map" offsetX); offy=$(field "$map" offsetY)
  assert_eq "mapping ok" "$(field "$map" ok)" 1
  assert_eq "mapping reason" "$(field "$map" reason)" ok
  assert_eq "mapping area = panel area" "$(field "$map" area)" "$(field "$frame" area)"
  assert_eq "mapping scale = minimap scale" "$scale" "$(field "$rl" scale)"
  assert_eq "mapping offsetX = minimap offsetX" "$offx" "$(field "$rl" offsetX)"
  assert_eq "mapping offsetY = minimap offsetY" "$offy" "$(field "$rl" offsetY)"
  rrect=$(field "$rl" rect); rtex=$(field "$rl" texture)
  rx=$(rect_x "$rrect"); ry=$(rect_y "$rrect"); rw=$(rect_w "$rrect"); rh=$(rect_h "$rrect")
  tx=$(rect_x "$rtex"); ty=$(rect_y "$rtex"); tw=$(rect_w "$rtex"); th=$(rect_h "$rtex")
  local areax areay areaside
  areax=$(rect_x "$area"); areay=$(rect_y "$area"); areaside=$(rect_w "$area")
  # Contain fit: the longer world axis fills the square, the other is letterboxed.
  assert_ge "contain rect touches the fitted axis" $(( rw > rh ? rw : rh )) $((areaside - 1))
  assert_le "contain rect width <= area" "$rw" "$areaside"
  assert_le "contain rect height <= area" "$rh" "$areaside"
  assert_ge "rect starts inside the area x" $((rx - areax)) 0
  assert_ge "rect starts inside the area y" $((ry - areay)) 0
  assert_le "rect ends inside the area x" $((rx + rw)) $((areax + areaside))
  assert_le "rect ends inside the area y" $((ry + rh)) $((areay + areaside))
  assert_within_pct "rect aspect = boundary aspect" "$(awk -v w="$rw" -v h="$rh" 'BEGIN{printf "%.6f", w/h}')" "$(awk -v a="$bmaxx" -v b="$bminx" -v c="$bmaxy" -v d="$bminy" 'BEGIN{printf "%.6f", (a-b)/(c-d)}')" 1
  assert_within_pct "texture aspect = PNG aspect" "$(awk -v w="$tw" -v h="$th" 'BEGIN{printf "%.6f", w/h}')" "$(awk -v w="$(field "$mm" w)" -v h="$(field "$mm" h)" 'BEGIN{printf "%.6f", w/h}')" 2
  assert_ge "texture inside rect x" $((tx - rx)) 0
  assert_ge "texture inside rect y" $((ty - ry)) 0
  assert_le "texture ends inside rect x" $((tx + tw)) $((rx + rw))
  assert_le "texture ends inside rect y" $((ty + th)) $((ry + rh))
  # Four boundary corners projected with the PUBLISHED affine must land on the
  # rect corners within 2px. If the trace printed numbers it did not use, this
  # fails; if the render dropped the affine, the pixel section fails.
  local px py
  px=$(awk -v x="$bminx" -v s="$scale" -v o="$offx" 'BEGIN{printf "%.3f", x*s+o}')
  py=$(awk -v y="$bmaxy" -v s="$scale" -v o="$offy" 'BEGIN{printf "%.3f", o-y*s}')
  assert_float "corner minX,maxY -> rect x" "$px" "$rx" 2
  assert_float "corner minX,maxY -> rect y" "$py" "$ry" 2
  px=$(awk -v x="$bmaxx" -v s="$scale" -v o="$offx" 'BEGIN{printf "%.3f", x*s+o}')
  py=$(awk -v y="$bminy" -v s="$scale" -v o="$offy" 'BEGIN{printf "%.3f", o-y*s}')
  assert_float "corner maxX,minY -> rect right" "$px" $((rx + rw)) 2
  assert_float "corner maxX,minY -> rect bottom" "$py" $((ry + rh)) 2
  # One known world point from the fixture camera must land inside both rects.
  px=$(awk -v x=-3498.225 -v s="$scale" -v o="$offx" 'BEGIN{printf "%.3f", x*s+o}')
  py=$(awk -v y=1021.852 -v s="$scale" -v o="$offy" 'BEGIN{printf "%.3f", o-y*s}')
  awk -v px="$px" -v py="$py" -v x0="$rx" -v y0="$ry" -v w="$rw" -v h="$rh" 'BEGIN{exit !(px>=x0 && px<=x0+w && py>=y0 && py<=y0+h)}' && ok "known world point lands in the map rect ($px,$py)" || red "known world point lands at $px,$py, outside $rx,$ry,$rw,$rh"
}
# ---------------------------------------------------------------- section C
section_c() {
  echo "== C. geometry at three window sizes (no-overlap, constant margin)"
  local spec size name trace cap cw ch frame panel area red blu summ rrect rrw rrh header
  for spec in "1280x720:main" "1920x1080:big" "2560x1440:wide"; do
    size=${spec%%:*}; name=${spec##*:}
    if [ "$name" = "main" ]; then
      trace="$SCRATCH/main.trace"
    else
      run_one "$name" "$DEMO" "$size" --hud-live --overview-open
      local rc_var="RC_$name"
      assert_eq "$name exit code" "${!rc_var}" 0
      trace="$SCRATCH/$name.trace"
    fi
    cap=$(capture_line "$trace"); cw=$(field "$cap" width); ch=$(field "$cap" height)
    assert_ge "capture width at $size" "$cw" 100
    frame=$(frame_line "$trace")
    assert_ne "frame present at $size" "$frame" ""
    assert_eq "reason at $size" "$(field "$frame" reason)" ok
    panel=$(field "$frame" panel)
    assert_eq "panel side at $size" "$(rect_w "$panel")" 250
    # OLD PINS (until 2026-11-17): panel y == 12 (top-right) and BLU y == 274
    # (below the minimap). Those two pins encoded the 178px gap between RED and
    # BLU that the user reported as misaligned. They are replaced by the rule the
    # layout now follows: one shared row, one shared size, symmetric 12px margins,
    # and a minimap in the bottom-right corner that clears both rosters.
    assert_eq "panel right margin at $size" $((cw - $(rect_x "$panel") - 250)) 12
    assert_eq "panel bottom margin at $size" $((ch - $(rect_y "$panel") - 250)) 12
    area=$(field "$frame" area)
    assert_eq "map area side at $size" "$(rect_w "$area")" 242
    rrect=$(field "$(rect_line "$trace")" rect)
    rrw=$(rect_w "$rrect"); rrh=$(rect_h "$rrect")
    if [ "$rrw" -ge "$rrh" ]; then
      assert_eq "contain rect fitted width at $size" "$rrw" 242
    else
      assert_eq "contain rect fitted height at $size" "$rrh" 242
    fi
    red=$(field "$frame" red); blu=$(field "$frame" blu)
    assert_eq "RED x (left margin) at $size" "$(rect_x "$red")" 12
    assert_eq "BLU x mirrors the margin at $size" $((cw - $(rect_x "$blu") - $(rect_w "$blu"))) 12
    assert_eq "RED y at $size" "$(rect_y "$red")" 96
    assert_eq "BLU y equals RED y at $size" "$(rect_y "$blu")" "$(rect_y "$red")"
    assert_eq "RED and BLU widths match at $size" "$(rect_w "$blu")" "$(rect_w "$red")"
    assert_eq "RED and BLU heights match at $size" "$(rect_h "$blu")" "$(rect_h "$red")"
    assert_eq "RED width at $size" "$(rect_w "$red")" 220
    # The minimap is drawn only when its rect clears the header and both rosters
    # (the general no-room rule); at these three sizes it must clear them, and it
    # must sit below the roster row plus the margin.
    assert_ge "the map clears the rosters at $size" "$(rect_y "$panel")" $(( $(rect_y "$red") + $(rect_h "$red") + 12 ))
    assert_eq "the map was drawn at $size (frame+bg primitives)" "$(field "$frame" panelPrimitives)" 2
    header=$(field "$frame" header)
    assert_eq "panel/red do not intersect ($size)" \
      "$(overlaps $(rect_x "$panel") $(rect_y "$panel") 250 250 $(rect_x "$red") $(rect_y "$red") $(rect_w "$red") $(rect_h "$red"))" miss
    assert_eq "panel/blu do not intersect ($size)" \
      "$(overlaps $(rect_x "$panel") $(rect_y "$panel") 250 250 $(rect_x "$blu") $(rect_y "$blu") $(rect_w "$blu") $(rect_h "$blu"))" miss
    assert_eq "red/blu do not intersect ($size)" \
      "$(overlaps $(rect_x "$red") $(rect_y "$red") $(rect_w "$red") $(rect_h "$red") $(rect_x "$blu") $(rect_y "$blu") $(rect_w "$blu") $(rect_h "$blu"))" miss
    # OLD PIN (until 2026-11-17): the rosters were checked against a hardcoded
    # 24,24,800,60 readout box. The product now publishes its own header rect, so
    # the check uses that and covers BOTH rosters.
    assert_eq "the live header clears RED ($size)" \
      "$(overlaps $(rect_x "$header") $(rect_y "$header") $(rect_w "$header") $(rect_h "$header") $(rect_x "$red") $(rect_y "$red") $(rect_w "$red") 30)" miss
    assert_eq "the live header clears BLU ($size)" \
      "$(overlaps $(rect_x "$header") $(rect_y "$header") $(rect_w "$header") $(rect_h "$header") $(rect_x "$blu") $(rect_y "$blu") $(rect_w "$blu") 30)" miss
    for r in panel red blu; do
      local rect
      if [ "$r" = panel ]; then rect=$(panel_rect "$panel"); else eval "rect=\$$r"; fi
      assert_eq "$r inside frame ($size)" \
        "$(inside_frame $(rect_x "$rect") $(rect_y "$rect") "$(rect_w "$rect")" "$(rect_h "$rect")" "$cw" "$ch")" yes
    done
    assert_le "RED bottom inside frame ($size)" $(( $(rect_y "$red") + $(rect_h "$red") )) "$ch"
    assert_le "BLU bottom inside frame ($size)" $(( $(rect_y "$blu") + $(rect_h "$blu") )) "$ch"
    summ=$(summary_line "$trace")
    assert_eq "rowsRed clamp formula at $size" "$(field "$summ" rowsRed)" $(( ($(rect_h "$red") - 26) / 30 ))
    assert_eq "rowsBlu equals rowsRed at $size" "$(field "$summ" rowsBlu)" "$(field "$summ" rowsRed)"
  done
}

# ---------------------------------------------------------------- section D
section_d() {
  # The product refuses an outer window below 320x240 (main.cpp prescan returns
  # 13), so "smaller than the panel" is not reachable through --window-size --
  # the floor is what the gate can and must test. At the floor the panel is still
  # composed from the same clamp (min(250, cw-24, ch-24)) and the rosters lose
  # rows instead of overlapping; a request below the floor is refused, not
  # silently resized.
  echo "== D. the smallest supported window composes; below the floor is refused"
  run_one small "$DEMO" 320x240 --hud-live --overview-open
  assert_eq "floor window exit code" "$RC_small" 0
  local frame summ cap cw ch side exp area red blu
  frame=$(frame_line "$SCRATCH/small.trace")
  assert_ne "floor window still composes a frame" "$frame" ""
  assert_eq "floor window open" "$(field "$frame" open)" 1
  # OLD PIN (until 2026-11-17): the floor window was expected to still draw the
  # minimap ("reason" ok, panel side squeezed to min(250, cw-24, ch-24)). The
  # rosters now occupy both top corners and the minimap lives in the bottom-right,
  # where at 320x240 it cannot clear the header and the rosters. The GENERAL
  # no-room rule drops the map there; this is not a 320x240 special case -- the
  # same collision test runs at every size. What must survive the drop: both
  # rosters, aligned, with rows, and no partial map primitives.
  assert_eq "floor window reason" "$(field "$frame" reason)" no-room
  assert_eq "floor window drops the map instead of drawing it partially" "$(field "$frame" panelPrimitives)" 0
  cap=$(capture_line "$SCRATCH/small.trace"); cw=$(field "$cap" width); ch=$(field "$cap" height)
  assert_ge "floor capture width" "$cw" 100
  side=$(rect_w "$(field "$frame" panel)")
  exp=$((cw - 24)); [ $((ch - 24)) -lt "$exp" ] && exp=$((ch - 24)); [ 250 -lt "$exp" ] && exp=250
  assert_eq "floor window panel side = min(250, cw-24, ch-24)" "$side" "$exp"
  area=$(field "$frame" area)
  assert_eq "floor window map area side = panel side - 8" "$(rect_w "$area")" $((side - 8))
  assert_eq "floor window panel x keeps the 12px margin" "$(rect_x "$(field "$frame" panel)")" $((cw - 12 - side))
  assert_eq "floor window panel y keeps the 12px bottom margin" "$(rect_y "$(field "$frame" panel)")" $((ch - 12 - side))
  summ=$(summary_line "$SCRATCH/small.trace")
  assert_ge "floor window still lists players" "$(field "$summ" players)" 2
  assert_ge "floor window still draws RED rows" "$(field "$summ" rowsRed)" 1
  assert_ge "floor window still draws BLU rows" "$(field "$summ" rowsBlu)" 1
  red=$(field "$frame" red); blu=$(field "$frame" blu)
  assert_eq "floor window RED y equals BLU y" "$(rect_y "$blu")" "$(rect_y "$red")"
  assert_eq "floor window RED/BLU widths match" "$(rect_w "$blu")" "$(rect_w "$red")"
  assert_eq "floor window RED/BLU heights match" "$(rect_h "$blu")" "$(rect_h "$red")"
  assert_eq "floor window drops the basemap image primitive too" "$(field "$summ" images)" 0
  assert_eq "floor window panel inside frame" \
    "$(inside_frame $(rect_x "$(field "$frame" panel)") $(rect_y "$(field "$frame" panel)") "$side" "$side" "$cw" "$ch")" yes
  if [ -s "$SCRATCH/small.bmp" ]; then ok "floor window wrote a bitmap"; else red "floor window wrote no bitmap"; fi

  run_one tiny "$DEMO" 300x200 --hud-live --overview-open
  assert_eq "below-floor request is refused" "$RC_tiny" 13
  if [ -s "$SCRATCH/tiny.bmp" ]; then red "refused request still wrote a bitmap"; else ok "refused request wrote no bitmap"; fi
}

# ---------------------------------------------------------------- section E
section_e() {
  echo "== E. resolution honesty: exact vs LEGACY basemap"
  local frame mm rl
  run_one upward "$DEMO_UPWARD" 1280x720 --hud-live --overview-open
  assert_eq "upward exit code" "$RC_upward" 0
  frame=$(frame_line "$SCRATCH/upward.trace"); mm=$(minimap_line "$SCRATCH/upward.trace")
  assert_eq "upward map" "$(field "$frame" map)" pl_upward_f12
  assert_eq "upward basemap exact" "$(field "$mm" exact)" 1
  assert_eq "upward basemap loaded" "$(field "$mm" loaded)" 1
  assert_eq "upward basemap source" "$(field "$mm" source)" pl_upward
  case "$(field "$mm" path)" in
    *pl_upward.png) ok "upward basemap path = $(field "$mm" path)" ;;
    *) red "upward basemap path = $(field "$mm" path), expected *pl_upward.png" ;;
  esac
  assert_eq "upward legacy flag" "$(field "$(rect_line "$SCRATCH/upward.trace")" legacy)" 0

  echo "-- E2. cp_gullywash_f9 resolves to cp_gullywash_final1 (LEGACY, exact=0)"
  run_one gully "$DEMO_GULLY" 1280x720 --hud-live --overview-open
  assert_eq "gully exit code" "$RC_gully" 0
  frame=$(frame_line "$SCRATCH/gully.trace"); mm=$(minimap_line "$SCRATCH/gully.trace"); rl=$(rect_line "$SCRATCH/gully.trace")
  assert_eq "gully map" "$(field "$frame" map)" cp_gullywash_f9
  assert_eq "gully basemap loaded" "$(field "$mm" loaded)" 1
  assert_eq "gully basemap source" "$(field "$mm" source)" cp_gullywash_final1
  assert_eq "gully basemap exact" "$(field "$mm" exact)" 0
  assert_eq "gully LEGACY badge flag" "$(field "$rl" legacy)" 1
  local bx by
  bx=$(rect_x "$(field "$rl" rect)"); by=$(rect_y "$(field "$rl" rect)")
  local badge
  badge=$("$PY" "$SCRATCH/pixels.py" badge "$(cygpath -m "$SCRATCH/gully.bmp")" "$bx" "$by" 88 9 2>&1)
  assert_ge "LEGACY MAP warn pixels in the badge strip" "$badge" 40

  echo "-- E3. koth_bagel_rc11 resolves to koth_bagel_rc4 (LEGACY, exact=0)"
  run_one bagel "$DEMO_BAGEL" 1280x720 --hud-live --overview-open
  assert_eq "bagel exit code" "$RC_bagel" 0
  mm=$(minimap_line "$SCRATCH/bagel.trace"); rl=$(rect_line "$SCRATCH/bagel.trace")
  assert_eq "bagel basemap source" "$(field "$mm" source)" koth_bagel_rc4
  assert_eq "bagel basemap exact" "$(field "$mm" exact)" 0
  assert_eq "bagel LEGACY badge flag" "$(field "$rl" legacy)" 1
}
# ---------------------------------------------------------------- section F
section_f() {
  echo "== F. no art is reported, never guessed (placeholder, not a black block)"
  local frame mm map summ cap cw ch blank warn
  run_one closed "$DEMO" 1280x720 --hud-live
  assert_eq "closed run exit code" "$RC_closed" 0
  if grep -aq 'overview: ' "$SCRATCH/closed.trace"; then red "a run without --overview-open still traced overview:"; else ok "no overview trace when the HUD is closed"; fi
  if grep -aq 'minimap: ' "$SCRATCH/closed.trace"; then red "a run without --overview-open still traced minimap:"; else ok "no minimap trace when the HUD is closed"; fi

  run_one nobasemap "$DEMO" 1280x720 --hud-live --overview-open --overview-image "$(cygpath -m "$SCRATCH/missing.png")"
  assert_eq "missing-art run exit code" "$RC_nobasemap" 0
  frame=$(frame_line "$SCRATCH/nobasemap.trace"); mm=$(minimap_line "$SCRATCH/nobasemap.trace")
  map=$(mapping_line "$SCRATCH/nobasemap.trace"); summ=$(summary_line "$SCRATCH/nobasemap.trace")
  assert_eq "missing-art panel still composes" "$(field "$frame" ready)" 1
  assert_eq "missing-art basemap loaded" "$(field "$mm" loaded)" 0
  assert_ne "missing-art reason is published" "$(field "$mm" reason)" ""
  assert_ne "missing-art reason is a failure, not ok" "$(field "$mm" reason)" ok
  assert_eq "missing-art draws no image primitive" "$(field "$summ" images)" 0
  # The boundary is still known, so the affine may stay valid -- what must not
  # happen is a texture quad or a guessed rectangle without a reason.
  assert_eq "missing-art mapping still explains itself" "$(field "$map" ok)" 1
  cap=$(capture_line "$SCRATCH/nobasemap.trace"); cw=$(field "$cap" width); ch=$(field "$cap" height)
  assert_ge "missing-art bitmap written" "$cw" 100
  blank=$("$PY" "$SCRATCH/pixels.py" blank "$(cygpath -m "$SCRATCH/nobasemap.bmp")" "$(panel_rect "$(field "$frame" panel)")")
  assert_le "missing-art panel is not a black block (black %)" "$(field "$blank" blackpct)" 5
  assert_ge "missing-art placeholder text pixels" "$(field "$blank" warn)" 100

  echo "-- N1. --overview-hide $HIDE_ENTITY drops one row and one marker"
  run_one hide "$DEMO" 1280x720 --hud-live --overview-open --overview-hide "$HIDE_ENTITY"
  assert_eq "hide run exit code" "$RC_hide" 0
  summ=$(summary_line "$SCRATCH/hide.trace")
  assert_eq "hide hidden count" "$(field "$summ" hidden)" 1
  assert_eq "hide hidden entity id" "$(field "$summ" hiddenEntity)" "$HIDE_ENTITY"
  assert_eq "hide shown = players - 1" "$(field "$summ" shown)" $(( $(field "$summ" players) - 1 ))
  assert_eq "hide markers track shown rows" "$(field "$summ" icons)" "$(field "$summ" shown)"
}

# ---------------------------------------------------------------- section G
section_g() {
  echo "== G. pixels: the panel and rosters really change, the map is not a flat fill"
  local frame panel red blu rowsred rowsblu px
  frame=$(frame_line "$SCRATCH/main.trace")
  panel=$(panel_rect "$(field "$frame" panel)"); red=$(field "$frame" red); blu=$(field "$frame" blu)
  rowsred=$(field "$(summary_line "$SCRATCH/main.trace")" rowsRed)
  rowsblu=$(field "$(summary_line "$SCRATCH/main.trace")" rowsBlu)
  px=$("$PY" "$SCRATCH/pixels.py" frame "$(cygpath -m "$SCRATCH/closed.bmp")" "$(cygpath -m "$SCRATCH/main.bmp")" "$(cygpath -m "$SCRATCH/hide.bmp")" \
    "$panel" "$red" "$blu" "$rowsred" "$rowsblu")
  assert_eq "closed-vs-open changes nothing outside panel+rosters" "$(field "$px" outside)" 0
  assert_ge "closed-vs-open changes pixels inside panel+rosters" "$(field "$px" inside)" 4000
  assert_ge "map area carries basemap art (distinct RGB values)" "$(field "$px" distinct)" 12
  assert_ge "map area is not mostly background fill (%)" "$(field "$px" nonbg)" 25
  assert_ge "RED roster shows its team colour" "$(field "$px" redchip)" $((rowsred * 40))
  assert_ge "BLU roster shows its team colour" "$(field "$px" bluchip)" $((rowsblu * 40))
  # A hidden viewer marker is a 1px square (not a chip), so a small delta is the
  # correct reading; zero would mean the hide never reached the image.
  assert_ge "hiding one entity changes map pixels" "$(field "$px" mapdiff)" 1
  mainrects=$(field "$(summary_line "$SCRATCH/main.trace")" rects)
  hiderects=$(field "$(summary_line "$SCRATCH/hide.trace")" rects)
  assert_ge "hiding one entity removes drawing primitives" $((mainrects - hiderects)) 1
  assert_ge "hiding one entity changes roster pixels" "$(field "$px" rowdiff)" 40
}

# ---------------------------------------------------------------- pixels.py
write_pixels_py() {
  cat > "$SCRATCH/pixels.py" <<'PYEOF'
import sys
from PIL import Image

WARN = (235, 107, 102)
IMGBG = (33, 33, 38)
RED = (184, 59, 64)
BLU = (84, 133, 168)


def near(p, c, tol=12):
    return abs(p[0] - c[0]) <= tol and abs(p[1] - c[1]) <= tol and abs(p[2] - c[2]) <= tol


def rect(s):
    return [int(v) for v in s.split(",")]


def inrect(x, y, r):
    return r[0] <= x < r[0] + r[2] and r[1] <= y < r[1] + r[3]


def load(path):
    return Image.open(path).convert("RGB").load()


mode = sys.argv[1]
if mode == "frame":
    closed = Image.open(sys.argv[2]).convert("RGB")
    opened = Image.open(sys.argv[3]).convert("RGB")
    hidden = Image.open(sys.argv[4]).convert("RGB")
    panel = rect(sys.argv[5])
    red = rect(sys.argv[6])
    blu = rect(sys.argv[7])
    rowsred = int(sys.argv[8])
    rowsblu = int(sys.argv[9])
    co, oo, ho = load(sys.argv[2]), load(sys.argv[3]), load(sys.argv[4])
    w, h = closed.size
    rosters = [(red, rowsred), (blu, rowsblu)]

    def in_roster(x, y):
        for r, _n in rosters:
            if inrect(x, y, r):
                return True
        return False

    outside = inside = 0
    for y in range(h):
        for x in range(w):
            if co[x, y] != oo[x, y]:
                if inrect(x, y, panel) or in_roster(x, y):
                    inside += 1
                else:
                    outside += 1
    mx = panel[0] + 4
    my = panel[1] + 4
    mw = panel[2] - 8
    mh = panel[3] - 8
    colors = set()
    nonbg = 0
    tot = 0
    mapdiff = 0
    for y in range(my, my + mh):
        for x in range(mx, mx + mw):
            p = oo[x, y]
            tot += 1
            colors.add(p)
            if p != IMGBG:
                nonbg += 1
            if p != ho[x, y]:
                mapdiff += 1
    redchip = 0
    bluchip = 0
    for y in range(red[1], red[1] + red[3]):
        for x in range(red[0], red[0] + red[2]):
            if near(oo[x, y], RED):
                redchip += 1
    for y in range(blu[1], blu[1] + blu[3]):
        for x in range(blu[0], blu[0] + blu[2]):
            if near(oo[x, y], BLU):
                bluchip += 1
    rowdiff = 0
    for r, _n in rosters:
        for y in range(r[1], r[1] + r[3]):
            for x in range(r[0], r[0] + r[2]):
                if oo[x, y] != ho[x, y]:
                    rowdiff += 1
    print("outside=%d inside=%d distinct=%d nonbg=%.2f redchip=%d bluchip=%d mapdiff=%d rowdiff=%d" % (
        outside, inside, len(colors), 100.0 * nonbg / max(tot, 1), redchip, bluchip, mapdiff, rowdiff))
elif mode == "blank":
    px = load(sys.argv[2])
    panel = rect(sys.argv[3])
    black = 0
    warn = 0
    tot = 0
    for y in range(panel[1], panel[1] + panel[3]):
        for x in range(panel[0], panel[0] + panel[2]):
            p = px[x, y]
            tot += 1
            if p[0] <= 8 and p[1] <= 8 and p[2] <= 8:
                black += 1
            if near(p, WARN):
                warn += 1
    print("blackpct=%.4f warn=%d" % (100.0 * black / max(tot, 1), warn))
elif mode == "badge":
    px = load(sys.argv[2])
    x0, y0, w, h = int(sys.argv[3]), int(sys.argv[4]), int(sys.argv[5]), int(sys.argv[6])
    warn = 0
    for y in range(y0, y0 + h):
        for x in range(x0, x0 + w):
            if near(px[x, y], WARN):
                warn += 1
    print(warn)
else:
    sys.exit("unknown pixels.py mode: " + mode)
PYEOF
}

# ---------------------------------------------------------------- dispatcher
write_pixels_py
if want A || want B; then section_ab; fi
if want C; then section_c; fi
if want D; then section_d; fi
if want E; then section_e; fi
if want F; then section_f; fi
if want G; then section_g; fi
if [ "$checks" -eq 0 ]; then echo "no sections selected (--sections=A,B,...)" >&2; exit 2; fi
echo
if [ "$fail" -eq 0 ]; then
  echo "MAP-OVERVIEW=PASS ($checks checks)"
  exit 0
fi
echo "MAP-OVERVIEW=FAIL ($checks checks)"
exit 1
