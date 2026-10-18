#pragma once

// The world material and lighting capability layer.
//
// WHY THIS FILE EXISTS
// --------------------
// Six things the world pass claims are handled by the BSP/render path, and until
// now each of them was answered by one number somewhere: `lightmapMode` said
// "source-equivalent", `cubemapMode` said "approximate-2d", `waterSurface` said
// "approximate-warp". None of those is falsifiable. A number that cannot go red
// is a number that cannot be wrong, and this task's own rule is that a claim
// nobody can refute is not a claim.
//
// So this module turns each capability into three things a gate can assert:
//   * a state (Unavailable / Fallback / Active),
//   * a reason that names the measured cause, and
//   * the counters the state was derived from.
//
// Two deliberate restrictions:
//   1. It does NOT touch bsp_map.cpp. Everything below is computed from the
//      fields BspParser already fills, so the material-chain and
//      resource-reachability gates keep their frozen readings and the other
//      parallel tracks keep their binaries.
//   2. It does NOT link a VPK reader. Resource reachability is injected through
//      WorldResourceProbe, so the same evaluation runs against the real install
//      (the probe tool) and against a synthetic resource set (the fixtures)
//      without a second implementation of "does this file exist".

#include "bsp_map.h"

#include <map>

#include <array>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace tf2::native {

// ---------------------------------------------------------------------------
// Capability state
// ---------------------------------------------------------------------------

enum class WorldCapabilityState { Unavailable, Fallback, Active };

const char* worldCapabilityStateName(WorldCapabilityState state);

// Injected resource lookup. Implemented by the caller against whatever store it
// has: a VpkArchiveSet plus the loose tf directory for the product, a fixed
// in-memory set for the fixtures.
class WorldResourceProbe {
public:
  virtual ~WorldResourceProbe() = default;
  virtual bool reachable(const std::string& resourcePath) const = 0;
  // Bytes for the paths that resolve. The default is empty because a probe that
  // only answers reachability is a legitimate implementation: the cubemap
  // reader then reports "reachable but unread" rather than inventing faces.
  virtual std::vector<std::uint8_t> read(const std::string& resourcePath) const;
};

// One file in a fixture's "install": the path it answers to, and the bytes it
// holds. An entry with no bytes is reachable but unreadable, which is a state
// the cubemap reader names rather than silently treating as missing -- so a
// fixture can produce all three (there with bytes, there without, absent) and
// the difference between the last two stays visible.
struct FixedResource {
  std::string path;
  std::vector<std::uint8_t> bytes;
};

// A probe over an explicit set of files. Used by the fixtures: the "install" is
// exactly the list handed to the constructor, so a fixture that says a material
// exists makes it exist and nothing else does.
class FixedResourceProbe : public WorldResourceProbe {
public:
  explicit FixedResourceProbe(std::vector<FixedResource> present)
    : present_(std::move(present)) {}
  bool reachable(const std::string& resourcePath) const override;
  std::vector<std::uint8_t> read(const std::string& resourcePath) const override;

private:
  std::vector<FixedResource> present_;
};

// ---------------------------------------------------------------------------
// 1. RGBExp32 per-face lightmap
// ---------------------------------------------------------------------------

struct WorldLightmapReading {
  // Faces the BSP declared a lightmap for, faces that fit the 2048-wide shelf
  // packer, and the difference. `dropped` is the number this module exists to
  // name: before it, a face that had lightmap data and lost it to the packer
  // was indistinguishable from a face that never had any.
  std::size_t facesWithLightmap = 0;
  std::size_t facesPacked = 0;
  std::size_t facesDropped = 0;
  // Triangles whose lightmap UVs the parser actually emitted. This is the
  // per-face claim made countable: a triangle with lightmapped=false carries
  // (-1,-1) and the shader refuses to sample for it.
  std::size_t trianglesWithLightUv = 0;
  std::size_t triangles = 0;
  std::size_t samples = 0;
  std::size_t atlasBytes = 0;
  int atlasWidth = 0;
  int atlasHeight = 0;
  // T1 (2026-10-13): the packer now chooses its shelf width and says so. The
  // three fields are the map's own, carried through rather than recomputed, so
  // the probe and the parser cannot disagree about which width was used.
  int atlasMaxWidth = 0;
  bool atlasGrown = false;
  std::string atlasReason;
  std::size_t lightUvOutOfRange = 0;
  bool atlasBytesMatchDimensions = false;
  bool uvAllInRange = false;
};

WorldLightmapReading measureLightmap(const BspMap& map);

// ---------------------------------------------------------------------------
// 2. Cubemap: six-face discovery, order, sampling
// ---------------------------------------------------------------------------

