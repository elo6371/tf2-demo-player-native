#include "entity_model.h"

#include <cmath>
#include <cstring>
#include <unordered_set>

namespace tf2::native {
namespace {

bool suffixMatch(const std::string& name, const char* suffix) {
  const auto length = std::strlen(suffix);
  if (name.size() == length && name == suffix) return true;
  if (name.size() > length && name.compare(name.size() - length, length, suffix) == 0) {
    return name[name.size() - length - 1] == '.';
  }
  return false;
}

const EntityPropertyValue* findProperty(const EntityState& state, const char* suffix) {
  for (const auto& [name, value] : state.properties) {
    if (suffixMatch(name, suffix)) return &value;
  }
  return nullptr;
}

bool isPlayerClassName(const std::string& className) {
  return className.find("TFPlayer") != std::string::npos || className.find("Player") != std::string::npos;
}

bool isViewModelPath(const std::string& path) {
  const auto slash = path.find_last_of("/\\");
  const auto file = slash == std::string::npos ? path : path.substr(slash + 1);
  return file.size() > 6 && file.rfind("v_", 0) == 0 && file.size() >= 4
    && file.compare(file.size() - 4, 4, ".mdl") == 0;
}

} // namespace

ModelInstanceTransform EntityModelResolver::extractTransform(const EntityState& state) {
  ModelInstanceTransform transform;
  if (const auto* origin = findProperty(state, "m_vecOrigin")) {
    if ((origin->type == SendPropType::Vector || origin->type == SendPropType::VectorXY)
        && std::isfinite(origin->x) && std::isfinite(origin->y) && std::isfinite(origin->z)) {
      transform.hasOrigin = true;
      transform.origin[0] = origin->x;
      transform.origin[1] = origin->y;
      transform.origin[2] = origin->z;
    }
  }
  if (const auto* angles = findProperty(state, "m_angEyeAngles")) {
    if ((angles->type == SendPropType::Vector || angles->type == SendPropType::VectorXY)
        && std::isfinite(angles->x) && std::isfinite(angles->y) && std::isfinite(angles->z)) {
      transform.hasAngles = true;
      transform.angles[0] = angles->x;
      transform.angles[1] = angles->y;
      transform.angles[2] = angles->z;
    }
  }
  if (!transform.hasAngles) {
    const auto* pitch = findProperty(state, "m_angEyeAngles[0]");
    const auto* yaw = findProperty(state, "m_angEyeAngles[1]");
    if (pitch && yaw && std::isfinite(pitch->x) && std::isfinite(yaw->x)) {
      transform.hasAngles = true;
      transform.angles[0] = pitch->type == SendPropType::Float || pitch->type == SendPropType::Int ? (pitch->type == SendPropType::Int ? static_cast<float>(pitch->intValue) : pitch->x) : pitch->x;
      transform.angles[1] = yaw->type == SendPropType::Int ? static_cast<float>(yaw->intValue) : yaw->x;
      transform.angles[2] = 0.0f;
    }
  }
  if (!transform.hasAngles) {
    if (const auto* rotation = findProperty(state, "m_angRotation")) {
      if ((rotation->type == SendPropType::Vector || rotation->type == SendPropType::VectorXY)
          && std::isfinite(rotation->x) && std::isfinite(rotation->y) && std::isfinite(rotation->z)) {
        transform.hasAngles = true;
        transform.angles[0] = rotation->x;
        transform.angles[1] = rotation->y;
        transform.angles[2] = rotation->z;
      }
    }
  }
  if (const auto* team = findProperty(state, "m_iTeamNum")) {
    if (team->type == SendPropType::Int) {
      transform.hasTeam = true;
      transform.team = team->intValue;
    }
  }
  if (const auto* skin = findProperty(state, "m_nSkin")) {
    if (skin->type == SendPropType::Int) {
      transform.hasSkin = true;
      transform.skin = skin->intValue;
    }
  }
  if (const auto* playerClass = findProperty(state, "m_iClass")) {
    if (playerClass->type == SendPropType::Int && playerClass->intValue >= 1 && playerClass->intValue <= 9) {
      transform.hasPlayerClass = true;
      transform.playerClass = playerClass->intValue;
    }
  }
  if (!transform.hasOrigin) transform.diagnostic = "origin missing";
  else if (!transform.hasAngles) transform.diagnostic = "angles missing";
  return transform;
}

std::string EntityModelResolver::defaultPlayerModelPath(std::int64_t tfClass) {
  switch (tfClass) {
    case 1: return "models/player/scout.mdl";
    case 2: return "models/player/sniper.mdl";
    case 3: return "models/player/soldier.mdl";
    case 4: return "models/player/demo.mdl";
    case 5: return "models/player/medic.mdl";
    case 6: return "models/player/heavy.mdl";
    case 7: return "models/player/pyro.mdl";
    case 8: return "models/player/spy.mdl";
    case 9: return "models/player/engineer.mdl";
    default: return {};
  }
}

std::vector<ModelInstance> EntityModelResolver::buildInstances(
    const std::vector<ModelRenderRequest>& requests,
    const std::vector<EntityState>& statesByIndex,
    const std::vector<ServerClassSchema>& classSchemas,
    std::size_t maxInstances) {
  if (maxInstances == 0 || maxInstances > kMaxInstances) maxInstances = kMaxInstances;
  std::vector<ModelInstance> instances;
  instances.reserve(std::min(requests.size() + 64u, maxInstances));
  std::unordered_set<std::uint16_t> covered;
  covered.reserve(requests.size() + 64u);

  auto classNameOf = [&](std::int32_t classId) -> std::string {
    if (classId < 0) return {};
    const auto index = static_cast<std::size_t>(classId);
    if (index >= classSchemas.size()) return {};
    return classSchemas[index].name;
  };

  for (const auto& request : requests) {
    if (instances.size() >= maxInstances) break;
    ModelInstance instance;
    instance.entityIndex = request.entityIndex;
    instance.classId = request.classId;
    instance.className = request.className.empty() ? classNameOf(request.classId) : request.className;
    instance.modelPath = request.modelPath;
    instance.cacheKey = request.cacheKey;
    instance.checksum = request.checksum;
    instance.resolution = request.resolution;
    instance.companionSetComplete = request.companionSetComplete;
    instance.renderable = request.renderable;
    instance.missingAssetFallback = !request.renderable;
    instance.viewModelSkipped = isViewModelPath(request.modelPath);
    instance.diagnostic = request.diagnostic;
    if (instance.viewModelSkipped) {
      instance.renderable = false;
      instance.diagnostic += "; world pass skips viewmodel";
    }
    if (request.entityIndex < statesByIndex.size()) {
      instance.transform = extractTransform(statesByIndex[request.entityIndex]);
    } else {
      instance.transform.diagnostic = "entity index outside snapshot";
    }
    covered.insert(request.entityIndex);
    instances.push_back(std::move(instance));
  }

  for (std::size_t entityIndex = 0; entityIndex < statesByIndex.size() && instances.size() < maxInstances; ++entityIndex) {
    if (entityIndex > 0xffffu) break;
    const auto index16 = static_cast<std::uint16_t>(entityIndex);
    if (covered.count(index16)) continue;
    const auto& state = statesByIndex[entityIndex];
    const auto className = classNameOf(state.classId);
    if (!isPlayerClassName(className)) continue;
    const auto transform = extractTransform(state);
    if (!transform.hasPlayerClass) continue;
    const auto path = defaultPlayerModelPath(transform.playerClass);
    if (path.empty()) continue;
    ModelInstance instance;
    instance.entityIndex = index16;
    instance.classId = state.classId;
    instance.className = className;
    instance.modelPath = path;
    instance.cacheKey = path + "#class-fallback";
    instance.transform = transform;
    instance.resolution = ModelAssetResolution::Unknown;
    instance.playerClassFallback = true;
    instance.missingAssetFallback = true;
    instance.diagnostic = "player class fallback model path";
    instances.push_back(std::move(instance));
  }
  return instances;
}

bool buildEntityModelInstanceRows(
    const float origin[3],
    const float angles[3],
    bool hasAngles,
    const EntityModelWorldMap& world,
    EntityModelInstanceRows& rows) {
  if (!origin || !std::isfinite(origin[0]) || !std::isfinite(origin[1]) || !std::isfinite(origin[2])) return false;
  if (!std::isfinite(world.centerX) || !std::isfinite(world.centerY) || !std::isfinite(world.minZ)
      || !std::isfinite(world.horizontalScale) || !std::isfinite(world.zScale)
      || !(world.horizontalScale > 0.0f) || !(world.zScale > 0.0f)) return false;
  float m00 = 1.0f, m01 = 0.0f, m02 = 0.0f;
  float m10 = 0.0f, m11 = 1.0f, m12 = 0.0f;
  float m20 = 0.0f, m21 = 0.0f, m22 = 1.0f;
  if (hasAngles) {
    if (!angles || !std::isfinite(angles[0]) || !std::isfinite(angles[1]) || !std::isfinite(angles[2])) return false;
    const float deg = 0.01745329252f;
    const float sp = std::sin(angles[0] * deg), cp = std::cos(angles[0] * deg);
    const float sy = std::sin(angles[1] * deg), cy = std::cos(angles[1] * deg);
    const float sr = std::sin(angles[2] * deg), cr = std::cos(angles[2] * deg);
    m00 = cp * cy;
    m10 = cp * sy;
    m20 = -sp;
    m01 = sr * sp * cy + cr * -sy;
    m11 = sr * sp * sy + cr * cy;
    m21 = sr * cp;
    m02 = cr * sp * cy + sr * sy;
    m12 = cr * sp * sy + -sr * cy;
    m22 = cr * cp;
  }
  const float horizontal = world.horizontalScale;
  const float zScale = world.zScale;
  rows.positionRows[0] = horizontal * m00;
  rows.positionRows[1] = horizontal * m01;
  rows.positionRows[2] = horizontal * m02;
  rows.positionRows[3] = (origin[0] - world.centerX) * horizontal;
  rows.positionRows[4] = horizontal * m10;
  rows.positionRows[5] = horizontal * m11;
  rows.positionRows[6] = horizontal * m12;
  rows.positionRows[7] = (origin[1] - world.centerY) * horizontal;
  rows.positionRows[8] = zScale * m20;
  rows.positionRows[9] = zScale * m21;
  rows.positionRows[10] = zScale * m22;
  rows.positionRows[11] = (origin[2] - world.minZ) * zScale + 0.1f;
  rows.normalRows[0] = m00; rows.normalRows[1] = m01; rows.normalRows[2] = m02;
  rows.normalRows[3] = m10; rows.normalRows[4] = m11; rows.normalRows[5] = m12;
  rows.normalRows[6] = m20; rows.normalRows[7] = m21; rows.normalRows[8] = m22;
  return std::isfinite(rows.positionRows[3]) && std::isfinite(rows.positionRows[7]) && std::isfinite(rows.positionRows[11]);
}

} // namespace tf2::native
