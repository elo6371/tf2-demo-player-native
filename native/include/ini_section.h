#pragma once

#include <filesystem>
#include <string>
#include <utility>
#include <vector>

namespace tf2::native {

// ---------------------------------------------------------------------------
// Whole-section INI access (T50).
//
// The product writes private-profile INI files through
// WritePrivateProfileStringW, and every one of those writers had its own idea
// of how to reach a section. That is fine while a file has one owner; it stops
// being fine once [renderer], [camera] and [keys] live in the same file, because
// a writer that rewrites the whole file from an empty buffer silently deletes
// the sections it does not know about. One pair of functions reads and writes
// one section, leaves every other section alone, and reports failure instead of
// half-writing.
//
// A section is an ordered list of (key, value) pairs; "key" and "value" are
// wide strings because that is what the Win32 profile API speaks. The parser
// splits on the FIRST '=' so a value may contain '=' (a bookmark name or a
// path with a query string), and it never trims anything: WritePrivateProfile
// has its own opinion about leading/trailing spaces and the caller's sanitiser
// is the place that decides what is legal.
using IniSection = std::vector<std::pair<std::wstring, std::wstring>>;

// Reads every key in `section`. Returns false when the file cannot be read at
// all (missing file is not an error: the section is simply empty and true is
// returned) or the section cannot be enumerated within a sane buffer growth.
bool readIniSection(const std::filesystem::path& file, const std::wstring& section,
  IniSection& out);

// Replaces the whole section with `values` and flushes it. Any other section in
// the file is preserved byte for byte by the profile API. An empty list deletes
// the section.
bool writeIniSection(const std::filesystem::path& file, const std::wstring& section,
  const IniSection& values);

// Deletes the section and nothing else. Returns true when it is gone afterwards
// (a section that was never there counts as removed).
bool removeIniSection(const std::filesystem::path& file, const std::wstring& section);

// Every section name in the file, in file order.
std::vector<std::wstring> iniSectionNames(const std::filesystem::path& file);

// Copies every section except \p except [\p except] from \p from into \p to.
// Used by the settings writer, which rebuilds settings.ini through a temporary
// file: without this it would replace the whole file with [renderer] alone and
// silently delete [camera] and [keys] on every exit.
bool copyIniSectionsExcept(const std::filesystem::path& from, const std::filesystem::path& to,
  const std::wstring& except);

// The value for `key` in an already-read section, or `fallback` when absent.
const std::wstring& iniSectionValue(const IniSection& section, const std::wstring& key,
  const std::wstring& fallback);

// Narrow/wide bridges for callers whose keys are UTF-8 std::strings. A value that
// is not representable is refused (returned false), never silently transliterated.
bool wideToUtf8(const std::wstring& wide, std::string& out);
bool utf8ToWide(const std::string& utf8, std::wstring& out);

} // namespace tf2::native
