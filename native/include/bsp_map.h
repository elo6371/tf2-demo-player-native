#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace tf2::native {

struct BspVertex { float x = 0; float y = 0; float z = 0; };
struct BspTriangle {
  BspVertex a; BspVertex b; BspVertex c;
  float au = 0; float av = 0; float bu = 0; float bv = 0; float cu = 0; float cv = 0;
  float alu = -1; float alv = -1; float blu = -1; float blv = -1; float clu = -1; float clv = -1;
  // T12 (2026-10-18): the per-vertex blend weight, straight from
  // `CDispVert::m_flAlpha` (bspfile.h, 20 bytes per vertex: vector, distance,
  // alpha). WorldVertexTransition -- the shader every terrain material on the
  // reference map uses -- picks between `$basetexture` and `$basetexture2` with
  // exactly this number (`lightmappedgeneric_ps2_3_x.h`: `blendfactor =
  // i.vertexBlendX_fogFactorW.r`), so without it the second layer has nothing
  // to be weighted by and the surface can only be drawn as one of the two.
  //
  // -1 is "no weight", which is what a non-displacement triangle gets and what
  // a displacement vertex gets when its alpha is unreadable. The renderer reads
  // the sentinel as "this vertex has one layer", so a missing weight degrades
  // to the single-layer picture instead of to a fabricated blend.
  float aAlpha = -1.0f;
  float bAlpha = -1.0f;
  float cAlpha = -1.0f;
  // T27 (2026-10-19): the face's texture axes, straight from `texinfo_t\`s
  // textureVecs. They are the basis the uv above was computed in
  // (`uv = dot(p, axis.xyz) + axis.w`), which is exactly the basis a normal map
  // is authored against: the bump map's x axis runs along the texture's u
  // direction on the face. Carrying them means the pixel shader can build a
  // tangent frame from the map's own data instead of inventing one from a world
  // up vector, and it is why the `$bumpmap` path can decode a normal map as a
  // normal map. Not normalised here: the BSP stores texels-per-world-unit, and
  // the renderer normalises once per triangle (they are constant across it).
  float uAxis[3] = {1.0f, 0.0f, 0.0f};
  float vAxis[3] = {0.0f, 1.0f, 0.0f};
  std::string material;
  bool lightmapped = false;
  bool water = false;
  bool displacement = false;
  // T11: which side of its own BSP plane this face was authored on (dface_t
  // `side`), so a diagnostic can ask a triangle "was the side you were seen from
  // the side your own data says you are seen from" without re-parsing. It is
  // also the bit the emission's orientation rule uses.
  std::uint8_t planeSide = 0;
  // The plane the face was authored on, so a diagnostic can rebuild the face's
  // outward normal (plane normal, flipped by planeSide) from the map itself.
  std::int32_t planeIndex = -1;
  // T11 third round: whether emission reversed this triangle. With it a
  // diagnostic can recover the STORED winding (`a, b, c` swapped back) and
  // therefore measure the shipped data instead of measuring the rule that
  // overwrote it -- which is the difference between "the map is inconsistent"
  // and "the rule is".
  std::uint8_t windingFlipped = 0;
  // T23: this triangle is the flat quad of a displacement face whose grid was
  // refused. It is emitted through the brush path (no grid, no blend weights),
  // so it carries the brush orientation rule -- which is exactly why it can be
  // back-face culled where the grid it replaces would have been oriented. The
  // flag lets a diagnostic count those triangles without re-parsing the face.
  bool fallbackQuad = false;
  int cluster = -1;
  // T26: the dface_t this triangle was emitted from. The renderer never reads
  // it; a diagnostic does, because "which face is this pixel" is the question
  // a visible hole has to answer before anybody changes geometry. -1 means the
  // triangle was not emitted from a face (no such path today).
  std::int32_t faceIndex = -1;
};

enum class BspLightmapMode { Unavailable, AverageIntensity, RgbExp32 };
enum class BspVisMode { Unavailable, ClusterPvs };
// T105/task-37: which pair of leaf-ambient lumps the map carries, if any.
// Unavailable is not an edge case: 695094 of the 1629874 leaves in this host's
// 261-map corpus carry no sample at all, and one shipped map carries neither
// lump. A caller that cannot tell Unavailable from a black cube lights the
// model to zero and calls it a reading.
enum class BspLeafAmbientMode { Unavailable, RgbExp32 };

