# T65 — interaction acceptance points (T69 + T66)

Status: DEFINED, NOT RUN. The minimap feature does not exist in the product yet
(grep minimap/radar/overview over native/src + native/include = 0 hits). These are the points the
implementation must pass; each names its source requirement.

| id | requirement (source) | acceptance check | pass criterion | status |
|---|---|---|---|---|
| IA-1 | minimap sits top-right and is collapsed by default (T69) | open the viewer with a demo loaded | panel is collapsed on first run, anchored top-right, does not cover scoreboard/health/crosshair | NOT RUN |
| IA-2 | expanded panel shows the base map with red/blue player dots (T69) | expand the panel | base map fills the panel; every visible player has exactly one dot; team colors red/blue; no blank canvas | NOT RUN |
| IA-3 | hover shows name, team, status (T69) | hover each dot | tooltip shows name + team + status (alive/dead); hovering does not move or re-sort any dot | NOT RUN |
| IA-4 | click selects the player and switches the camera immediately; must not trigger the 0.3 s death auto-relay (T69, T66) | click a dot while a death auto-relay is in progress | camera target changes in the same frame the click is processed; no 0.3 s continuous transition is started; auto-relay state is cancelled | NOT RUN |
| IA-5 | zoom in/out and reset (T69) | use zoom controls | zoom is centered on the cursor; dot size stays constant in screen pixels; reset returns to the default frame | NOT RUN |
| IA-6 | remember last state across sessions (T69) | collapse/expand, zoom, close and reopen | last collapsed state and zoom are restored; a map change does not restore a stale frame | NOT RUN |
| IA-7 | unavailable map shows a reason, never a blank canvas (T69) | open a demo whose map BSP is missing (koth_bagel_rc13 is the concrete case) | panel shows the map name and a reason (BSP not found); no empty grid or fabricated map is drawn | NOT RUN |
| IA-8 | dense/overlapping dots must not cause misclicks (T69) | click in a cluster | hit test picks the nearest dot within its radius; overlapping dots show a count badge; no selection changes on hover | NOT RUN |
| IA-9 | dots at the map edge stay clickable (T69) | move a player to the frame edge | dot is clamped to the edge with an off-map indicator and remains clickable | NOT RUN |
| IA-10 | switching maps does not keep stale dots (T69) | switch between two demos/maps | old dots and old base map are cleared; the new identity line is printed; no stale frame | NOT RUN |
| IA-11 | multi-level positions are not silently ambiguous (T65) | stand two players at the same (x,y) on different floors | the panel shows a layer indicator or count badge; it does not silently draw one dot for two players | NOT RUN |
| IA-12 | scaling/DPI does not break the projection (T65) | render the panel at two sizes (e.g. 200 px and 1024 px) | projection stays linear; hit radius is in screen pixels; the half-pixel error budget scales with panel size | NOT RUN |

T66 background: the automatic relay on death is a ~0.3 s continuous camera transition and continues
while paused; a manual selection sets the target directly and must not play that transition. IA-4 is
the check that separates the two paths.

Verification method once implemented: hidden-window run under the shared host lock, screenshots via the
existing --capture-frame path, pixel assertions for IA-2/IA-7/IA-9/IA-11, and a timestamped click test
for IA-4 (target set in the same frame, no transition state entered).
