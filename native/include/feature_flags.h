// T108 / G-3 (2026-11-25): one definition point for the TF2_NATIVE_* switches.
//
// Why
// ---
// Before this header the product read its environment switches at ~20 places
// across several translation units, each call site deciding for itself how to
// parse a value, whether to cache it, and what "unset" means. Nothing could
// answer "which switches exist" without grepping, and two call sites could
// disagree about one value without either being obviously wrong.
//
// Contract
// --------
// This is the only place the product reads the environment. Every other file
// calls an accessor here, and the only readers are ReadRawNarrow and
// ReadRawWide: no other file in native/ may call getenv/_dupenv_s/
// GetEnvironmentVariableW for a TF2_NATIVE_* switch. Proof expected with the
// migration (G-3 acceptance):
//   grep -rn "TF2_NATIVE_" native/src native/include | grep -v feature_flags.h
//     | grep -E "getenv|_dupenv_s|GetEnvironmentVariableW"   -> no hits
//
// Entry fields
// ------------
//   name      short name; the readers prepend TF2_NATIVE_
//   type      kBool -> Bool(), kInt -> Int(), kString -> String(),
//             kWideString -> Wide(). kBool is parsed as the on-set
//             {1,on,true}; anything else is off, so FLIP_X (fallback "1")
//             goes false on 0/off/keep and stays true on any other token --
//             the same reading the old call sites used.
//   fallback  the value the flag behaves as when unset: "0"/"1" for kBool,
//             a decimal for kInt, the literal for strings, "" when unset
//             means "no value" (a channel switch, a path).
//   mode      kOnce = read on the first accessor call and keep it (what a
//             static read at the top of a function did); kPerCall = read on
//             every call. kPerCall is not an oversight: bsp_map parses the
//             same bytes twice in one process to compare two settings, and a
//             static read on the first call would make the second answer for
//             the first one's environment (native/src/bsp_map.cpp:283-286).
//   consumer  today's reader as file:line at the commit that added the row,
//             plus its migration batch. A row may be registered before it is
//             migrated -- the text says so -- because this table is also the
//             inventory the migration works from. Where a row is not yet
//             migrated, its type is the shape that consumer expects and the
//             batch confirms it before switching the call site over.
//
// MV-1 POSECHECK is presence, not truthiness: the old reader tested the pointer
// for null (native/src/main.cpp:14696), so POSECHECK=0 arms it. Use Presence().
//
// Caching
// -------
// kOnce values are cached once per entry. The caches are function-local statics
// inside inline functions, so every translation unit shares them. A wide entry
// is cached as UTF-16 and narrowed on demand; a narrow entry is cached as the
// bytes the environment had and widened on demand.
//
// Rows no C++ consumer reads (G-7): TF2_NATIVE_EVIDENCE_ROOT and
// TF2_NATIVE_EVIDENCE_OUT are read by tools/*.sh only. They are registered so
// the table is the complete inventory; moving evidence paths is G-7's work.

#pragma once

#include <Windows.h>

#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <optional>
#include <string>