struct BspCubemapSample { int origin[3] = {}; int size = 0; };
struct BspPlane { float normal[3] = {}; float dist = 0; };
struct BspNode { int plane = 0; int child[2] = {}; };
struct BspLeaf {
  int cluster = -1;
  int firstFace = 0;
  int faceCount = 0;
  // T105/task-37: the leaf's bounding box, read from the same 32-byte lump 10
  // record (mins at +8, maxs at +14, three int16 each). It is kept because an
  // ambient sample stores its position as an 0..255 fraction OF THIS BOX: the
  // three bytes alone are not a location, so without the box the sample record
  // decodes to a colour but not to a place.
  short mins[3] = {};
  short maxs[3] = {};
};

// T105/task-37: one leaf's slice of LUMP_LEAF_AMBIENT_LIGHTING (56, HDR 55),
// as the 4-byte dleafambientindex_t record holds it: how many cubes and where
// the first one starts. The index is a PER-LEAF array, so its length is the
// leaf count times four or the lump was not usable.
struct BspLeafAmbientEntry {
  std::uint32_t firstSample = 0;
  std::uint32_t sampleCount = 0;
};

// T105/task-37: one decoded ambient cube. Six directions in the order
// (+X, -X, +Y, -Y, +Z, -Z) as linear RGB. The SDK header that declares the six
// slot array (mathlib/compressed_light_cube.h:17-21) does NOT name the
// directions; the order here was derived from the corpus of 261 shipped maps,
// where the +Z slot is the brightest of the six on 3279485 of 7868947 cubes
// (41.7%) and the -Z slot has the lowest mean luminance (three dimmest slots
// would not separate; -Z separates by 1.9x from +Z). An indoor map lights its
// ceiling more than its floor, so this is strong corroboration -- not a proof.
//
// positionFraction keeps the three bytes as stored (a 0..255 fraction of the
// OWNING leaf's box, per bspfile.h:857-860) rather than converting here: the
// box belongs to the leaf, not to the sample, and a conversion done at parse
// time would need a leaf->box lookup for a record that may be claimed by more
// than one leaf. BspParser::leafAmbientAtLeaf does the conversion with the box
// of the leaf actually being asked about.
struct BspLeafAmbientSample {
  float positionFraction[3] = {};
  float color[6][3] = {};
};

// T105/task-37: the answer to "what is the ambient light here", carrying its
// own provenance so a caller cannot mistake a fallback for a reading. An
// available flag of false means there is no cube for this point and the caller
// must use whatever it used before -- never a zero cube, which reads as "the
// model is in the dark" when the truth is "this map never shipped the lump".
struct BspLeafAmbientCube {
  bool available = false;
  int leafIndex = -1;      // leaf the point resolved to; -1 if the tree could not answer
  int cluster = -1;        // that leaf's cluster, for pairing with clusterAt()
  int sampleCount = 0;     // cubes the leaf carries (0 => nothing to read)
  int sampleIndex = -1;    // which of them was chosen; -1 if none
  float position[3] = {};  // the chosen sample's point, map units
  float color[6][3] = {};  // +X -X +Y -Y +Z -Z, INFERRED (see above), linear RGB
};
struct BspStaticProp {
  std::string modelPath;
  float origin[3] = {};
  float angles[3] = {};
  std::int32_t skin = 0;
  std::uint16_t firstLeaf = 0;
  std::uint16_t leafCount = 0;
  // The game-lump stores leaf indices, not cluster ids. Keep the indices so
  // the renderer can apply the map's actual cluster PVS without duplicating
  // the BSP leaf table in every prop.
  std::vector<std::uint16_t> leafIndices;
  std::uint8_t solid = 0;
  std::uint8_t flags = 0;
};

// D-14 (2026-11-09): a brush model this parser refuses to draw, named. The
// counters in BspMap say how many; a count cannot say WHICH, and 84 refusals
// read the same whether they are 84 doors or 84 trains. One row per refused
// model carries the entity's position in lump 0, its classname, the `*N` dmodel
// index and that model's face range (lump 14 firstface/face count), so a hole
// in the frame can be traced back to a brush in the map.
struct BspRefusedBrushModel {
  int entityIndex = -1;          // 0-based position in lump 0, -1 if unnamed
  std::string className;         // empty when the entity carried no classname
  std::int32_t modelIndex = 0;   // the N in the entity's "model" key (*N)
  std::int32_t firstFace = 0;    // dmodel firstface (lump 14)
  std::int32_t faceCount = 0;    // dmodel face count (lump 14)
  bool moving = false;           // true: skippedMoving; false: unknown classname
};