// The six suffixes in face order. This order is D3D11's TextureCube face order
// (+X, -X, +Y, -Y, +Z, -Z) and it is the same order BspParser::skyMaterialPath
// already uses, which is why one table serves both the sky and the env path:
// a second table would be a second thing to get wrong.
const char* worldCubemapFaceSuffix(int face);

// The Source convention for an `env_cubemap` sample: ONE VTF file per sample,
// named after the sample's integer coordinates, holding all six faces:
//   materials/maps/<map>/c<X>_<Y>_<Z>.vtf
// The BSP cubemap lump carries the origins but no names, so the path is derived
// from them. TF2 maps additionally embed these files in the BSP's own pakfile
// lump, which is why reachability alone cannot answer the question and the
// bytes have to be readable (see bspPakfileEntry).
//
// This replaces an earlier six-file convention (c_<map>_<n><suffix>.vtf) that
// this install's data refutes: cp_snakewater_final1's BSP pakfile holds 33
// single c<X>_<Y>_<Z>.vtf files, and no six-file set exists anywhere in the
// 149734 VPK entries or the BSP.
bool worldCubemapVtfPath(const std::string& mapName, const int origin[3], std::string& out);

// Reads one entry out of the ZIP a Source BSP carries in its pakfile lump
// (lump 40). Two storage methods appear in this install and both are decoded:
//
//   method 0   stored, copied verbatim      cp_snakewater_final1, all 3354 entries
//   method 14  LZMA                        2koth_abbey, all 4935 entries
//
// LZMA is the PKWARE framing -- two version bytes, a two-byte properties size,
// the properties, then the raw stream -- and the decoder is the same one the BSP
// lumps already use. Any other method is refused by name rather than guessed at,
// and `reason` carries that name back so a reading can say which method it hit
// instead of reporting a missing file.
bool bspPakfileEntry(const std::vector<std::uint8_t>& bspBytes, const std::string& name,
  std::vector<std::uint8_t>& out, std::string* reason = nullptr);

// Direction -> face index and the (u,v) inside that face, in [0,1].
//
// The mapping is D3D11's TextureCube sampling convention, chosen because it is
// written down somewhere other than this file: the face with the largest
// |axis| wins, sc/tc are the two remaining axes with the signs the D3D spec
// gives, and u = (sc/|ma| + 1)/2, v = (1 - tc/|ma|)/2. Picking the convention
// instead of inventing one is what makes "the order is right" checkable against
// a third-party definition rather than against itself.
//
// A zero or non-finite direction has no dominant axis and returns false; the
// caller must fall back rather than sample face 0 by default.
bool worldCubemapFaceForDirection(float x, float y, float z, int& face, float& u, float& v);

struct WorldCubemapSampleReading {
  int index = 0;
  int origin[3] = {};
  // The one file this sample's faces live in, and where its bytes came from.
  // "loose", "vpk" or "bsp-pakfile" is recorded because the same path can
  // resolve from more than one store, and which one answered changes what a
  // missing file means.
  std::string vtfPath;
  std::string source;
  bool reachable = false;
  bool bytesRead = false;
  bool parsed = false;
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  std::uint32_t format = 0;
  std::uint8_t mipCount = 0;
  // Face slots the image section holds, measured from the file, and the count
  // the header's own rule predicts. They disagree on this install's data (seven
  // measured, six declared), and both are reported so the disagreement is a
  // reading rather than something a decoder silently normalises away.
  std::uint32_t facesInFile = 0;
  std::uint32_t declaredFaces = 0;
  std::size_t facesDecoded = 0;
  // What the decoder actually returned for each face: mean luminance, and a
  // checksum over the decoded RGBA bytes. The counts above say how many faces
  // came back; these say WHICH pixels did, which is the only reading that can
  // tell the format's layout apart from a plausible-looking wrong one -- every
  // face of a misread file still decodes to something, so a decoder that walks
  // the image section at the wrong stride reports six faces and hands back
  // pixels nobody wrote.
  int faceMean[6] = {};
  std::uint32_t faceChecksum[6] = {};
  std::string firstMissing;
};

struct WorldCubemapReading {
  std::size_t samples = 0;
  std::vector<WorldCubemapSampleReading> perSample;
  // Samples whose VTF parsed and yielded six decodable faces. `anyComplete` is
  // what the renderer needs: one complete sample is enough to bind a cube, and
  // saying "all or nothing" would make a map with one broken sample look like a
  // map with none.
  std::size_t samplesComplete = 0;
  bool anyComplete = false;
  // Where the complete samples' bytes came from. The two stores are different
  // code paths and a fixture has to be able to tell them apart: a reading built
  // only from the loose tree would never exercise the bsp-pakfile reader that
  // every TF2 map actually needs.
  std::size_t samplesFromProbe = 0;
  std::size_t samplesFromPakfile = 0;
  // Faces decoded across every sample, so the number is 6 per complete sample
  // and the fixture's expectation is derived rather than remembered.
  std::size_t facesDecoded = 0;
  // Samples resolved but unreadable/unparsable, with the first reason. The
  // state machine needs the difference between "the file is not there" and
  // "the file is there and does not parse" to name a cause.
  std::size_t samplesUnparsed = 0;
  std::string firstUnparsedReason;
  // Face slots beyond the six a cube needs, summed over complete samples: the
  // seventh face these files carry is measured and reported, not interpreted.
  std::size_t facesBeyondSix = 0;
};

