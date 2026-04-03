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

    static constexpr std::array<const char*, 3> kKnownPrefixes = {
        "mixamorig",
        "betajoints",
        "betasurface"
    };
    for (const char* prefix : kKnownPrefixes) {
        const std::size_t prefixLength = std::char_traits<char>::length(prefix);
        if (compact.size() >= prefixLength && compact.compare(0, prefixLength, prefix) == 0) {
            compact.erase(0, prefixLength);
            break;
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

constexpr std::array<const char*, 5> kHipsAliases = {"Pelvis", "Hips", "mixamorig:Hips", "Beta_Joints:Hips", "Beta_Surface:Hips"};
constexpr std::array<const char*, 5> kSpineAliases = {"Spine", "Spine1", "mixamorig:Spine", "Beta_Joints:Spine", "Beta_Surface:Spine"};
constexpr std::array<const char*, 7> kChestAliases = {"Chest", "Spine2", "UpperChest", "mixamorig:Spine1", "mixamorig:Spine2", "Beta_Joints:Spine2", "Beta_Surface:Spine2"};
constexpr std::array<const char*, 4> kNeckAliases = {"Neck", "mixamorig:Neck", "Beta_Joints:Neck", "Beta_Surface:Neck"};
constexpr std::array<const char*, 4> kHeadAliases = {"Head", "mixamorig:Head", "Beta_Joints:Head", "Beta_Surface:Head"};
constexpr std::array<const char*, 7> kUpperArmLAliases = {"UpperArm_L", "LeftArm", "LeftUpperArm", "Arm_L", "mixamorig:LeftArm", "Beta_Joints:LeftArm", "Beta_Surface:LeftArm"};
constexpr std::array<const char*, 7> kLowerArmLAliases = {"LowerArm_L", "LeftForeArm", "LeftLowerArm", "ForeArm_L", "mixamorig:LeftForeArm", "Beta_Joints:LeftForeArm", "Beta_Surface:LeftForeArm"};
constexpr std::array<const char*, 5> kHandLAliases = {"Hand_L", "LeftHand", "mixamorig:LeftHand", "Beta_Joints:LeftHand", "Beta_Surface:LeftHand"};
constexpr std::array<const char*, 7> kUpperArmRAliases = {"UpperArm_R", "RightArm", "RightUpperArm", "Arm_R", "mixamorig:RightArm", "Beta_Joints:RightArm", "Beta_Surface:RightArm"};
constexpr std::array<const char*, 7> kLowerArmRAliases = {"LowerArm_R", "RightForeArm", "RightLowerArm", "ForeArm_R", "mixamorig:RightForeArm", "Beta_Joints:RightForeArm", "Beta_Surface:RightForeArm"};
constexpr std::array<const char*, 5> kHandRAliases = {"Hand_R", "RightHand", "mixamorig:RightHand", "Beta_Joints:RightHand", "Beta_Surface:RightHand"};
constexpr std::array<const char*, 6> kUpperLegLAliases = {"UpperLeg_L", "LeftUpLeg", "LeftThigh", "mixamorig:LeftUpLeg", "Beta_Joints:LeftUpLeg", "Beta_Surface:LeftUpLeg"};
constexpr std::array<const char*, 6> kLowerLegLAliases = {"LowerLeg_L", "LeftLeg", "LeftCalf", "mixamorig:LeftLeg", "Beta_Joints:LeftLeg", "Beta_Surface:LeftLeg"};
constexpr std::array<const char*, 6> kUpperLegRAliases = {"UpperLeg_R", "RightUpLeg", "RightThigh", "mixamorig:RightUpLeg", "Beta_Joints:RightUpLeg", "Beta_Surface:RightUpLeg"};
constexpr std::array<const char*, 6> kLowerLegRAliases = {"LowerLeg_R", "RightLeg", "RightCalf", "mixamorig:RightLeg", "Beta_Joints:RightLeg", "Beta_Surface:RightLeg"};
constexpr std::array<const char*, 6> kLeftShoulderAliases = {"Clavicle_L", "Shoulder_L", "LeftShoulder", "mixamorig:LeftShoulder", "Beta_Joints:LeftShoulder", "Beta_Surface:LeftShoulder"};
constexpr std::array<const char*, 6> kRightShoulderAliases = {"Clavicle_R", "Shoulder_R", "RightShoulder", "mixamorig:RightShoulder", "Beta_Joints:RightShoulder", "Beta_Surface:RightShoulder"};
constexpr std::array<const char*, 5> kLeftFootAliases = {"Foot_L", "LeftFoot", "mixamorig:LeftFoot", "Beta_Joints:LeftFoot", "Beta_Surface:LeftFoot"};
constexpr std::array<const char*, 5> kRightFootAliases = {"Foot_R", "RightFoot", "mixamorig:RightFoot", "Beta_Joints:RightFoot", "Beta_Surface:RightFoot"};

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