// DB-2 (2026-11-26, E/map-materials): a brush model this parser keeps OUT of
// the static world buffer because its entity can move it -- a door, a train, a
// rotating brush, a breakable, a respawn visualizer. The engine draws such a
// model with the ENTITY pose, not the compile-time pose: [S]
// game/client/c_baseentity.cpp:1963 draws the brush model with
// GetAbsOrigin()/GetAbsAngles(), and :1427 makes EF_NODRAW the visibility test.
// So this row is the parse-time half of the binding; the runtime half walks
// entityIndex every tick, reads m_nModelIndex / m_vecOrigin / m_angRotation and
// poses the model from those values.
//
// firstTriangle/triangleCount address dynamicBrushTriangles, a sibling of
// triangles. Vertices in it are MODEL space: no faceOrigin was applied at
// emission, exactly as N20 leaves static brush vertices before its origin
// shift. That is what makes the runtime transform the entity's own absolute
// origin and angles and nothing else (no centre normalisation, no -0.1: those
// belong to the entity-model path, entity_model.cpp:849-857 and :913).
struct BspDynamicBrushModel {
  int entityIndex = -1;          // 0-based position in lump 0, -1 if unnamed
  std::string className;         // entity classname as written in the BSP
  std::int32_t modelIndex = 0;   // the N in the entity model key (*N)
  std::int32_t firstFace = 0;    // dmodel firstface (lump 14)
  std::int32_t faceCount = 0;    // dmodel face count (lump 14)
  std::size_t firstTriangle = 0; // first triangle in dynamicBrushTriangles
  std::size_t triangleCount = 0; // triangles emitted for this model
  float boundsMin[3] = {0.0f, 0.0f, 0.0f};  // MODEL-space AABB of those triangles
  float boundsMax[3] = {0.0f, 0.0f, 0.0f};
};


// T101 (2026-11-09): the map's environment fog, as the entity lump declares it.
// `env_fog_controller` is a POINT entity -- no `model` key, so the brush walk
// never looked at it -- and before this struct none of its keys reached memory:
// the entity parser keeps classname/model/origin and drops the rest. The values
// are in MAP units (fogstart/fogend), 0..255 (fogcolor), and the engine reads
// them as the controller's INITIAL state -- `foglerptime` says the demo can move
// them at run time, so this is a starting value, not the whole truth.
struct BspFog {
  bool present = false;      // an env_fog_controller exists in lump 0
  bool enabled = false;      // its fogenable key is present and not "0"
  float start = 0.0f;        // fogstart, map units
  float end = 0.0f;          // fogend, map units
  float maxDensity = 1.0f;   // fogmaxdensity
  float color[3] = {1.0f, 1.0f, 1.0f};  // fogcolor, normalised to 0..1
  // fogblend = 1 means the engine blends fogcolor toward fogcolor2 along
  // fogdir -- a bilateral, direction-dependent fog this renderer does not
  // implement. Carried so a reader can see the map asked for it rather than
  // being told the fog is "done"; koth_bagel_rc11/rc12 both set it.
  bool blend = false;
  int entityIndex = -1;      // 0-based position in lump 0
};

// N21 (2026-11-18): one point light, as `light` and `light_spot` declare it.
// Before this struct the entity parser kept classname/model/origin and dropped
// every other key, so no light of any kind reached memory and the world's
// shading was one direction and one strength invented in the renderer
// (native_renderer_frame.cpp's 0.35, -0.45, 0.82). Values are in MAP units and
// degrees. `_light`'s colour is normalised here; its 4th component -- the
// map's brightness number -- is carried unscaled, because turning brightness
// into a falloff distance is a renderer decision and this file records what
// the map said.
struct BspLight {
  bool spot = false;                        // true: light_spot, false: light
  float origin[3] = {0.0f, 0.0f, 0.0f};     // world space, map units
  float direction[3] = {0.0f, 0.0f, -1.0f}; // spot only: the cone's axis
  float color[3] = {1.0f, 1.0f, 1.0f};      // _light rgb / 255
  float intensity = 200.0f;                 // _light's 4th component
  float coneDeg = 45.0f;                    // _cone, outer half-angle
  float innerConeDeg = 0.0f;                // _inner_cone
  float exponent = 1.0f;                    // _exponent
  int entityIndex = -1;                     // 0-based position in lump 0
};

