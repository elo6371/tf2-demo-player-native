#include "native_renderer.h"
#include "asset_root.h"
#include "settings_store.h"
#include "vpk_archive.h"
#include "vtf_texture.h"
#include "vmt_material.h"
#include "bsp_map.h"
#include "demo_header.h"
#include "audio_player.h"
#include "model_loader.h"
#include "item_schema.h"

#include <Windows.h>
#include <bcrypt.h>
#include <dxgi.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <cctype>
#include <cstring>
#include <fstream>
#include <shellapi.h>
#include <sstream>
#include <vector>
#include <memory>
#include <unordered_map>
#include <unordered_set>

namespace {
tf2::native::Renderer* g_renderer = nullptr;
bool g_orbiting = false;
POINT g_lastMouse{};

// Convert 3x4 poseToBone matrix to 4x4 for GPU upload
std::array<float, 16> poseToBoneToMatrix4x4(const std::array<float, 12>& poseToBone) {
  // poseToBone is stored as [row0(4), row1(4), row2(4)]
  // Convert to column-major 4x4 for HLSL
  return {
    poseToBone[0], poseToBone[4], poseToBone[8],  0.0f,
    poseToBone[1], poseToBone[5], poseToBone[9],  0.0f,
    poseToBone[2], poseToBone[6], poseToBone[10], 0.0f,
    poseToBone[3], poseToBone[7], poseToBone[11], 1.0f
  };
}

struct PlaybackState {
  bool enabled = false;
  bool paused = false;
  bool reverse = false;
  double speed = 1.0;
  double tickRate = 66.6666667;
  std::int32_t tick = 0;
  std::int32_t endTick = 0;
  std::int32_t anchorLookupTick = -1;
  bool anchorValid = false;
  tf2::native::DemoIndexEntry anchor{};
};
PlaybackState g_playback{};
constexpr double kTargetFrameSeconds = 1.0 / 120.0;

const wchar_t* entitySnapshotStatusName(tf2::native::EntitySnapshotQueryStatus status) {
  switch (status) {
    case tf2::native::EntitySnapshotQueryStatus::Available: return L"available";
    case tf2::native::EntitySnapshotQueryStatus::NoHistory: return L"no-history";
    case tf2::native::EntitySnapshotQueryStatus::TickBeforeHistory: return L"before-window";
    case tf2::native::EntitySnapshotQueryStatus::Gap: return L"gap";
    case tf2::native::EntitySnapshotQueryStatus::DeltaBaseMissing: return L"delta-base-missing";
    default: return L"unknown";
  }
}

void addSteamRegistryRoots(std::vector<std::filesystem::path>& roots) {
  constexpr std::array<const wchar_t*, 2> keys = {
    L"Software\\Valve\\Steam",
    L"Software\\WOW6432Node\\Valve\\Steam",
  };
  constexpr std::array<const wchar_t*, 2> values = { L"SteamPath", L"InstallPath" };
  for (const auto* key : keys) {
    for (const auto* value : values) {
      for (const auto hive : { HKEY_CURRENT_USER, HKEY_LOCAL_MACHINE }) {
        HKEY handle = nullptr;
        if (RegOpenKeyExW(hive, key, 0, KEY_READ, &handle) != ERROR_SUCCESS) continue;
        wchar_t buffer[MAX_PATH]{};
        DWORD bytes = sizeof(buffer);
        const LONG result = RegQueryValueExW(handle, value, nullptr, nullptr,
          reinterpret_cast<LPBYTE>(buffer), &bytes);
        RegCloseKey(handle);
        if (result == ERROR_SUCCESS && buffer[0] != L'\0') roots.emplace_back(buffer);
      }
    }
  }
}

LRESULT CALLBACK windowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
  switch (message) {
  case WM_SIZE:
    if (g_renderer) g_renderer->resize(LOWORD(lParam), HIWORD(lParam));
    return 0;
  case WM_RBUTTONDOWN:
    g_orbiting = true;
    g_lastMouse = POINT{static_cast<int>(static_cast<short>(LOWORD(lParam))), static_cast<int>(static_cast<short>(HIWORD(lParam)))};
    SetCapture(window);
    return 0;
  case WM_RBUTTONUP:
    g_orbiting = false;
    if (GetCapture() == window) ReleaseCapture();
    return 0;
  case WM_MOUSEMOVE:
    if (g_orbiting && g_renderer) {
      const POINT current{static_cast<int>(static_cast<short>(LOWORD(lParam))), static_cast<int>(static_cast<short>(HIWORD(lParam)))};
      g_renderer->orbitCamera(static_cast<float>(current.x - g_lastMouse.x), static_cast<float>(current.y - g_lastMouse.y));
      g_lastMouse = current;
    }
    return 0;
  case WM_MOUSEWHEEL:
    if (g_renderer) g_renderer->zoomCamera(static_cast<float>(GET_WHEEL_DELTA_WPARAM(wParam)) / static_cast<float>(WHEEL_DELTA));
    return 0;
  case WM_PAINT:
    {
      PAINTSTRUCT paint{};
      BeginPaint(window, &paint);
      EndPaint(window, &paint);
    }
    return 0;
  case WM_ERASEBKGND:
    return 1;
  case WM_KEYDOWN: {
    // Toggle commands must be edge-triggered; Windows may repeat WM_KEYDOWN
    // while a key is held, which otherwise flips state several times.
    if ((lParam & (1u << 30)) != 0u
        && (wParam == VK_SPACE || wParam == 'R' || wParam == VK_HOME || wParam == VK_END
            || (wParam >= VK_F1 && wParam <= VK_F8))) return 0;
    const bool functionKey = wParam >= VK_F1 && wParam <= VK_F8;
    if (functionKey && g_renderer) {
      const std::size_t slot = static_cast<std::size_t>(wParam - VK_F1);
      const bool control = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
      const bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
      if (control && shift) g_renderer->clearCameraPreset(slot);
      else if (control) g_renderer->saveCameraPreset(slot);
      else g_renderer->applyCameraPreset(slot);
    } else if (wParam == VK_HOME) {
      if (g_renderer) g_renderer->resetCamera();
      if (g_playback.enabled) { g_playback.tick = 0; g_playback.reverse = false; }
    } else if (wParam == VK_END && g_playback.enabled) {
      g_playback.tick = g_playback.endTick;
    } else if (wParam == VK_SPACE && g_playback.enabled) {
      g_playback.paused = !g_playback.paused;
    } else if (wParam == 'R' && g_playback.enabled) {
      if (!g_playback.reverse && g_playback.tick == 0) g_playback.tick = g_playback.endTick;
      g_playback.reverse = !g_playback.reverse;
    } else if (wParam == VK_LEFT && g_playback.enabled) {
      const std::int32_t step = (GetKeyState(VK_SHIFT) & 0x8000) ? 66 : 1;
      g_playback.tick = std::max<std::int32_t>(0, g_playback.tick - step);
    } else if (wParam == VK_RIGHT && g_playback.enabled) {
      const std::int32_t step = (GetKeyState(VK_SHIFT) & 0x8000) ? 66 : 1;
      g_playback.tick = std::min(g_playback.endTick, g_playback.tick + step);
    } else if ((wParam == VK_OEM_PLUS || wParam == VK_ADD) && g_playback.enabled) {
      g_playback.speed = std::min(8.0, g_playback.speed * 2.0);
    } else if ((wParam == VK_OEM_MINUS || wParam == VK_SUBTRACT) && g_playback.enabled) {
      g_playback.speed = std::max(0.125, g_playback.speed * 0.5);
    }
    return 0;
  }
  case WM_DESTROY:
    PostQuitMessage(0);
    return 0;
  default:
    return DefWindowProcW(window, message, wParam, lParam);
  }
}