WorldCubemapReading measureCubemaps(const BspMap& map, const std::string& mapName,
  const WorldResourceProbe& probe, const std::vector<std::uint8_t>& bspBytes);

// ---------------------------------------------------------------------------
// 3. Skybox
// ---------------------------------------------------------------------------

// Where one cube face's pixels came from. `measureSky` counts the six resources
// and `captureSkyFaces` keeps their bytes, but until this existed neither said
// which file landed on which side of the box -- so "six sides decode" and "the
// six sides are on the six directions they belong to" read the same. They do
// not: two entries swapped in the suffix table still decode six textures, they
// just show the sky rotated, which is a defect no count would catch.
//
// `sampleRgba` is the centre pixel of the decoded side. It is what makes the
// mapping check non-circular: the fixture puts a different colour in each file,
// and the gate then asks which cube index is holding it -- so the answer comes
// from the pixels rather than from a second copy of the table being checked.
struct WorldSkyFaceOrigin {
  int face = 0;
  std::string suffix;        // "rt" .. "dn", from worldCubemapFaceSuffix
  std::string direction;     // "+X" .. "-Z", derived from the geometry basis
  std::string materialPath;  // the .vmt this index resolved, or "-"
  std::string texturePath;   // the .vtf its $basetexture named, or "-"
  std::string sampleRgba;    // "r,g,b,a" centre pixel, or "not-decoded"
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  bool decoded = false;
};

struct WorldSkyReading {
  bool declared = false;
  std::string name;
  std::size_t skyFaces = 0;
  int vmtFound = 0;
  std::string firstMissing;
  // A VMT that resolves is not a sky. Each one names a $basetexture, and the
  // capability is the six textures behind them decoding to pixels -- which is
  // what the renderer needs and what "six vmt files exist" does not say. A VMT
  // with no $basetexture, or one whose VTF is missing, is a different failure
  // from a missing VMT and gets its own counter.
  int texturesResolved = 0;
  int texturesDecoded = 0;
  std::uint32_t firstWidth = 0;
  std::uint32_t firstHeight = 0;
  // The smallest of the six decoded faces. Measured because the content makes
  // it worth measuring: sky_well_01dn.vtf is 1x1 in this install while its five
  // siblings are 512x256 and 512x512, so "six faces decode" and "six faces are
  // the same size" are different statements and only the second one is a sky.
  std::uint32_t smallestWidth = 0;
  std::uint32_t smallestHeight = 0;
  std::size_t decodedBytes = 0;
  std::string firstTextureProblem;
  // T8 (2026-10-16): the per-face provenance, filled by the same walk that
  // produced the counters above. Kept here rather than only on the capture so
  // `measureSky` and `captureSkyFaces` cannot disagree about which file is on
  // which side -- they share skyFaceOrigin().
  std::array<WorldSkyFaceOrigin, 6> origins{};
};

WorldSkyReading measureSky(const BspMap& map, const WorldResourceProbe& probe);

// ---------------------------------------------------------------------------
// 4. Displacement
// ---------------------------------------------------------------------------

struct WorldDisplacementReading {
  std::size_t faces = 0;
  std::size_t cornerMatches = 0;
  std::size_t triangles = 0;
  // Counted from map.triangles rather than trusted from map.displacementTriangles,
  // so the two disagreeing is a reading instead of a surprise.
  std::size_t trianglesTagged = 0;
  std::size_t renderedFaces = 0;
  std::size_t facesTotal = 0;
  // The parser's triangle limit truncates the walk. When it fires, every count
  // below is a count of a prefix, and a capability reading that does not say so
  // is lying about coverage. BspMap does not carry the limit, so the caller
  // passes the one it handed to parse(); 0 means "unknown, do not claim".
  bool limitTruncated = false;
  bool cornerMatchesAllFaces = false;
};

WorldDisplacementReading measureDisplacement(const BspMap& map, std::size_t triangleLimit = 0);

// ---------------------------------------------------------------------------
// 5. Water capability and missing-resource fallback
// ---------------------------------------------------------------------------