// N21: `light_environment` -- the sun and the ambient term every map has. The
// direction is stored as SURFACE -> LIGHT because that is the vector the
// shading dot product needs. Source's `pitch` is positive when the light
// points DOWN, so a sun at pitch -55 / yaw 325 puts (0.4698, -0.3290, 0.8192)
// in the sky -- the same octant as the hard-coded guess this replaces, which
// is the one check available without a reference frame.
struct BspSun {
  bool present = false;
  float direction[3] = {0.0f, 0.0f, 1.0f};    // surface -> light, map space
  float color[3] = {1.0f, 1.0f, 1.0f};        // _light rgb / 255
  float intensity = 1000.0f;                  // _light's 4th component
  float ambientColor[3] = {0.0f, 0.0f, 0.0f}; // _ambient rgb / 255
  float ambientIntensity = 0.0f;              // _ambient's 4th component
  float pitchDeg = 0.0f;
  float yawDeg = 0.0f;
  int entityIndex = -1;                       // 0-based position in lump 0
};

// N21: `sky_camera` -- the anchor of the 3D skybox. The geometry around it is
// authored at 1/`scale` of world size (16 unless the map says otherwise) and
// sits outside the map's own bounds: pl_upward_f12 puts it at 7168 -2048 0,
// past world_maxs' 4096. Parsed so the geometry can be claimed later; whether
// anything draws it yet is the renderer's switch, not this reading.
struct BspSkyCamera {
  bool present = false;
  float origin[3] = {0.0f, 0.0f, 0.0f};
  float scale = 16.0f;
  int entityIndex = -1;
};

struct BspMap {
  int version = 0;
  int revision = 0;
  std::size_t faceCount = 0;
  std::size_t renderedFaceCount = 0;
  // T23: faces the parser could not turn into a polygon at all (a surfedge or
  // vertex index outside its lump, or fewer than three vertices). A non-zero
  // count here is the other way geometry can be missing before any cull runs;
  // it is counted rather than inferred from faceCount - renderedFaceCount,
  // because a face can also be skipped by the triangle budget.
  std::size_t facesSkippedInvalid = 0;
  // Faces the map itself marks not-drawn (`SURF_NODRAW`/`SURF_HINT` bits
  // 64/128). They are geometry the renderer is supposed to omit, so they must
  // not be counted as missing surfaces.
  std::size_t facesSkippedNoDraw = 0;
  // T83: how the face lump splits. Model 0 (worldspawn) owns [0, worldFaceCount);
  // every other dmodel is a brush entity -- func_brush, doors, breakables -- and
  // those faces live in the same lumps at their compile-time position. The
  // parser used to read model 0's range only, so 22.4 % of ashville's faces were
  // never built. brushFaceCount is the share this change adds.
  std::size_t worldFaceCount = 0;
  std::size_t brushFaceCount = 0;
  // Which brush models are drawn, and how many were refused. A brush entity is
  // drawn only when the entity lump says it cannot move on its own
  // (func_brush/func_wall/func_lod): a door, a train or a breakable is stored at
  // its compile-time pose and drawing that pose would put a closed door across a
  // doorway the demo has open. The refused models are counted by reason so the
  // gap is a number, not silence.
  std::size_t brushModelsTotal = 0;
  std::size_t brushModelsDrawn = 0;
  std::size_t brushModelsSkippedMoving = 0;
  std::size_t brushModelsSkippedUnknown = 0;
  // D-14: the rows behind those two counts, in the order the dmodel index walks
  // them. Additive by construction: every refused model is pushed here at the
  // moment its counter is incremented, and no counter is derived from this
  // vector, so a reader of the counts cannot be moved by it.
  std::vector<BspRefusedBrushModel> brushModelsRefused;
  // DB-2: the brush models that move, kept apart from the static buffer above.
  // Filled only when --world-dynamic-brush is on: the walk marks a model dynamic
  // at the classification step and pushes a row there, so an off run leaves both
  // vectors empty, does no extra work, and its world buffer and counters are
  // byte-identical to the build before this change.
  std::vector<BspDynamicBrushModel> dynamicBrushModels;
  std::vector<BspTriangle> dynamicBrushTriangles;
  std::size_t dynamicBrushFaceCount = 0;      // faces emitted into the list above
  std::size_t dynamicBrushTriangleCount = 0;  // triangles emitted into it
  std::size_t dynamicBrushTrianglesDropped = 0;  // refused by the dynamic budget