std::vector<std::uint8_t> readFile(const std::filesystem::path& path) {
  std::ifstream file(path, std::ios::binary | std::ios::ate);
  if (!file) return {};
  const auto size = file.tellg();
  if (size <= 0) return {};
  std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
  file.seekg(0);
  if (!file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()))) return {};
  return bytes;
}

std::string sha256File(const std::filesystem::path& path, std::string& status) {
  std::ifstream file(path, std::ios::binary);
  if (!file) { status = "missing"; return {}; }
  BCRYPT_ALG_HANDLE algorithm = nullptr;
  BCRYPT_HASH_HANDLE hash = nullptr;
  DWORD objectLength = 0, resultLength = 0;
  if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) != 0
      || BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&objectLength), sizeof(objectLength), &resultLength, 0) != 0) {
    if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0);
    status = "failed"; return {};
  }
  std::vector<std::uint8_t> object(objectLength);
  std::array<std::uint8_t, 32> digest{};
  std::string result;
  bool ok = BCryptCreateHash(algorithm, &hash, object.data(), objectLength, nullptr, 0, 0) == 0;
  // Keep the streaming buffer on the heap; the Windows default thread stack
  // is small enough that a 1 MiB local array can trigger STATUS_STACK_OVERFLOW.
  std::vector<std::uint8_t> chunk(1024u * 1024u);
  while (ok && file) {
    file.read(reinterpret_cast<char*>(chunk.data()), static_cast<std::streamsize>(chunk.size()));
    const auto count = file.gcount();
    if (count > 0) ok = BCryptHashData(hash, chunk.data(), static_cast<ULONG>(count), 0) == 0;
  }
  if (ok && BCryptFinishHash(hash, digest.data(), static_cast<ULONG>(digest.size()), 0) == 0) {
    static constexpr char hex[] = "0123456789abcdef";
    result.reserve(64);
    for (const auto byte : digest) { result.push_back(hex[byte >> 4u]); result.push_back(hex[byte & 15u]); }
    status = "ready";
  }
  if (hash) BCryptDestroyHash(hash);
  BCryptCloseAlgorithmProvider(algorithm, 0);
  if (result.empty()) status = "failed";
  return result;
}

std::filesystem::path localCacheDirectory(std::string& status) {
  wchar_t* value = nullptr; std::size_t length = 0;
  if (_wdupenv_s(&value, &length, L"LOCALAPPDATA") == 0 && value) {
    std::filesystem::path path = std::filesystem::path(value) / L"TF2 Demo Player" / L"cache";
    free(value);
    std::error_code error; std::filesystem::create_directories(path, error);
    if (error) { status = "unavailable"; return {}; }
    const auto probe = path / L".write-test";
    std::ofstream probeFile(probe, std::ios::binary | std::ios::trunc);
    if (!probeFile) { status = "unwritable"; return {}; }
    probeFile.close();
    std::filesystem::remove(probe, error);
    status = "ready";
    return path;
  }
  status = "unavailable";
  return {};
}