struct WorldWaterReading {
  std::size_t faces = 0;
  std::size_t trianglesTagged = 0;
  // What the map declares, what the material declares, what is reachable. The
  // product's own RenderSettings carries waterReflection/waterRefraction as
  // preset flags; BspMap hard-codes both to false. That disagreement is the
  // reason this struct keeps them apart.
  bool reflectionDeclaredBySettings = false;
  bool refractionDeclaredBySettings = false;
  bool materialDeclared = false;
  bool textureReachable = false;
};

WorldWaterReading measureWater(const BspMap& map, bool reflectionDeclaredBySettings,
  bool refractionDeclaredBySettings, bool waterMaterialDeclared, bool waterTextureReachable);

// ---------------------------------------------------------------------------
// T2 interface freeze (2026-10-14): the water surface, as geometry
// ---------------------------------------------------------------------------
//
// `measureWater` above counts what the map declares. This half is what the
// renderer draws, and it exists because the counting is not the claim the task
// makes: a map with 18 water faces and no water pass has the same
// `waterFaces` as one with 18 water faces and a pass that draws them, and only
// one of those pictures changes when the switch is thrown.
//
// Everything here is pure. The renderer calls evaluateWaterDrawRequest to
// decide, and worldWaterWarpUv to perturb; the probe drives both so a gate that
// tests a second implementation of either is a gate that can pass while the
// product is wrong.

// Five words, not three. `Declared` and `Parsed` are different facts and the
// difference is the one the task's "declared is not applied" rule is about: a
// water VMT that exists is declared, one that exists and named a texture that
// resolved is parsed, and a pass that drew triangles is Active.
enum class WorldWaterSurfaceState { Unavailable, Declared, Parsed, Active, Fallback };
const char* worldWaterSurfaceStateName(WorldWaterSurfaceState state);

// One water vertex. The field order and count deliberately mirror the world
// pass's own WorldVertex, so the water pass reuses the world's input layout and
// vertex shader instead of owning a second copy of both -- a second layout would
// be a second thing that has to agree with the first about what a vertex is, and
// the water pass has nothing to say that the world shader does not already
// output. The `static_assert` in native_renderer.cpp is what keeps the two in
// step: if WorldVertex moves, the water pass stops compiling rather than
// silently reading the wrong floats.
struct WorldWaterVertex {
  float x = 0.0f;
  float y = 0.0f;
  float z = 0.0f;
  // Per-triangle shade. This is what the flat fallback draws, so a water surface
  // whose texture never resolved is visibly water-shaped rather than black.
  float r = 0.0f;
  float g = 0.0f;
  float b = 0.0f;
  float a = 1.0f;
  // Normalised into [0,1] at upload time. The warp is applied to these by
  // evaluateWaterDrawRequest, on the CPU, from the caller's phase -- so a fixed
  // tick gives a fixed picture and the repeat comparison has something real to
  // compare.
  float u = 0.0f;
  float v = 0.0f;
  float nx = 0.0f;
  float ny = 0.0f;
  float nz = 1.0f;
  // Deliberately -1: the water pass draws no lightmap, and the world pixel
  // shader only samples the atlas for a vertex whose light uv is >= 0. Keeping
  // the fields at -1 is what stops water from being lit by the map's lightmap.
  float lu = -1.0f;
  float lv = -1.0f;
  // T12: the blend inputs the world pass carries for a terrain material. Water
  // is not a blend material, so the sentinel (-1) is the whole of what this
  // pass says about them -- but the fields have to exist here, because the two
  // structs share one input layout.
  float blend = -1.0f;
  float blendModulate = 0.0f;
};

// The water material's parsed parameters. TF2's water shaders carry the two
// neighbour materials, the two Fresnel coefficients and the cheap/above-water
// switches; none of them is a reflection, which is why none of them is used to
// claim one.
struct WorldWaterParams {
  bool declared = false;
  std::string reason;
  std::string materialPath;
  std::string bottomMaterial;
  std::string underwaterOverlay;
  bool aboveWater = false;
  bool cheap = false;
  float refractivity = 0.0f;
  float reflectivity = 0.0f;
  // The normal/bump texture the material names, if any. `bumpMap` is the
  // legacy key and `normalMap` the current one; a material carrying both is
  // reported with both rather than one silently winning.
  std::string bumpMap;
  std::string normalMap;
};

// Reads the parameters out of a water VMT's text. Pure: the caller supplies the
// bytes so the probe and the product read the same file.
WorldWaterParams measureWaterParams(const std::string& materialText,
  const std::string& materialPath = {});