  std::size_t brushFacesDrawn = 0;
  std::size_t brushFacesSkipped = 0;
  // N20: brush entities are compiled in MODEL space -- VBSP subtracts the
  // entity's "origin" from every vertex and the engine adds it back at draw
  // time. These five fields say how much of that translation this parser
  // applied, so "the brush is in the wall" is a number and not a guess:
  //   brushOriginModels   drawn models that moved by a non-zero origin
  //   brushOriginFaces    faces whose POSITION moved (uv/lightUv did not)
  //   brushOriginNoOrigin drawn models with no usable origin key (unshifted)
  //   brushOriginLodExcluded  func_lod models, already world-space by design
  //   brushOriginMaxShift  largest single-axis |origin| applied, in world units
  std::size_t brushOriginModels = 0;
  std::size_t brushOriginFaces = 0;
  std::size_t brushOriginNoOrigin = 0;
  std::size_t brushOriginLodExcluded = 0;
  float brushOriginMaxShift = 0.0f;
  std::size_t triangleCount = 0;
  // T88: the triangle budget is a CAPACITY, and a capacity that is hit is a
  // decision this parser made, not a fact about the file. Before this change
  // pushTriangle() returned early and told nobody, so the product asked a
  // 234,699-triangle map for 200,000 triangles and 3,632 of its faces vanished
  // from the frame without a line in any trace. These fields make the refusal
  // readable: triangleCapacity is what parse() was given, trianglesRequested is
  // what the map asked for (every push attempt, before the capacity check),
  // trianglesDropped is the difference, and the face/model counters say where
  // the loss happened so a hole can be named instead of inferred.
  std::size_t triangleCapacity = 0;
  std::size_t trianglesRequested = 0;
  std::size_t trianglesDropped = 0;
  std::size_t facesDroppedByBudget = 0;        // faces that lost >= 1 triangle
  std::size_t brushFacesDroppedByBudget = 0;   // the subset owned by a brush model
  std::size_t brushModelsDroppedByBudget = 0;  // distinct brush models that lost a face
  std::int32_t firstDroppedFace = -1;          // -1 when nothing was dropped
  std::string firstDroppedBrushModel;          // class of that face (worldspawn for model 0)
  std::size_t lightmapSampleCount = 0;
  std::size_t lightmapFaceCount = 0;
  std::size_t lightmapTriangleCount = 0;
  std::size_t lightmapBytes = 0;
  std::size_t lightmapPackedFaces = 0;
  std::size_t lightmapLuxelFits = 0;
  float lightmapIntensity = 1.0f;
  std::size_t hdrLightmapSampleCount = 0;
  std::size_t hdrLightmapFaceCount = 0;
  std::size_t hdrLightmapBytes = 0;
  float hdrLightmapIntensity = 1.0f;
  bool hasHdrLightmap = false;
  BspLightmapMode lightmapMode = BspLightmapMode::Unavailable;
  int lightmapAtlasWidth = 0;
  int lightmapAtlasHeight = 0;
  // T1 interface (2026-10-13): what the atlas packer ended up doing. The three
  // fields are read by world_material's lightmap reading, so a map that loses
  // faces to the packer says so with a number and a reason instead of silently
  // drawing unlit. `grown` is false for every map that fits the first shelf, so
  // a map whose readings are already pinned keeps them.
  int lightmapAtlasMaxWidth = 0;
  std::size_t lightmapAtlasDroppedFaces = 0;
  bool lightmapAtlasGrown = false;
  // T17: the tone of what the packer WROTE, per face, before any uv is
  // involved. The per-triangle tone is what the shader samples through the
  // uv; this is the face's own luxels. If the two histograms agree, the uvs
  // address their own rectangle and a dark frame is dark data; if they
  // disagree, the uv mapping is reading somebody else's rectangle. Buckets are
  // 32 wide, the same as the triangle histogram in world_geometry_probe.
  std::array<std::uint32_t, 8> lightmapFaceToneHistogram{};
  std::size_t lightmapFaceToneDark = 0;
  std::size_t lightmapFaceToneZero = 0;
  // The same two histograms taken on lump 53, the HDR page. A map compiled with
  // HDR keeps both and the engine chooses; if the tonemapped page is black
  // where the wide-range page is lit, the frame is dark because of the page we
  // chose, not because of the uvs or the map.
  std::array<std::uint32_t, 8> lightmapHdrFaceToneHistogram{};
  std::size_t lightmapHdrFaceToneDark = 0;
  std::size_t lightmapHdrFaceToneZero = 0;
  std::uint64_t hdrLightmapToneSum = 0;
  std::uint64_t hdrLightmapToneCount = 0;
  std::string lightmapAtlasReason;
  std::vector<std::uint8_t> lightmapAtlas;
  // T82: every cluster that carries a face, flattened as (offsets, values), so a
  // face can be tested against the pvs leaf by leaf. Source draws a face when ANY
  // leaf that carries it is potentially visible; the older one-cluster-per-face
  // rule kept whichever leaf came first in lump 16, and when that leaf was behind
  // the camera the whole face vanished -- a hole you can see straight through.
  // faceClusterOffsets has faceCount + 1 entries; values are never negative.
  std::vector<std::uint32_t> faceClusterOffsets;
  std::vector<std::int32_t> faceClusterValues;
  // Faces that carry more than one cluster. This is how many faces the any-leaf
  // rule can decide differently from the first-leaf rule, so a change in it is a
  // change in what the clip is allowed to hide.
  std::size_t facesWithSeveralClusters = 0;
  std::vector<BspTriangle> triangles;
  // T26: the sky faces, as geometry, kept apart from `triangles` on purpose.
  // The product must not draw them -- a sky brush face inside the world pass
  // would be a new surface with a sky material whose lightmap and blend data do
  // not exist -- but a measurement of "is this empty pixel sky by design" needs
  // their polygons, and guessing that answer from a colour is not a
  // measurement. They are emitted here and drawn only by the `--world-geometry
  // sky` diagnostic mask, which is the same camera, projection and transform as
  // the world pass.
  std::vector<BspTriangle> skyTriangles;
  bool staticPropsAvailable = false;
  std::string staticPropsReason;
  std::size_t staticPropModelCount = 0;
  std::vector<BspStaticProp> staticProps;
  std::string skyName;
  // T101: the environment fog the entity lump declares (see BspFog). Parsed
  // regardless of the renderer's switch; whether it is drawn is a caller
  // decision, so the reading exists before the capability does.
  BspFog fog;
  // N21: the lights the entity lump declares. Parsed regardless of the
  // renderer's switch, like the fog above: the reading exists before anything
  // draws it. `lightsDeclared` counts every light/light_spot block seen and
  // `lightsRejected` the ones whose `_light` or `origin` did not parse, so
  // "the map asks for 204 lights and 3 were refused" is a reading rather than
  // a shortfall a reader has to infer.
  BspSun sun;
  std::vector<BspLight> lights;
  std::size_t lightsDeclared = 0;
  std::size_t lightsRejected = 0;
  BspSkyCamera skyCamera;
  std::size_t skyFaces = 0;
  std::size_t displacementFaces = 0;
  std::size_t displacementTriangles = 0;
  std::size_t displacementCornerMatches = 0;
  // T23: not every face that carries a dispinfo becomes a displacement grid.
  // A face whose grid cannot be built is emitted as its flat quad instead, so
  // "the terrain is missing" and "the terrain is flat here" are different
  // readings. `candidates` counts the faces that asked for a grid; `fallback`
  // counts the ones that did not get one, split by the branch that refused it.
  // A map whose candidate count equals `displacementFaces` has no fallback
  // faces at all, and a hole cannot be blamed on this path.
  std::size_t displacementFaceCandidates = 0;
  std::size_t displacementFallbackFaces = 0;
  std::size_t displacementFallbackLump = 0;       // no usable dispinfo/dispvert lump
  std::size_t displacementFallbackPolygon = 0;    // face is not a quad
  std::size_t displacementFallbackIndex = 0;      // dispinfo index out of range
  std::size_t displacementFallbackPower = 0;      // power outside 2..4
  std::size_t displacementFallbackRange = 0;      // vertex range outside the lump
  std::size_t displacementFallbackCorner = 0;     // no polygon corner matches the start
  std::size_t displacementFallbackNonFinite = 0;  // a grid vertex is not finite
  std::size_t displacementFallbackCell = 0;       // a grid cell is degenerate/oversized
  std::size_t displacementFallbackLimit = 0;      // the triangle budget truncated it
  std::size_t displacementFallbackOther = 0;      // refused with no more specific reason
  // The faces behind those counters, so a fallback is a place in the map and
  // not only a number. Bounded by the number of candidates (1392 on the
  // reference map); the probe prints the first few.
  std::vector<std::int32_t> displacementFallbackFaceIndex;
  std::vector<std::string> displacementFallbackMaterial;
  // T10 winding census, read-only: how many face polygons wind along the side
  // of their own BSP plane the face is meant to be seen from (planeSide 1
  // faces count as agreeing when the winding normal points AGAINST the stored
  // plane normal). The plane lump is ground truth, so agree/total measures the
  // winding the renderer will cull against without depending on how the
  // triangle soup was emitted. No polygon is reordered here: the census only
  // counts.
  std::size_t windingFaceCount = 0;
  std::size_t windingAgreeCount = 0;
  // T11: how many emitted triangles were reversed at emission so their winding
  // normal points along their face's own front side. A triangle is either
  // stored the way the cull mode wants or it is reversed; this count is the
  // number that had to move, and the gate pins it.
  std::size_t windingFlippedTriangles = 0;
  // T11 follow-up: the side bit is part of the ground truth. `windingAgreeCount`
  // above already honours it, but the emission did not: a face whose plane side
  // bit is 1 is seen from the plane's BACK, so the outward direction is -normal,
  // and a rule that orients every face to +normal leaves those faces wound
  // backwards. These two counters are the measurement that says how many faces
  // are on that side and how many emitted triangles still point away from their
  // own outward direction after emission. `misfaced` must read 0 on a coherent
  // buffer; it is the number the cull mode would otherwise remove.
  std::size_t windingSideOneFaces = 0;
  std::size_t windingMisfacedTriangles = 0;
  std::size_t windingMisfacedDisplaced = 0;
  // T12 post-merge: T11 reverses a displacement triangle's b and c to orient
  // it, and the blend weight rides on the vertex. `std::swap(b, c)` alone would
  // leave the alphas where they were and attach the author's weight to the
  // wrong corner -- a picture that still blends, just with the mask mirrored
  // inside every quad. These two counters compare the emitted triangle's
  // (position, weight) pairs against the grid cells it was built from: a swap
  // that moves the positions and not the weights reads as `detached`.
  std::size_t alphaPairingChecked = 0;
  std::size_t alphaPairingDetached = 0;
  // T11 third round: is the SOURCE winding uniform? The rule matrix left one
  // question open -- whether the stored polygon order is a per-face coin toss,
  // which no plane-derived rule could ever reproduce, or a fixed convention
  // relative to the plane, which makes the emission rule a constant rather than
  // a choice. These count, per face class, the face polygons whose own winding
  // normal points ALONG (+) or AGAINST (-) the raw plane normal, and -- for
  // displacement faces -- whether the emitted grid's first triangle keeps or
  // reverses the polygon's own order. A class that reads one column at zero is
  // a uniform convention; a class that splits is the class no single sign can
  // orient. Face counts, not triangle counts, so a 32x32 grid cannot outvote a
  // quad.
  std::size_t windingStoredBrushAlong = 0;
  std::size_t windingStoredBrushAgainst = 0;
  std::size_t windingStoredDispAlong = 0;
  std::size_t windingStoredDispAgainst = 0;
  std::size_t windingGridKeepsPolygon = 0;
  std::size_t windingGridReversesPolygon = 0;
  // The same question asked of the EMITTED triangle before any reversal, which
  // is the only place the fan's own orientation shows: the brush fan emits
  // (0, i+1, i), the reverse of the polygon's order, so the polygon census
  // above and the buffer's order are not the same statement. [0] counts along
  // the raw plane normal, [1] counts along the side-aware outward direction.
  std::size_t windingPreBrushAlongPlane = 0;
  std::size_t windingPreBrushAgainstPlane = 0;
  std::size_t windingPreDispAlongPlane = 0;
  std::size_t windingPreDispAgainstPlane = 0;
  std::size_t windingPreBrushAlongOutward = 0;
  std::size_t windingPreBrushAgainstOutward = 0;
  std::size_t windingPreDispAlongOutward = 0;
  std::size_t windingPreDispAgainstOutward = 0;
  std::size_t waterFaces = 0;
  bool waterReflection = false;
  bool waterRefraction = false;
  std::size_t cubemapCount = 0;
  std::vector<BspCubemapSample> cubemaps;
  BspVisMode visMode = BspVisMode::Unavailable;
  int visClusterCount = 0;
  std::vector<int> visOffsets;
  std::vector<std::uint8_t> visBits;
  // T105/task-37: the leaf ambient cube. Two lumps, and the whole point of
  // parsing them is that a consumer can ask one question -- "what is the
  // ambient light at this point" -- and get either a cube or an explicit
  // "no". leafAmbientSamples is the decoded lump 56/55 page (each record is 28
  // bytes: a six-colour cube then the sample position); leafAmbientEntries is
  // the per-leaf index from lump 52/51. Both stay empty when the map shipped
  // neither, which is the state 1 of this host's 261 maps is in.
  BspLeafAmbientMode leafAmbientMode = BspLeafAmbientMode::Unavailable;
  // True when the page that was read came from the HDR pair (51/55) because the
  // LDR pair was absent. Every map on this host's corpus ships the LDR pair, so
  // this is false on all 260 that have the lumps at all; it exists so a corpus
  // census can tell "HDR-only compile" from "LDR", which a single mode enum
  // cannot say without re-reading the lump table.
  bool leafAmbientHdrPage = false;
  std::vector<BspLeafAmbientEntry> leafAmbientEntries;
  std::vector<BspLeafAmbientSample> leafAmbientSamples;
  // The count the caller actually cares about: leaves that carry at least one
  // cube. leavesWithAmbient + leavesWithoutAmbient == leaves.size() on any map
  // whose index lump parsed, so a wrong stride cannot hide as "some leaves are
  // just dark".
  std::size_t leavesWithAmbient = 0;
  std::size_t leavesWithoutAmbient = 0;
  // Index records whose count/first reached past the data lump (clamped, and
  // counted -- a record that reads another leaf's cube is worse than a miss),
  // and data records no leaf claims. Both are 0 on all 260 shipped maps that
  // carry the lumps, so a non-zero value here means the layout assumption broke.
  std::size_t leafAmbientRecordsOutOfRange = 0;
  std::size_t leafAmbientSamplesUnreferenced = 0;
  // Lookups that resolved to a leaf carrying cubes, and lookups that had to
  // fall back (no tree, no lump, an empty leaf, an out-of-range sample). The
  // second number is the one a lighting claim has to quote: it is how often
  // the feature said nothing rather than something.
  std::size_t ambientLookupsHit = 0;
  std::size_t ambientLookupsMissed = 0;
  std::vector<BspPlane> planes;
  std::vector<BspNode> nodes;
  std::vector<BspLeaf> leaves;
  std::vector<std::uint16_t> leafFaces;
  std::string error;
  bool valid = false;
};

