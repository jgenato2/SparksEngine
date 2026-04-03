#include "sparks/core/FbxImport.hpp"

#include <algorithm>
#include <filesystem>
#include <limits>

#define GLM_ENABLE_EXPERIMENTAL
#include <assimp/config.h>
#include <assimp/Importer.hpp>
#include <assimp/material.h>
#include <assimp/postprocess.h>
#include <assimp/scene.h>
#include <glm/gtx/euler_angles.hpp>
#include <glm/gtx/quaternion.hpp>
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

namespace {

using sparks::core::rigging::extractRigBonesFromScene;
using sparks::core::rigging::extractVertexGroupsFromScene;

glm::vec3 eulerDegreesFromQuatXYZ(const glm::quat& q) {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    glm::extractEulerAngleXYZ(glm::toMat4(glm::normalize(q)), x, y, z);
    return glm::degrees(glm::vec3(x, y, z));
}

float readFbxUnitScale(const aiScene* scene) {
    if (scene == nullptr || scene->mMetaData == nullptr) {
        return 1.0f;
    }

    ai_real unitScaleFactor = 0.0;
    if (!scene->mMetaData->Get("UnitScaleFactor", unitScaleFactor) || unitScaleFactor <= 0.0) {
        return 1.0f;
    }

    return static_cast<float>(unitScaleFactor / 100.0);
}

glm::mat4 toGlmMatrix(const aiMatrix4x4& matrix) {
    glm::mat4 result(1.0f);
    result[0][0] = matrix.a1;
    result[1][0] = matrix.a2;
    result[2][0] = matrix.a3;
    result[3][0] = matrix.a4;
    result[0][1] = matrix.b1;
    result[1][1] = matrix.b2;
    result[2][1] = matrix.b3;
    result[3][1] = matrix.b4;
    result[0][2] = matrix.c1;
    result[1][2] = matrix.c2;
    result[2][2] = matrix.c3;
    result[3][2] = matrix.c4;
    result[0][3] = matrix.d1;
    result[1][3] = matrix.d2;
    result[2][3] = matrix.d3;
    result[3][3] = matrix.d4;
    return result;
}

const aiNode* findMeshNode(const aiNode* node, const unsigned int meshIndex) {
    if (node == nullptr) {
        return nullptr;
    }

    for (unsigned int i = 0; i < node->mNumMeshes; ++i) {
        if (node->mMeshes[i] == meshIndex) {
            return node;
        }
    }

    for (unsigned int i = 0; i < node->mNumChildren; ++i) {
        if (const aiNode* found = findMeshNode(node->mChildren[i], meshIndex)) {
            return found;
        }
    }

    return nullptr;
}

bool findMeshGlobalTransform(const aiNode* currentNode, const aiNode* targetNode, const glm::mat4& parentTransform, glm::mat4& globalTransform) {
    if (currentNode == nullptr) {
        return false;
    }

    const glm::mat4 currentTransform = parentTransform * toGlmMatrix(currentNode->mTransformation);
    if (currentNode == targetNode) {
        globalTransform = currentTransform;
        return true;
    }

    for (unsigned int i = 0; i < currentNode->mNumChildren; ++i) {
        if (findMeshGlobalTransform(currentNode->mChildren[i], targetNode, currentTransform, globalTransform)) {
            return true;
        }
    }

    return false;
}

}  // namespace

