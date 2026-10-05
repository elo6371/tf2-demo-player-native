#include "asset_root.h"
#include "model_loader.h"
#include "vpk_archive.h"

#include <cmath>
#include <fstream>
#include <iostream>

namespace {

// This is the renderer's existing bind-pose upload shape without requiring a D3D device.
bool consumeRendererModel(const std::vector<tf2::native::ModelDrawVertex>& vertices) {
  if (vertices.empty()) return false;
  for (const auto& vertex : vertices) {
    for (const float value : vertex.position) if (!std::isfinite(value)) return false;
    if (vertex.boneCount > vertex.boneIndices.size()) return false;
  }
  return true;
}

bool writeArchiveFile(const tf2::native::VpkArchive& archive, const std::string& path,
  const std::filesystem::path& destination, std::string& error) {
  const auto bytes = archive.read(path, &error);
  if (bytes.empty()) return false;
  std::ofstream file(destination, std::ios::binary);
  if (!file) return false;
  file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
  return static_cast<bool>(file);
}

}

int main(int argc, char** argv) {
  if (argc != 2) { std::cerr << "usage: model_mesh_adapter_probe <tf-directory>\n"; return 2; }
  const auto root = tf2::native::AssetRoot::fromPath(std::filesystem::u8path(argv[1]));
  tf2::native::VpkArchive archive;
  std::string error;
  if (!root.valid() || !archive.open(root.tfDirectory / "tf2_misc_dir.vpk", &error)) return 1;
  const auto temp = std::filesystem::temp_directory_path() / "tf2-model-mesh-adapter-probe";
  std::error_code ec;
  std::filesystem::remove_all(temp, ec);
  std::filesystem::create_directories(temp / "models", ec);
  const bool extracted = writeArchiveFile(archive, "models/ambulance.mdl", temp / "models/ambulance.mdl", error)
    && writeArchiveFile(archive, "models/ambulance.vvd", temp / "models/ambulance.vvd", error)
    && writeArchiveFile(archive, "models/ambulance.dx90.vtx", temp / "models/ambulance.dx90.vtx", error);
  tf2::native::ModelMeshData mesh;
  std::string meshError;
  bool adapterConsumed = false;
  if (extracted) {
    const auto inspection = tf2::native::ModelLoader::inspect(temp / "models/ambulance.mdl");
    if (tf2::native::ModelLoader::buildModelMeshData(inspection, 0, mesh, meshError)) {
      adapterConsumed = mesh.triangleList && mesh.indices.size() == mesh.vertices.size()
        && mesh.indices.size() % 3u == 0 && consumeRendererModel(mesh.vertices);
    }
  }
  std::cout << "extracted=" << (extracted ? 1 : 0)
            << " mesh_vertices=" << mesh.vertices.size()
            << " mesh_indices=" << mesh.indices.size()
            << " triangle_list=" << (mesh.triangleList ? 1 : 0)
            << " renderer_shape_consumed=" << (adapterConsumed ? 1 : 0) << '\n';
  std::filesystem::remove_all(temp, ec);
  return extracted && adapterConsumed ? 0 : 1;
}