struct WorldWaterDrawRequest {
  // The caller's switch. Off means "do not draw", and the reading then names the
  // caller rather than reporting a map with no water.
  bool enabled = false;
  // The water triangles, already converted into the world's scaled space. Empty
  // means the map declared no water geometry, which is a different fact from a
  // switch that was turned off.
  std::vector<WorldWaterVertex> vertices;
  // Whether a water texture resolved. A declared texture is not a resolved one:
  // the pass falls back to the flat shade when this is false, and the reading
  // says so instead of drawing an untextured surface and calling it water.
  bool textureResolved = false;
  WorldWaterParams params;
  // Warp amplitude in uv units, and the phase derived from the playback tick.
  // `warpPhase` is a double so a tick counter can drive it without losing the
  // low bits over a long recording.
  float warpAmplitude = 0.02f;
  double warpPhase = 0.0;
};

struct WorldWaterDrawReading {
  WorldWaterSurfaceState state = WorldWaterSurfaceState::Unavailable;
  // What the pass was given, counted from the request rather than trusted from
  // the parse: `faces` is derived from the vertex count the same way the sky
  // pass derives its own, so the two cannot disagree about what a face is.
  std::size_t waterFaces = 0;
  std::size_t waterTriangles = 0;
  std::size_t waterDrawCalls = 0;
  // Whether the surface is in the picture at all. A pass that uploaded a buffer
  // and drew nothing reports visible=false with draws=0, which is the pair that
  // separates "the buffer exists" from "the water is on screen".
  bool waterVisible = false;
  bool waterWarpApplied = false;
  // The two render targets. Unavailable is the honest answer until one exists;
  // the reason names which half is missing so a reader is not left to guess
  // whether reflection, refraction or both are absent.
  WorldWaterSurfaceState waterReflectionState = WorldWaterSurfaceState::Unavailable;
  WorldWaterSurfaceState waterRefractionState = WorldWaterSurfaceState::Unavailable;
  // True when the pass ran without a resolved texture, i.e. drew the flat shade.
  bool waterFallback = false;
  std::string reason;
};

// Builds the draw list and judges the request. `outVertices` receives the
// warped vertices when the request is drawable, so the renderer uploads exactly
// what the reading counted rather than warping a second time with a second
// implementation.
//
// The output mirrors WorldVertex's layout field for field (see the comment on
// WorldWaterVertex), which is what lets the water pass reuse the world's input
// layout and vertex shader rather than owning a second copy of both.
WorldWaterDrawReading evaluateWaterDrawRequest(const WorldWaterDrawRequest& request,
  std::vector<WorldWaterVertex>& outVertices);

// The approximate UV warp, as a property rather than as a value:
//
//   * bounded      -- outU is inside [u - amplitude, u + amplitude] for every
//                     phase and every input in [0,1]
//   * NaN-free     -- a non-finite input or amplitude is passed through
//                     unchanged instead of poisoning the sample
//   * repeatable   -- f(u,v,p) == f(u,v,p), and f(u,v,0) != f(u,v,0.37) for the
//                     phases the product uses, which is what makes
//                     water-on == water-repeat a statement about the warp
//
// The formula is the two-term sinusoid the water shaders use: a slow drift plus
// a faster ripple, each scaled by the amplitude and each a function of the
// caller's phase. It is approximate on purpose and is never reported as a
// normal map.
void worldWaterWarpUv(float u, float v, double phase, float amplitude,
  float& outU, float& outV);

// ---------------------------------------------------------------------------
// T1 interface freeze (2026-10-13): skybox geometry and VIS clipping
// ---------------------------------------------------------------------------
//
// FROZEN BEFORE IMPLEMENTATION, because T2 (water and advanced materials) may
// depend on these names and on nothing else: the reading structs, the state
// words and the two pure entry points below are the contract, and the renderer
// is not. T2 must not edit native_renderer.*; T1 owns them.
//
// Both entry points are pure (no GPU, no file I/O) so the probe can drive the
// same code the product drives: a gate that tests a second implementation of
// the clip is a gate that can pass while the clip is wrong.

// One skybox vertex: a position on the unit box and the uv inside that side's
// texture. The renderer scales the box by the request's half-extent and drops
// the view matrix's translation, so the box follows the camera.
struct WorldSkyVertex {
  float x = 0.0f;
  float y = 0.0f;
  float z = 0.0f;
  float u = 0.0f;
  float v = 0.0f;
};

// One decoded side. RGBA8, because that is what the renderer uploads and what
// a fixture can construct without a VTF encoder.
struct WorldSkyFace {
  std::vector<std::uint8_t> rgba;
  std::uint32_t width = 0;
  std::uint32_t height = 0;
};

enum class WorldSkyState { Unavailable, Fallback, Active };
const char* worldSkyStateName(WorldSkyState state);

