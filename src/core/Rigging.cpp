#include "sparks/core/Rigging.hpp"

#include <algorithm>
#include <array>
#include <cctype>
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

int findRigBoneByAliases(const std::vector<RigBone>& rigBones, std::initializer_list<const char*> aliases) {
    std::vector<std::string> canonicalAliases;
    canonicalAliases.reserve(aliases.size());
    for (const char* alias : aliases) {
        canonicalAliases.push_back(canonicalBoneName(alias));
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
    humanoidBoneMap[0] = findRigBoneByAliases(rigBones, {"Pelvis", "Hips", "mixamorig:Hips", "Beta_Joints:Hips", "Beta_Surface:Hips"});
    humanoidBoneMap[1] = findRigBoneByAliases(rigBones, {"Spine", "Spine1", "mixamorig:Spine", "Beta_Joints:Spine", "Beta_Surface:Spine"});
    humanoidBoneMap[2] = findRigBoneByAliases(rigBones, {"Chest", "Spine2", "UpperChest", "mixamorig:Spine1", "mixamorig:Spine2", "Beta_Joints:Spine2", "Beta_Surface:Spine2"});
    humanoidBoneMap[3] = findRigBoneByAliases(rigBones, {"Neck", "mixamorig:Neck", "Beta_Joints:Neck", "Beta_Surface:Neck"});
    humanoidBoneMap[4] = findRigBoneByAliases(rigBones, {"Head", "mixamorig:Head", "Beta_Joints:Head", "Beta_Surface:Head"});
    humanoidBoneMap[5] = findRigBoneByAliases(rigBones, {"UpperArm_L", "LeftArm", "LeftUpperArm", "Arm_L", "mixamorig:LeftArm", "Beta_Joints:LeftArm", "Beta_Surface:LeftArm"});
    humanoidBoneMap[6] = findRigBoneByAliases(rigBones, {"LowerArm_L", "LeftForeArm", "LeftLowerArm", "ForeArm_L", "mixamorig:LeftForeArm", "Beta_Joints:LeftForeArm", "Beta_Surface:LeftForeArm"});
    humanoidBoneMap[7] = findRigBoneByAliases(rigBones, {"Hand_L", "LeftHand", "mixamorig:LeftHand", "Beta_Joints:LeftHand", "Beta_Surface:LeftHand"});
    humanoidBoneMap[8] = findRigBoneByAliases(rigBones, {"UpperArm_R", "RightArm", "RightUpperArm", "Arm_R", "mixamorig:RightArm", "Beta_Joints:RightArm", "Beta_Surface:RightArm"});
    humanoidBoneMap[9] = findRigBoneByAliases(rigBones, {"LowerArm_R", "RightForeArm", "RightLowerArm", "ForeArm_R", "mixamorig:RightForeArm", "Beta_Joints:RightForeArm", "Beta_Surface:RightForeArm"});
    humanoidBoneMap[10] = findRigBoneByAliases(rigBones, {"Hand_R", "RightHand", "mixamorig:RightHand", "Beta_Joints:RightHand", "Beta_Surface:RightHand"});
    humanoidBoneMap[11] = findRigBoneByAliases(rigBones, {"UpperLeg_L", "LeftUpLeg", "LeftThigh", "mixamorig:LeftUpLeg", "Beta_Joints:LeftUpLeg", "Beta_Surface:LeftUpLeg"});
    humanoidBoneMap[12] = findRigBoneByAliases(rigBones, {"LowerLeg_L", "LeftLeg", "LeftCalf", "mixamorig:LeftLeg", "Beta_Joints:LeftLeg", "Beta_Surface:LeftLeg"});
    humanoidBoneMap[13] = findRigBoneByAliases(rigBones, {"UpperLeg_R", "RightUpLeg", "RightThigh", "mixamorig:RightUpLeg", "Beta_Joints:RightUpLeg", "Beta_Surface:RightUpLeg"});
    humanoidBoneMap[14] = findRigBoneByAliases(rigBones, {"LowerLeg_R", "RightLeg", "RightCalf", "mixamorig:RightLeg", "Beta_Joints:RightLeg", "Beta_Surface:RightLeg"});
}

void enforceHumanoidTPose(std::vector<RigBone>& rigBones) {
    for (std::size_t i = 0; i < rigBones.size(); ++i) {
        rigBones[i].localRotationDegrees = glm::vec3(0.0f);
    }

    const auto setRotation = [&](std::initializer_list<const char*> aliases, const glm::vec3& degrees) {
        const int index = findRigBoneByAliases(rigBones, aliases);
        if (index >= 0) {
            rigBones[static_cast<std::size_t>(index)].localRotationDegrees = degrees;
        }
    };

    setRotation({"Clavicle_L", "Shoulder_L", "LeftShoulder", "mixamorig:LeftShoulder", "Beta_Joints:LeftShoulder", "Beta_Surface:LeftShoulder"}, glm::vec3(0.0f, 0.0f, 90.0f));
    setRotation({"Clavicle_R", "Shoulder_R", "RightShoulder", "mixamorig:RightShoulder", "Beta_Joints:RightShoulder", "Beta_Surface:RightShoulder"}, glm::vec3(0.0f, 0.0f, -90.0f));
    setRotation({"UpperLeg_L", "LeftUpLeg", "LeftThigh", "mixamorig:LeftUpLeg", "Beta_Joints:LeftUpLeg", "Beta_Surface:LeftUpLeg"}, glm::vec3(0.0f, 0.0f, 180.0f));
    setRotation({"UpperLeg_R", "RightUpLeg", "RightThigh", "mixamorig:RightUpLeg", "Beta_Joints:RightUpLeg", "Beta_Surface:RightUpLeg"}, glm::vec3(0.0f, 0.0f, 180.0f));
    setRotation({"Foot_L", "LeftFoot", "mixamorig:LeftFoot", "Beta_Joints:LeftFoot", "Beta_Surface:LeftFoot"}, glm::vec3(-90.0f, 0.0f, 0.0f));
    setRotation({"Foot_R", "RightFoot", "mixamorig:RightFoot", "Beta_Joints:RightFoot", "Beta_Surface:RightFoot"}, glm::vec3(-90.0f, 0.0f, 0.0f));
}

}  // namespace sparks::core::rigging