class BspParser {
public:
  static bool parse(const std::vector<std::uint8_t>& bytes, BspMap& map,
    std::size_t triangleLimit = 200000, int atlasMaxWidth = 4096, bool keepMapWinding = false,
    bool flipDisplacementWinding = false,
    const std::vector<std::string>& extraStaticClasses = {},
    bool drawDynamicBrushes = false);
  static const char* lightmapModeName(BspLightmapMode mode);
  static bool skyMaterialPath(const std::string& skyName, int face, std::string& out);
  static bool clusterVisible(const BspMap& map, int fromCluster, int toCluster);
  static int clusterAt(const BspMap& map, float x, float y, float z);
  static int countVisibleClusters(const BspMap& map, int fromCluster);
  // T105/task-37: the leaf index a point lands in, -1 when the tree cannot
  // answer. clusterAt() is the sibling this mirrors: same walk, same guards,
  // same 4096-step ceiling; it returns the leaf's CLUSTER and this returns the
  // leaf itself. Both exist because a caller that wants the ambient cube needs
  // the leaf (the index lump is per-leaf), while a caller doing PVS needs the
  // cluster -- and deriving either one from the other is how they drift.
  static int leafAt(const BspMap& map, float x, float y, float z);
  // The cube for a leaf index. No tree walk, so this is also the entry point a
  // consumer that already has a leaf (a static prop stores leaf INDICES in lump
  // 35) can use. Returns false and leaves `out` untouched on any miss. A leaf
  // with several cubes answers with the one nearest its own box centre; the
  // point-taking overload below answers with the one nearest the point.
  static bool leafAmbientAtLeaf(BspMap& map, int leafIndex, BspLeafAmbientCube& out);
  // The cube for a world point: leafAt(), then the same private chooser the
  // leaf overload uses, but aimed at the caller's point instead of the leaf
  // centre. Takes a mutable map because every call counts itself in
  // BspMap::ambientLookups{Hit,Missed} -- the miss count is the number a
  // lighting claim has to quote, and a counter nobody increments reads as zero
  // misses.
  static bool leafAmbientAt(BspMap& map, float x, float y, float z, BspLeafAmbientCube& out);
  static const char* leafAmbientModeName(BspLeafAmbientMode mode);
};

} // namespace tf2::native