// The cube direction a face index is drawn on, as "+X", "-X", "+Y", "-Y", "+Z",
// "-Z" -- read off the same basis the geometry is built from, so the name a gate
// asserts cannot drift from the direction the box actually uses. Nullptr outside
// [0,6), which is how the caller says "there is no such face".
const char* worldSkyFaceDirection(int face);

struct WorldSkyDrawRequest {
  // The caller's switch. Off means "do not draw", and the reading then says
  // Disabled-by-caller rather than "no sky", which are different facts.
  bool enabled = false;
  // Half the box edge, in the renderer's world units. The world is scaled into
  // a ~1.8-unit box, so 40 keeps the sky outside it and inside the far plane.
  float halfExtent = 40.0f;
  // Six sides in D3D11 cube order (+X, -X, +Y, -Y, +Z, -Z), the same order
  // worldCubemapFaceSuffix and BspParser::skyMaterialPath use.
  std::array<WorldSkyFace, 6> faces{};
};

struct WorldSkyDrawReading {
  WorldSkyState state = WorldSkyState::Unavailable;
  // 36 vertices / 12 triangles is a box. Zero means nothing was drawn, and the
  // reason then names why -- "no skyname", "3 of 6 sides decoded", "disabled".
  std::uint32_t skyboxVertices = 0;
  std::uint32_t skyboxTriangles = 0;
  std::uint32_t skyboxFaces = 0;
  std::uint32_t skyboxDraws = 0;
  std::string reason;
};

// Builds the box and judges the request. `outVertices` is filled with 6 sides x
// 6 vertices in D3D11 cube order; face f occupies vertices [f*6, f*6+6).
WorldSkyDrawReading evaluateSkyDrawRequest(const WorldSkyDrawRequest& request,
  std::vector<WorldSkyVertex>& outVertices);

// The six sides' pixels, for a caller that means to draw them. `measureSky`
// proves the files decode and counts them; this hands the decoded bytes to
// whoever uploads them, so the renderer cannot be drawing a different set of
// files from the ones the reading counted. `read` answers "the bytes at this
// resource path", which is the product's VPK-and-loose-tree reader.
//
// The sides are returned at their own sizes because that is what the content
// has: this install's sky_well_01dn.vtf is 1x1 while its five siblings are
// 512x256 and 512x512. A cube needs six equal squares, so the resample is the
// uploader's job and the fact that it happened is a reading, not a detail --
// hence `reason`.
struct WorldSkyFaceCapture {
  std::array<WorldSkyFace, 6> faces{};
  // Parallel to `faces`: index f describes the resource behind faces[f].
  std::array<WorldSkyFaceOrigin, 6> origins{};
  int facesDecoded = 0;
  std::string skyName;
  // Named whenever the capture is short of six, and "the bsp declares no
  // skyname" is a different fallback from "3 of 6 sides decoded".
  std::string reason;
};

WorldSkyFaceCapture captureSkyFaces(const BspMap& map,
  const std::function<std::vector<std::uint8_t>(const std::string&)>& read);

enum class WorldVisClipState { Unavailable, Disabled, Active };
const char* worldVisClipStateName(WorldVisClipState state);

struct WorldVisClipRequest {
  bool enabled = false;
  // The cluster the camera is in; -1 means unknown, and a clip that cannot name
  // the camera's cluster hides nothing rather than guessing.
  int cameraCluster = -1;
};

struct WorldVisClipReading {
  WorldVisClipState state = WorldVisClipState::Unavailable;
  // What the clip did to the geometry. `input - output == hidden` whenever the
  // clip ran, which is the pair a gate asserts: turning the clip on has to
  // change the number of triangles the renderer is given.
  bool clipApplied = false;
  std::size_t inputTriangles = 0;
  std::size_t outputTriangles = 0;
  std::size_t hiddenTriangles = 0;
  // Triangles the pvs cannot speak about: no cluster means no visibility bit,
  // and dropping them would be a claim the data does not support, so they are
  // kept and counted.
  std::size_t trianglesWithoutCluster = 0;
  int cameraCluster = -1;
  std::string reason;
};

// Per-triangle PVS clip. The PVS is a cluster-to-cluster bitmap, so a triangle
// is hidden when its cluster exists and the camera's cluster cannot see it.
//
// `keptIndices` is optional and, when given, receives the input indices of the
// triangles that survived, in order. It exists so a caller that already owns a
// vertex buffer can build a draw list without re-deriving the decision: the
// alternative -- a second function that reports visibility -- would be a second
// implementation of the clip, and a gate that tests that one can pass while
// this one is wrong.
WorldVisClipReading clipWorldTrianglesByVis(const BspMap& map,
  const std::vector<BspTriangle>& input, const WorldVisClipRequest& request,
  std::vector<BspTriangle>& output, std::vector<std::size_t>* keptIndices = nullptr);