bool isVoiceOrMusic(const std::string& name) {
  std::string lower = name;
  std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return lower.find("vo/") != std::string::npos || lower.find("voice") != std::string::npos
    || lower.find("announcer") != std::string::npos || lower.find("commentary") != std::string::npos
    || lower.find("radio") != std::string::npos || lower.find("music") != std::string::npos;
}
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int showCommand) {
  // Keep the render surface in physical pixels on mixed-DPI desktop setups.
  if (!SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)) {
    SetProcessDPIAware();
  }
  const wchar_t* className = L"TF2DemoNativeRenderer";
  WNDCLASSW windowClass{};
  windowClass.hInstance = instance;
  windowClass.lpfnWndProc = windowProc;
  windowClass.lpszClassName = className;
  windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  if (!RegisterClassW(&windowClass) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return 10;

  HWND window = CreateWindowExW(
    0, className, L"TF2 Demo Player - Native D3D11",
    WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 1280, 720,
    nullptr, nullptr, instance, nullptr);
  if (!window) return 11;

  tf2::native::Renderer renderer;
  g_renderer = &renderer;
  auto persistent = tf2::native::loadSettings();
  int argumentCount = 0;
  std::filesystem::path commandTfRoot;
  std::filesystem::path commandDemo;
  std::filesystem::path commandModel;
  UINT audioDevice = WAVE_MAPPER;
  bool audioDeviceRejected = false;
  bool startPaused = false;
  LPWSTR* arguments = CommandLineToArgvW(GetCommandLineW(), &argumentCount);
  if (arguments) {
    for (int i = 1; i < argumentCount; ++i) {
      if (wcscmp(arguments[i], L"--quality") == 0 && i + 1 < argumentCount) {
        if (wcscmp(arguments[++i], L"standard") == 0) {
          persistent.render = tf2::native::RenderSettings::fromPreset(
            tf2::native::QualityPreset::Standard);
        } else if (wcscmp(arguments[i], L"performance") == 0) {
          persistent.render = tf2::native::RenderSettings::fromPreset(
            tf2::native::QualityPreset::Performance);
        }
      } else if (wcscmp(arguments[i], L"--vsync") == 0) {
        persistent.render.vsync = true;
      } else if (wcscmp(arguments[i], L"--no-vsync") == 0) {
        persistent.render.vsync = false;
      } else if (wcscmp(arguments[i], L"--tf-root") == 0 && i + 1 < argumentCount) {
        commandTfRoot = arguments[++i];
      } else if (wcscmp(arguments[i], L"--demo") == 0 && i + 1 < argumentCount) {
        commandDemo = arguments[++i];
      } else if (wcscmp(arguments[i], L"--model") == 0 && i + 1 < argumentCount) {
        commandModel = arguments[++i];
      } else if (wcscmp(arguments[i], L"--audio-device") == 0 && i + 1 < argumentCount) {
        wchar_t* end = nullptr;
        const unsigned long parsed = std::wcstoul(arguments[++i], &end, 10);
        if (end && *end == L'\0' && parsed < waveOutGetNumDevs()) audioDevice = static_cast<UINT>(parsed);
        else audioDeviceRejected = true;
      } else if (wcscmp(arguments[i], L"--audio-device") == 0) {
        audioDeviceRejected = true;
      } else if (wcscmp(arguments[i], L"--start-paused") == 0) {
        startPaused = true;
      }
    }
    persistent.render.normalize();
  }
  if (audioDeviceRejected) {
    OutputDebugStringW(L"TF2 Demo Player: invalid --audio-device; default device not used.\n");
    return 13;
  }
  renderer.setSettings(persistent.render);
  // The native shell uses the installed TF2 `tf` directory as its first
  // resource boundary; Demo header parsing and the first resource selection pass are active.
  std::vector<std::filesystem::path> candidates = {
    L"D:\\SteamLibrary\\steamapps\\common\\Team Fortress 2\\tf",
    L"C:\\Program Files (x86)\\Steam\\steamapps\\common\\Team Fortress 2\\tf",
    L"C:\\Program Files\\Steam\\steamapps\\common\\Team Fortress 2\\tf",
  };
  if (!persistent.tfRoot.empty()) candidates.insert(candidates.begin(), persistent.tfRoot);
  if (!commandTfRoot.empty()) candidates.insert(candidates.begin(), commandTfRoot);
  if (arguments) LocalFree(arguments);
  wchar_t* steamRoot = nullptr;
  size_t steamRootLength = 0;
  if (_wdupenv_s(&steamRoot, &steamRootLength, L"STEAM_DIR") == 0 && steamRoot) {
    candidates.emplace_back(std::filesystem::path(steamRoot)
      / L"steamapps\\common\\Team Fortress 2\\tf");
    free(steamRoot);
  }
  std::vector<std::filesystem::path> steamRoots = {
    L"C:\\Program Files (x86)\\Steam",
    L"C:\\Program Files\\Steam",
    L"D:\\SteamLibrary",
  };
  addSteamRegistryRoots(steamRoots);
  const auto steamCandidates = tf2::native::AssetRoot::steamLibraryCandidates(steamRoots);
  candidates.insert(candidates.end(), steamCandidates.begin(), steamCandidates.end());
  const auto assets = tf2::native::AssetRoot::findInstalled(candidates);
  std::string cacheStatus;
  const auto cacheDirectory = localCacheDirectory(cacheStatus);
  std::string hashStatus = commandDemo.empty() ? "missing" : "failed";
  const std::string demoSha256 = commandDemo.empty() ? std::string{} : sha256File(commandDemo, hashStatus);
  std::size_t modelCompanionMissing = 0;
  std::size_t modelVpkHits = 0;
  std::size_t modelLooseHits = 0;
  std::size_t modelUnknownPaths = 0;
  std::size_t itemSchemaHits = 0;
  std::size_t itemSchemaMisses = 0;
  tf2::native::ModelFeatureStatus modelAttachmentStatus = tf2::native::ModelFeatureStatus::Unknown;
  tf2::native::ModelFeatureStatus modelBodygroupStatus = tf2::native::ModelFeatureStatus::Unknown;
  tf2::native::ModelFeatureStatus modelViewModelStatus = tf2::native::ModelFeatureStatus::Unknown;
  std::size_t modelBoneCount = 0;
  std::size_t modelAttachmentCount = 0;
  std::size_t modelBodyPartCount = 0;
  bool modelPosePreflight = false;
  std::string itemSchemaStatus = "missing";
  std::unique_ptr<tf2::native::ItemSchema> itemSchema;
  if (assets.valid()) {
    std::string itemSchemaError;
    itemSchema = tf2::native::ItemSchema::load(
      assets.tfDirectory / L"scripts/items/items_game.txt", itemSchemaError);
    if (itemSchema) itemSchemaStatus = "ready";
  }
  tf2::native::DemoHeader demoHeader;
  tf2::native::DemoIndex demoIndex;
  tf2::native::DemoNetworkSummary demoNetworkSummary;
  if (!commandDemo.empty()) {
    if (tf2::native::parseDemoHeaderFile(commandDemo, demoHeader)) {
      tf2::native::indexDemoFile(commandDemo, demoHeader, demoIndex);
      demoNetworkSummary.networkProtocol = demoHeader.networkProtocol;
      tf2::native::scanKnownDemoMessages(commandDemo, demoIndex, demoNetworkSummary);
      tf2::native::buildAssetReferenceList(demoNetworkSummary, demoNetworkSummary.assetReferences);
      for (const auto& reference : demoNetworkSummary.assetReferences) {
        const auto candidate = tf2::native::ModelLoader::resolveAssetReference(assets, reference, itemSchema.get());
        if (candidate.resolution == tf2::native::ModelAssetResolution::FoundLoose) ++modelLooseHits;
        else if (candidate.resolution == tf2::native::ModelAssetResolution::FoundVpk) ++modelVpkHits;
        else if (candidate.resolution == tf2::native::ModelAssetResolution::Unknown) ++modelUnknownPaths;
        if (reference.hasItemDefIndex) {
          if (itemSchema && itemSchema->find(static_cast<int>(reference.itemDefIndex))) ++itemSchemaHits;
          else ++itemSchemaMisses;
        }
      }
    }
  }
  std::size_t indexedVpkEntries = 0;
  std::vector<std::uint8_t> displayRgba;
  UINT displayWidth = 0;
  UINT displayHeight = 0;
  tf2::native::VmtMaterial displayMaterial;
  std::string displayShader;
  std::string displayBaseTexture;
  tf2::native::BspMap displayMap;
  std::vector<std::uint8_t> mapRgba;
  UINT mapWidth = 0;
  UINT mapHeight = 0;
  std::string mapMaterialName;
  std::string mapTexturePath;
  std::string mapBaseTexture2Path;
  std::string mapBumpMapPath;
  std::string mapEnvMapPath;
  std::vector<std::uint8_t> mapBumpRgba;
  std::vector<std::uint8_t> mapEnvRgba;
  UINT mapBumpWidth = 0, mapBumpHeight = 0, mapEnvWidth = 0, mapEnvHeight = 0;
  bool mapBaseTexture2Found = false;
  bool mapBumpMapFound = false;
  bool mapBaseTexture2Declared = false;
  bool mapBumpMapDeclared = false;
  tf2::native::WorldMaterialParams mapMaterialParams;
  std::vector<tf2::native::WorldTexture> worldTextures;
  std::vector<std::shared_ptr<tf2::native::VpkArchive>> soundArchives;
  if (assets.valid()) {
    std::filesystem::path mapPath = L"maps/2koth_abbey.bsp";
    if (demoHeader.valid) mapPath = std::filesystem::path(L"maps") / std::filesystem::path(std::wstring(demoHeader.mapName.begin(), demoHeader.mapName.end()) + L".bsp");
    for (const auto* name : { L"pak01_dir.vpk", L"tf2_misc_dir.vpk", L"tf2_textures_dir.vpk", L"tf2_sound_misc_dir.vpk", L"tf2_sound_vo_english_dir.vpk" }) {
      auto archive = std::make_shared<tf2::native::VpkArchive>();
      if (archive->open(assets.tfDirectory / name)) soundArchives.push_back(std::move(archive));
    }
    auto mapBytes = readFile(assets.tfDirectory / mapPath);
    if (mapBytes.empty()) {
      const std::string mapResource =
        std::filesystem::path(mapPath).generic_string();
      for (const auto& archive : soundArchives) {
        mapBytes = archive->read(mapResource);
        if (!mapBytes.empty()) break;
      }
    }
    tf2::native::BspParser::parse(mapBytes, displayMap, 200000);
    std::unordered_map<std::string, std::vector<std::uint8_t>> materialVtfCache;
    const auto readMaterialVtf = [&](const std::string& path) -> const std::vector<std::uint8_t>& {
      auto found = materialVtfCache.find(path);
      if (found != materialVtfCache.end()) return found->second;
      auto bytes = readFile(assets.tfDirectory / std::filesystem::path(path));
      for (const auto& archive : soundArchives) {
        if (!bytes.empty()) break;
        bytes = archive->read(path);
      }
      return materialVtfCache.emplace(path, std::move(bytes)).first->second;
    };
    const std::string displayVmtPath = "materials/vgui/logos/spray.vmt";
    auto displayVmt = readFile(assets.tfDirectory / L"materials/vgui/logos/spray.vmt");
    if (!displayVmt.empty()) {
      const std::string materialText(displayVmt.begin(), displayVmt.end());
      if (tf2::native::VmtParser::parse(materialText, displayMaterial)) {
        displayShader = displayMaterial.shader;
        displayBaseTexture = displayMaterial.baseTexture;
      }
    }
    const std::string displayPath = tf2::native::VmtParser::resourcePath(
      displayBaseTexture.empty() ? "vgui/logos/spray" : displayBaseTexture, ".vtf");
    auto displayVtf = readFile(assets.tfDirectory / std::filesystem::path(displayPath));
    for (const auto& archive : soundArchives) {
      indexedVpkEntries = std::max(indexedVpkEntries, archive->entryCount());
      if (displayVtf.empty()) displayVtf = archive->read(displayPath);
      if (displayVmt.empty()) displayVmt = archive->read(displayVmtPath);
    }
    if (displayVtf.empty()) displayVtf = readFile(assets.tfDirectory / L"materials/vgui/logos/spray.vtf");
    if (displayMap.valid) {
      for (const auto& triangle : displayMap.triangles) if (!triangle.material.empty()) { mapMaterialName = triangle.material; break; }
      if (!mapMaterialName.empty()) {
        const std::string mapVmtPath = tf2::native::VmtParser::resourcePath(mapMaterialName, ".vmt");
        auto mapVmt = readFile(assets.tfDirectory / std::filesystem::path(mapVmtPath));
        for (const auto& archive : soundArchives) {
          if (!mapVmt.empty()) break;
          mapVmt = archive->read(mapVmtPath);
        }
        tf2::native::VmtMaterial mapMaterial;
        if (!mapVmt.empty() && tf2::native::VmtParser::parse(std::string(mapVmt.begin(), mapVmt.end()), mapMaterial)) {
          mapBaseTexture2Declared = !mapMaterial.baseTexture2.empty();
          mapBumpMapDeclared = !mapMaterial.bumpMap.empty();
          mapMaterialParams.envMap = false;
          mapMaterialParams.waterMaterial = mapMaterial.waterShader;
          mapMaterialParams.selfIllum = mapMaterial.selfIllum;
          mapMaterialParams.bumpMapping = false;
          if (!mapMaterial.selfIllumTint.empty()) {
            std::string tint = mapMaterial.selfIllumTint;
            for (char& character : tint) {
              if (character == ',' || character == '[' || character == ']' || character == '{' || character == '}') character = ' ';
            }
            std::istringstream values(tint);
            float r = 0.0f, g = 0.0f, b = 0.0f;
            std::string trailing;
            if (values >> r >> g >> b && !(values >> trailing)
                && std::isfinite(r) && std::isfinite(g) && std::isfinite(b)) {
              mapMaterialParams.selfIllumR = std::clamp(r, 0.0f, 4.0f);
              mapMaterialParams.selfIllumG = std::clamp(g, 0.0f, 4.0f);
              mapMaterialParams.selfIllumB = std::clamp(b, 0.0f, 4.0f);
            }
          }
        }
        if (!mapVmt.empty() && mapMaterial.valid && !mapMaterial.baseTexture.empty()) {
          mapTexturePath = tf2::native::VmtParser::resourcePath(mapMaterial.baseTexture, ".vtf");
          const auto& mapVtf = readMaterialVtf(mapTexturePath);
          tf2::native::VtfTexture mapTexture;
          if (!mapVtf.empty() && mapTexture.parse(mapVtf) && !(mapRgba = mapTexture.decodeRgba(mapVtf)).empty()) { mapWidth = mapTexture.header().width; mapHeight = mapTexture.header().height; }
        }
        const auto findMaterialTexture = [&](const std::string& materialName, std::string& path) {
          if (materialName.empty()) return false;
          path = tf2::native::VmtParser::resourcePath(materialName, ".vtf");
          return !readMaterialVtf(path).empty();
        };
        mapBaseTexture2Found = findMaterialTexture(mapMaterial.baseTexture2, mapBaseTexture2Path);
        mapBumpMapFound = findMaterialTexture(mapMaterial.bumpMap, mapBumpMapPath);
        if (!mapBumpMapPath.empty()) {
          const auto& bytes = readMaterialVtf(mapBumpMapPath);
          tf2::native::VtfTexture texture;
          if (!bytes.empty() && texture.parse(bytes) && !texture.header().isCubemap()) {
            mapBumpRgba = texture.decodeRgba(bytes);
            mapBumpWidth = texture.header().width; mapBumpHeight = texture.header().height;
            mapMaterialParams.bumpMapping = !mapBumpRgba.empty();
          }
        }
        if (!mapMaterial.envMap.empty()) {
          mapEnvMapPath = tf2::native::VmtParser::resourcePath(mapMaterial.envMap, ".vtf");
          const auto& bytes = readMaterialVtf(mapEnvMapPath);
          tf2::native::VtfTexture texture;
          if (!bytes.empty() && texture.parse(bytes) && !texture.header().isCubemap()) {
            mapEnvRgba = texture.decodeRgba(bytes);
            mapEnvWidth = texture.header().width; mapEnvHeight = texture.header().height;
            mapMaterialParams.envMap = !mapEnvRgba.empty();
          }
        }
      }
    }
    if (displayMap.valid) {
      std::unordered_set<std::string> seenMaterials;
      for (const auto& triangle : displayMap.triangles) {
        if (worldTextures.size() >= 64u || triangle.material.empty() || !seenMaterials.insert(triangle.material).second) continue;
        const auto materialPath = tf2::native::VmtParser::resourcePath(triangle.material, ".vmt");
        auto materialBytes = readFile(assets.tfDirectory / std::filesystem::path(materialPath));
        if (materialBytes.empty()) for (const auto& archive : soundArchives) {
          materialBytes = archive->read(materialPath); if (!materialBytes.empty()) break;
        }
        tf2::native::VmtMaterial material;
        if (materialBytes.empty() || !tf2::native::VmtParser::parse(std::string(materialBytes.begin(), materialBytes.end()), material)
            || material.baseTexture.empty()) continue;
        const auto texturePath = tf2::native::VmtParser::resourcePath(material.baseTexture, ".vtf");
        auto textureBytes = readFile(assets.tfDirectory / std::filesystem::path(texturePath));
        if (textureBytes.empty()) for (const auto& archive : soundArchives) {
          textureBytes = archive->read(texturePath); if (!textureBytes.empty()) break;
        }
        tf2::native::VtfTexture texture;
        if (textureBytes.empty() || !texture.parse(textureBytes) || texture.header().width > 512 || texture.header().height > 512) continue;
        auto rgba = texture.decodeRgba(textureBytes);
        if (!rgba.empty()) worldTextures.push_back({triangle.material, std::move(rgba), texture.header().width, texture.header().height});
      }
    }
    if (!displayVtf.empty()) {
      tf2::native::VtfTexture texture;
      if (texture.parse(displayVtf) && !(displayRgba = texture.decodeRgba(displayVtf)).empty()) {
        displayWidth = texture.header().width;
        displayHeight = texture.header().height;
      }
    }
  }
  g_playback.enabled = demoHeader.valid;
  g_playback.endTick = std::max<std::int32_t>(0, demoHeader.ticks);
  g_playback.tick = 0;
  g_playback.paused = startPaused;
  g_playback.reverse = false;
  g_playback.tickRate = (demoHeader.playbackTime > 0.0f && demoHeader.ticks > 0)
    ? std::clamp(static_cast<double>(demoHeader.ticks) / static_cast<double>(demoHeader.playbackTime), 30.0, 128.0)
    : 66.6666667;
  tf2::native::SoundEventTimeline soundTimeline;
  std::int32_t firstPacketTick = 0;
  const auto firstPacket = std::find_if(demoIndex.entries.begin(), demoIndex.entries.end(),
    [](const auto& entry) { return entry.command == 1 || entry.command == 2; });
  if (firstPacket != demoIndex.entries.end()) firstPacketTick = firstPacket->tick;
  for (const auto& source : demoNetworkSummary.decodedSoundEvents) {
    if (source.resourceName.empty() || isVoiceOrMusic(source.resourceName)) continue;
    tf2::native::SoundPlaybackEvent event;
    event.tick = std::max<std::int32_t>(0, source.tick - firstPacketTick);
    event.soundIndex = source.soundIndex;
    event.name = source.resourceName;
    event.skipVoice = false;
    event.volume = std::clamp(source.volume, 0.0f, 1.0f);
    event.origin[0] = source.origin[0];
    event.origin[1] = source.origin[1];
    event.origin[2] = source.origin[2];
    soundTimeline.add(std::move(event));
  }
  soundTimeline.sortByTick();
  tf2::native::AudioEventScheduler soundScheduler;
  soundScheduler.setDeviceId(audioDevice);
  std::unordered_map<std::string, tf2::native::WavPcmData> soundCache;
  constexpr std::size_t kSoundCacheLimit = 32u * 1024u * 1024u;
  constexpr std::size_t kSoundCacheEntryLimit = 512u;
  std::size_t soundCacheBytes = 0;
  soundScheduler.setResolver([&soundArchives, &soundCache, &soundCacheBytes](const std::string& name, tf2::native::WavPcmData& out) {
    const auto cached = soundCache.find(name);
    if (cached != soundCache.end()) { out = cached->second; return true; }
    for (const auto& archive : soundArchives) {
      tf2::native::SoundResource resource;
      if (tf2::native::readSoundResource(*archive, name, resource) && resource.codec == tf2::native::SoundCodec::Wav) {
        if (!resource.wav.valid) return false;
        const std::size_t pcmBytes = resource.wav.pcm.size();
        if (soundCache.size() < kSoundCacheEntryLimit && pcmBytes <= kSoundCacheLimit - soundCacheBytes) {
          auto inserted = soundCache.emplace(name, std::move(resource.wav));
          soundCacheBytes += inserted.first->second.pcm.size();
          out = inserted.first->second;
        } else {
          out = std::move(resource.wav);
        }
        return true;
      }
    }
    return false;
  });
  std::wstring demoSuffix;
  if (!commandDemo.empty()) {
    if (demoHeader.valid) {
      const char* typeName = tf2::native::demoRecordingTypeName(demoHeader.recordingType);
      demoSuffix = L" | Demo: " + std::wstring(demoHeader.mapName.begin(), demoHeader.mapName.end())
        + L" " + std::wstring(typeName, typeName + std::strlen(typeName))
        + L" ticks=" + std::to_wstring(demoHeader.ticks)
        + L" commands=" + std::to_wstring(demoIndex.commandCount)
        + L" packets=" + std::to_wstring(demoIndex.packetCount)
        + L" serverinfo=" + std::to_wstring(demoNetworkSummary.serverInfoCount)
        + L" signon=" + std::to_wstring(demoNetworkSummary.signonStateCount)
        + L" state=" + std::to_wstring(demoNetworkSummary.lastSignonState)
        + L" classes=" + std::to_wstring(demoNetworkSummary.serverClassCount)
        + L" setview=" + std::to_wstring(demoNetworkSummary.setViewCount)
        + L" viewent=" + std::to_wstring(demoNetworkSummary.lastViewEntity)
        + L" nettick=" + std::to_wstring(demoNetworkSummary.netTickCount)
        + L" print=" + std::to_wstring(demoNetworkSummary.printCount)
        + L" setconvar=" + std::to_wstring(demoNetworkSummary.setConVarCount)
        + L" convars=" + std::to_wstring(demoNetworkSummary.setConVarPairCount)
        + L" temp=" + std::to_wstring(demoNetworkSummary.tempEntitiesCount)
        + L" teheaders=" + std::to_wstring(demoNetworkSummary.tempEventHeadersDecoded)
        + L" tefail=" + std::to_wstring(demoNetworkSummary.tempEventDecodeFailures)
        + L" teprops=" + std::to_wstring(demoNetworkSummary.tempPropDecodedCount)
        + L" teproj=" + std::to_wstring(demoNetworkSummary.tempClientProjectileCount)
        + L" teparticle=" + std::to_wstring(demoNetworkSummary.tempParticleEffectCount)
        + L" teexplosion=" + std::to_wstring(demoNetworkSummary.tempExplosionCount)
        + L" tebullets=" + std::to_wstring(demoNetworkSummary.tempFireBulletsCount)
        + L" teanim=" + std::to_wstring(demoNetworkSummary.tempPlayerAnimEventCount)
        + L" tefbhits=" + std::to_wstring(demoNetworkSummary.tempFireBulletsFieldHits)
        + L" tepehits=" + std::to_wstring(demoNetworkSummary.tempParticleEffectFieldHits)
        + L" teexhits=" + std::to_wstring(demoNetworkSummary.tempExplosionFieldHits)
        + L" teanimhits=" + std::to_wstring(demoNetworkSummary.tempPlayerAnimEventFieldHits)
        + L" tecached=" + std::to_wstring(demoNetworkSummary.tempEntityEvents.size())
        + L" projectileTimeline=" + std::to_wstring(demoNetworkSummary.projectileTimeline.size())
        + L" assetRefs=" + std::to_wstring(demoNetworkSummary.assetReferences.size())
        + L" modelKnown=" + std::to_wstring(demoNetworkSummary.assetModelIndexKnown)
        + L" weaponKnown=" + std::to_wstring(demoNetworkSummary.assetWeaponKnown)
        + L" itemKnown=" + std::to_wstring(demoNetworkSummary.assetItemDefKnown)
        + L" paintKnown=" + std::to_wstring(demoNetworkSummary.assetPaintKitKnown)
        + L" skinKnown=" + std::to_wstring(demoNetworkSummary.assetSkinKnown)
        + L" qualityKnown=" + std::to_wstring(demoNetworkSummary.assetQualityKnown)
        + L" modelPathKnown=" + std::to_wstring(demoNetworkSummary.assetModelPathKnown)
        + L" weaponClassKnown=" + std::to_wstring(demoNetworkSummary.assetWeaponClassKnown)
        + L" assetUnknown=" + std::to_wstring(demoNetworkSummary.assetIdentityUnknown)
        + L" modelLoose=" + std::to_wstring(modelLooseHits)
        + L" modelVpk=" + std::to_wstring(modelVpkHits)
        + L" modelPathUnknown=" + std::to_wstring(modelUnknownPaths)
        + L" schema=" + std::wstring(itemSchemaStatus.begin(), itemSchemaStatus.end())
        + L" schemaHits=" + std::to_wstring(itemSchemaHits)
        + L" schemaMiss=" + std::to_wstring(itemSchemaMisses)
        + L" tempevents=" + std::to_wstring(demoNetworkSummary.tempEventCount)
        + L" sounds=" + std::to_wstring(demoNetworkSummary.soundMessageCount)
        + L" soundevents=" + std::to_wstring(demoNetworkSummary.soundEventCount)
        + L" sounddecoded=" + std::to_wstring(demoNetworkSummary.decodedSoundEvents.size())
        + L" sounddecodefail=" + std::to_wstring(demoNetworkSummary.decodedSoundEventFailures)
        + L" soundprecache=" + std::to_wstring(demoNetworkSummary.soundPrecache.size())
        + L" soundtablefail=" + std::to_wstring(demoNetworkSummary.soundPrecacheDecodeFailures)
        + L" soundtableid=" + std::to_wstring(demoNetworkSummary.soundPrecacheTableId)
        + L" soundupdates=" + std::to_wstring(demoNetworkSummary.soundPrecacheUpdateCount)
        + L" updateentries=" + std::to_wstring(demoNetworkSummary.lastStringTableUpdateEntries)
        + L" updatebits=" + std::to_wstring(demoNetworkSummary.lastStringTableUpdateBits)
        + L" soundupdate=" + std::to_wstring(demoNetworkSummary.lastStringTableUpdateId)
        + L" updateids=" + (demoNetworkSummary.stringTableUpdateIds.empty() ? L"" : std::to_wstring(demoNetworkSummary.stringTableUpdateIds.front()))
        + L" soundmatch=" + std::to_wstring(demoNetworkSummary.decodedSoundResourceMatches)
        + L" soundmiss=" + std::to_wstring(demoNetworkSummary.decodedSoundResourceMisses)
        + L" voice=" + std::to_wstring(demoNetworkSummary.voiceDataCount)
        + L" updates=" + std::to_wstring(demoNetworkSummary.updateStringTableCount)
        + L" entities=" + std::to_wstring(demoNetworkSummary.packetEntitiesCount)
        + L" entityupdates=" + std::to_wstring(demoNetworkSummary.packetEntityUpdates)
        + L" entityheaders=" + std::to_wstring(demoNetworkSummary.packetEntityHeaderUpdates)
        + L" enters=" + std::to_wstring(demoNetworkSummary.packetEntityEnterCount)
        + L" preserves=" + std::to_wstring(demoNetworkSummary.packetEntityPreserveCount)
        + L" leaves=" + std::to_wstring(demoNetworkSummary.packetEntityLeaveCount)
        + L" deletes=" + std::to_wstring(demoNetworkSummary.packetEntityDeleteCount)
        + L" entityfail=" + std::to_wstring(demoNetworkSummary.packetEntityDecodeFailures)
        + L" epmiss=" + std::to_wstring(demoNetworkSummary.entityPropMissingTableFailures)
        + L" epidx=" + std::to_wstring(demoNetworkSummary.entityPropIndexFailures)
        + L" epval=" + std::to_wstring(demoNetworkSummary.entityPropValueFailures)
        + L" epfirst=" + std::to_wstring(demoNetworkSummary.firstEntityPropFailureTick)
        + L" epent=" + std::to_wstring(demoNetworkSummary.firstEntityPropFailureEntity)
        + L" epclass=" + std::to_wstring(demoNetworkSummary.firstEntityPropFailureClass)
        + L" epidx0=" + std::to_wstring(demoNetworkSummary.firstEntityPropFailureIndex)
        + L" epstage=" + std::wstring(demoNetworkSummary.firstEntityPropFailureStage.begin(), demoNetworkSummary.firstEntityPropFailureStage.end())
        + L" epname=" + std::wstring(demoNetworkSummary.firstEntityPropFailureName.begin(), demoNetworkSummary.firstEntityPropFailureName.end())
        + L" eptype=" + std::to_wstring(demoNetworkSummary.firstEntityPropFailureType)
        + L" epflags=" + std::to_wstring(demoNetworkSummary.firstEntityPropFailureFlags)
        + L" epbits=" + std::to_wstring(demoNetworkSummary.firstEntityPropFailureBits)
        + L" netraw=" + (demoNetworkSummary.lastNetworkTickRawValid ? std::to_wstring(demoNetworkSummary.lastNetworkTickRaw) : L"-1")
        + L" tefirst=" + std::to_wstring(demoNetworkSummary.firstTempEntityFailureTick)
        + L" teclass=" + std::to_wstring(demoNetworkSummary.firstTempEntityFailureClass)
        + L" tebits=" + std::to_wstring(demoNetworkSummary.firstTempEntityFailurePayloadBits)
        + L" testage=" + std::wstring(demoNetworkSummary.firstTempEntityFailureStage.begin(), demoNetworkSummary.firstTempEntityFailureStage.end())
        + L" tename=" + std::wstring(demoNetworkSummary.firstTempEntityFailureName.begin(), demoNetworkSummary.firstTempEntityFailureName.end())
        + L" tetype=" + std::to_wstring(demoNetworkSummary.firstTempEntityFailureType)
        + L" teflags=" + std::to_wstring(demoNetworkSummary.firstTempEntityFailureFlags)
        + L" tepropbits=" + std::to_wstring(demoNetworkSummary.firstTempEntityFailureBits)
        + L" epstate=" + std::to_wstring(demoNetworkSummary.entityUnknownStateFailures)
        + L" ephdr=" + std::to_wstring(demoNetworkSummary.entityUpdateHeaderFailures)
        + L" base=" + std::to_wstring(demoNetworkSummary.instanceBaselineEntryCount)
        + L" baseok=" + std::to_wstring(demoNetworkSummary.instanceBaselineAppliedCount)
        + L" basemiss=" + std::to_wstring(demoNetworkSummary.instanceBaselineLookupMisses)
        + L" baseapplyfail=" + std::to_wstring(demoNetworkSummary.instanceBaselineApplyFailures)
        + L" basefirst=" + std::to_wstring(demoNetworkSummary.firstInstanceBaselineClassId)
        + L" baselook=" + std::to_wstring(demoNetworkSummary.firstInstanceBaselineLookupClassId)
        + L" basetable=" + (demoNetworkSummary.firstInstanceBaselineLookupHasTable ? L"1" : L"0")
        + L" baseentry=" + (demoNetworkSummary.firstInstanceBaselineLookupHasEntry ? L"1" : L"0")
        + L" baseapplied=" + (demoNetworkSummary.firstInstanceBaselineLookupApplied ? L"1" : L"0")
        + L" baseclass=" + std::to_wstring(demoNetworkSummary.firstInstanceBaselineClassId)
        + L" basefail=" + std::to_wstring(demoNetworkSummary.instanceBaselineDecodeFailures)
        + L" basezip=" + std::to_wstring(demoNetworkSummary.instanceBaselineCompressedCount)
        + L" basemagic=" + std::to_wstring(demoNetworkSummary.instanceBaselineMagic)
        + L" basec=" + std::to_wstring(demoNetworkSummary.instanceBaselineCompressedBytes)
        + L" based=" + std::to_wstring(demoNetworkSummary.instanceBaselineDecompressedBytes)
        + L" active=" + std::to_wstring(demoNetworkSummary.activeEntityCount)
        + L" activeMax=" + std::to_wstring(demoNetworkSummary.maxActiveEntityCount)
        + L" histEvents=" + std::to_wstring(demoNetworkSummary.entityHistoryEvents.size())
        + L" histCheckpoints=" + std::to_wstring(demoNetworkSummary.entityHistoryCheckpoints.size())
        + L" histDropped=" + std::to_wstring(demoNetworkSummary.entityHistoryDroppedPackets)
        + L" histDeltaMiss=" + std::to_wstring(demoNetworkSummary.entityHistoryDeltaBaseMisses)
        + L" histGap=" + (demoNetworkSummary.entityHistoryHasGap ? L"1" : L"0")
        + L" eventlists=" + std::to_wstring(demoNetworkSummary.gameEventListCount)
        + L" events=" + std::to_wstring(demoNetworkSummary.gameEventCount)
        + L" usermsg=" + std::to_wstring(demoNetworkSummary.userMessageCount)
        + L" prefetch=" + std::to_wstring(demoNetworkSummary.prefetchCount)
        + L" stringcmd=" + std::to_wstring(demoNetworkSummary.stringCommandCount)
        + L" fixangle=" + std::to_wstring(demoNetworkSummary.fixAngleCount)
        + L" cvar=" + std::to_wstring(demoNetworkSummary.getCvarValueCount)
        + L" entitymsg=" + std::to_wstring(demoNetworkSummary.entityMessageCount)
        + L" sendtables=" + std::to_wstring(demoNetworkSummary.sendTableCount)
        + L" dtables=" + std::to_wstring(demoNetworkSummary.dataTableDefinitionCount)
        + L" dtprops=" + std::to_wstring(demoNetworkSummary.dataTablePropCount)
        + L" flatprops=" + std::to_wstring(demoNetworkSummary.dataTableFlattenedPropCount)
        + L" dtclasses=" + std::to_wstring(demoNetworkSummary.dataTableServerClassCount)
        + L" unknown0=" + (demoNetworkSummary.unknownMessageTypes.empty() ? L"" : std::to_wstring(demoNetworkSummary.unknownMessageTypes.front()))
        + L" tables=" + std::to_wstring(demoNetworkSummary.stringTableCount)
        + L" table0=" + (demoNetworkSummary.stringTableNames.empty() ? L"" : std::wstring(demoNetworkSummary.stringTableNames.front().begin(), demoNetworkSummary.stringTableNames.front().end()))
        + L" servermap=" + std::wstring(demoNetworkSummary.serverMap.begin(), demoNetworkSummary.serverMap.end())
        + L" stv=" + (demoNetworkSummary.sourceTv ? L"1" : L"0");
    } else {
      demoSuffix = L" | Demo error: " + std::wstring(demoHeader.error.begin(), demoHeader.error.end());
    }
  }
  const std::wstring title = assets.valid()
    ? std::wstring(L"TF2 Demo Player - TF2 资源已连接: ") + assets.tfDirectory.wstring()
      + L" [VPK entries: " + std::to_wstring(indexedVpkEntries)
      + L", VTF: " + (displayWidth ? std::to_wstring(displayWidth) + L"x" + std::to_wstring(displayHeight) : L"fallback")
      + L", VMT: " + (displayShader.empty() ? L"unparsed" : std::wstring(displayShader.begin(), displayShader.end()))
      + L"/" + (displayBaseTexture.empty() ? L"no base texture" : std::wstring(displayBaseTexture.begin(), displayBaseTexture.end()))
      + L", BSP: " + (displayMap.valid ? std::to_wstring(displayMap.triangleCount) + L" tris" : (L"invalid/" + std::wstring(displayMap.error.begin(), displayMap.error.end())))
      + L", lightmap: samples=" + std::to_wstring(displayMap.lightmapSampleCount)
      + L" faces=" + std::to_wstring(displayMap.lightmapFaceCount)
      + L" tris=" + std::to_wstring(displayMap.lightmapTriangleCount)
      + L" bytes=" + std::to_wstring(displayMap.lightmapBytes)
      + L" state=FallbackUnlit"
      + L", mapmat: " + (mapMaterialName.empty() ? L"none" : std::wstring(mapMaterialName.begin(), mapMaterialName.end()))
      + L", deps: base2=" + (mapBaseTexture2Declared ? (mapBaseTexture2Found ? L"ok" : L"missing") : L"none")
      + L" bump=" + (mapBumpMapDeclared ? (mapBumpMapFound ? L"ok" : L"missing") : L"none")
      + L", maptex: " + (mapWidth ? std::to_wstring(mapWidth) + L"x" + std::to_wstring(mapHeight) : L"fallback") + L"]"
    : L"TF2 Demo Player - TF2 资源未找到";
  const std::wstring titleBase = title + demoSuffix;
  auto entitySnapshotStatus = tf2::native::EntitySnapshotQueryStatus::NoHistory;
  std::size_t hudAudioPlayed = 0;
  std::size_t hudAudioMissing = 0;
  std::size_t hudTrailVertices = 0;
  const auto refreshWindowTitle = [&]() {
    if (!g_playback.enabled) {
      SetWindowTextW(window, titleBase.c_str());
      return;
    }
    if (!g_playback.anchorValid || g_playback.tick < g_playback.anchorLookupTick
        || g_playback.tick - g_playback.anchorLookupTick >= 32) {
      g_playback.anchorValid = tf2::native::findDemoPacketAtOrBeforeTick(demoIndex, g_playback.tick, g_playback.anchor);
      g_playback.anchorLookupTick = g_playback.tick;
    }
    const std::wstring anchorSuffix = g_playback.anchorValid
      ? L" anchorAbs=" + std::to_wstring(g_playback.anchor.tick) + L"@" + std::to_wstring(g_playback.anchor.offset)
      : L" anchor=none";
    const std::wstring playbackSuffix = L" playback=" + std::to_wstring(g_playback.tick) + L"/" + std::to_wstring(g_playback.endTick)
      + L" speed=" + std::to_wstring(g_playback.speed) + L" rate=" + std::to_wstring(g_playback.tickRate)
      + anchorSuffix + (g_playback.reverse ? L" reverse" : L" forward")
      + (g_playback.paused ? L" paused" : L" playing")
      + L" HUD[tick=" + std::to_wstring(g_playback.tick)
      + L" state=" + (g_playback.paused ? L"paused" : (g_playback.reverse ? L"reverse" : L"playing"))
      + L" speed=" + std::to_wstring(g_playback.speed)
      + L" entity=" + std::wstring(entitySnapshotStatusName(entitySnapshotStatus))
      + L" audio=" + std::to_wstring(hudAudioPlayed) + L"/" + std::to_wstring(hudAudioMissing)
      + L" trailVerts=" + std::to_wstring(hudTrailVertices) + L"/1024"
      + L" cache=" + std::wstring(cacheStatus.begin(), cacheStatus.end())
      + (cacheDirectory.empty() ? L"" : L"@" + cacheDirectory.wstring())
      + L" hash=" + std::wstring(hashStatus.begin(), hashStatus.end())
      + (demoSha256.empty() ? L"" : L":" + std::wstring(demoSha256.begin(), demoSha256.end()))
      + L" modelMissing=" + std::to_wstring(modelCompanionMissing)
      + L" vpkHits=" + std::to_wstring(modelVpkHits)
      + L" attachment=" + std::wstring(modelAttachmentStatus == tf2::native::ModelFeatureStatus::Available ? L"available" : modelAttachmentStatus == tf2::native::ModelFeatureStatus::Missing ? L"missing" : L"unknown")
      + L" bodygroup=" + std::wstring(modelBodygroupStatus == tf2::native::ModelFeatureStatus::Available ? L"available" : modelBodygroupStatus == tf2::native::ModelFeatureStatus::Missing ? L"missing" : L"unknown")
      + L" viewmodel=" + std::wstring(modelViewModelStatus == tf2::native::ModelFeatureStatus::Available ? L"available" : modelViewModelStatus == tf2::native::ModelFeatureStatus::Missing ? L"missing" : L"unknown")
      + L" posePreflight=" + (modelPosePreflight ? L"ready" : L"unknown")
      + L" bones=" + std::to_wstring(modelBoneCount)
      + L" attachments=" + std::to_wstring(modelAttachmentCount)
      + L" bodyParts=" + std::to_wstring(modelBodyPartCount)
      + L" resources=" + ((modelCompanionMissing == 0 && demoNetworkSummary.decodedSoundResourceMisses == 0) ? L"ok" : L"missing") + L"]"
      + L" export=unavailable(native-encoder-missing)";
    SetWindowTextW(window, (titleBase + playbackSuffix).c_str());
  };
  refreshWindowTitle();
  if (!renderer.initialize(window)) {
    DestroyWindow(window);
    return 12;
  }
  renderer.setWorldMaterialParams(mapMaterialParams);
  tf2::native::ModelMetadata modelMetadata;
  bool modelReady = false;
  if (!commandModel.empty()) {
    auto inspection = tf2::native::ModelLoader::inspect(commandModel);
    modelAttachmentStatus = inspection.attachmentStatus;
    modelBodygroupStatus = inspection.bodygroupStatus;
    modelViewModelStatus = inspection.viewModelStatus;
    modelBoneCount = inspection.metadata.bones.size();
    modelAttachmentCount = inspection.metadata.attachments.size();
    modelBodyPartCount = inspection.metadata.bodyParts.size();
    modelCompanionMissing = static_cast<std::size_t>(!inspection.vvd.exists)
      + static_cast<std::size_t>(!inspection.vtx.exists && !inspection.vtxDx80.exists && !inspection.vtxSw.exists);
    modelVpkHits = inspection.metadata.valid && inspection.mdl.diagnostic.find("VPK") != std::string::npos ? 1u : 0u;
    if (!inspection.metadata.indices.empty()) {
      std::string modelError;
      modelReady = tf2::native::ModelLoader::buildBindPoseMesh(inspection.metadata, 0, modelError);
      modelPosePreflight = inspection.mdl.signatureValid && inspection.vvd.signatureValid
        && modelReady && modelBoneCount > 0;
      modelMetadata = std::move(inspection.metadata);
    }
  }
  const auto reloadGpuResources = [&]() {
    bool uploadedTexture = mapRgba.empty() && displayRgba.empty();
    if (!mapRgba.empty()) uploadedTexture = renderer.uploadTexture(mapRgba, mapWidth, mapHeight);
    else if (!displayRgba.empty()) uploadedTexture = renderer.uploadTexture(displayRgba, displayWidth, displayHeight);
    bool uploadedWorld = displayMap.triangles.empty();
    if (!renderer.uploadWorldAuxTextures(mapBumpRgba, mapBumpWidth, mapBumpHeight, mapEnvRgba, mapEnvWidth, mapEnvHeight)) return false;
    if (!uploadedWorld && !worldTextures.empty()) uploadedWorld = renderer.uploadWorldGeometry(displayMap, worldTextures);
    if (!uploadedWorld) uploadedWorld = renderer.uploadWorldGeometry(displayMap, mapMaterialName, mapWidth, mapHeight);
    const bool uploadedModel = !modelReady || renderer.uploadBindPoseModel(modelMetadata.bindPoseVertices);
    bool uploadedBones = true;
    if (modelReady && !modelMetadata.bones.empty()) {
      std::vector<std::array<float, 16>> boneMatrices;
      boneMatrices.reserve(modelMetadata.bones.size());
      bool poseMatricesValid = modelMetadata.bones.size() <= 128;
      for (const auto& bone : modelMetadata.bones) {
        if (!bone.poseToBoneValid) { poseMatricesValid = false; break; }
        boneMatrices.push_back(poseToBoneToMatrix4x4(bone.poseToBone));
      }
      uploadedBones = poseMatricesValid && renderer.uploadBoneMatrices(boneMatrices, true);
    }
    return uploadedTexture && uploadedWorld && uploadedModel && uploadedBones;
  };
  if (!reloadGpuResources()) {
    renderer.shutdown();
    g_renderer = nullptr;
    DestroyWindow(window);
    return 14;
  }
  ShowWindow(window, showCommand);
  UpdateWindow(window);

  LARGE_INTEGER frequency{};
  LARGE_INTEGER lastFrame{};
  if (!QueryPerformanceFrequency(&frequency) || frequency.QuadPart <= 0
      || !QueryPerformanceCounter(&lastFrame)) {
    renderer.shutdown();
    g_renderer = nullptr;
    return 13;
  }
  unsigned deviceRecoveryAttempts = 0;
  MSG message{};
  bool running = true;
  double tickAccumulator = 0.0;
  std::int32_t lastAudioTick = -1;
  bool lastReverse = false;
  bool lastPaused = false;
  std::vector<tf2::native::EntityState> currentEntityStates;
  soundScheduler.reset(0);
  while (running) {
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
      if (message.message == WM_QUIT) { running = false; break; }
      TranslateMessage(&message);
      DispatchMessageW(&message);
    }
    if (!running) break;

    LARGE_INTEGER now{};
    QueryPerformanceCounter(&now);
    const double measuredElapsed = static_cast<double>(now.QuadPart - lastFrame.QuadPart)
      / static_cast<double>(frequency.QuadPart);
    // Do not fast-forward a long interval after a debugger break, resize, or
    // temporary OS stall. The next frame resumes at the current playback rate.
    const double elapsed = std::clamp(measuredElapsed, 0.0, 0.25);
    if (measuredElapsed > 0.25) tickAccumulator = 0.0;
    if (g_playback.paused != lastPaused) {
      if (g_playback.paused) soundScheduler.stop();
      else soundScheduler.reset(g_playback.tick);
      lastPaused = g_playback.paused;
    }
    if (g_playback.enabled && !g_playback.paused) {
      const bool directionChanged = g_playback.reverse != lastReverse;
      if (directionChanged) {
        tickAccumulator = 0.0;
        lastReverse = g_playback.reverse;
        soundScheduler.reset(g_playback.tick);
      }
      tickAccumulator += elapsed * g_playback.tickRate * g_playback.speed;
      while (tickAccumulator >= 1.0) {
        if (g_playback.reverse) {
          if (g_playback.tick <= 0) { tickAccumulator = 0.0; break; }
          --g_playback.tick;
        } else {
          if (g_playback.tick >= g_playback.endTick) { tickAccumulator = 0.0; break; }
          ++g_playback.tick;
        }
        tickAccumulator -= 1.0;
      }
      if (!g_playback.reverse) {
        if (g_playback.tick < lastAudioTick) soundScheduler.reset(g_playback.tick);
        float listenerX = 0.0f, listenerY = 0.0f, listenerZ = 0.25f;
        if (renderer.observerFocusWorld(listenerX, listenerY, listenerZ)) {
          soundScheduler.setListenerPosition(listenerX, listenerY, listenerZ);
        }
        soundScheduler.advance(soundTimeline, g_playback.tick);
        hudAudioPlayed = soundScheduler.stats().eventsPlayed;
        hudAudioMissing = soundScheduler.stats().eventsMissingResource;
        lastAudioTick = g_playback.tick;
      }
    }
    if (g_renderer && g_playback.enabled) {
      entitySnapshotStatus = tf2::native::queryEntitySnapshotAtOrBeforeTick(
          demoNetworkSummary, g_playback.tick, currentEntityStates);
      if (entitySnapshotStatus == tf2::native::EntitySnapshotQueryStatus::Available) {
        bool focused = false;
        const auto tryFocusEntity = [&](std::size_t entityIndex) {
          if (entityIndex >= currentEntityStates.size()) return false;
          const auto& entity = currentEntityStates[entityIndex];
          bool playerClass = entity.classId >= 0 &&
            static_cast<std::size_t>(entity.classId) < demoNetworkSummary.serverClassSchemas.size();
          if (playerClass) {
            const auto& entityClassName = demoNetworkSummary.serverClassSchemas[static_cast<std::size_t>(entity.classId)].name;
            playerClass = entityClassName.find("Player") != std::string::npos || entityClassName.find("TFPlayer") != std::string::npos;
          }
          if (!playerClass) return false;
          const auto origin = entity.properties.find("m_vecOrigin");
          if (origin == entity.properties.end() || origin->second.type != tf2::native::SendPropType::Vector) return false;
          if (!std::isfinite(origin->second.x) || !std::isfinite(origin->second.y) || !std::isfinite(origin->second.z)) return false;
          renderer.setObserverFocusWorld(origin->second.x, origin->second.y, origin->second.z);
          return true;
        };
        if (demoNetworkSummary.lastViewEntity < currentEntityStates.size()) {
          focused = tryFocusEntity(demoNetworkSummary.lastViewEntity);
        }
        for (std::size_t entityIndex = 0; !focused && entityIndex < currentEntityStates.size(); ++entityIndex) {
          focused = tryFocusEntity(entityIndex);
        }
        if (!focused) currentEntityStates.clear();
      }
      renderer.setProjectileTimeline(demoNetworkSummary.projectileTimeline, g_playback.tick);
      renderer.setCpuParticleTimeline(demoNetworkSummary.projectileTimeline, g_playback.tick);
      hudTrailVertices = renderer.projectileVertexCount();
    }
    refreshWindowTitle();
    if (elapsed >= kTargetFrameSeconds) {
      if (g_renderer) renderer.draw(0.055f, 0.07f, 0.085f);
      lastFrame = now;
      continue;
    }

    const double remainingMs = (kTargetFrameSeconds - elapsed) * 1000.0;
    const DWORD waitMs = static_cast<DWORD>(std::max(0.0, std::min(remainingMs, 1.0)));
    MsgWaitForMultipleObjectsEx(0, nullptr, waitMs, QS_ALLINPUT, MWMO_INPUTAVAILABLE);

    if (g_renderer && renderer.lastError() != S_OK) {
      const HRESULT error = renderer.lastError();
      if (error == DXGI_ERROR_DEVICE_REMOVED || error == DXGI_ERROR_DEVICE_RESET) {
        if (deviceRecoveryAttempts++ >= 1) {
          PostQuitMessage(4);
          continue;
        }
        renderer.shutdown();
        if (!renderer.initialize(window) || !reloadGpuResources()) {
          PostQuitMessage(2);
        }
      } else {
        PostQuitMessage(3);
      }
    }
  }
  renderer.shutdown();
  persistent.render = renderer.settings();
  if (assets.valid()) persistent.tfRoot = assets.tfDirectory;
  tf2::native::saveSettings(persistent);
  g_renderer = nullptr;
  return static_cast<int>(message.wParam);
}
