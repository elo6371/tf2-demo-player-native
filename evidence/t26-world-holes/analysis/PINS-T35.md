# T35: what moved in the T26 pin set, and the one assertion that stays red
#
# Every pin this file describes was measured on 9b4e734, same camera (tick
# 2000), same switches, with the binary that tree built. The run is
# .scratch/t35/forward/geometry-hole.before.log; the analysis it produced is
# committed beside this note in evidence/t26-world-holes/analysis/.
#
# SPLIT BY ORIGIN -- this is the evidence that T29 changed the projection and
# not a geometry rule:
#
#   frame-derived pins        moved
#   missing                     38328 -> 1648
#   sky-backed missing          38328 -> 1648
#   unexplained                     0 -> 0       (unchanged)
#   enclosed                     2313 -> 1648
#   open                        36015 -> 0
#   components                     44 -> 34
#   back-face rejections       167030 -> 80917
#   textured frame sha       5e24802a..-> ac2cfe9b..
#   ref frame sha            f87f396a..-> 6bb4d9a7..
#
#   BSP-derived pins          unchanged, re-derived from this tree
#   sky faces                     731 -> 731
#   sky triangles                1842 -> 1842
#   buffer triangles            99312 -> 99312
#   enclosed components >= 200px    3 -> 3
#   face account      18052/17321/731 -> 18052/17321/731
#
# A projection change moves the first group and cannot move the second. That
# the second group reads the pinned values exactly is the control.
#
# ---------------------------------------------------------------------------
# THE ASSERTION THAT STAYS RED: seamCulled
# ---------------------------------------------------------------------------
# HOLE seamCulled read 2 before and reads 0 now, so
#   if [ "${SEAM_CULLED:-0}" -ge 1 ]
# fails and the gate prints "no sliver had a culled neighbour:
# world-ids-nocull.bmp is not being read".
#
# That message is the assertion's own guess at the cause and on this tree it is
# WRONG: the cull-off frame IS being read. Verified independently of the gate
# (.scratch/t35/seam-check.py, using the account tool's own read_bmp and
# match_mask, so it shares the gate's decode rather than re-implementing it):
#
#   - all 32 directions around the 8 representative points of the enclosed
#     components resolve from the CULL-ON frame (world-ids.bmp);
#   - the faces named are 8555, 15324, 17217, 17260, 17262, 17266, 17268,
#     17326 -- the seam account is working;
#   - 81,218 pixels ARE drawn only with the cull off, so the mechanism the
#     assertion tests for is alive in this frame; it simply does not fall on
#     any of the slivers the account now reports.
#
# So the reading is real: after the near-plane fix, none of the surviving
# slivers is bracketed by a surface the product culls. The assertion is left
# RED rather than relaxed to ">= 0", because relaxing it would turn a measured
# change into a pass without measuring anything -- exactly what the pin set
# exists to prevent. Closing it is the report's NOT VERIFIED item 5.
