#include "sparks/core/Rigging.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <string>
#include <unordered_map>
#include <unordered_set>

#define GLM_ENABLE_EXPERIMENTAL
#include <assimp/scene.h>
#include <glm/gtx/euler_angles.hpp>
#include <glm/gtx/quaternion.hpp>

namespace sparks::core::rigging {
namespace {

const aiNode* findNodeByName(const aiNode* node, const std::string& name) {
    if (node == nullptr) {
        return nullptr;
    }

    if (name == node->mName.C_Str()) {
        return node;
    }

    for (unsigned int i = 0; i < node->mNumChildren; ++i) {
        if (const aiNode* found = findNodeByName(node->mChildren[i], name)) {
            return found;
        }
    }

    return nullptr;
}

bool collectRigNodes(const aiNode* node, const std::unordered_set<std::string>& importedBoneNames, std::vector<const aiNode*>& outNodes) {
    if (node == nullptr) {
        return false;
    }

    const bool isImportedBone = importedBoneNames.find(node->mName.C_Str()) != importedBoneNames.end();
    bool childContainsBone = false;
    for (unsigned int i = 0; i < node->mNumChildren; ++i) {
        childContainsBone = collectRigNodes(node->mChildren[i], importedBoneNames, outNodes) || childContainsBone;
    }

    if ((isImportedBone || childContainsBone) && node->mParent != nullptr) {
        outNodes.push_back(node);
    }

    return isImportedBone || childContainsBone;
}

glm::vec3 eulerDegreesFromQuatXYZ(const glm::quat& q) {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    glm::extractEulerAngleXYZ(glm::toMat4(glm::normalize(q)), x, y, z);
    return glm::degrees(glm::vec3(x, y, z));
}

std::string canonicalBoneName(const std::string& name) {
    std::string compact;
    compact.reserve(name.size());
    for (const unsigned char c : name) {
        if (std::isalnum(c)) {
            compact.push_back(static_cast<char>(std::tolower(c)));
        }
    }
    return compact;
}

struct AliasList {
    const char* const* names;
    std::size_t count;
};

template <std::size_t N>
constexpr AliasList makeAliasList(const std::array<const char*, N>& aliases) {
    return AliasList{aliases.data(), aliases.size()};
}

int findRigBoneByAliases(const std::vector<RigBone>& rigBones, const AliasList aliases) {
    std::vector<std::string> canonicalAliases;
    canonicalAliases.reserve(aliases.count);
    for (std::size_t i = 0; i < aliases.count; ++i) {
        canonicalAliases.push_back(canonicalBoneName(aliases.names[i]));
    }

    for (int i = 0; i < static_cast<int>(rigBones.size()); ++i) {
        const std::string canonicalBone = canonicalBoneName(rigBones[static_cast<std::size_t>(i)].name);
        for (const std::string& alias : canonicalAliases) {
            if (canonicalBone == alias) {
                return i;
            }
        }
    }

    return -1;
}

constexpr std::array<const char*, 2> kHipsAliases = {"Pelvis", "Hips"};
constexpr std::array<const char*, 2> kSpineAliases = {"Spine", "Spine1"};
constexpr std::array<const char*, 3> kChestAliases = {"Chest", "Spine2", "UpperChest"};
constexpr std::array<const char*, 1> kNeckAliases = {"Neck"};
constexpr std::array<const char*, 1> kHeadAliases = {"Head"};
constexpr std::array<const char*, 4> kUpperArmLAliases = {"UpperArm_L", "LeftArm", "LeftUpperArm", "Arm_L"};
constexpr std::array<const char*, 4> kLowerArmLAliases = {"LowerArm_L", "LeftForeArm", "LeftLowerArm", "ForeArm_L"};
constexpr std::array<const char*, 2> kHandLAliases = {"Hand_L", "LeftHand"};
constexpr std::array<const char*, 4> kUpperArmRAliases = {"UpperArm_R", "RightArm", "RightUpperArm", "Arm_R"};
constexpr std::array<const char*, 4> kLowerArmRAliases = {"LowerArm_R", "RightForeArm", "RightLowerArm", "ForeArm_R"};
constexpr std::array<const char*, 2> kHandRAliases = {"Hand_R", "RightHand"};
constexpr std::array<const char*, 3> kUpperLegLAliases = {"UpperLeg_L", "LeftUpLeg", "LeftThigh"};
constexpr std::array<const char*, 3> kLowerLegLAliases = {"LowerLeg_L", "LeftLeg", "LeftCalf"};
constexpr std::array<const char*, 3> kUpperLegRAliases = {"UpperLeg_R", "RightUpLeg", "RightThigh"};
constexpr std::array<const char*, 3> kLowerLegRAliases = {"LowerLeg_R", "RightLeg", "RightCalf"};
constexpr std::array<const char*, 3> kLeftShoulderAliases = {"Clavicle_L", "Shoulder_L", "LeftShoulder"};
constexpr std::array<const char*, 3> kRightShoulderAliases = {"Clavicle_R", "Shoulder_R", "RightShoulder"};
constexpr std::array<const char*, 2> kLeftFootAliases = {"Foot_L", "LeftFoot"};
constexpr std::array<const char*, 2> kRightFootAliases = {"Foot_R", "RightFoot"};

struct HumanoidMappingRule {
    std::size_t slot;
    AliasList aliases;
};

constexpr std::array<HumanoidMappingRule, 15> kHumanoidMappingRules = {{
    {0, makeAliasList(kHipsAliases)},
    {1, makeAliasList(kSpineAliases)},
    {2, makeAliasList(kChestAliases)},
    {3, makeAliasList(kNeckAliases)},
    {4, makeAliasList(kHeadAliases)},
    {5, makeAliasList(kUpperArmLAliases)},
    {6, makeAliasList(kLowerArmLAliases)},
    {7, makeAliasList(kHandLAliases)},
    {8, makeAliasList(kUpperArmRAliases)},
    {9, makeAliasList(kLowerArmRAliases)},
    {10, makeAliasList(kHandRAliases)},
    {11, makeAliasList(kUpperLegLAliases)},
    {12, makeAliasList(kLowerLegLAliases)},
    {13, makeAliasList(kUpperLegRAliases)},
    {14, makeAliasList(kLowerLegRAliases)},
}};

struct TPoseRule {
    AliasList aliases;
    glm::vec3 rotationDegrees;
};

const std::array<TPoseRule, 6> kHumanoidTPoseRules = {{
    {makeAliasList(kLeftShoulderAliases), glm::vec3(0.0f, 0.0f, 90.0f)},
    {makeAliasList(kRightShoulderAliases), glm::vec3(0.0f, 0.0f, -90.0f)},
    {makeAliasList(kUpperLegLAliases), glm::vec3(0.0f, 0.0f, 180.0f)},
    {makeAliasList(kUpperLegRAliases), glm::vec3(0.0f, 0.0f, 180.0f)},
    {makeAliasList(kLeftFootAliases), glm::vec3(-90.0f, 0.0f, 0.0f)},
    {makeAliasList(kRightFootAliases), glm::vec3(-90.0f, 0.0f, 0.0f)},
}};

}  // namespace

std::vector<RigBone> extractRigBonesFromScene(const aiScene* scene, const float unitScale) {
    if (scene == nullptr || scene->mRootNode == nullptr || !scene->HasMeshes()) {
        return {};
    }

    std::unordered_set<std::string> importedBoneNames;
    for (unsigned int meshIndex = 0; meshIndex < scene->mNumMeshes; ++meshIndex) {
        const aiMesh* mesh = scene->mMeshes[meshIndex];
        if (mesh == nullptr || !mesh->HasBones()) {
            continue;
        }

        for (unsigned int boneIndex = 0; boneIndex < mesh->mNumBones; ++boneIndex) {
            const aiBone* bone = mesh->mBones[boneIndex];
            if (bone == nullptr) {
                continue;
            }
            importedBoneNames.insert(bone->mName.C_Str());
        }
    }

    if (importedBoneNames.empty()) {
        return {};
    }

    std::vector<const aiNode*> rigNodes;
    collectRigNodes(scene->mRootNode, importedBoneNames, rigNodes);

    std::vector<RigBone> rigBones;
    rigBones.reserve(rigNodes.size());
    std::unordered_map<std::string, int> boneIndexByName;

    for (const aiNode* node : rigNodes) {
        const std::string nodeName = node->mName.C_Str();
        if (boneIndexByName.find(nodeName) != boneIndexByName.end()) {
            continue;
        }

        aiVector3D scaling;
        aiVector3D position;
        aiQuaternion rotation;
        aiMatrix4x4 localTransform = node->mTransformation;
        localTransform.Decompose(scaling, rotation, position);

        RigBone bone;
        bone.name = nodeName;
        bone.localPosition = glm::vec3(position.x, position.y, position.z) * unitScale;
        bone.localRotationDegrees = eulerDegreesFromQuatXYZ(glm::quat(rotation.w, rotation.x, rotation.y, rotation.z));
        bone.length = 0.1f;

        for (unsigned int c = 0; c < node->mNumChildren; ++c) {
            const aiNode* child = node->mChildren[c];
            if (child == nullptr) {
                continue;
            }

            aiVector3D childScaling;
            aiVector3D childPosition;
            aiQuaternion childRotation;
            aiMatrix4x4 childTransform = child->mTransformation;
            childTransform.Decompose(childScaling, childRotation, childPosition);
            const float inferredLength = glm::length(glm::vec3(childPosition.x, childPosition.y, childPosition.z) * unitScale);
            if (inferredLength > 0.001f) {
                bone.length = inferredLength;
                break;
            }
        }

        boneIndexByName[bone.name] = static_cast<int>(rigBones.size());
        rigBones.push_back(bone);
    }

    for (std::size_t i = 0; i < rigBones.size(); ++i) {
        const aiNode* node = findNodeByName(scene->mRootNode, rigBones[i].name);
        if (node == nullptr || node->mParent == nullptr) {
            continue;
        }

        const std::string parentName = node->mParent->mName.C_Str();
        const auto found = boneIndexByName.find(parentName);
        rigBones[i].parentIndex = (found != boneIndexByName.end()) ? found->second : -1;
    }

    return rigBones;
}

std::vector<VertexGroupInfo> extractVertexGroupsFromScene(const aiScene* scene) {
    if (scene == nullptr || !scene->HasMeshes()) {
        return {};
    }

    std::unordered_map<std::string, VertexGroupInfo> groups;
    for (unsigned int meshIndex = 0; meshIndex < scene->mNumMeshes; ++meshIndex) {
        const aiMesh* mesh = scene->mMeshes[meshIndex];
        if (mesh == nullptr || !mesh->HasBones()) {
            continue;
        }

        for (unsigned int boneIndex = 0; boneIndex < mesh->mNumBones; ++boneIndex) {
            const aiBone* bone = mesh->mBones[boneIndex];
            if (bone == nullptr) {
                continue;
            }

            const std::string name = bone->mName.C_Str();
            VertexGroupInfo& group = groups[name];
            if (group.name.empty()) {
                group.name = name;
            }

            for (unsigned int w = 0; w < bone->mNumWeights; ++w) {
                const float weight = bone->mWeights[w].mWeight;
                if (weight <= 0.0f) {
                    continue;
                }
                ++group.weightedVertexCount;
                group.totalWeight += weight;
                group.maxWeight = std::max(group.maxWeight, weight);
            }
        }
    }

    std::vector<VertexGroupInfo> result;
    result.reserve(groups.size());
    for (auto& entry : groups) {
        result.push_back(entry.second);
    }

    std::sort(
        result.begin(),
        result.end(),
        [](const VertexGroupInfo& a, const VertexGroupInfo& b) {
            if (a.weightedVertexCount != b.weightedVertexCount) {
                return a.weightedVertexCount > b.weightedVertexCount;
            }
            return a.name < b.name;
        });

    return result;
}

void applyDefaultHumanoidMapping(std::array<int, 15>& humanoidBoneMap, const std::vector<RigBone>& rigBones) {
    for (const HumanoidMappingRule& rule : kHumanoidMappingRules) {
        humanoidBoneMap[rule.slot] = findRigBoneByAliases(rigBones, rule.aliases);
    }
}

void enforceHumanoidTPose(std::vector<RigBone>& rigBones) {
    for (std::size_t i = 0; i < rigBones.size(); ++i) {
        rigBones[i].localRotationDegrees = glm::vec3(0.0f);
    }

    for (const TPoseRule& rule : kHumanoidTPoseRules) {
        const int index = findRigBoneByAliases(rigBones, rule.aliases);
        if (index >= 0) {
            rigBones[static_cast<std::size_t>(index)].localRotationDegrees = rule.rotationDegrees;
        }
    }
}

}  // namespace sparks::core::rigging
