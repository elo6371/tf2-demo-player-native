#include "ini_section.h"

#include <Windows.h>

#include <algorithm>

namespace tf2::native {

namespace {

const std::wstring kMissing;

// GetPrivateProfileSectionW reports "the buffer was too small" by returning
// size-2, so the loop grows the buffer until it fits rather than trusting one
// fixed 4 KiB buffer: a HUD/key section is small, but a hand-edited file is not
// bounded by what this program writes.
bool enumerate(const std::filesystem::path& file, const std::wstring& section,
  std::vector<wchar_t>& buffer) {
  for (int attempt = 0; attempt < 8; ++attempt) {
    const DWORD copied = GetPrivateProfileSectionW(section.c_str(), buffer.data(),
      static_cast<DWORD>(buffer.size()), file.c_str());
    if (copied == 0) return true;                       // empty or missing section
    if (copied < buffer.size() - 2) return true;         // fits, NUL-terminated
    buffer.resize(buffer.size() * 2);
  }
  return false;
}

} // namespace

bool readIniSection(const std::filesystem::path& file, const std::wstring& section,
  IniSection& out) {
  out.clear();
  if (file.empty()) return false;
  std::error_code error;
  if (!std::filesystem::exists(file, error)) return true;  // no file = empty section
  std::vector<wchar_t> buffer(4096);
  if (!enumerate(file, section, buffer)) return false;
  for (const wchar_t* cursor = buffer.data(); *cursor != L'\0';) {
    const std::wstring line(cursor);
    const auto split = line.find(L'=');
    if (split != std::wstring::npos) {
      out.emplace_back(line.substr(0, split), line.substr(split + 1));
    }
    cursor += line.size() + 1;
  }
  return true;
}

bool writeIniSection(const std::filesystem::path& file, const std::wstring& section,
  const IniSection& values) {
  if (file.empty()) return false;
  std::error_code error;
  const auto parent = file.parent_path();
  if (!parent.empty()) std::filesystem::create_directories(parent, error);
  // Delete first: "replace the section" must also remove keys the caller no
  // longer lists, and the profile API has no other way to say that.
  if (!WritePrivateProfileStringW(section.c_str(), nullptr, nullptr, file.c_str())) return false;
  for (const auto& entry : values) {
    if (entry.first.empty()) continue;
    if (!WritePrivateProfileStringW(section.c_str(), entry.first.c_str(),
        entry.second.c_str(), file.c_str())) return false;
  }
  // Flush the profile cache so a later read of the same path sees these bytes.
  WritePrivateProfileStringW(nullptr, nullptr, nullptr, file.c_str());
  return true;
}

bool removeIniSection(const std::filesystem::path& file, const std::wstring& section) {
  if (file.empty()) return false;
  WritePrivateProfileStringW(section.c_str(), nullptr, nullptr, file.c_str());
  WritePrivateProfileStringW(nullptr, nullptr, nullptr, file.c_str());
  std::error_code error;
  if (!std::filesystem::exists(file, error)) return true;
  IniSection now;
  if (!readIniSection(file, section, now)) return false;
  return now.empty();
}

std::vector<std::wstring> iniSectionNames(const std::filesystem::path& file) {
  std::vector<std::wstring> names;
  if (file.empty()) return names;
  std::vector<wchar_t> buffer(4096);
  for (int attempt = 0; attempt < 8; ++attempt) {
    const DWORD copied = GetPrivateProfileSectionNamesW(buffer.data(),
      static_cast<DWORD>(buffer.size()), file.c_str());
    if (copied == 0) return names;
    if (copied < buffer.size() - 2) {
      for (const wchar_t* cursor = buffer.data(); *cursor != L'\0';) {
        const std::wstring name(cursor);
        names.push_back(name);
        cursor += name.size() + 1;
      }
      return names;
    }
    buffer.resize(buffer.size() * 2);
  }
  return names;
}

bool copyIniSectionsExcept(const std::filesystem::path& from, const std::filesystem::path& to,
    const std::wstring& except) {
  if (from.empty() || to.empty()) return false;
  std::error_code error;
  if (!std::filesystem::exists(from, error)) return true;
  for (const auto& name : iniSectionNames(from)) {
    if (name == except) continue;
    IniSection values;
    if (!readIniSection(from, name, values)) return false;
    if (values.empty()) continue;
    if (!writeIniSection(to, name, values)) return false;
  }
  return true;
}

const std::wstring& iniSectionValue(const IniSection& section, const std::wstring& key,
  const std::wstring& fallback) {
  for (const auto& entry : section) {
    if (entry.first == key) return entry.second;
  }
  return fallback;
}

bool wideToUtf8(const std::wstring& wide, std::string& out) {
  out.clear();
  if (wide.empty()) return true;
  const int length = static_cast<int>(wide.size());
  const int needed = WideCharToMultiByte(CP_UTF8, 0, wide.data(), length, nullptr, 0, nullptr, nullptr);
  if (needed <= 0) return false;
  out.resize(static_cast<std::size_t>(needed));
  if (WideCharToMultiByte(CP_UTF8, 0, wide.data(), length, out.data(), needed, nullptr, nullptr) != needed) {
    out.clear();
    return false;
  }
  return true;
}

bool utf8ToWide(const std::string& utf8, std::wstring& out) {
  out.clear();
  if (utf8.empty()) return true;
  const int length = static_cast<int>(utf8.size());
  const int needed = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), length, nullptr, 0);
  if (needed <= 0) return false;
  out.resize(static_cast<std::size_t>(needed));
  if (MultiByteToWideChar(CP_UTF8, 0, utf8.data(), length, out.data(), needed) != needed) {
    out.clear();
    return false;
  }
  return true;
}

} // namespace tf2::native