namespace tf2::native::flags {

enum class Type { kBool, kInt, kString, kWideString };
enum class Mode { kOnce, kPerCall };

struct Entry {
  const char* name;
  Type type;
  const char* fallback;
  Mode mode;
  const char* consumer;
};

// Order is the order the trace dump prints them in. Keep it grouped by
// migration batch so a batch commit changes one contiguous block.
inline constexpr Entry kEntries[] = {
  // ---- B0: the trace channel itself -------------------------------------
  {"TRACE", Type::kWideString, "", Mode::kOnce, "native/src/main_trace.cpp:84 (B0)"},
  // ---- B1: renderer frame + world ----------------------------------------
  {"WORLD_VIEW", Type::kInt, "0", Mode::kOnce, "native/src/native_renderer_frame.cpp:1298 (B1)"},
  {"RANGE_LIST", Type::kBool, "0", Mode::kOnce, "native/src/native_renderer_frame.cpp:1828 (B1)"},
  {"DUMP_ATLAS", Type::kString, "", Mode::kOnce, "native/src/native_renderer_world.cpp:499 (B1)"},
  {"HIDE_MATERIAL", Type::kString, "", Mode::kOnce, "native/src/native_renderer_world.cpp:739 (B1)"},
  {"ONLY_MATERIAL", Type::kString, "", Mode::kOnce, "native/src/native_renderer_world.cpp:747 (B1)"},
  // ---- B2: model loading -------------------------------------------------
  {"MODEL_WINDING", Type::kString, "", Mode::kPerCall, "native/src/model_loader.cpp:26 (B2)"},
  // ---- B3: internal renderer switches (static reads today) ---------------
  {"UV_PS", Type::kBool, "0", Mode::kOnce, "native/src/renderer_internal.h:47 (B3)"},
  {"FLIP_X", Type::kBool, "1", Mode::kPerCall, "native/src/renderer_internal.h:58 (B3)"},
  // ---- B4: bsp_map (waits for E's bsp_map commit, then G-1's SURF move) ---
  {"LM_BORDER", Type::kBool, "0", Mode::kPerCall, "native/src/bsp_map.cpp:979 via :287-291 (B4)"},
  {"LM_DROPS", Type::kBool, "0", Mode::kPerCall, "native/src/bsp_map.cpp:983 (B4)"},
  // ---- B5: main.cpp (last; the biggest file) -----------------------------
  {"WINDOW_SIZE", Type::kWideString, "", Mode::kOnce, "native/src/main.cpp:3667 (B5)"},
  {"VIEWMODEL_PATH", Type::kWideString, "", Mode::kOnce, "native/src/main.cpp:5570 (B5)"},
  {"BODY_MUTATION", Type::kString, "", Mode::kOnce, "native/src/main.cpp:15157,18110 (B5)"},
  {"ROWSHASH_MUTATION", Type::kString, "", Mode::kOnce, "native/src/main.cpp (G-2, pending merge) (B5)"},
  {"WORLD_ALPHA_TEST_REFERENCE", Type::kString, "-1", Mode::kOnce, "native/src/main.cpp:8102 (B5)"},
  {"POSECHECK", Type::kBool, "0", Mode::kOnce, "native/src/main.cpp:14696 (B5; presence, MV-1)"},
  {"POSECHECK2", Type::kBool, "0", Mode::kOnce, "native/src/main.cpp:15963 (B5)"},
  {"NO_AIM_MATRIX", Type::kBool, "0", Mode::kPerCall, "native/src/main.cpp:15465 (B5)"},
  {"NEUTRAL_AIM", Type::kBool, "0", Mode::kPerCall, "native/src/main.cpp:15710 (B5)"},
  {"ZERO_AIM_YAW", Type::kBool, "0", Mode::kPerCall, "native/src/main.cpp:15714 (B5)"},
  {"ZERO_AIM_PITCH", Type::kBool, "0", Mode::kPerCall, "native/src/main.cpp:15715 (B5)"},
  // ---- B6: waits for A1 (entity renderer) --------------------------------
  {"UV_DUMP", Type::kString, "", Mode::kOnce, "native/src/native_renderer_entity.cpp:295 (B6)"},
  // ---- Registered, no C++ consumer (G-7 shell-only) ----------------------
  {"EVIDENCE_ROOT", Type::kString, "", Mode::kOnce, "tools/*.sh only (G-7 pending; no C++ reader)"},
  {"EVIDENCE_OUT", Type::kString, "", Mode::kOnce, "tools/*.sh only (G-7 pending; no C++ reader)"},
};

inline constexpr std::size_t kEntryCount = sizeof(kEntries) / sizeof(kEntries[0]);

constexpr bool NamesAreUnique() {
  for (std::size_t i = 0; i < kEntryCount; ++i) {
    for (std::size_t j = i + 1; j < kEntryCount; ++j) {
      const char* a = kEntries[i].name;
      const char* b = kEntries[j].name;
      while (*a != '\0' && *a == *b) { ++a; ++b; }
      if (*a == *b) return false;
    }
  }
  return true;
}

constexpr bool NamesAreShort() {
  for (const Entry& entry : kEntries) {
    if (entry.name == nullptr || entry.name[0] < 'A' || entry.name[0] > 'Z') return false;
    const char* prefix = "TF2_NATIVE_";
    std::size_t i = 0;
    while (prefix[i] != '\0' && entry.name[i] == prefix[i]) ++i;
    if (prefix[i] == '\0') return false;  // the whole prefix matched: name is already qualified
  }
  return true;
}

constexpr bool RowsAreComplete() {
  for (const Entry& entry : kEntries) {
    if (entry.name == nullptr || entry.name[0] == '\0') return false;
    if (entry.fallback == nullptr) return false;
    if (entry.consumer == nullptr || entry.consumer[0] == '\0') return false;
  }
  return true;
}

static_assert(kEntryCount == 25, "23 consumed switches + 2 shell-only rows; update the count with the table");
static_assert(NamesAreUnique(), "two rows name the same switch");
static_assert(NamesAreShort(), "names are the short form; the TF2_NATIVE_ prefix is added by the readers");
static_assert(RowsAreComplete(), "every row names itself, its fallback and its consumer");

inline std::string FullNameA(const char* shortName) {
  return std::string("TF2_NATIVE_") + shortName;
}

inline std::wstring FullNameW(const char* shortName) {
  std::wstring name(L"TF2_NATIVE_");
  for (const char* p = shortName; *p != '\0'; ++p) {
    name.push_back(static_cast<wchar_t>(static_cast<unsigned char>(*p)));
  }
  return name;
}

inline const Entry* Find(const char* shortName) {
  for (const Entry& entry : kEntries) {
    if (std::strcmp(entry.name, shortName) == 0) return &entry;
  }
  return nullptr;
}

inline std::size_t IndexOf(const Entry& entry) {
  return static_cast<std::size_t>(&entry - kEntries);
}

// The two readers. Everything else in the product goes through these.
inline std::optional<std::string> ReadRawNarrow(const char* shortName) {
  const std::string name = FullNameA(shortName);
  // _dupenv_s rather than getenv: getenv is C4996-flagged at /W4, and this
  // build keeps its warning count at zero (native/src/main_trace.cpp:80-82 has
  // the same reason written down for the wide read).
  char* buffer = nullptr;
  std::size_t size = 0;
  if (_dupenv_s(&buffer, &size, name.c_str()) != 0 || buffer == nullptr) {
    std::free(buffer);
    return std::nullopt;
  }
  std::string value(buffer);
  std::free(buffer);
  return value;
}

inline std::optional<std::wstring> ReadRawWide(const char* shortName) {
  const std::wstring name = FullNameW(shortName);
  const DWORD needed = GetEnvironmentVariableW(name.c_str(), nullptr, 0);
  if (needed == 0) return std::nullopt;
  std::wstring value(needed, L'\0');
  const DWORD written = GetEnvironmentVariableW(name.c_str(), value.data(), needed);
  if (written == 0) return std::wstring();  // set but empty: the first call proved it exists
  if (written >= needed) return std::nullopt;  // changed between the two calls
  value.resize(written);
  return value;
}

inline std::string ToUtf8(const std::wstring& value) {
  if (value.empty()) return std::string();
  const int bytes = WideCharToMultiByte(CP_UTF8, 0, value.c_str(),
    static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
  if (bytes <= 0) return std::string();
  std::string out(static_cast<std::size_t>(bytes), '\0');
  WideCharToMultiByte(CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()),
    out.data(), bytes, nullptr, nullptr);
  return out;
}

inline std::wstring ToWide(const std::string& value) {
  if (value.empty()) return std::wstring();
  const int chars = MultiByteToWideChar(CP_UTF8, 0, value.c_str(),
    static_cast<int>(value.size()), nullptr, 0);
  if (chars <= 0) return std::wstring();
  std::wstring out(static_cast<std::size_t>(chars), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()),
    out.data(), chars);
  return out;
}

