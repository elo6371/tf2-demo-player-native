#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace tf2::native {

// ---------------------------------------------------------------------------
// Custom HUD import: validation, layout parsing and screen-space resolution.
//
// WHAT THIS IS, AND WHAT IT IS NOT
//   This module imports a *single VGUI-style layout document* -- the shape of a
//   Source .res file -- and turns its panels into screen-space rectangles the
//   renderer can fill. It does NOT implement Source's VGUI runtime, and it does
//   not claim to. The supported subset, and every refusal it can name, is
//   written in native/docs/hud-supported-formats.md; that document and this
//   header are the same claim and must not drift apart.
//
//   In particular: no #base includes, no font rasterisation, no image
//   rasterisation, no text layout, no animations, no script (.nut) execution,
//   and no 'o'/'p'/'s' coordinates. Each of those has a named refusal rather
//   than a silent approximation, because a HUD that renders 'almost right' is
//   worse evidence than one that says which feature it does not have.
//
//   Nesting IS supported: a child panel's coordinates are relative to its
//   parent's resolved rectangle, which is Source's rule and which the real
//   m0rehud corpus on this machine needs (it reaches brace depth 7).
//
// WHY THE PURE PART IS SEPARATE FROM THE RENDERER
//   Every rule here is a total function over strings and integers: path safety,
//   the KeyValues subset, the coordinate grammar, and the rectangle arithmetic
//   that makes HUD scaling and resolution adaptation measurable. Keeping them
//   free of D3D and of Win32 means hud_probe can assert them without a device
//   and without a window, and the product calls the same functions, so a green
//   probe is a statement about the code the product runs.
// ---------------------------------------------------------------------------

// Every way an import can be refused. A refusal is a value, not a message: the
// UI shows hudRefusalName(), the trace prints the same token, and a gate can
// assert on the token instead of on prose.
enum class HudRefusal {
  None = 0,
  EmptyPath,
  AbsolutePath,          // "C:\\x", "/x", "\\\\server\\share"
  DriveRelative,         // "C:x" -- resolves against that drive's cwd
  ParentTraversal,       // ".." anywhere in the relative path
  AlternateStream,       // "file.txt:stream" (NTFS ADS)
  TrailingDotOrSpace,    // "dir." or "dir " -- Windows silently strips these
  ControlCharacter,      // any byte < 0x20 or 0x7f in the name
  ReservedDeviceName,    // CON, PRN, AUX, NUL, COM1..9, LPT1..9
  PathTooLong,           // over 240 characters in the relative part
  NotInsideRoot,         // canonical path escapes the imported root
  Missing,
  NotARegularFile,
  TooLarge,
  TooManyFiles,
  InvalidUtf8,
  UnsupportedExtension,
  MalformedKeyValues,
  UnsupportedInclude,
  NestingTooDeep,
  TooManyPanels,
  DuplicatePanelName,
  MissingDocumentName,
  MissingRootPanel,
  UnsupportedCoordinate,
  ZeroExtent,
  IoError,
};

const char* hudRefusalName(HudRefusal reason);

// Limits. Named rather than inlined so the gate can assert the product refuses
// a file one byte over the cap instead of guessing where the cap is.
struct HudLimits {
  std::uint64_t maxFileBytes = 256u * 1024u;
  std::size_t maxFiles = 64;
  std::size_t maxPanels = 512;
  // Real HUDs nest: the m0rehud corpus on this machine reaches brace depth 7
  // (hudplayerhealth.res, hudtournament.res). Nesting is supported, not refused,
  // because a child panel is positioned relative to its parent -- that is the
  // Source semantics and implementing it is cheaper than explaining a hole. The
  // cap exists to bound the recursion, not to model a restriction.
  unsigned maxNesting = 16;
};

// ---- path safety ----------------------------------------------------------

// A path that has been proven relative, non-traversing and inside a root.
struct HudSafePath {
  bool ok = false;
  HudRefusal reason = HudRefusal::EmptyPath;
  std::string text;               // the sanitised relative text, '/'-separated
};

// Purely textual: rejects absolute, drive-relative, UNC, "..", ADS, control
// characters, reserved device names, trailing dots/spaces and over-long paths.
// Separators may be '\\' or '/'; the result always uses '/'.
HudSafePath sanitizeHudRelativePath(const std::string& text);

// Adds the filesystem half of the rule: the resolved path must still sit under
// the resolved root. Returns NotInsideRoot when a symlink/reparse point or a
// hand-edited root makes the textual answer insufficient.
HudSafePath resolveInsideRoot(const std::filesystem::path& root, const std::string& text);

// ---- file reading ---------------------------------------------------------

struct HudFileRead {
  bool ok = false;
  HudRefusal reason = HudRefusal::IoError;
  std::string text;          // UTF-8, BOM removed, nothing else transformed
  std::uint64_t bytes = 0;   // bytes on disk, before BOM removal
  bool hadBom = false;
};

// Reads one regular file under the given root. Refuses a non-regular file, a
// file over limits.maxFileBytes (checked on the OS-reported size AND on the
// bytes actually read, because a growing file would otherwise slip past), and
// any content that is not valid UTF-8. A BOM is honoured and removed; nothing
// else is rewritten, so a gate can compare the text this returns against the
// file on disk.
HudFileRead readHudFile(const std::filesystem::path& root, const std::string& relative,
  const HudLimits& limits = {});

// ---- the coordinate grammar ----------------------------------------------