namespace sparks::core {

std::optional<render::ImportedModelData> loadFbxModel(
    const std::string& filePath,
    std::string& errorMessage,
    std::vector<rigging::RigBone>* importedRigBones,
    std::vector<rigging::VertexGroupInfo>* importedVertexGroups) {
    Assimp::Importer importer;
    importer.SetPropertyBool(AI_CONFIG_IMPORT_FBX_PRESERVE_PIVOTS, false);
    const aiScene* scene = importer.ReadFile(
        filePath,
        aiProcess_Triangulate |
            aiProcess_GenSmoothNormals |
            aiProcess_JoinIdenticalVertices |
            aiProcess_ImproveCacheLocality);

    if (scene == nullptr || !scene->HasMeshes()) {
        errorMessage = importer.GetErrorString();
        return std::nullopt;
    }

    if (importedRigBones != nullptr) {
        *importedRigBones = extractRigBonesFromScene(scene, readFbxUnitScale(scene));
    }
    if (importedVertexGroups != nullptr) {
        *importedVertexGroups = extractVertexGroupsFromScene(scene);
    }

    const aiMesh* mesh = scene->mMeshes[0];
    if (mesh == nullptr || mesh->mNumVertices == 0 || mesh->mNumFaces == 0) {
        errorMessage = "FBX has no valid mesh data.";
        return std::nullopt;
    }

    const aiNode* meshNode = findMeshNode(scene->mRootNode, 0);
    glm::mat4 meshGlobalTransform(1.0f);
    if (meshNode != nullptr) {
        findMeshGlobalTransform(scene->mRootNode, meshNode, glm::mat4(1.0f), meshGlobalTransform);
    }
    const float unitScale = readFbxUnitScale(scene);

    render::ImportedModelData model;
    model.vertices.reserve(static_cast<std::size_t>(mesh->mNumVertices) * 8);
    glm::vec3 boundsMin(std::numeric_limits<float>::max());
    glm::vec3 boundsMax(std::numeric_limits<float>::lowest());

    for (unsigned int i = 0; i < mesh->mNumVertices; ++i) {
        const aiVector3D p = mesh->mVertices[i];
        const aiVector3D n = mesh->HasNormals() ? mesh->mNormals[i] : aiVector3D(0.0f, 1.0f, 0.0f);
        const aiVector3D uv = mesh->HasTextureCoords(0) ? mesh->mTextureCoords[0][i] : aiVector3D(0.0f, 0.0f, 0.0f);
        const glm::vec3 localPosition(p.x, p.y, p.z);
        const glm::vec3 localNormal = glm::normalize(glm::vec3(n.x, n.y, n.z));
        boundsMin = glm::min(boundsMin, localPosition);
        boundsMax = glm::max(boundsMax, localPosition);

        model.vertices.push_back(localPosition.x);
        model.vertices.push_back(localPosition.y);
        model.vertices.push_back(localPosition.z);
        model.vertices.push_back(localNormal.x);
        model.vertices.push_back(localNormal.y);
        model.vertices.push_back(localNormal.z);
        model.vertices.push_back(uv.x);
        model.vertices.push_back(uv.y);
    }

    aiVector3D scaling;
    aiVector3D position;
    aiQuaternion rotation;
    aiMatrix4x4 transform = aiMatrix4x4(
        meshGlobalTransform[0][0], meshGlobalTransform[1][0], meshGlobalTransform[2][0], meshGlobalTransform[3][0],
        meshGlobalTransform[0][1], meshGlobalTransform[1][1], meshGlobalTransform[2][1], meshGlobalTransform[3][1],
        meshGlobalTransform[0][2], meshGlobalTransform[1][2], meshGlobalTransform[2][2], meshGlobalTransform[3][2],
        meshGlobalTransform[0][3], meshGlobalTransform[1][3], meshGlobalTransform[2][3], meshGlobalTransform[3][3]);
    transform.Decompose(scaling, rotation, position);

    model.position = glm::vec3(position.x, position.y, position.z) * unitScale;
    model.scale = glm::vec3(scaling.x, scaling.y, scaling.z) * unitScale;
    model.rotationEulerDegrees = eulerDegreesFromQuatXYZ(glm::quat(rotation.w, rotation.x, rotation.y, rotation.z));
    model.dimensions = boundsMax - boundsMin;

    for (unsigned int f = 0; f < mesh->mNumFaces; ++f) {
        const aiFace& face = mesh->mFaces[f];
        if (face.mNumIndices != 3) {
            continue;
        }
        model.indices.push_back(face.mIndices[0]);
        model.indices.push_back(face.mIndices[1]);
        model.indices.push_back(face.mIndices[2]);
    }

    model.textureWidth = 1;
    model.textureHeight = 1;
    model.textureRgba = {255, 255, 255, 255};
    model.opacity = 1.0f;
    model.alphaBlend = false;

    if (scene->HasMaterials() && mesh->mMaterialIndex < scene->mNumMaterials) {
        const aiMaterial* material = scene->mMaterials[mesh->mMaterialIndex];
        float materialOpacity = 1.0f;
        if (material->Get(AI_MATKEY_OPACITY, materialOpacity) == AI_SUCCESS) {
            model.opacity = std::clamp(materialOpacity, 0.0f, 1.0f);
        }

        aiBlendMode blendMode = aiBlendMode_Default;
        if (material->Get(AI_MATKEY_BLEND_FUNC, blendMode) == AI_SUCCESS && blendMode != aiBlendMode_Default) {
            model.alphaBlend = true;
        }

        aiString texPath;
        if (material->GetTexture(aiTextureType_DIFFUSE, 0, &texPath) == AI_SUCCESS) {
            const std::string texRef = texPath.C_Str();
            int width = 0;
            int height = 0;
            int channels = 0;
            stbi_uc* pixels = nullptr;
            stbi_set_flip_vertically_on_load(true);

            const aiTexture* embedded = scene->GetEmbeddedTexture(texRef.c_str());
            if (embedded != nullptr) {
                if (embedded->mHeight == 0) {
                    pixels = stbi_load_from_memory(
                        reinterpret_cast<const stbi_uc*>(embedded->pcData),
                        static_cast<int>(embedded->mWidth),
                        &width,
                        &height,
                        &channels,
                        4);
                } else {
                    width = static_cast<int>(embedded->mWidth);
                    height = static_cast<int>(embedded->mHeight);
                    model.textureRgba.resize(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4);
                    for (int y = 0; y < height; ++y) {
                        for (int x = 0; x < width; ++x) {
                            const aiTexel& t = embedded->pcData[y * width + x];
                            const int flippedY = height - 1 - y;
                            const std::size_t idx = (static_cast<std::size_t>(flippedY) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)) * 4;
                            model.textureRgba[idx + 0] = t.r;
                            model.textureRgba[idx + 1] = t.g;
                            model.textureRgba[idx + 2] = t.b;
                            model.textureRgba[idx + 3] = t.a;
                        }
                    }
                    model.textureWidth = width;
                    model.textureHeight = height;
                }
            } else {
                const std::filesystem::path texturePath = std::filesystem::path(filePath).parent_path() / texRef;
                pixels = stbi_load(texturePath.string().c_str(), &width, &height, &channels, 4);
            }

            if (pixels != nullptr) {
                model.textureWidth = width;
                model.textureHeight = height;
                model.textureRgba.assign(pixels, pixels + static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4);
                stbi_image_free(pixels);
            }

            stbi_set_flip_vertically_on_load(false);
        }

        for (std::size_t index = 3; index < model.textureRgba.size(); index += 4) {
            if (model.textureRgba[index] < 250) {
                model.alphaBlend = true;
                break;
            }
        }

        if (model.opacity < 0.999f) {
            model.alphaBlend = true;
        }
    }

    return model;
}

}  // namespace sparks::core