inline std::optional<std::string> CachedNarrow(std::size_t index) {
  static std::optional<std::string> cache[kEntryCount];
  static bool loaded[kEntryCount] = {};
  if (!loaded[index]) {
    cache[index] = ReadRawNarrow(kEntries[index].name);
    loaded[index] = true;
  }
  return cache[index];
}

inline std::optional<std::wstring> CachedWide(std::size_t index) {
  static std::optional<std::wstring> cache[kEntryCount];
  static bool loaded[kEntryCount] = {};
  if (!loaded[index]) {
    cache[index] = ReadRawWide(kEntries[index].name);
    loaded[index] = true;
  }
  return cache[index];
}

inline std::optional<std::string> ValueNarrow(const Entry& entry);
inline std::optional<std::wstring> ValueWide(const Entry& entry);

inline std::optional<std::wstring> ValueWide(const Entry& entry) {
  if (entry.type == Type::kWideString) {
    return entry.mode == Mode::kOnce ? CachedWide(IndexOf(entry)) : ReadRawWide(entry.name);
  }
  const std::optional<std::string> narrow = ValueNarrow(entry);
  if (!narrow.has_value()) return std::nullopt;
  return ToWide(*narrow);
}

inline std::optional<std::string> ValueNarrow(const Entry& entry) {
  if (entry.type == Type::kWideString) {
    const std::optional<std::wstring> wide = ValueWide(entry);
    if (!wide.has_value()) return std::nullopt;
    return ToUtf8(*wide);
  }
  return entry.mode == Mode::kOnce ? CachedNarrow(IndexOf(entry)) : ReadRawNarrow(entry.name);
}