// ---------------------------------------------------------------------------
// 6. VIS clipping
// ---------------------------------------------------------------------------

struct WorldVisReading {
  const char* mode = "unavailable";
  int clusters = 0;
  int visibleFromCluster0 = 0;
  // Whether the PVS can answer a question at all. A map with one cluster has a
  // PVS that hides nothing, and "0 hidden" is then a fact about the map rather
  // than about clipping -- hence the separate flag.
  bool queryable = false;
  std::size_t trianglesWithCluster = 0;
  std::size_t trianglesWithoutCluster = 0;
  // The share of the world's triangles that a per-triangle cluster clip could
  // act on. Without this number, "VIS is parsed" and "VIS clips anything" read
  // the same.
  double clusterCoverage = 0.0;
};

WorldVisReading measureVis(const BspMap& map);

// ---------------------------------------------------------------------------
// All six, in the order the task lists them
// ---------------------------------------------------------------------------

struct WorldCapabilityReadings {
  WorldLightmapReading lightmap;
  WorldCubemapReading cubemap;
  WorldSkyReading sky;
  WorldDisplacementReading displacement;
  WorldWaterReading water;
  WorldVisReading vis;
};

WorldCapabilityReadings measureWorldCapabilities(const BspMap& map, const std::string& mapName,
  const WorldResourceProbe& probe, bool reflectionDeclaredBySettings,
  bool refractionDeclaredBySettings, bool waterMaterialDeclared, bool waterTextureReachable,
  const std::vector<std::uint8_t>& bspBytes = {}, std::size_t triangleLimit = 0);

// ---------------------------------------------------------------------------
// Judgement: state + reason per capability
// ---------------------------------------------------------------------------
// One implementation, shared by the probe and the product, for the same reason
// viewmodel_material.cpp exists: a gate that asserts the two agree is only
// non-circular if there is one thing to agree with.
//
// The reason is a measured sentence, not a label. It names the counters the
// state came from, so a reader can re-derive the state from the numbers printed
// next to it instead of taking the word on trust.

struct WorldCapabilityVerdict {
  WorldCapabilityState state = WorldCapabilityState::Unavailable;
  std::string reason;
};

WorldCapabilityVerdict judgeLightmap(const WorldLightmapReading& reading);
WorldCapabilityVerdict judgeCubemaps(const WorldCubemapReading& reading);
WorldCapabilityVerdict judgeSky(const WorldSkyReading& reading);
WorldCapabilityVerdict judgeDisplacement(const WorldDisplacementReading& reading);
WorldCapabilityVerdict judgeWater(const WorldWaterReading& reading);
WorldCapabilityVerdict judgeVis(const WorldVisReading& reading);

// ---------------------------------------------------------------------------
// Synthetic BSP fixtures
// ---------------------------------------------------------------------------
// A fixture is built as real BSP bytes and read back through BspParser::parse,
// so the parser is under test and not the test's own idea of the parser. Every
// knob a mutation needs is a field, because a mutation that has to hunt for a
// byte offset is a mutation that breaks when an unrelated field moves.
//
// Layout notes taken from bsp_map.cpp (the only spec):
//   faces      56 B: +4 firstEdge i32, +8 count i16, +10 infoIndex i16,
//                    +12 dispIndex i16, +16 styles[4] B, +20 lightOffset i32,
//                    +28 minsU i32, +32 minsV i32, +36 samplesW-1 i32,
//                    +40 samplesH-1 i32
//   texInfo    72 B: +0 u axis 4f, +16 v axis 4f, +32 lightmap u axis 4f,
//                    +48 lightmap v axis 4f, +64 flags i32, +68 texDataIndex i32
//   texData    32 B: +12 name table id i32
//   vertices   12 B: 3f
//   edges       4 B: 2 x u16 vertex
//   surfEdges   4 B: i32 (negative reverses)
//   models     48 B: +40 firstFace i32, +44 faceCount i32
//   lighting    4 B per luxel: RGB plus a signed exponent byte (RGBExp32)
//   cubemaps   16 B: 3 x i32 origin, 1 B size
//   dispInfo  176 B: +0 start 3f, +12 vertStart i32, +20 power i32
//   dispVerts  20 B: +0 direction 3f, +12 distance f
//   leafs      32 B: +4 cluster i16, +20 firstFace u16, +22 faceCount u16
//   leafFaces   2 B: u16 face index
//   planes     20 B: normal 3f + dist f
//   nodes      32 B: +0 plane i32, +4 child[2] i32
//   vis         i32 cluster count, then cluster count x i32 offsets, then bits
//   entities   text with a "skyname" "..." pair

