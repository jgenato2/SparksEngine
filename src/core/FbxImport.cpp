#include "sparks/core/FbxImport.hpp"

#include <algorithm>
#include <filesystem>
#include <map>
#include <limits>

#define GLM_ENABLE_EXPERIMENTAL
#include <assimp/config.h>
#include <assimp/Importer.hpp>
#include <assimp/material.h>
#include <assimp/postprocess.h>
#include <assimp/scene.h>
#include <fbxsdk.h>
#include <glm/gtx/euler_angles.hpp>
#include <glm/gtx/quaternion.hpp>
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>
#include <set>
#include <functional>

namespace
{

    using sparks::core::rigging::extractRigBonesFromScene;
    using sparks::core::rigging::extractVertexGroupsFromScene;

    glm::vec3 eulerDegreesFromQuatXYZ(const glm::quat &q)
    {
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
        glm::extractEulerAngleXYZ(glm::toMat4(glm::normalize(q)), x, y, z);
        return glm::degrees(glm::vec3(x, y, z));
    }

    float readFbxUnitScale(const aiScene *scene)
    {
        if (scene == nullptr || scene->mMetaData == nullptr)
        {
            return 1.0f;
        }

        ai_real unitScaleFactor = 0.0;
        if (!scene->mMetaData->Get("UnitScaleFactor", unitScaleFactor) || unitScaleFactor <= 0.0)
        {
            return 1.0f;
        }

        return static_cast<float>(unitScaleFactor / 100.0);
    }

    glm::mat4 toGlmMatrix(const aiMatrix4x4 &matrix)
    {
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

    const aiNode *findMeshNode(const aiNode *node, const unsigned int meshIndex)
    {
        if (node == nullptr)
        {
            return nullptr;
        }

        for (unsigned int i = 0; i < node->mNumMeshes; ++i)
        {
            if (node->mMeshes[i] == meshIndex)
            {
                return node;
            }
        }

        for (unsigned int i = 0; i < node->mNumChildren; ++i)
        {
            if (const aiNode *found = findMeshNode(node->mChildren[i], meshIndex))
            {
                return found;
            }
        }

        return nullptr;
    }

    bool findMeshGlobalTransform(const aiNode *currentNode, const aiNode *targetNode, const glm::mat4 &parentTransform, glm::mat4 &globalTransform)
    {
        if (currentNode == nullptr)
        {
            return false;
        }

        const glm::mat4 currentTransform = parentTransform * toGlmMatrix(currentNode->mTransformation);
        if (currentNode == targetNode)
        {
            globalTransform = currentTransform;
            return true;
        }

        for (unsigned int i = 0; i < currentNode->mNumChildren; ++i)
        {
            if (findMeshGlobalTransform(currentNode->mChildren[i], targetNode, currentTransform, globalTransform))
            {
                return true;
            }
        }

        return false;
    }

} // namespace

namespace sparks::core
{