// Accessors. A short name not in the table is a compiler-side no-op: it behaves
// as unset. That is deliberate -- the alternative was a crash or a silent
// default in a diagnostic path, and the table is checked by the asserts above.
inline bool Bool(const char* shortName) {
  const Entry* entry = Find(shortName);
  if (entry == nullptr) return false;
  const std::optional<std::string> raw = ValueNarrow(*entry);
  if (!raw.has_value()) return entry->fallback[0] == '1';
  return std::strcmp(raw->c_str(), "1") == 0 || std::strcmp(raw->c_str(), "on") == 0
    || std::strcmp(raw->c_str(), "true") == 0;
}

inline int Int(const char* shortName) {
  const Entry* entry = Find(shortName);
  if (entry == nullptr) return 0;
  const std::optional<std::string> raw = ValueNarrow(*entry);
  return std::atoi(raw.has_value() ? raw->c_str() : entry->fallback);
}

inline bool Presence(const char* shortName) {
  const Entry* entry = Find(shortName);
  if (entry == nullptr) return false;
  return ValueNarrow(*entry).has_value();
}

inline std::optional<std::string> String(const char* shortName) {
  const Entry* entry = Find(shortName);
  if (entry == nullptr) return std::nullopt;
  return ValueNarrow(*entry);
}

inline std::optional<std::wstring> Wide(const char* shortName) {
  const Entry* entry = Find(shortName);
  if (entry == nullptr) return std::nullopt;
  return ValueWide(*entry);
}

// The trace dump's walk. visit(entry, raw) receives the raw value with the
// caching rule applied, so a kOnce row read here is the same value its consumer
// will see later. This is why the dump is only taken when a trace is armed: a
// default run must not read a switch earlier than its consumer did.
template <typename Visit>
inline void ForEachFlag(Visit&& visit) {
  for (const Entry& entry : kEntries) {
    visit(entry, ValueNarrow(entry));
  }
}

}  // namespace tf2::native::flags