struct BspFixtureFace {
  // Quad corners in winding order. Four and only four: the displacement path in
  // the parser requires polygon.size() == 4 and the flat path needs >= 3.
  float corners[4][3] = {};
  // Texture axes: uv = dot(p, axis.xyz) + axis.w.
  float uAxis[4] = {1.0f, 0.0f, 0.0f, 0.0f};
  float vAxis[4] = {0.0f, 1.0f, 0.0f, 0.0f};
  // Lightmap axes, same convention. The parser derives each triangle's light
  // UV from these plus the face's mins/samples box, so a fixture that wants a
  // face's luxels to land inside its own box sets these to match the corners.
  float luAxis[4] = {1.0f, 0.0f, 0.0f, 0.0f};
  float lvAxis[4] = {0.0f, 1.0f, 0.0f, 0.0f};
  // texInfo flags. The parser reads four bits and ignores the rest: bit 2
  // (value 2) and bit 3 (value 4) mark a sky face and the parser skips it
  // entirely (counting it in skyFaces); bit 7 (value 64) and bit 8 (value 128)
  // skip it as a trigger; bit 4 (value 8) marks water. There is no lightmap
  // bit: whether a face has one is decided by styles[0], lightOffset and the
  // samples box alone, which is why the fixture sets those instead.
  std::int32_t flags = 0;
  std::int32_t texDataIndex = 0;
  // Lighting slice for this face. The parser reads samplesW*samplesH*4 bytes at
  // lightOffset, so a fixture appends its luxels to BspFixture::lighting and
  // records where they landed.
  std::int32_t lightOffset = 0;
  std::int32_t minsU = 0;
  std::int32_t minsV = 0;
  // Stored as value-1, matching the file format.
  std::int32_t samplesW = 0;
  std::int32_t samplesH = 0;
  // styles[0] == 255 means "no lightmap on this face" to the parser.
  std::uint8_t style0 = 0;
  std::int16_t dispIndex = -1;
};

struct BspFixture {
  std::string mapName = "fixture";
  // Entity-lump text. When it contains a "skyname" pair the parser picks it up.
  std::string entities;
  std::vector<BspFixtureFace> faces;
  // Raw RGBExp32 luxel bytes, shared by every face through its lightOffset.
  std::vector<std::uint8_t> lighting;
  std::vector<BspCubemapSample> cubemaps;
  // Files to write into the bsp's pakfile lump (40), as a stored ZIP, read back
  // by bspPakfileEntry. This is where TF2 ships a map's own cubemaps, so a
  // fixture that wants to be read the way a real map is read has to carry them
  // the same way -- a fixture that put them anywhere else would test a path the
  // product never takes.
  std::map<std::string, std::vector<std::uint8_t>> pakfileEntries;
  // Displacement: one dispInfo per displacement face, and the dispVerts grid
  // (size x size x 20 bytes) that face indexes. The parser requires the dispInfo
  // start position to be within 1 unit of one of the face's corners.
  struct Displacement {
    BspVertex start{};
    int power = 2;         // 2..4 -> grid size 5..17
    std::int32_t vertStart = 0;
    // Grid rows, each `size` verts. A null direction with distance 0 leaves the
    // face flat; a real one displaces it.
    std::vector<std::array<float, 4>> verts;  // x,y,z direction + distance
  };
  std::vector<Displacement> displacements;
  std::vector<std::uint8_t> dispVertBytes;
  // VIS. clusterCount == 0 leaves the lump absent and the mode Unavailable.
  int clusterCount = 0;
  std::vector<std::int32_t> visOffsets;
  std::vector<std::uint8_t> visBits;
  // Planes/nodes/leafs. Left empty, clusterAt() cannot answer and every face
  // gets cluster -1, which is the state a fixture uses when it wants the
  // "triangle has no cluster" half of the VIS reading.
  struct Leaf { std::int16_t cluster = -1; std::vector<std::uint16_t> faces; };
  std::vector<Leaf> leaves;
  // BSP tree for clusterAt(): a single plane splitting on x at `splitX`, with
  // child 0 -> leaf 0 and child 1 -> leaf 1. Enough to make the camera's cluster
  // answerable without a full tree.
  bool withTree = false;
  float splitX = 0.0f;
};

// Build the BSP bytes. The result is what a real .bsp file would be, so the only
// thing between a fixture and a reading is BspParser::parse.
std::vector<std::uint8_t> buildFixtureBsp(const BspFixture& fixture);

// Append one RGBExp32 luxel (r,g,b and a signed exponent) to a lighting vector
// and return the offset it landed at. A fixture that wants face 0's first luxel
// to be a known colour uses this and then mutates the byte at that offset.
std::int32_t appendLuxel(std::vector<std::uint8_t>& lighting, std::uint8_t r, std::uint8_t g,
  std::uint8_t b, std::int8_t exponent);

} // namespace tf2::native