    std::optional<render::ImportedModelData> loadFbxModel(
        const std::string &filePath,
        std::string &errorMessage,
        std::vector<rigging::RigBone> *importedRigBones,
        std::vector<rigging::VertexGroupInfo> *importedVertexGroups,
        const FbxAnimationSettings *animationSettings)
    {
        Assimp::Importer importer;
        importer.SetPropertyBool(AI_CONFIG_IMPORT_FBX_PRESERVE_PIVOTS, false);
        const aiScene *scene = importer.ReadFile(
            filePath,
            aiProcess_Triangulate |
                aiProcess_GenSmoothNormals |
                aiProcess_JoinIdenticalVertices |
                aiProcess_ImproveCacheLocality);

        if (scene == nullptr || !scene->HasMeshes())
        {
            errorMessage = importer.GetErrorString();
            return std::nullopt;
        }

        if (importedRigBones != nullptr)
        {
            *importedRigBones = extractRigBonesFromScene(scene, readFbxUnitScale(scene));
        }
        render::ImportedModelData model;
        // Populate model.boneNames from importedRigBones if available
        if (importedRigBones != nullptr && !importedRigBones->empty())
        {
            model.boneNames.clear();
            for (const auto &bone : *importedRigBones)
            {
                model.boneNames.push_back(bone.name);
            }
        }
        if (importedVertexGroups != nullptr)
        {
            *importedVertexGroups = extractVertexGroupsFromScene(scene);
        }

        // --- FBX SDK: Enumerate animation stacks and extract animation data ---
        FbxManager* fbxManager = FbxManager::Create();
        FbxIOSettings* ios = FbxIOSettings::Create(fbxManager, IOSROOT);
        fbxManager->SetIOSettings(ios);
        FbxImporter* fbxImporter = FbxImporter::Create(fbxManager, "");
        if (fbxImporter->Initialize(filePath.c_str(), -1, fbxManager->GetIOSettings())) {
            FbxScene* fbxScene = FbxScene::Create(fbxManager, "scene");
            if (fbxImporter->Import(fbxScene)) {
                int animStackCount = fbxScene->GetSrcObjectCount<FbxAnimStack>();
                for (int i = 0; i < animStackCount; ++i) {
                    FbxAnimStack* stack = fbxScene->GetSrcObject<FbxAnimStack>(i);
                    render::ImportedModelData::AnimationClip clip;
                    clip.name = stack->GetName();
                    // Get the first animation layer
                    FbxAnimLayer* layer = stack->GetMember<FbxAnimLayer>(0);
                    if (!layer) continue;
                    // Traverse all nodes in the scene (recursive)
                    std::function<void(FbxNode*)> traverseNodes;
                    traverseNodes = [&](FbxNode* node) {
                        if (!node) return;
                        FbxAnimCurve* tCurveX = node->LclTranslation.GetCurve(layer, FBXSDK_CURVENODE_COMPONENT_X);
                        FbxAnimCurve* tCurveY = node->LclTranslation.GetCurve(layer, FBXSDK_CURVENODE_COMPONENT_Y);
                        FbxAnimCurve* tCurveZ = node->LclTranslation.GetCurve(layer, FBXSDK_CURVENODE_COMPONENT_Z);
                        FbxAnimCurve* rCurveX = node->LclRotation.GetCurve(layer, FBXSDK_CURVENODE_COMPONENT_X);
                        FbxAnimCurve* rCurveY = node->LclRotation.GetCurve(layer, FBXSDK_CURVENODE_COMPONENT_Y);
                        FbxAnimCurve* rCurveZ = node->LclRotation.GetCurve(layer, FBXSDK_CURVENODE_COMPONENT_Z);
                        FbxAnimCurve* sCurveX = node->LclScaling.GetCurve(layer, FBXSDK_CURVENODE_COMPONENT_X);
                        FbxAnimCurve* sCurveY = node->LclScaling.GetCurve(layer, FBXSDK_CURVENODE_COMPONENT_Y);
                        FbxAnimCurve* sCurveZ = node->LclScaling.GetCurve(layer, FBXSDK_CURVENODE_COMPONENT_Z);
                        // Only add channel if there is at least one curve
                        if (tCurveX || tCurveY || tCurveZ || rCurveX || rCurveY || rCurveZ || sCurveX || sCurveY || sCurveZ) {
                            render::ImportedModelData::AnimationChannel channel;
                            channel.boneName = node->GetName();
                            // Collect all unique key times
                            std::set<float> keyTimes;
                            auto addCurveTimes = [&](FbxAnimCurve* curve) {
                                if (!curve) return;
                                for (int k = 0; k < curve->KeyGetCount(); ++k) {
                                    keyTimes.insert(static_cast<float>(curve->KeyGetTime(k).GetSecondDouble()));
                                }
                            };
                            addCurveTimes(tCurveX); addCurveTimes(tCurveY); addCurveTimes(tCurveZ);
                            addCurveTimes(rCurveX); addCurveTimes(rCurveY); addCurveTimes(rCurveZ);
                            addCurveTimes(sCurveX); addCurveTimes(sCurveY); addCurveTimes(sCurveZ);
                            // For each key time, sample the transform
                            for (float t : keyTimes) {
                                render::ImportedModelData::Keyframe kf;
                                kf.time = t;
                                // Translation
                                double tx = tCurveX ? tCurveX->Evaluate(t) : node->LclTranslation.Get()[0];
                                double ty = tCurveY ? tCurveY->Evaluate(t) : node->LclTranslation.Get()[1];
                                double tz = tCurveZ ? tCurveZ->Evaluate(t) : node->LclTranslation.Get()[2];
                                kf.position = glm::vec3(static_cast<float>(tx), static_cast<float>(ty), static_cast<float>(tz));
                                // Rotation (FBX is in degrees)
                                double rx = rCurveX ? rCurveX->Evaluate(t) : node->LclRotation.Get()[0];
                                double ry = rCurveY ? rCurveY->Evaluate(t) : node->LclRotation.Get()[1];
                                double rz = rCurveZ ? rCurveZ->Evaluate(t) : node->LclRotation.Get()[2];
                                glm::vec3 rotRad = glm::radians(glm::vec3(static_cast<float>(rx), static_cast<float>(ry), static_cast<float>(rz)));
                                kf.rotation = glm::quat(rotRad);
                                // Scaling
                                double sx = sCurveX ? sCurveX->Evaluate(t) : node->LclScaling.Get()[0];
                                double sy = sCurveY ? sCurveY->Evaluate(t) : node->LclScaling.Get()[1];
                                double sz = sCurveZ ? sCurveZ->Evaluate(t) : node->LclScaling.Get()[2];
                                kf.scale = glm::vec3(static_cast<float>(sx), static_cast<float>(sy), static_cast<float>(sz));
                                channel.keyframes.push_back(kf);
                            }
                            clip.channels.push_back(channel);
                        }
                        // Recurse into children
                        int childCount = node->GetChildCount();
                        for (int i = 0; i < childCount; ++i) {
                            traverseNodes(node->GetChild(i));
                        }
                    };
                    FbxNode* rootNode = fbxScene->GetRootNode();
                    if (rootNode) {
                        traverseNodes(rootNode);
                    }
                    // Set duration and ticksPerSecond (approximate)
                    const FbxTimeSpan& timeSpan = stack->GetLocalTimeSpan();
                    clip.duration = static_cast<float>(timeSpan.GetDuration().GetSecondDouble());
                    clip.ticksPerSecond = 1.0f; // FBX SDK uses seconds, so 1 tick = 1 second
                    model.animations.push_back(clip);
                }
            }
            fbxScene->Destroy();
        }
        fbxImporter->Destroy();
        fbxManager->Destroy();

        const aiMesh *mesh = scene->mMeshes[0];
        if (mesh == nullptr || mesh->mNumVertices == 0 || mesh->mNumFaces == 0)
        {
            errorMessage = "FBX has no valid mesh data.";
            return std::nullopt;
        }

        const aiNode *meshNode = findMeshNode(scene->mRootNode, 0);
        glm::mat4 meshGlobalTransform(1.0f);
        if (meshNode != nullptr)
        {
            findMeshGlobalTransform(scene->mRootNode, meshNode, glm::mat4(1.0f), meshGlobalTransform);
        }
        const float unitScale = readFbxUnitScale(scene);

        // Animation import logic (moved after model declaration)
        if (animationSettings && animationSettings->importAnimations && scene->HasAnimations())
        {
            int numAnimations = static_cast<int>(scene->mNumAnimations);
            int startAnim = 0;
            int endAnim = numAnimations;
            if (!animationSettings->importAllAnimations)
            {
                startAnim = std::clamp(animationSettings->selectedAnimationIndex, 0, numAnimations - 1);
                endAnim = startAnim + 1;
            }
            for (int animIdx = startAnim; animIdx < endAnim; ++animIdx)
            {
                const aiAnimation *anim = scene->mAnimations[animIdx];
                if (!anim)
                    continue;
                render::ImportedModelData::AnimationClip clip;
                clip.name = anim->mName.C_Str();
                clip.duration = static_cast<float>(anim->mDuration);
                clip.ticksPerSecond = anim->mTicksPerSecond > 0.0 ? static_cast<float>(anim->mTicksPerSecond) : 25.0f;
                for (unsigned int ch = 0; ch < anim->mNumChannels; ++ch)
                {
                    const aiNodeAnim *nodeAnim = anim->mChannels[ch];
                    render::ImportedModelData::AnimationChannel channel;
                    channel.boneName = nodeAnim->mNodeName.C_Str();
                    // Merge keyframes by time
                    std::map<float, render::ImportedModelData::Keyframe> keyframeMap;
                    for (unsigned int k = 0; k < nodeAnim->mNumPositionKeys; ++k)
                    {
                        float t = static_cast<float>(nodeAnim->mPositionKeys[k].mTime);
                        keyframeMap[t].time = t;
                        keyframeMap[t].position = glm::vec3(
                            nodeAnim->mPositionKeys[k].mValue.x,
                            nodeAnim->mPositionKeys[k].mValue.y,
                            nodeAnim->mPositionKeys[k].mValue.z);
                    }
                    for (unsigned int k = 0; k < nodeAnim->mNumRotationKeys; ++k)
                    {
                        float t = static_cast<float>(nodeAnim->mRotationKeys[k].mTime);
                        keyframeMap[t].time = t;
                        keyframeMap[t].rotation = glm::quat(
                            nodeAnim->mRotationKeys[k].mValue.w,
                            nodeAnim->mRotationKeys[k].mValue.x,
                            nodeAnim->mRotationKeys[k].mValue.y,
                            nodeAnim->mRotationKeys[k].mValue.z);
                    }
                    for (unsigned int k = 0; k < nodeAnim->mNumScalingKeys; ++k)
                    {
                        float t = static_cast<float>(nodeAnim->mScalingKeys[k].mTime);
                        keyframeMap[t].time = t;
                        keyframeMap[t].scale = glm::vec3(
                            nodeAnim->mScalingKeys[k].mValue.x,
                            nodeAnim->mScalingKeys[k].mValue.y,
                            nodeAnim->mScalingKeys[k].mValue.z);
                    }
                    // Convert map to sorted vector
                    for (auto it = keyframeMap.begin(); it != keyframeMap.end(); ++it)
                    {
                        channel.keyframes.push_back(it->second);
                    }
                    clip.channels.push_back(channel);
                }
                model.animations.push_back(clip);
            }
        }
        model.vertices.reserve(static_cast<std::size_t>(mesh->mNumVertices) * 8);
        glm::vec3 boundsMin(std::numeric_limits<float>::max());
        glm::vec3 boundsMax(std::numeric_limits<float>::lowest());

        for (unsigned int i = 0; i < mesh->mNumVertices; ++i)
        {
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

        for (unsigned int f = 0; f < mesh->mNumFaces; ++f)
        {
            const aiFace &face = mesh->mFaces[f];
            if (face.mNumIndices != 3)
            {
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
        model.alphaCutoff = 0.01f;
        model.unlitShading = false;

        if (scene->HasMaterials() && mesh->mMaterialIndex < scene->mNumMaterials)
        {
            const aiMaterial *material = scene->mMaterials[mesh->mMaterialIndex];
            float materialOpacity = 1.0f;
            if (material->Get(AI_MATKEY_OPACITY, materialOpacity) == AI_SUCCESS)
            {
                model.opacity = std::clamp(materialOpacity, 0.0f, 1.0f);
            }

            aiBlendMode blendMode = aiBlendMode_Default;
            if (material->Get(AI_MATKEY_BLEND_FUNC, blendMode) == AI_SUCCESS && blendMode != aiBlendMode_Default)
            {
                model.alphaBlend = true;
            }

            int shadingModel = 0;
            if (material->Get(AI_MATKEY_SHADING_MODEL, shadingModel) == AI_SUCCESS && shadingModel == aiShadingMode_NoShading)
            {
                model.unlitShading = true;
            }

            aiColor4D diffuse(1.0f, 1.0f, 1.0f, 1.0f);
            if (material->Get(AI_MATKEY_COLOR_DIFFUSE, diffuse) == AI_SUCCESS)
            {
                model.diffuseColor = glm::vec4(diffuse.r, diffuse.g, diffuse.b, diffuse.a);
            }

            aiColor3D emissive(0.0f, 0.0f, 0.0f);
            if (material->Get(AI_MATKEY_COLOR_EMISSIVE, emissive) == AI_SUCCESS)
            {
                model.emissiveColor = glm::vec3(emissive.r, emissive.g, emissive.b);
            }

            aiString texPath;
            if (material->GetTexture(aiTextureType_DIFFUSE, 0, &texPath) == AI_SUCCESS)
            {
                const std::string texRef = texPath.C_Str();
                int width = 0;
                int height = 0;
                int channels = 0;
                stbi_uc *pixels = nullptr;
                stbi_set_flip_vertically_on_load(true);

                const aiTexture *embedded = scene->GetEmbeddedTexture(texRef.c_str());
                if (embedded != nullptr)
                {
                    if (embedded->mHeight == 0)
                    {
                        pixels = stbi_load_from_memory(
                            reinterpret_cast<const stbi_uc *>(embedded->pcData),
                            static_cast<int>(embedded->mWidth),
                            &width,
                            &height,
                            &channels,
                            4);
                    }
                    else
                    {
                        width = static_cast<int>(embedded->mWidth);
                        height = static_cast<int>(embedded->mHeight);
                        model.textureRgba.resize(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4);
                        for (int y = 0; y < height; ++y)
                        {
                            for (int x = 0; x < width; ++x)
                            {
                                const aiTexel &t = embedded->pcData[y * width + x];
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
                }
                else
                {
                    const std::filesystem::path texturePath = std::filesystem::path(filePath).parent_path() / texRef;
                    pixels = stbi_load(texturePath.string().c_str(), &width, &height, &channels, 4);
                }

                if (pixels != nullptr)
                {
                    model.textureWidth = width;
                    model.textureHeight = height;
                    model.textureRgba.assign(pixels, pixels + static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4);
                    stbi_image_free(pixels);
                }

                stbi_set_flip_vertically_on_load(false);
            }

            for (std::size_t index = 3; index < model.textureRgba.size(); index += 4)
            {
                if (model.textureRgba[index] < 250)
                {
                    model.alphaBlend = true;
                    break;
                }
            }

            if (model.opacity < 0.999f)
            {
                model.alphaBlend = true;
            }
        }

        return model;
    }

} // namespace sparks::core
