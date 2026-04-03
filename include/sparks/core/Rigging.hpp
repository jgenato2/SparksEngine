#pragma once

#include <array>
#include <vector>
#include <string>

#include <glm/vec3.hpp>

struct aiScene;

namespace sparks::core::rigging {

struct RigBone {
    std::string name;
    int parentIndex{-1};
    glm::vec3 localPosition{0.0f, 0.0f, 0.0f};
    glm::vec3 localRotationDegrees{0.0f, 0.0f, 0.0f};
    float length{0.1f};
};

struct VertexGroupInfo {
    std::string name;
    int weightedVertexCount{0};
    float totalWeight{0.0f};
    float maxWeight{0.0f};
};

inline constexpr std::array<const char*, 15> kHumanoidSlotNames = {
    "Hips", "Spine", "Chest", "Neck", "Head",
    "Left Upper Arm", "Left Lower Arm", "Left Hand",
    "Right Upper Arm", "Right Lower Arm", "Right Hand",
    "Left Upper Leg", "Left Lower Leg",
    "Right Upper Leg", "Right Lower Leg"
};

std::vector<RigBone> extractRigBonesFromScene(const aiScene* scene, float unitScale);
std::vector<VertexGroupInfo> extractVertexGroupsFromScene(const aiScene* scene);

void applyDefaultHumanoidMapping(std::array<int, 15>& humanoidBoneMap, const std::vector<RigBone>& rigBones);
void enforceHumanoidTPose(std::vector<RigBone>& rigBones);

}  // namespace sparks::core::rigging