// Source VGUI writes a coordinate as a string whose first character selects the
// anchor. The supported subset is deliberately small and named per form:
//   "0", "-175", "+12"   pixels from the parent's left/top edge, scaled by dpi
//   "c95", "c-175"       centred: (parent - self)/2 + value
//   "r10", "r-4"         right/bottom anchored: parent - self - value
//   "f0", "f0.5"         fraction of the parent extent
//   "50%", "-25%"        percentage of the parent extent
//   "o3"                 REFUSED: parent-relative offset needs the live tree
//   "p0.5"               REFUSED: proportional needs the design resolution
//   "s0.5"               REFUSED: "scaled" needs Source's own base resolution
enum class HudCoordUnit {
  Pixels, Center, Right, Fraction, Percent,
  ParentOffset,        // 'o' -- needs the live panel tree, refused
  ParentProportional,  // 'p' -- needs Source's design resolution, refused
  Scaled,              // 's' -- needs Source's base resolution, refused
  Invalid,
};

struct HudCoord {
  bool ok = false;
  HudCoordUnit unit = HudCoordUnit::Invalid;
  float value = 0.0f;
  std::string raw;
};

HudCoord parseHudCoord(const std::string& text);

// Resolves one coordinate to whole pixels. parentExtent is the client width or
// height, selfExtent the panel's own resolved width or height (only the Center
// and Right anchors need it). scalePercent applies to the pixel and centre
// anchors only: a fractional or percentage coordinate is already
// resolution-relative, so scaling it again would be a second, unstated rule.
// Returns false for a coordinate parseHudCoord refused, and for ParentOffset /
// ParentProportional / Scaled, which this build does not resolve.
bool resolveHudCoord(const HudCoord& coord, int parentExtent, int selfExtent,
  float scalePercent, int& out);

// ---- the layout document --------------------------------------------------

struct HudPanel {
  std::string name;          // the block's name in the document
  std::string controlName;   // "ControlName"
  std::string fieldName;     // "fieldName"
  std::string labelText;     // "labelText", "%Health%" style tokens kept verbatim
  std::string font;          // declared only: this build rasterises no font
  std::string textAlignment;
  std::string image;         // declared only: this build rasterises no image
  HudCoord xpos, ypos, wide, tall;
  int zpos = 0;
  bool visible = true;
  bool enabled = true;
  // Index of the enclosing panel in HudLayout::panels, or -1 for a panel whose
  // parent is the document. Panels are stored in pre-order, so a parent always
  // precedes its children and resolution can walk the vector once.
  int parent = -1;
  unsigned depth = 1;   // 1 = a direct child of the document
  // Keys whose values this build understood. Everything else is counted in
  // HudLayout::unknownKeys so "we ignored 40 keys" is a reading, not a surprise.
  std::size_t knownKeys = 0;
  std::size_t unknownKeys = 0;
};

struct HudLayout {
  bool ok = false;
  HudRefusal reason = HudRefusal::None;
  std::string detail;        // which panel or key caused a refusal
  std::string documentName;  // the first token, e.g. "Resource/UI/MyHud.res"
  std::string rootName;
  std::vector<HudPanel> panels;
  std::size_t refusedPanels = 0;
  std::size_t unknownKeys = 0;
  std::size_t includeCount = 0;
  std::size_t nestingDepth = 0;
  std::size_t duplicateNames = 0;
  bool zeroExtentPanel = false;
};

// Parses the document. Structural refusals (no document name, no root block, an
// include, too much nesting, too many panels) fail the whole import: a HUD that
// is only partly what it claims is worse than one that says no. Per-panel
// problems (one unsupported coordinate) refuse that panel and are counted, so a
// HUD with one broken control still previews with the others and reports one
// refused panel rather than looking complete.
HudLayout parseHudLayout(const std::string& text, const HudLimits& limits = {});

// ---- screen-space rectangles ---------------------------------------------

struct HudScreenRect {
  bool valid = false;
  // The panel resolved but the document says it is not shown. Kept separate from
  // `valid` on purpose: "the geometry is wrong" and "the document hid it" are
  // different findings, and a HUD that reported the second as the first would
  // look broken when it was following its own instructions.
  bool visible = true;
  HudRefusal reason = HudRefusal::None;
  int x = 0, y = 0, w = 0, h = 0;
  std::size_t panel = 0;   // index into HudLayout::panels
};

// Resolves every visible panel into client pixels. screenWidth/screenHeight are
// physical pixels (what the swap chain uses), dpiScale is windowDpi/96 and is
// folded into the pixel anchors only, scalePercent is the user's hud.scale.
// Panels that resolve to no area, or whose coordinates this build refuses, come
// back with valid=false and a reason; they are never silently dropped.
std::vector<HudScreenRect> resolveHudRects(const HudLayout& layout, int screenWidth,
  int screenHeight, float dpiScale, int scalePercent);

// The union of every VALID and VISIBLE rectangle, which is the region a HUD is
// allowed to own. Outside it the captured frame must be byte-identical to a
// frame drawn with no HUD at all -- that is the assertion this function exists
// to make checkable rather than approximate. An invisible panel contributes
// nothing: it is not drawn, so excluding its rectangle would hand the pass an
// alibi it never uses.
struct HudPixelRect { int x = 0, y = 0, w = 0, h = 0; bool empty = true; };
HudPixelRect hudUnionRect(const std::vector<HudScreenRect>& rects);

// The [hud] settings block, in ui.ini, per the shared contract (T50/T51
// camera-ui-contract.md 1.3). Defaults are the contract's defaults: disabled,
// preview on, scale 100.
struct HudSettings {
  bool enabled = false;
  std::string root;      // absolute directory the user imported
  std::string layout;    // path relative to root
  int scalePercent = 100;
  bool preview = true;
};

// Clamps to the contract's 50..200 window; a value outside it is a malformed
// file, and the clamp is reported so the UI can say the file was repaired.
int clampHudScale(int scalePercent);
bool hudScaleInRange(int scalePercent);

} // namespace tf2::native
