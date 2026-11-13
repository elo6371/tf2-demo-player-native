# T88 -- the two map fixes and the one that is still open

Branch task/t88-maps, commit fc884cd (parent e15577b, integration/t79-final).
This page exists for the release report: what was wrong in the map axis, what the
fix changed, and how a gate catches the same regression again. Every number is a
measurement from this tree with the command next to it; anything without a gate
assertion is marked **未验证**.

## 1. The budget: a number nobody measured, truncating in silence

**Before.** native/src/main.cpp called tf2::native::BspParser::parse(mapBytes,
displayMap, 200000) and native/src/bsp_map.cpp stopped pushing world triangles
the moment the count reached that limit: no counter, no trace line, no name for
the first refused face. 200000 was the parser's default, never compared with the
maps the product draws.

**The measurement.** world_geometry_probe --tf-root D:/SteamLibrary/steamapps/
common/Team Fortress 2/tf --map <bsp> --triangle-limit 4000000 (a limit above
every map, so requested = the demand):

    cp_snakewater_final1  242,361      koth_bagel_rc12  98,066
    cp_sunshine            82,859      koth_bagel_rc11  98,029
    pl_odyssey (reading)  105,486

**Before the whitelist fix** (defect 2 below) snakewater asked for 234,699 and the
old buffer drew 200,000: 34,699 triangles over 3,513 faces were dropped, 14.8 % of
the map, with no reading anywhere. After the whitelist fix the same map asks for
242,361, so the old buffer would have dropped 42,643.

**The fix.** main.cpp:4282-4292 defines constexpr std::size_t
kWorldTriangleCapacity = 400000 -- the measured worst case (242,361) with 65 %
headroom, not a round number picked for looks -- and passes it to the parser.
bsp_map.cpp:676-716 makes the refusal countable: requested counts every triangle
the map asks for (pushTriangle, displacement grids), dropped counts every refusal,
facesDropped / brushFacesDropped / brushModelsDropped count by kind, and the first
refused face and brush model are named. Two trace lines carry it:

    map: geometry budget capacity=400000 requested=242361 dropped=0 facesDropped=0 \
      brushFacesDropped=0 brushModelsDropped=0 firstFace=-1 firstBrush=- state=within
    gpu: world geometry budget capacity=400000 requested=242361 dropped=0 ... state=within

The first is written on the parse path (every run), the second on the drawn frame
(world_geometry_probe.cpp:294-305 prints the same fields for the probe).

**How a gate catches it.** world-geometry-budget-check.sh (new):
B1 both lines exist in a capture trace and agree on requested/dropped;
B2 capacity=400000, requested<=capacity, dropped=0, state=within on all four
authorised maps; B3 the requested/capacity pair is pinned; B5 the probe at 4M
agrees with the product; B6 at 200,000 and 100,000 the requested value does not
move (it is a map property) while dropped >= requested-limit -- an inequality,
because a refused displacement grid still emits a fallback fan and lands in the
same counter (measured: 200,000 gives dropped=42,643 against an arithmetic
shortfall of 42,361). Shrinking the constant back to 200,000 must turn B2 red at
"B2 cp_snakewater_final1: dropped=" (--mutation capacity-shrink).

## 2. func_illusionary: the entity whose purpose is "draw me" was deleted

**Before.** native/src/bsp_map.cpp decided which brush entities are world
geometry with a name list: func_brush, func_wall, func_lod. func_illusionary was
not in it, so its faces were parsed and then discarded -- the one entity class in
Source that exists exactly to be drawn and not collided with. On
cp_snakewater_final1 that is 2,892 faces over 61 entities (7,662 triangles),
invisible in every frame and in every gate. 427 faces on cp_sunshine, 100 on each
bagel.

**The fix.** bsp_map.cpp:330-331 adds func_illusionary to the same whitelist as
func_brush. It is the right list rather than a special case because the brush
model path already clips per leaf and filters per vis cluster; the whitelist only
decides "this brush model's faces are world geometry", which is exactly what the
entity declares.

**How a gate catches it.** B4 pins facesDrawn/facesSkipped per map: removing
func_illusionary from the whitelist must turn it red at
"B4 cp_snakewater_final1: facesDrawn=" (--mutation illusionary-whitelist).
B4b pins worldFaces+brushFaces (faces must not enter or leave the map, only move
between drawn and skipped) and B4c pins the unattributed gap
brushFaces-(drawn+skipped): if a face is neither drawn nor counted, B4c fails
even when the drawn/skipped numbers happen to add up.

**What this does not fix.** Dynamic brush entities -- func_door (10 on snakewater,
10 on sunshine, 4 rotating on bagel), func_tracktrain (2), func_movelinear,
func_breakable (2 on sunshine) -- are still 0-hit in this source: they do not move
with demo time. Adding faces to the world pass does not change that, and the T85
handoff forbids closing the item by renaming it. See GAPS.md 8.2-3.

## 3. World alpha: still open (diagnosed, not fixed here)

**What the code does.** native/src/vmt_material.cpp:266-267 resolves $translucent
into material.translucent and $alphatest into material.alphaTest, and from then on
the world path never reads either one. native/src/native_renderer_world.cpp has
zero occurrences of OMSetBlendState and zero of "translucent" (grep -c on both
strings), and the world pixel shader that starts at native/src/native_renderer_shaders.cpp:110
ends with `return float4(albedo * lighting, 1.0);` at :335 -- every world fragment
is written opaque. The only place $alphatest is consumed at all is
native_renderer_shaders.cpp:801, inside the ViewModel pixel shader that starts at
:756, so a world material with a cut-out alpha mask draws solid as well. Alpha
blending exists for the ViewModel (native_renderer_viewmodel.cpp:701, :843, :875)
and the HUD (native_renderer_frame.cpp:1284), never for the world.

Nothing counts it, nothing warns, and no gate reads it: of the 99 gate scripts,
only hud-text-check.sh and viewmodel-material-check.sh mention "translucent"
(grep -rln translucent *.sh) -- a silent downgrade, the same shape of failure as
defect 1 before this task.

**Status.** **未修, 未验证.** Fixing it is not a constant: it needs sorted
transparent draws (or at least alpha-test fallback) and a decision about what
"correct" means for a frame, which is a rendering task, not a map-parser one. A
gate for it would assert on a reading, not a constant: count the world faces whose
material resolved $translucent and require the blend path to consume them, then a
known translucent surface (e.g. a glass or fence material from the authorised
maps) must change pixels between blend-on and blend-off frames. Neither the
counter nor the capture exists yet; this belongs to a new task, and this commit
does not claim it.

## 4. Re-run

    bash hostlock.sh run t88-budget bash -c 'cd <tree> && HOLD_LOCK=1 \
      bash world-geometry-budget-check.sh --reuse --re-pin="<why>"'
    bash hostlock.sh run t88-budget bash -c 'cd <tree> && HOLD_LOCK=1 \
      bash world-geometry-budget-check.sh --mutation'

Tokens: WORLD-GEOMETRY-BUDGET=PASS / =FAIL (N assertions);
WORLD-GEOMETRY-BUDGET-MUTATION=PASS / =FAIL (N). The deciding assertion strings
are listed in evidence/t88-maps/MAP-BUDGET-FIX.md section 9; the case for the
capacity and the demand readings are in evidence/t88-maps/demand.txt.

**On this branch the forward run and the mutation pair were NOT executed** (the
merge window runs them); the only product evidence committed here is four capture
traces under evidence/t88-maps/trace-*.trace and the two window logs. Do not read
this page as a green gate.
