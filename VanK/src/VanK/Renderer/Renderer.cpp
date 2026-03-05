#include "Renderer.h"

#include <imgui.h>

#include <SDL3/SDL_log.h>

#include "VanK/Core/Application.h"
#include "VanK/Core/Log.h"
#include "VanK/Core/Timer.h"
#define TINYGLTF_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <tiny_gltf.h>

#include <meshoptimizer.h>
#include <glm/gtc/type_ptr.inl>
#include <glm/gtx/rotate_vector.hpp>

#include "MSDFData.h"
#include "VanK/Asset/AssetManager.h"

namespace VanK
{
    static std::vector<std::unique_ptr<filewatch::FileWatch<std::string>>> s_ShaderWatcher;
    static std::atomic<bool> s_IsPipelineReloadFinished = false;
    static bool IsShaderReloadFinished = false;
    static std::string changedFile;
    static Timer ReloadTimer;

    struct Renderer3DData
    {
        Ref<Texture2D> FontAtlasTexture;
    };

    static Renderer3DData s_Data;

    struct TaskMeshPipelinePushConstant
    {
        uint64_t sceneData;
        uint64_t culledDataBuffer;
        uint64_t vertexBuffer;
        uint64_t indexBuffer;
        uint64_t meshletVerticesBuffer;
        uint64_t meshletTrianglesBuffer;
        uint64_t meshletBuffer;
        uint64_t meshletPrimitives;
        uint64_t meshDraws;
        uint64_t materialBuffer;
        uint64_t instanceLutBuffer;
        uint64_t lightsBuffer;
    };

    struct Transform
    {
        glm::vec3 translation{};
        glm::quat rotation{};
        glm::vec3 scale{};

        [[nodiscard]] glm::mat4 GetMatrix() const { return glm::translate(glm::mat4(1.0f), translation) * mat4_cast(rotation) * glm::scale(glm::mat4(1.0f), scale); }

        static const Transform Identity;
    };

    inline const Transform Transform::Identity{
        {0.0f, 0.0f, 0.0f},
        {1.0f, 0.0f, 0.0f, 0.0f},
        {1.0f, 1.0f, 1.0f}
    };

    struct Frustum
    {
        glm::vec4 planes[6];

        Frustum() = default;

        explicit Frustum(const glm::mat4& viewProj);
    };

    Frustum::Frustum(const glm::mat4& viewProj)
    {
        planes[0] = glm::vec4(
            viewProj[0][3] + viewProj[0][0],
            viewProj[1][3] + viewProj[1][0],
            viewProj[2][3] + viewProj[2][0],
            viewProj[3][3] + viewProj[3][0]
        );

        planes[1] = glm::vec4(
            viewProj[0][3] - viewProj[0][0],
            viewProj[1][3] - viewProj[1][0],
            viewProj[2][3] - viewProj[2][0],
            viewProj[3][3] - viewProj[3][0]
        );

        planes[2] = glm::vec4(
            viewProj[0][3] + viewProj[0][1],
            viewProj[1][3] + viewProj[1][1],
            viewProj[2][3] + viewProj[2][1],
            viewProj[3][3] + viewProj[3][1]
        );

        planes[3] = glm::vec4(
            viewProj[0][3] - viewProj[0][1],
            viewProj[1][3] - viewProj[1][1],
            viewProj[2][3] - viewProj[2][1],
            viewProj[3][3] - viewProj[3][1]
        );

        planes[4] = glm::vec4(
            viewProj[0][3] + viewProj[0][2],
            viewProj[1][3] + viewProj[1][2],
            viewProj[2][3] + viewProj[2][2],
            viewProj[3][3] + viewProj[3][2]
        );

        planes[5] = glm::vec4(
            viewProj[0][3] - viewProj[0][2],
            viewProj[1][3] - viewProj[1][2],
            viewProj[2][3] - viewProj[2][2],
            viewProj[3][3] - viewProj[3][2]
        );

        for (auto& plane : planes)
        {
            float length = glm::length(glm::vec3(plane));
            plane /= length;
        }
    }

    struct SceneDatas
    {
        glm::mat4 view{1.0f};
        glm::mat4 proj{1.0f};
        glm::mat4 viewProj{1.0f};

        glm::mat4 viewInverse{1.0f};
        glm::mat4 projInverse{1.0f};
        glm::mat4 viewProjInverse{1.0f};

        glm::mat4 frozenView{1.0f};
        glm::mat4 frozenProj{1.0f};
        glm::mat4 frozenViewProj{1.0f};

        glm::vec4 cameraWorldPos{0.0f};
        glm::vec4 frozenCameraWorldPos{0.0f};

        Frustum frustum{};
        Frustum frozenFrustum{};

        glm::vec2 renderTargetSize{};
        glm::vec2 texelSize{};

        float deltaTime{};

        bool FrustumCullEnabled{true};

        uint32_t brdflutTexture;
        uint32_t irradianceTexture;
        uint32_t prefilteredTexture;

        //path tracer
        uint32_t skyBoxIndex;
        bool useSky;
        glm::vec3 backgroundColor;
        uint32_t frameIndex;
    };

    static std::vector<SceneDatas> scene;
    static SceneDatas scenesData{};
    static SceneDatas frozenSceneData{};

    struct CulledData
    {
        uint32_t frustumCulled{0};
        uint32_t backfaceCulled{0};
        uint32_t totalCulled{0};
    };

    struct Meshlet
    {
        glm::vec4 meshletBoundingSphere;

        glm::vec3 coneApex;
        float coneCutoff;

        glm::vec3 coneAxis;
        uint32_t vertexOffset;

        uint32_t meshletVerticesOffset;
        uint32_t meshletTriangleOffset;
        uint32_t meshletVerticesCount;
        uint32_t meshletTriangleCount;
    };

    static std::vector<shaderio::Material> materials;
    static std::vector<shaderio::InstanceLUT> instanceLUTs;

  
    struct meshTasksSubmitPushConstant
    {
        uint64_t meshTasksIndirectBufferAddress;
        uint64_t localMeshTasksIndirectBufferAddress;
    };

    struct VanKDrawMeshTasksIndirectCommand
    {
        uint32_t groupCountX;
        uint32_t groupCountY;
        uint32_t groupCountZ;
    };

    static std::vector<VanKDrawMeshTasksIndirectCommand> meshTasks;

    struct MeshDraw
    {
        // because this can change every frame needs to be uploaded every frame
        glm::mat4 modelMatrix;
        glm::mat3 normalMatrix;
        uint64_t primitiveID;
    };

    static std::vector<MeshDraw> meshDraws;

    struct Geometry
    {
        // only needs to be updated once a new model is added so no need for upload every frame
        std::vector<shaderio::Vertex> vertices{};
        std::vector<uint32_t> indices{};
        std::vector<uint32_t> meshletVertices{};
        std::vector<uint8_t> meshletTriangles{};
        std::vector<Meshlet> meshlets{};
        std::vector<shaderio::MeshletPrimitive> primitives{};
    };

    static Geometry geometry;

    static glm::mat4 GetNodeTransform(const tinygltf::Node& node)
    {
        glm::mat4 localTransform(1.0f);

        if (!node.matrix.empty())
        {
            localTransform = glm::make_mat4(node.matrix.data()); // 4x4 matrix in column-major
        }
        else
        {
            glm::vec3 translation(0.0f);
            if (!node.translation.empty())
                translation = glm::vec3(node.translation[0], node.translation[1], node.translation[2]);

            glm::quat rotation(1, 0, 0, 0);
            if (!node.rotation.empty())
                rotation = glm::quat(node.rotation[3], node.rotation[0], node.rotation[1], node.rotation[2]); // xyzw to glm

            glm::vec3 scale(1.0f);
            if (!node.scale.empty())
                scale = glm::vec3(node.scale[0], node.scale[1], node.scale[2]);

            localTransform = glm::translate(glm::mat4(1.0f), translation)
                * glm::mat4_cast(rotation)
                * glm::scale(glm::mat4(1.0f), scale);
        }

        return localTransform;
    }

    static std::vector<shaderio::Vertex> primitiveVertices{};
    static std::vector<uint32_t> primitiveIndices{};
    static std::unordered_map<std::string, Ref<Texture2D>> textureCache;

    static int ResolveImageFromTexture(const tinygltf::Model& model, int textureIndex)
    {
        if (textureIndex < 0 || textureIndex >= model.textures.size())
        {
            std::cout << "Invalid texture index: " << textureIndex << std::endl;
            return -1;
        }

        const tinygltf::Texture& tex = model.textures[textureIndex];

        // Prefer DDS
        auto dds = tex.extensions.find("MSFT_texture_dds");
        if (dds != tex.extensions.end())
        {
            const tinygltf::Value& ext = dds->second;
            if (ext.Has("source"))
                return ext.Get("source").Get<int>();
        }

        // fallback PNG/JPG
        return tex.source;
    }

    static Ref<Texture2D> LoadTextureFromTexture
    (
        int textureIndex,
        const std::string& basePath,
        const tinygltf::Model& model,
        bool generateMips,
        ImageFormat format,
        bool flipTexture
    )
    {
        if (textureIndex < 0 || textureIndex >= model.textures.size())
            return {};

        int imageIndex = ResolveImageFromTexture(model, textureIndex);
        if (imageIndex < 0 || imageIndex >= model.images.size())
            return {};

        const tinygltf::Image& img = model.images[imageIndex];

        std::string key = img.uri.empty()
                              ? "embedded_" + std::to_string(imageIndex)
                              : img.uri;

        auto it = textureCache.find(key);
        if (it != textureCache.end())
            return it->second;

        if (!img.uri.empty())
        {
            std::filesystem::path fullpath = std::filesystem::path(basePath) / img.uri;

            if (!std::filesystem::exists(fullpath))
            {
                std::cout << "Texture file not found: " << fullpath << std::endl;
                return {};
            }

            Ref<Texture2D> tex = TextureImporter::LoadTexture2D(
                fullpath.string(),
                {.Format = format, .GenerateMips = generateMips, .FlipTexture = flipTexture}
            );

            textureCache[key] = tex;
            return tex;
        }

        return {};
    }

    static uint32_t BuildMaterial
    (
        const tinygltf::Material& gltfMat,
        const std::string& basePath,
        const tinygltf::Model& model
    )
    {
        shaderio::Material mat{};
        mat.albedoTexture = 0;
        mat.diffuseTexture = 0;
        mat.normalTexture = 0;
        mat.metallicRoughnessTexture = 0;

        mat.specularTexture = 0;
        mat.emissiveTexture = 0;
        mat.ambientOcclusionTexture = 0;

        mat.baseColorFactor = glm::vec4(1.0f);
        mat.diffuseFactor = glm::vec4(1.0f);
        mat.metallicFactor = 1.0f;
        mat.roughnessFactor = 1.0f;

        mat.specularGlossFactor = glm::vec4(1.0f);
        mat.isSpecularGlossWorkflow = false;
        mat.specularFactor = 1.0f;
        mat.specularColorFactor = glm::vec3(1.0f);
        mat.emissiveFactor = glm::vec3(0.0f);
        mat.emissiveStrength = 1.0f;
        mat.thicknessFactor = 0.0f;
        mat.attenuationDistance = 1.0f;
        mat.attenuationColor = glm::vec3(1.0f, 1.0f, 1.0f);

        mat.ior = 1.5f;
        mat.ambientOcclusionFactor = 1.0f;

        mat.transparent = 0;
        mat.doubleSided = 0;

        mat.transmissionFactor = 0; // 0 = opaque, 1 = full transparent


        if (gltfMat.extensions.contains("KHR_materials_pbrSpecularGlossiness")) // Old PBR
        {
            mat.isSpecularGlossWorkflow = true;

            const auto& ext = gltfMat.extensions.at("KHR_materials_pbrSpecularGlossiness");

            // Diffuse texture
            if (ext.Has("diffuseTexture"))
            {
                int texIndex = ext.Get("diffuseTexture").Get("index").Get<int>();
                if (Ref<Texture2D> t = LoadTextureFromTexture(texIndex, basePath, model, false, ImageFormat::RGBA8, false))
                    mat.diffuseTexture = t->GetTextureIndex();
            }

            // Diffuse factor
            if (ext.Has("diffuseFactor"))
            {
                const tinygltf::Value& df = ext.Get("diffuseFactor");
                if (df.IsArray() && df.ArrayLen() >= 4)
                {
                    mat.diffuseFactor = glm::vec4(
                        static_cast<float>(df.Get(0).Get<double>()),
                        static_cast<float>(df.Get(1).Get<double>()),
                        static_cast<float>(df.Get(2).Get<double>()),
                        static_cast<float>(df.Get(3).Get<double>())
                    );
                }
            }

            // Specular + glossiness factor
            if (ext.Has("specularFactor") || ext.Has("glossinessFactor"))
            {
                glm::vec4 specGloss(0.0f);

                if (ext.Has("specularFactor"))
                {
                    const tinygltf::Value& sf = ext.Get("specularFactor");
                    if (sf.IsArray() && sf.ArrayLen() >= 3)
                    {
                        specGloss.r = static_cast<float>(sf.Get(0).Get<double>());
                        specGloss.g = static_cast<float>(sf.Get(1).Get<double>());
                        specGloss.b = static_cast<float>(sf.Get(2).Get<double>());
                    }
                }

                if (ext.Has("glossinessFactor"))
                    specGloss.a = static_cast<float>(ext.Get("glossinessFactor").Get<double>());
                else
                    specGloss.a = 1.0f;

                mat.specularGlossFactor = specGloss;
            }

            // Specular-Glossiness texture
            if (ext.Has("specularGlossinessTexture"))
            {
                int texIndex = ext.Get("specularGlossinessTexture").Get("index").Get<int>();
                if (Ref<Texture2D> t = LoadTextureFromTexture(texIndex, basePath, model, false, ImageFormat::RGBA8, false))
                    mat.specularTexture = t->GetTextureIndex();
            }
        }
        else // New PBR (Metallic-Roughness)
        {
            // -----------------------------
            // BASE COLOR / ALBEDO
            // -----------------------------
            if (gltfMat.pbrMetallicRoughness.baseColorFactor.size() == 4)
            {
                const auto& bc = gltfMat.pbrMetallicRoughness.baseColorFactor;
                mat.baseColorFactor = glm::vec4(bc[0], bc[1], bc[2], bc[3]);
            }

            if (gltfMat.pbrMetallicRoughness.baseColorTexture.index >= 0) // New PBR (Metallic-Roughness)
            {
                int texIndex = gltfMat.pbrMetallicRoughness.baseColorTexture.index;
                if (Ref<Texture2D> t = LoadTextureFromTexture(texIndex, basePath, model, false, ImageFormat::SRGBA8, true))
                    mat.albedoTexture = t->GetTextureIndex();
            }

            // -----------------------------
            // METALLIC / ROUGHNESS
            // -----------------------------
            mat.metallicFactor = static_cast<float>(gltfMat.pbrMetallicRoughness.metallicFactor);
            mat.roughnessFactor = static_cast<float>(gltfMat.pbrMetallicRoughness.roughnessFactor);

            if (gltfMat.pbrMetallicRoughness.metallicRoughnessTexture.index >= 0)
            {
                int texIndex = gltfMat.pbrMetallicRoughness.metallicRoughnessTexture.index;
                if (Ref<Texture2D> t = LoadTextureFromTexture(texIndex, basePath, model, false, ImageFormat::RGBA8, false))
                    mat.metallicRoughnessTexture = t->GetTextureIndex();
            }
        }

        if (!gltfMat.extensions.contains("KHR_materials_pbrSpecularGlossiness"))
        {
            if (gltfMat.extensions.contains("KHR_materials_specular"))
            {
                const auto& specExt = gltfMat.extensions.at("KHR_materials_specular");

                // --- specularFactor (float) ---
                if (specExt.Has("specularFactor"))
                {
                    mat.specularFactor = static_cast<float>(
                        specExt.Get("specularFactor").Get<double>()
                    );
                }
                else
                {
                    mat.specularFactor = 1.0f; // default per spec
                }

                // --- specularColorFactor (vec3) ---
                if (specExt.Has("specularColorFactor"))
                {
                    const auto& scf =
                        specExt.Get("specularColorFactor").Get<std::vector<tinygltf::Value>>();

                    mat.specularColorFactor = glm::vec4(
                        static_cast<float>(scf[0].Get<double>()),
                        static_cast<float>(scf[1].Get<double>()),
                        static_cast<float>(scf[2].Get<double>()),
                        1.0f
                    );
                }
                else
                {
                    mat.specularColorFactor = glm::vec4(1.0f); // default
                }
            }
        }

        // -----------------------------
        // NORMAL
        // -----------------------------
        if (gltfMat.normalTexture.index >= 0)
        {
            int texIndex = gltfMat.normalTexture.index;
            if (Ref<Texture2D> t = LoadTextureFromTexture(texIndex, basePath, model, false, ImageFormat::RGBA8, false))
                mat.normalTexture = t->GetTextureIndex();
        }

        // -----------------------------
        // EMISSIVE
        // -----------------------------
        if (gltfMat.emissiveFactor.size() == 3)
        {
            mat.emissiveFactor = glm::vec3(
                static_cast<float>(gltfMat.emissiveFactor[0]),
                static_cast<float>(gltfMat.emissiveFactor[1]),
                static_cast<float>(gltfMat.emissiveFactor[2])
            );
        }

        if (gltfMat.emissiveTexture.index >= 0)
        {
            int texIndex = gltfMat.emissiveTexture.index;
            if (Ref<Texture2D> t = LoadTextureFromTexture(texIndex, basePath, model, false, ImageFormat::SRGBA8, false))
                mat.emissiveTexture = t->GetTextureIndex();
        }

        auto extIt = gltfMat.extensions.find("KHR_materials_emissive_strength");
        if (extIt != gltfMat.extensions.end())
        {
            const tinygltf::Value& ext = extIt->second;
            if (ext.Has("emissiveStrength"))
            {
                mat.emissiveStrength = static_cast<float>(ext.Get("emissiveStrength").GetNumberAsDouble());
            }
        }

        // -----------------------------
        // AMBIENT OCCLUSION
        // -----------------------------
        mat.ambientOcclusionFactor = 1.0f;
        if (gltfMat.occlusionTexture.index >= 0)
        {
            int texIndex = gltfMat.occlusionTexture.index;
            if (Ref<Texture2D> t = LoadTextureFromTexture(texIndex, basePath, model, false, ImageFormat::RGBA8, false))
                mat.ambientOcclusionTexture = t->GetTextureIndex();

            mat.ambientOcclusionFactor = static_cast<float>(gltfMat.occlusionTexture.strength);
        }

        // -----------------------------
        // TRANSMISSION
        // -----------------------------
        mat.transmissionFactor = 0.0f;
        if (gltfMat.extensions.contains("KHR_materials_transmission"))
        {
            const auto& ext = gltfMat.extensions.at("KHR_materials_transmission");
            if (ext.Has("transmissionFactor"))
                mat.transmissionFactor = static_cast<float>(ext.Get("transmissionFactor").Get<double>());
        }

        // -----------------------------
        // VOLUME
        // -----------------------------

        if (gltfMat.extensions.contains("KHR_materials_volume"))
        {
            const auto& volExt = gltfMat.extensions.at("KHR_materials_volume");

            // thicknessFactor scales the path through the object
            if (volExt.Has("thicknessFactor"))
                mat.thicknessFactor = static_cast<float>(volExt.Get("thicknessFactor").Get<double>());

            // attenuationDistance: distance at which light is reduced to 1/e
            if (volExt.Has("attenuationDistance"))
                mat.attenuationDistance = static_cast<float>(volExt.Get("attenuationDistance").Get<double>());
            else
                mat.attenuationDistance = std::numeric_limits<float>::infinity(); // default if not specified

            // attenuationColor: color of light that survives through the medium
            if (volExt.Has("attenuationColor"))
            {
                const auto& colorArray = volExt.Get("attenuationColor").Get<std::vector<tinygltf::Value>>();
                if (colorArray.size() >= 3)
                {
                    mat.attenuationColor = glm::vec3(
                        static_cast<float>(colorArray[0].Get<double>()),
                        static_cast<float>(colorArray[1].Get<double>()),
                        static_cast<float>(colorArray[2].Get<double>())
                    );
                }
            }
        }

        // -----------------------------
        // IOR
        // -----------------------------
        if (gltfMat.extensions.contains("KHR_materials_ior"))
        {
            const auto& iorExt = gltfMat.extensions.at("KHR_materials_ior");
            if (iorExt.Has("ior"))
                mat.ior = static_cast<float>(iorExt.Get("ior").Get<double>());
        }

        // -----------------------------
        // TRANSPARENCY
        // -----------------------------
        mat.transparent = (gltfMat.alphaMode == "BLEND") || (gltfMat.alphaMode == "MASK") || (mat.transmissionFactor > 0.0f) ? 1 : 0;

        mat.alphaMode = 0;
        if (gltfMat.alphaMode == "MASK") mat.alphaMode = 1;
        else if (gltfMat.alphaMode == "BLEND") mat.alphaMode = 2;

        mat.doubleSided = gltfMat.doubleSided ? 1 : 0;

        mat.alphaCutoff = static_cast<float>(gltfMat.alphaCutoff); // Default is 0.5

        materials.push_back(mat);
        return static_cast<uint32_t>(materials.size() - 1);
    }

    static void TraverseNode(
        std::string& basePath,
        const tinygltf::Model& model,
        int nodeIndex,
        const glm::mat4& parentTransform,
        std::vector<shaderio::Vertex>& verticesOut,
        std::vector<uint32_t>& indicesOut,
        uint32_t& materialIndex
    )
    {
        const tinygltf::Node& node = model.nodes[nodeIndex];

        // Compute this node's world transform
        glm::mat4 worldTransform = parentTransform * GetNodeTransform(node);

        // If the node has a mesh, process its primitives
        if (node.mesh >= 0)
        {
            const tinygltf::Mesh& mesh = model.meshes[node.mesh];

            for (const auto& primitive : mesh.primitives)
            {
                primitiveVertices.clear();
                primitiveIndices.clear();

                // Load primitive vertices and indices (similar to your existing code)
                const tinygltf::Accessor& posAccessor = model.accessors[primitive.attributes.at("POSITION")];
                const tinygltf::BufferView& posBufferView = model.bufferViews[posAccessor.bufferView];
                const tinygltf::Buffer& posBuffer = model.buffers[posBufferView.buffer];

                const size_t stride = posBufferView.byteStride ? posBufferView.byteStride : sizeof(float) * 3;
                const uint8_t* bufferStart = posBuffer.data.data() + posBufferView.byteOffset + posAccessor.byteOffset;

                uint32_t baseVertex = static_cast<uint32_t>(verticesOut.size());

                // Optional: TEXCOORD_0
                bool hasTexCoords = primitive.attributes.find("TEXCOORD_0") != primitive.attributes.end();
                const tinygltf::Accessor* texCoordAccessor = nullptr;
                const tinygltf::BufferView* texCoordBufferView = nullptr;
                const tinygltf::Buffer* texCoordBuffer = nullptr;

                if (hasTexCoords)
                {
                    texCoordAccessor = &model.accessors[primitive.attributes.at("TEXCOORD_0")];
                    texCoordBufferView = &model.bufferViews[texCoordAccessor->bufferView];
                    texCoordBuffer = &model.buffers[texCoordBufferView->buffer];
                }

                bool hasNormals = primitive.attributes.contains("NORMAL");
                const tinygltf::Accessor* normalAccessor = nullptr;
                const tinygltf::BufferView* normalBufferView = nullptr;
                const tinygltf::Buffer* normalBuffer = nullptr;

                if (hasNormals)
                {
                    normalAccessor = &model.accessors[primitive.attributes.at("NORMAL")];
                    normalBufferView = &model.bufferViews[normalAccessor->bufferView];
                    normalBuffer = &model.buffers[normalBufferView->buffer];
                }

                bool hasTangent = primitive.attributes.contains("TANGENT");
                const tinygltf::Accessor* tangentAccessor = nullptr;
                const tinygltf::BufferView* tangentBufferView = nullptr;
                const tinygltf::Buffer* tangentBuffer = nullptr;

                if (hasTangent)
                {
                    tangentAccessor = &model.accessors[primitive.attributes.at("TANGENT")];
                    tangentBufferView = &model.bufferViews[tangentAccessor->bufferView];
                    tangentBuffer = &model.buffers[tangentBufferView->buffer];
                }

                for (size_t i = 0; i < posAccessor.count; i++)
                {
                    const float* pos = reinterpret_cast<const float*>(bufferStart + i * stride);
                    glm::vec4 p(pos[0], pos[1], pos[2], 1.0f);

                    shaderio::Vertex v{};
                    v.position = glm::vec3(worldTransform * p); // <--- APPLY WORLD TRANSFORM HERE

                    if (hasTexCoords)
                    {
                        size_t texStride = texCoordBufferView->byteStride ? texCoordBufferView->byteStride : sizeof(float) * 2;
                        const float* texCoord = reinterpret_cast<const float*>(texCoordBuffer->data.data() + texCoordBufferView->byteOffset + texCoordAccessor->byteOffset + i * texStride);
                        v.texcoords = {texCoord[0], 1.0f - texCoord[1]}; // invert Y
                    }
                    else
                    {
                        v.texcoords = {0.0f, 0.0f};
                    }

                    if (hasNormals)
                    {
                        size_t normalStride =
                            normalBufferView->byteStride
                                ? normalBufferView->byteStride
                                : sizeof(float) * 3;

                        const float* n = reinterpret_cast<const float*>(
                            normalBuffer->data.data() +
                            normalBufferView->byteOffset +
                            normalAccessor->byteOffset +
                            i * normalStride
                        );

                        glm::vec3 localNormal(n[0], n[1], n[2]);
                        glm::mat3 normalMatrix = glm::transpose(glm::inverse(glm::mat3(worldTransform)));

                        v.normals = normalMatrix * localNormal;
                    }
                    else
                    {
                        v.normals = glm::vec3(0.0f, 0.0f, 1.0f); // fallback
                    }

                    if (hasTangent)
                    {
                        size_t tangentStride = tangentBufferView->byteStride ? tangentBufferView->byteStride : sizeof(float) * 4;
                        const float* t = reinterpret_cast<const float*>(
                            tangentBuffer->data.data() + tangentBufferView->byteOffset + tangentAccessor->byteOffset + i * tangentStride
                        );

                        // Transform only rotation/scale (ignore translation)
                        glm::vec3 worldTangent = glm::mat3(worldTransform) * glm::vec3(t[0], t[1], t[2]);

                        v.tangents = glm::vec4(worldTangent, t[3]); // keep w as handedness
                    }
                    else
                    {
                        v.tangents = glm::vec4(1, 0, 0, 1); // fallback
                    }

                    verticesOut.push_back(v);
                }

                // Process indices
                const tinygltf::Accessor& indexAccessor = model.accessors[primitive.indices];
                const tinygltf::BufferView& indexBufferView = model.bufferViews[indexAccessor.bufferView];
                const tinygltf::Buffer& indexBuffer = model.buffers[indexBufferView.buffer];

                size_t indexStride = 0;
                if (indexAccessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT) indexStride = sizeof(uint16_t);
                else if (indexAccessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT) indexStride = sizeof(uint32_t);
                else if (indexAccessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE) indexStride = sizeof(uint8_t);
                else throw std::runtime_error("Unsupported index type");

                const uint8_t* indexData = indexBuffer.data.data() + indexBufferView.byteOffset + indexAccessor.byteOffset;

                for (size_t i = 0; i < indexAccessor.count; i++)
                {
                    uint32_t idx = 0;
                    if (indexAccessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT)
                        idx = *reinterpret_cast<const uint16_t*>(indexData + i * indexStride);
                    else if (indexAccessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT)
                        idx = *reinterpret_cast<const uint32_t*>(indexData + i * indexStride);
                    else if (indexAccessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE)
                        idx = *reinterpret_cast<const uint8_t*>(indexData + i * indexStride);

                    indicesOut.push_back(baseVertex + idx);
                }

                std::vector<uint32_t> remap(primitiveIndices.size());
                size_t vertexCount = meshopt_generateVertexRemap
                (
                    remap.data(),
                    primitiveIndices.data(),
                    primitiveIndices.size(),
                    primitiveVertices.data(),
                    primitiveVertices.size(),
                    sizeof(shaderio::Vertex)
                );

                std::vector<shaderio::Vertex> remappedVertices(vertexCount);
                meshopt_remapVertexBuffer(remappedVertices.data(), primitiveVertices.data(), primitiveVertices.size(), sizeof(shaderio::Vertex), remap.data());
                std::vector<uint32_t> remappedIndices(primitiveIndices.size());
                meshopt_remapIndexBuffer(remappedIndices.data(), primitiveIndices.data(), primitiveIndices.size(), remap.data());

                meshopt_optimizeVertexCache(remappedIndices.data(), remappedIndices.data(), primitiveIndices.size(), vertexCount);
                meshopt_optimizeVertexFetch(remappedVertices.data(), remappedIndices.data(), primitiveIndices.size(), remappedVertices.data(), vertexCount, sizeof(shaderio::Vertex));

                primitiveVertices = remappedVertices;
                primitiveIndices = remappedIndices;

                const size_t maxVertices = 64;
                const size_t maxTriangles = 96;

                // build clusters (meshlets) out of the mesh
                size_t maxMeshlets = meshopt_buildMeshletsBound(primitiveIndices.size(), maxVertices, maxTriangles);
                std::vector<meshopt_Meshlet> meshlets(maxMeshlets);
                std::vector<unsigned int> meshletVertices(primitiveIndices.size());
                std::vector<unsigned char> meshletTriangles(primitiveIndices.size());

                std::vector<uint32_t> primitiveVertexPositions;
                meshlets.resize(meshopt_buildMeshlets(&meshlets[0], &meshletVertices[0], &meshletTriangles[0],
                                                      primitiveIndices.data(), primitiveIndices.size(),
                                                      reinterpret_cast<const float*>(primitiveVertices.data()), primitiveVertices.size(), sizeof(shaderio::Vertex),
                                                      maxVertices, maxTriangles, 0.f));

                // Optimize each meshlet's micro index buffer/vertex layout individually
                for (auto& meshlet : meshlets)
                {
                    meshopt_optimizeMeshlet(&meshletVertices[meshlet.vertex_offset], &meshletTriangles[meshlet.triangle_offset], meshlet.triangle_count, meshlet.vertex_count);
                }

                // Trim the meshlet data to minimize waste for meshletVertices/meshletTriangles
                {
                    const meshopt_Meshlet& last = meshlets.back();
                    meshletVertices.resize(last.vertex_offset + last.vertex_count);
                    meshletTriangles.resize(last.triangle_offset + last.triangle_count * 3);
                }

                // ------------------------------------------------------------
                // Primitive (whole-mesh) bounding sphere using meshoptimizer
                // ------------------------------------------------------------
                meshopt_Bounds primitiveBounds = meshopt_computeSphereBounds(
                    reinterpret_cast<const float*>(primitiveVertices.data()), // float3 position at offset 0
                    primitiveVertices.size(), // vertex count
                    sizeof(shaderio::Vertex), // vertex stride
                    nullptr, // no per-vertex radii
                    0
                );

                shaderio::MeshletPrimitive prim{};
                prim.meshletOffset = static_cast<uint32_t>(geometry.meshlets.size());
                prim.meshletCount = static_cast<uint32_t>(meshlets.size());
                prim.boundingSphere = glm::vec4(
                    primitiveBounds.center[0],
                    primitiveBounds.center[1],
                    primitiveBounds.center[2],
                    primitiveBounds.radius
                );

                if (primitive.material >= 0)
                {
                    prim.materialIndex = BuildMaterial
                    (
                        model.materials[primitive.material],
                        basePath,
                        model
                    );
                }
                else
                {
                    prim.materialIndex = materialIndex;
                }

                prim.firstVertex = static_cast<uint32_t>(geometry.vertices.size());
                prim.vertexCount = static_cast<uint32_t>(primitiveVertices.size());
                prim.indexOffset = static_cast<uint32_t>(geometry.indices.size());
                prim.indexCount = static_cast<uint32_t>(primitiveIndices.size());
                prim.maxVertex = prim.firstVertex + prim.vertexCount - 1;

                geometry.primitives.emplace_back(prim);

                // ------------------------------------------------------------
                // Offsets into global buffers
                // ------------------------------------------------------------
                uint32_t vertexOffset = static_cast<uint32_t>(geometry.vertices.size());
                uint32_t meshletVertexOffset = static_cast<uint32_t>(geometry.meshletVertices.size());
                uint32_t meshletTrianglesOffset = static_cast<uint32_t>(geometry.meshletTriangles.size());

                // ------------------------------------------------------------
                // Append primitive data to model buffers
                // ------------------------------------------------------------
                geometry.vertices.insert(
                    geometry.vertices.end(),
                    primitiveVertices.begin(),
                    primitiveVertices.end()
                );

                geometry.indices.insert(
                    geometry.indices.end(),
                    primitiveIndices.begin(),
                    primitiveIndices.end()
                );

                geometry.meshletVertices.insert(
                    geometry.meshletVertices.end(),
                    meshletVertices.begin(),
                    meshletVertices.end()
                );

                geometry.meshletTriangles.insert(
                    geometry.meshletTriangles.end(),
                    meshletTriangles.begin(),
                    meshletTriangles.end()
                );

                for (meshopt_Meshlet& meshlet : meshlets)
                {
                    meshopt_Bounds bounds = meshopt_computeMeshletBounds(
                        &meshletVertices[meshlet.vertex_offset],
                        &meshletTriangles[meshlet.triangle_offset],
                        meshlet.triangle_count,
                        reinterpret_cast<const float*>(primitiveVertices.data()),
                        primitiveVertices.size(),
                        sizeof(shaderio::Vertex)
                    );

                    geometry.meshlets.push_back({
                        .meshletBoundingSphere = glm::vec4(
                            bounds.center[0], bounds.center[1], bounds.center[2],
                            bounds.radius
                        ),
                        .coneApex = glm::vec3(bounds.cone_apex[0], bounds.cone_apex[1], bounds.cone_apex[2]),
                        .coneCutoff = bounds.cone_cutoff,

                        .coneAxis = glm::vec3(bounds.cone_axis[0], bounds.cone_axis[1], bounds.cone_axis[2]),
                        .vertexOffset = vertexOffset,

                        .meshletVerticesOffset = meshletVertexOffset + meshlet.vertex_offset,
                        .meshletTriangleOffset = meshletTrianglesOffset + meshlet.triangle_offset,
                        .meshletVerticesCount = meshlet.vertex_count,
                        .meshletTriangleCount = meshlet.triangle_count,
                    });
                }
            }
        }

        // Recurse into children
        for (int child : node.children)
        {
            TraverseNode(basePath, model, child, worldTransform, verticesOut, indicesOut, materialIndex);
        }
    }

    static ModelHandle LoadMeshModel(std::filesystem::path path, uint32_t materialIndex = 0, std::vector<shaderio::Vertex> vertices = {}, std::vector<uint32_t> indices = {}, bool FrustumFor2D = false)
    {
        uint64_t firstPrimitive = geometry.primitives.size();

        if (!path.empty())
        {
            // Use tinygltf to load the model instead of tinyobjloader
            tinygltf::Model model;
            tinygltf::TinyGLTF loader;
            std::string err;
            std::string warn;

            std::filesystem::path modelPath(path);
            std::filesystem::path baseDir = std::filesystem::absolute(modelPath).parent_path();
            std::string baseTexturePath = baseDir.string();
            if (!baseTexturePath.empty() && baseTexturePath.back() != '/')
            {
                baseTexturePath += "/";
            }
            std::cout << "Using base texture path: " << baseTexturePath << std::endl;

            loader.SetImageLoader([](tinygltf::Image* image, const int image_idx, std::string* err,
                                     std::string* warn, int req_width, int req_height,
                                     const unsigned char* bytes, int size, void* user_data) -> bool
            {
                // Skip KTX2
                if (!image->uri.empty() && image->uri.ends_with(".ktx2"))
                    return true;


                return true;
            }, nullptr);

            bool ret;
            if (path.extension() == ".gltf")
                ret = loader.LoadASCIIFromFile(&model, &err, &warn, path.string());
            else if (path.extension() == ".glb")
                ret = loader.LoadBinaryFromFile(&model, &err, &warn, path.string());

            if (!warn.empty())
            {
                std::cout << "glTF warning: " << warn << std::endl;
            }

            if (!err.empty())
            {
                std::cout << "glTF error: " << err << std::endl;
            }

            if (!ret)
            {
                throw std::runtime_error("Failed to load glTF model");
            }

            for (const auto& allscene : model.scenes)
            {
                for (int nodeIndex : allscene.nodes)
                {
                    TraverseNode(baseTexturePath, model, nodeIndex, glm::mat4(1.0f), primitiveVertices, primitiveIndices, materialIndex);
                }
            }
        }
        else
        {
            primitiveVertices = vertices;
            primitiveIndices = indices;
        }

        uint64_t primitiveCount = geometry.primitives.size() - firstPrimitive;
        return {firstPrimitive, primitiveCount};
    }

    void Renderer::BeginScene(const Camera& camera, const glm::mat4& transform)
    {
        scenesData.view = glm::scale(glm::mat4(1.0f), glm::vec3(1.0f, 1.0f, -1.0f)) * glm::inverse(transform);
        scenesData.proj = camera.GetProjection();
        scenesData.viewProj = camera.GetProjection() * scenesData.view;

        scenesData.viewInverse = glm::inverse(scenesData.view);
        scenesData.projInverse = glm::inverse(scenesData.proj);
        scenesData.viewProjInverse = glm::inverse(scenesData.viewProj);

        /*scenesData.cameraWorldPos = {camera.GetPosition(), 0.0f};*/
        scenesData.frustum = Frustum(scenesData.viewProj);
        scenesData.FrustumCullEnabled = FrustumCullEnabled;

        if (frozen)
        {
            if (!frozenDone)
            {
                frozenSceneData = scenesData;
                frozenDone = true;
            }
            scenesData.frozenView = frozenSceneData.view;
            scenesData.frozenProj = frozenSceneData.proj;
            scenesData.frozenViewProj = frozenSceneData.viewProj;
            scenesData.frozenCameraWorldPos = frozenSceneData.cameraWorldPos;
            scenesData.frozenFrustum = frozenSceneData.frustum;
        }
        else
        {
            scenesData.frozenView = scenesData.view;
            scenesData.frozenProj = scenesData.proj;
            scenesData.frozenViewProj = scenesData.viewProj;
            scenesData.frozenCameraWorldPos = scenesData.cameraWorldPos;
            scenesData.frozenFrustum = scenesData.frustum;
        }
    }

    void Renderer::BeginScene(const EditorCamera& camera)
    {
        scenesData.view = camera.GetViewMatrix();
        scenesData.proj = camera.GetProjection();
        scenesData.viewProj = camera.GetViewProjection();

        scenesData.viewInverse = glm::inverse(scenesData.view);
        scenesData.projInverse = glm::inverse(scenesData.proj);
        scenesData.viewProjInverse = glm::inverse(scenesData.viewProj);

        scenesData.cameraWorldPos = {camera.GetPosition(), 0.0f};

        // Compare to previous frame
        static glm::mat4 lastView = scenesData.view;
        static glm::mat4 lastProj = scenesData.proj;

        bool cameraMoved = (lastView != scenesData.view) || (lastProj != scenesData.proj);

        if (cameraMoved)
            resetFrame(); // reset progressive accumulation
        else
            s_frameIndex = std::min(++s_frameIndex, s_maxAccumulationFrames);

        // Store current matrices for next frame
        lastView = scenesData.view;
        lastProj = scenesData.proj;

        scenesData.frustum = Frustum(scenesData.viewProj);
        scenesData.FrustumCullEnabled = FrustumCullEnabled;

        if (frozen)
        {
            if (!frozenDone)
            {
                frozenSceneData = scenesData;
                frozenDone = true;
            }
            scenesData.frozenView = frozenSceneData.view;
            scenesData.frozenProj = frozenSceneData.proj;
            scenesData.frozenViewProj = frozenSceneData.viewProj;
            scenesData.frozenCameraWorldPos = frozenSceneData.cameraWorldPos;
            scenesData.frozenFrustum = frozenSceneData.frustum;
        }
        else
        {
            scenesData.frozenView = scenesData.view;
            scenesData.frozenProj = scenesData.proj;
            scenesData.frozenViewProj = scenesData.viewProj;
            scenesData.frozenCameraWorldPos = scenesData.cameraWorldPos;
            scenesData.frozenFrustum = scenesData.frustum;
        }
    }

    void Renderer::EndScene()
    {
        /*shaderio::InstancedPBRData inst1;
        glm::mat4 model = glm::translate(glm::mat4(1.0f), glm::vec3(2.5f, 0.0f, 0.0f));
        model = glm::rotate(model, glm::radians(-90.0f), glm::vec3(0, 1, 0)); // rotate around Y axis
        inst1.Model = model;
        inst1.color = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
        inst1.textureIndex = vikingRoom->GetTextureIndex();
        inst1.EntityID = -1;

        RegistryMesh::registerInstance(shaderio::PipelineType_PBR, s_Data.vikingHandle, inst1);*/

        /*
        shaderio::InstancedPBRData inst2;
        inst2.Model = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 2.0f, 0.0f));
        inst2.color = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
        inst2.textureIndex = whiteTexture->GetTextureIndex();
        inst2.EntityID = -1;
        
        RegistryMesh::registerInstance(shaderio::PipelineType_PBR, s_Data.cubeHandle, inst2);
        
        shaderio::InstancedPBRData inst3;
        inst3.Model = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, -2.0f, 1.0f));
        inst3.color = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
        inst3.textureIndex = vikingRoom->GetTextureIndex();
        inst3.EntityID = -1;
        
        RegistryMesh::registerInstance(shaderio::PipelineType_PBR, s_Data.vikingHandle, inst3);*/
    }

    struct QuadData
    {
        glm::mat4 modelMatrix;
        glm::vec4 color;
        uint32_t materialIndex;
        int EntityID;
    };

    static std::vector<QuadData> quads;

    void Renderer::DrawQuad(const glm::mat4& transform, const glm::vec4& color, int entityID)
    {
        /*shaderio::InstancedQuadData instance;
        instance.Model = transform;
        instance.color = color;
        instance.textureIndex = whiteTexture->GetTextureIndex();
        instance.EntityID = entityID;

        RegistryMesh::registerInstance(shaderio::PipelineType_Quad, s_Data.quadHandle, instance);*/
        QuadData instance;
        instance.modelMatrix = transform;
        instance.color = color;
        instance.materialIndex = whiteTexture->GetTextureIndex();
        instance.EntityID = entityID;

        quads.emplace_back(instance);
    }

    void Renderer::DrawQuad(const glm::mat4& transform, const Ref<Texture2D>& texture, float tilingFactor, const glm::vec4& tintColor, int entityID)
    {
        /*shaderio::InstancedQuadData instance;
        instance.Model = transform;
        instance.color = tintColor;
        instance.textureIndex = texture->GetTextureIndex();
        instance.EntityID = entityID;

        RegistryMesh::registerInstance(shaderio::PipelineType_Quad, s_Data.quadHandle, instance);*/
        QuadData instance;
        instance.modelMatrix = transform;
        instance.color = tintColor;
        instance.materialIndex = texture->GetTextureIndex();
        instance.EntityID = entityID;

        quads.emplace_back(instance);
    }

    void Renderer::DrawSprite(const glm::mat4& transform, SpriteRendererComponent& src, int entityID)
    {
        if (src.Texture)
        {
            Ref<Texture2D> texture = AssetManager::GetAsset<Texture2D>(src.Texture);
            DrawQuad(transform, texture, src.TilingFactor, src.Color, entityID);
        }
        else
        {
            DrawQuad(transform, src.Color, entityID);
        }
    }

    struct CircleData
    {
        glm::mat4 WorldPosition;
        glm::vec4 Color;
        float Thickness;
        float Fade;

        // Editor-only
        int EntityID;
    };

    static std::vector<CircleData> circles;

    void Renderer::DrawCircle(const glm::mat4& transform, const glm::vec4& color, float thickness /*= 1.0f*/, float fade /*= 0.005f*/, int entityID /*= -1*/)
    {
        /*shaderio::InstancedCircleData instance;
        instance.WorldPosition = transform;
        instance.Color = color;
        instance.Thickness = thickness;
        instance.Fade = fade;
        instance.EntityID = entityID;

        RegistryMesh::registerInstance(shaderio::PipelineType_Circle, s_Data.circleHandle, instance);*/
        CircleData instance;
        instance.WorldPosition = transform;
        instance.Color = color;
        instance.Thickness = thickness;
        instance.Fade = fade;
        instance.EntityID = entityID;

        circles.emplace_back(instance);
    }

    struct TextData
    {
        glm::mat4 Transform;
        glm::vec2 QuadMin;
        glm::vec2 QuadMax;
        glm::vec2 TexMin;
        glm::vec2 TexMax;
        glm::vec4 Color;
        uint32_t TextureIndex;

        // TODO: bg color for outline/bg

        // Editor-only
        int EntityID;
    };

    static std::vector<TextData> texts;

    void Renderer::DrawString(const std::string& string, Ref<Font> font, const glm::mat4& transform, const TextParams& textParams, int entityID)
    {
        const auto& fontGeometry = font->GetMSDFData()->FontGeometry;
        const auto& metrics = fontGeometry.getMetrics();
        Ref<Texture2D> fontAtlas = font->GetAtlasTexture();

        s_Data.FontAtlasTexture = fontAtlas;

        double x = 0.0;
        double fsScale = 1.0 / (metrics.ascenderY - metrics.descenderY);
        double y = 0.0;

        const float spaceGlyphAdvance = fontGeometry.getGlyph(' ')->getAdvance();

        for (size_t i = 0; i < string.size(); i++)
        {
            char character = string[i];
            if (character == '\r')
                continue;

            if (character == '\n')
            {
                x = 0;
                y -= fsScale * metrics.lineHeight * textParams.LineSpacing;
                continue;
            }

            if (character == ' ')
            {
                float advance = spaceGlyphAdvance;
                if (i < string.size() - 1)
                {
                    char nextCharacter = string[i + 1];
                    double dAdvance;
                    fontGeometry.getAdvance(dAdvance, character, nextCharacter);
                    advance = (float)dAdvance;
                }

                x += fsScale * advance + textParams.Kerning;
                continue;
            }

            if (character == '\t')
            {
                // NOTE(Yan): is this right?
                x += 4.0f * (fsScale * spaceGlyphAdvance + textParams.Kerning);
                continue;
            }

            auto glyph = fontGeometry.getGlyph(character);
            if (!glyph)
                glyph = fontGeometry.getGlyph('?');
            if (!glyph)
                return;

            double al, ab, ar, at;
            glyph->getQuadAtlasBounds(al, ab, ar, at);
            glm::vec2 texCoordMin((float)al, (float)ab);
            glm::vec2 texCoordMax((float)ar, (float)at);

            double pl, pb, pr, pt;
            glyph->getQuadPlaneBounds(pl, pb, pr, pt);
            glm::vec2 quadMin((float)pl, (float)pb);
            glm::vec2 quadMax((float)pr, (float)pt);

            quadMin *= fsScale, quadMax *= fsScale;
            quadMin += glm::vec2(x, y);
            quadMax += glm::vec2(x, y);

            float texelWidth = 1.0f / fontAtlas->GetWidth();
            float texelHeight = 1.0f / fontAtlas->GetHeight();
            texCoordMin *= glm::vec2(texelWidth, texelHeight);
            texCoordMax *= glm::vec2(texelWidth, texelHeight);

            // render here
            /*shaderio::InstancedTextData instance;
            instance.QuadMin = quadMin;
            instance.QuadMax = quadMax;
            instance.Transform = transform; // store transform per draw
            instance.TexMin = texCoordMin;
            instance.TexMax = texCoordMax;
            instance.Color = textParams.Color;
            instance.TextureIndex = fontAtlas->GetTextureIndex();
            instance.EntityID = entityID;

            RegistryMesh::registerInstance(shaderio::PipelineType_Text, s_Data.textHandle, instance);*/

            TextData instance;
            instance.QuadMin = quadMin;
            instance.QuadMax = quadMax;
            instance.Transform = transform; // store transform per draw
            instance.TexMin = texCoordMin;
            instance.TexMax = texCoordMax;
            instance.Color = textParams.Color;
            instance.TextureIndex = fontAtlas->GetTextureIndex();
            instance.EntityID = entityID;

            texts.emplace_back(instance);

            if (i < string.size() - 1)
            {
                double advance = glyph->getAdvance();
                char nextCharacter = string[i + 1];
                fontGeometry.getAdvance(advance, character, nextCharacter);

                x += fsScale * advance + textParams.Kerning;
            }
        }
    }

    void Renderer::DrawString(const std::string& string, const glm::mat4& transform, const TextComponent& component, int entityID)
    {
        DrawString(string, component.FontAsset, transform, {component.Color, component.Kerning, component.LineSpacing}, entityID);
    }

    struct LineData
    {
        glm::vec3 P0;
        glm::vec3 P1;
        glm::vec4 Color;

        // Editor-only
        int EntityID;
    };

    static std::vector<LineData> lines;

    void Renderer::DrawLine(const glm::vec3& p0, const glm::vec3& p1, const glm::vec4& color, int entityID)
    {
        /*shaderio::InstancedLineData instance;
        instance.P0 = p0;
        instance.P1 = p1;
        instance.Color = color;
        instance.EntityID = entityID;

        RegistryMesh::registerInstance(shaderio::PipelineType_Line, s_Data.lineHandle, instance);*/

        LineData instance;
        instance.P0 = p0;
        instance.P1 = p1;
        instance.Color = color;
        instance.EntityID = entityID;

        lines.emplace_back(instance);
    }

    void Renderer::DrawRect(const glm::vec3& position, const glm::vec2& size, const glm::vec4& color, int entityID)
    {
        glm::vec3 p0 = glm::vec3(position.x - size.x * 0.5f, position.y - size.y * 0.5f, position.z);
        glm::vec3 p1 = glm::vec3(position.x + size.x * 0.5f, position.y - size.y * 0.5f, position.z);
        glm::vec3 p2 = glm::vec3(position.x + size.x * 0.5f, position.y + size.y * 0.5f, position.z);
        glm::vec3 p3 = glm::vec3(position.x - size.x * 0.5f, position.y + size.y * 0.5f, position.z);

        DrawLine(p0, p1, color, entityID);
        DrawLine(p1, p2, color, entityID);
        DrawLine(p2, p3, color, entityID);
        DrawLine(p3, p0, color, entityID);
    }

    void Renderer::DrawRect(const glm::mat4& transform, const glm::vec4& color, int entityID)
    {
        glm::vec3 p0 = transform * glm::vec4(-0.5f, -0.5f, 0.0f, 1.0f);
        glm::vec3 p1 = transform * glm::vec4(0.5f, -0.5f, 0.0f, 1.0f);
        glm::vec3 p2 = transform * glm::vec4(0.5f, 0.5f, 0.0f, 1.0f);
        glm::vec3 p3 = transform * glm::vec4(-0.5f, 0.5f, 0.0f, 1.0f);

        DrawLine(p0, p1, color, entityID);
        DrawLine(p1, p2, color, entityID);
        DrawLine(p2, p3, color, entityID);
        DrawLine(p3, p0, color, entityID);
    }

    struct PushConstant2D
    {
        uint64_t numOfElements;
        uint64_t bufferAddress;
        uint64_t sceneData;
    };

    struct PushConstantSkyBox
    {
        uint32_t MaterialIndex;
        uint64_t sceneData;
    };

    struct PushConstantRayTrace
    {
        uint64_t sceneData;
        uint64_t vertexBuffer;
        uint64_t indexBuffer;
        uint64_t meshletVerticesBuffer;
        uint64_t meshletTrianglesBuffer;
        uint64_t meshletBuffer;
        uint64_t meshletPrimitives;
        uint64_t meshDraws;
        uint64_t materialBuffer;
        uint64_t instanceLutBuffer;
        uint64_t lightsBuffer;
    };

   

    

    RuntimeModel BuildRuntimeModel(const ModelHandle& model)
    {
        RuntimeModel rm{};
        rm.handle = model;

        for (uint64_t i = 0; i < model.primitiveCount; ++i)
        {
            uint64_t primitiveId = model.firstPrimitive + i;
            const auto& prim = geometry.primitives[primitiveId];

            rm.primitives.push_back({
                primitiveId,
                prim.meshletCount
            });
        }

        return rm;
    }

    void SubmitModel
    (
        const RuntimeModel& model,
        const glm::mat4& transform)
    {
        for (const auto& prim : model.primitives)
        {
            meshDraws.emplace_back(MeshDraw
                {
                transform,
                glm::transpose(glm::inverse(glm::mat3(transform))),
                prim.primitiveId
            });

            meshTasks.emplace_back(VanKDrawMeshTasksIndirectCommand
                {
                (prim.meshletCount + 63) / 64,
                1,
                    1
            });
        }
    }

    void SubmitModelPrimitive
    (
        const RuntimeModel& model,
        uint32_t primitiveIndex,
        const glm::mat4& transform)
    {
        const auto& prim = model.primitives[primitiveIndex];

        meshDraws.emplace_back(MeshDraw
            {
            transform,
            glm::transpose(glm::inverse(glm::mat3(transform))),
            prim.primitiveId
        });

        meshTasks.emplace_back(VanKDrawMeshTasksIndirectCommand
            {
            (prim.meshletCount + 63) / 64,
            1,
            1
        });
    }

    enum LightType
    {
        ePoint = 0, // Point light type
        eSpot = 1, // Spot light type
        eDirectional = 2 // Directional light type
    };


    struct Lights
    {
        glm::vec3 position; // Position of the punctual light in world space
        float intensity; // Intensity of the light
        glm::vec3 direction; // Direction of the light (for spot and directional lights)
        int type; // Type of the light (0 = point, 1 = spot, 2 = directional)
        glm::vec3 color; // Color of the light (RGB)
        float coneAngle; // Cone angle for spot lights (in radians, 0 for point and directional lights)
    };

    std::vector<Lights> lights;
    RuntimeModel runtimePlant;
    RuntimeModel runtimePlant2;

    void Renderer::CreateRenderTargets()
    {
        std::cout << "CreateRenderTargets called with viewport: " << m_ViewportSize.width << "x" << m_ViewportSize.height << std::endl;

        // Wait for GPU to finish using old render targets
        RenderCommand::waitForGraphicsQueueIdle();

        // Destroy old render targets
        /*sceneImage.reset();
        colorImage.reset();
        entityImage.reset();
        entityColorImage.reset();
        depthImage.reset();
        rayTracingImage.reset();
        finalImage.reset();*/
        // since those all are renderiamges i could put viewportsize inside vulkantexture class or call it once that inits it so i dont have to
        // provide myself here every time but this might be not needed if integrated into rendergraph
        // Create new render targets with current viewport size
        sceneImage = RenderTargetImage::Create({.Width = m_ViewportSize.width, .Height = m_ViewportSize.height});

        colorImage = RenderTargetImage::Create({
            .Width = m_ViewportSize.width, .Height = m_ViewportSize.height,
            .SampleCount = 64, .isResolveImage = true, .resolveTargetID = sceneImage->GetRenderImageIndex()
        });

        entityImage = RenderTargetImage::Create({.Width = m_ViewportSize.width, .Height = m_ViewportSize.height, .Format = ImageFormat::R32SINT});

        entityColorImage = RenderTargetImage::Create({
            .Width = m_ViewportSize.width, .Height = m_ViewportSize.height, .Format = ImageFormat::R32SINT,
            .SampleCount = 64, .isResolveImage = true, .resolveTargetID = entityImage->GetRenderImageIndex()
        });

        depthImage = RenderTargetImage::Create({.Width = m_ViewportSize.width, .Height = m_ViewportSize.height, .SampleCount = 64, .depthImage = true});

        rayTracingImage = RenderTargetImage::Create({.Width = m_ViewportSize.width, .Height = m_ViewportSize.height, .Format = ImageFormat::R32G32B32A32_SFLOAT, .isStorageImage = true});

        finalImage = RenderTargetImage::Create({.Width = m_ViewportSize.width, .Height = m_ViewportSize.height});
    }

    void Renderer::Init(Window& window)
    {
        RendererAPI::Config config;
        config.window = window.getWindowHandle();
        m_window = window.getWindowHandle();
        RenderCommand::SetConfig(config);
        RenderCommand::Init();

        m_BufferManager = std::make_unique<BufferManager>();

        // Render Target
        CreateRenderTargets();

        // Shader creation
        auto FinalRender = GetShaderLibrary().Load("FinalRender", "FinalRender.slang");
        auto MeshTaskSubmit = GetShaderLibrary().Load("MeshTaskSubmit", "MeshTaskSubmit.slang");
        auto MeshShader = GetShaderLibrary().Load("MeshShader", "MeshShader.slang");
        auto MeshQuad = GetShaderLibrary().Load("MeshQuad", "MeshQuad.slang");
        auto MeshCircle = GetShaderLibrary().Load("MeshCircle", "MeshCircle.slang");
        auto MeshText = GetShaderLibrary().Load("MeshText", "MeshText.slang");
        auto MeshLine = GetShaderLibrary().Load("MeshLine", "MeshLine.slang");
        auto SkyBox = GetShaderLibrary().Load("SkyBox", "SkyBox.slang");
        auto raytracingbasic = GetShaderLibrary().Load("raytracingbasic", "raytracingbasic.slang");
        auto PathTracer = GetShaderLibrary().Load("PathTracer", "PathTracer.slang");
        auto VoxelTracer = GetShaderLibrary().Load("VoxelTracer", "VoxelTracer.slang");

        // Pipeline Creation
        uint32_t useTexture = true;
        std::vector<VanKSpecializationMapEntries> mapEntries
        {
            {.constantID = 0, .offset = 0, .size = sizeof(uint32_t)}
        };
        //dont like needed to be like this because if reloadpipeline then it craashes because data in struct is not copied fk this
        VanKSpecializationInfo specInfo;
        specInfo.Data.resize(sizeof(uint32_t));
        std::memcpy(specInfo.Data.data(), &useTexture, sizeof(uint32_t));
        specInfo.MapEntries = mapEntries;

        VanKPipelineShaderStageCreateInfo ShaderStageCreateInfo
        {
            .specializationInfo = specInfo
        };

        /*BufferLayout DebugLayout
        {
            {ShaderDataType::Float3, "Position"},
            {ShaderDataType::Float3, "Color"},
            {ShaderDataType::Float2, "TexCoord"},
        };*/

        VanKPipelineVertexInputStateCreateInfo VertexInputStateCreateInfo
        {
            .VanKBufferLayout = {}
        };

        VanKPipelineInputAssemblyStateCreateInfo InputAssemblyStateCreateInfo
        {
            .VanKPrimitive = VanK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST // make it shorter maybe ??
        };

        VanKPipelineRasterizationStateCreateInfo RasterizationStateCreateInfo
        {
            .VanKPolygon = VanK_POLYGON_MODE_FILL,
            .VanKFrontFace = VanK_FRONT_FACE_COUNTER_CLOCKWISE,
        };

        const std::vector<VanKPipelineColorBlendAttachmentState> ColorBlendAttachmentStates =
        {
            {
                .blendEnable = true,
                .srcColorBlendFactor = VanK_BLEND_FACTOR_SRC_ALPHA,
                .dstColorBlendFactor = VanK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
                .colorBlendOp = VanK_BLEND_OP_ADD,
                .srcAlphaBlendFactor = VanK_BLEND_FACTOR_SRC_ALPHA,
                .dstAlphaBlendFactor = VanK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
                .alphaBlendOp = VanK_BLEND_OP_ADD,
                .colorWriteMask = VanK_COLOR_COMPONENT_R_BIT | VanK_COLOR_COMPONENT_G_BIT | VanK_COLOR_COMPONENT_B_BIT | VanK_COLOR_COMPONENT_A_BIT,
            },
            {
                .blendEnable = false,
                .srcColorBlendFactor = VanK_BLEND_FACTOR_ONE,
                .dstColorBlendFactor = VanK_BLEND_FACTOR_ZERO,
                .colorBlendOp = VanK_BLEND_OP_ADD,
                .srcAlphaBlendFactor = VanK_BLEND_FACTOR_ONE,
                .dstAlphaBlendFactor = VanK_BLEND_FACTOR_ZERO,
                .alphaBlendOp = VanK_BLEND_OP_ADD,
                .colorWriteMask = VanK_COLOR_COMPONENT_R_BIT,
            },
        };

        VanKPipelineColorBlendStateCreateInfo ColorBlendStateCreateInfo
        {
            .logicOp = false,
            .VanKLogicOp = VanK_LOGIC_OP_COPY,
            .VanKColorBlendAttachmentState = ColorBlendAttachmentStates
        };

        VanKPipelineMultisampleStateCreateInfo MultisampleStateCreateInfo
        {
            .sampleCount = VanK_SAMPLE_COUNT_64_BIT,
            .sampleShadingEnable = true,
            .minSampleShading = 0.2f
        };

        VanKPipelineDepthStencilStateCreateInfo DepthStencilStateCreateInfo
        {
            .depthTestEnable = true, // dsiabled ebcause of transparence textures
            .depthWriteEnable = true,
            .VanKdepthCompareOp = VanK_COMPARE_OP_GREATER
        };

        VanKPipelineRenderingCreateInfo RenderingCreateInfo
        {
            .VanKColorAttachmentFormats = {VanK_Format_B8G8R8A8Srgb, VanK_FORMAT_R32_SINT}
        };

        VanKPipelineLayoutCreateInfo PipelineLayoutCreateInfo
        {
            .PushConstants = {PushConstantRange{0, sizeof(uint32_t)}}
        };

        VanKGraphicsPipelineSpecification GraphicsPipelineSpecification
        {
            .PipelineType = VanK_Graphics,
            .ShaderStageCreateInfo = ShaderStageCreateInfo,
            .VertexInputStateCreateInfo = VertexInputStateCreateInfo,
            .InputAssemblyStateCreateInfo = InputAssemblyStateCreateInfo,
            .RasterizationStateCreateInfo = RasterizationStateCreateInfo,
            .ColorBlendStateCreateInfo = ColorBlendStateCreateInfo,
            .MultisampleStateCreateInfo = MultisampleStateCreateInfo,
            .DepthStateInfo = DepthStencilStateCreateInfo,
            .RenderingCreateInfo = RenderingCreateInfo,
            .PipelineLayoutInfo = PipelineLayoutCreateInfo,
        };

        m_FinalRenderPipelineSpecification = GraphicsPipelineSpecification;
        m_FinalRenderPipelineSpecification.PipelineType = VanK_Mesh;
        m_FinalRenderPipelineSpecification.ShaderStageCreateInfo.VanKShader = FinalRender;
        const std::vector<VanKPipelineColorBlendAttachmentState> ColorBlendAttachmentStates2 =
        {
            {
                .blendEnable = true,
                .srcColorBlendFactor = VanK_BLEND_FACTOR_SRC_ALPHA,
                .dstColorBlendFactor = VanK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
                .colorBlendOp = VanK_BLEND_OP_ADD,
                .srcAlphaBlendFactor = VanK_BLEND_FACTOR_SRC_ALPHA,
                .dstAlphaBlendFactor = VanK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
                .alphaBlendOp = VanK_BLEND_OP_ADD,
                .colorWriteMask = VanK_COLOR_COMPONENT_R_BIT | VanK_COLOR_COMPONENT_G_BIT | VanK_COLOR_COMPONENT_B_BIT | VanK_COLOR_COMPONENT_A_BIT,
            }
        };
        m_FinalRenderPipelineSpecification.ColorBlendStateCreateInfo.VanKColorBlendAttachmentState = ColorBlendAttachmentStates2;

        m_FinalRenderPipelineSpecification.MultisampleStateCreateInfo.sampleCount = VanK_SAMPLE_COUNT_1_BIT;
        m_FinalRenderPipelineSpecification.RenderingCreateInfo.VanKColorAttachmentFormats = {VanK_Format_B8G8R8A8Srgb};
        m_FinalRenderPipeline = RenderCommand::createGraphicsPipeline(m_FinalRenderPipelineSpecification);
        RegisterPipelineForShaderWatcher("FinalRender", "FinalRender.slang", &m_FinalRenderPipelineSpecification, nullptr, nullptr, &m_FinalRenderPipeline, VanKGraphics);

        m_MeshPipelineSpecification = GraphicsPipelineSpecification;
        m_MeshPipelineSpecification.PipelineType = VanK_Mesh;
        m_MeshPipelineSpecification.ShaderStageCreateInfo.VanKShader = MeshShader;
        m_MeshPipelineSpecification.PipelineLayoutInfo.PushConstants = {PushConstantRange{0, sizeof(TaskMeshPipelinePushConstant)}};
        m_MeshPipeline = RenderCommand::createGraphicsPipeline(m_MeshPipelineSpecification);
        RegisterPipelineForShaderWatcher("MeshShader", "MeshShader.slang", &m_MeshPipelineSpecification, nullptr, nullptr, &m_MeshPipeline, VanKGraphics);

        m_MeshQuadPipelineSpecification = GraphicsPipelineSpecification;
        m_MeshQuadPipelineSpecification.PipelineType = VanK_Mesh;
        m_MeshQuadPipelineSpecification.ShaderStageCreateInfo.VanKShader = MeshQuad;
        m_MeshQuadPipelineSpecification.PipelineLayoutInfo.PushConstants = {PushConstantRange{0, sizeof(PushConstant2D)}};
        m_MeshQuadPipeline = RenderCommand::createGraphicsPipeline(m_MeshQuadPipelineSpecification);
        RegisterPipelineForShaderWatcher("MeshQuad", "MeshQuad.slang", &m_MeshQuadPipelineSpecification, nullptr, nullptr, &m_MeshQuadPipeline, VanKGraphics);

        m_MeshCirclePipelineSpecification = GraphicsPipelineSpecification;
        m_MeshCirclePipelineSpecification.PipelineType = VanK_Mesh;
        m_MeshCirclePipelineSpecification.ShaderStageCreateInfo.VanKShader = MeshCircle;
        m_MeshCirclePipelineSpecification.PipelineLayoutInfo.PushConstants = {PushConstantRange{0, sizeof(PushConstant2D)}};
        m_MeshCirclePipeline = RenderCommand::createGraphicsPipeline(m_MeshCirclePipelineSpecification);
        RegisterPipelineForShaderWatcher("MeshCircle", "MeshCircle.slang", &m_MeshCirclePipelineSpecification, nullptr, nullptr, &m_MeshCirclePipeline, VanKGraphics);

        m_MeshTextPipelineSpecification = GraphicsPipelineSpecification;
        m_MeshTextPipelineSpecification.PipelineType = VanK_Mesh;
        m_MeshTextPipelineSpecification.ShaderStageCreateInfo.VanKShader = MeshText;
        m_MeshTextPipelineSpecification.PipelineLayoutInfo.PushConstants = {PushConstantRange{0, sizeof(PushConstant2D)}};
        m_MeshTextPipeline = RenderCommand::createGraphicsPipeline(m_MeshTextPipelineSpecification);
        RegisterPipelineForShaderWatcher("MeshText", "MeshText.slang", &m_MeshTextPipelineSpecification, nullptr, nullptr, &m_MeshTextPipeline, VanKGraphics);

        m_MeshLinePipelineSpecification = GraphicsPipelineSpecification;
        m_MeshLinePipelineSpecification.PipelineType = VanK_Mesh;
        m_MeshLinePipelineSpecification.ShaderStageCreateInfo.VanKShader = MeshLine;
        m_MeshLinePipelineSpecification.PipelineLayoutInfo.PushConstants = {PushConstantRange{0, sizeof(PushConstant2D)}};
        m_MeshLinePipeline = RenderCommand::createGraphicsPipeline(m_MeshLinePipelineSpecification);
        RegisterPipelineForShaderWatcher("MeshLine", "MeshLine.slang", &m_MeshLinePipelineSpecification, nullptr, nullptr, &m_MeshLinePipeline, VanKGraphics);

        m_MeshSkyBoxPipelineSpecification = GraphicsPipelineSpecification;
        m_MeshSkyBoxPipelineSpecification.PipelineType = VanK_Mesh;
        m_MeshSkyBoxPipelineSpecification.ShaderStageCreateInfo.VanKShader = SkyBox;
        m_MeshSkyBoxPipelineSpecification.DepthStateInfo.depthWriteEnable = false;
        m_MeshSkyBoxPipelineSpecification.DepthStateInfo.VanKdepthCompareOp = VanK_COMPARE_OP_GREATER_OR_EQUAL;
        m_MeshSkyBoxPipelineSpecification.PipelineLayoutInfo.PushConstants = {PushConstantRange{0, sizeof(PushConstantSkyBox)}};
        m_MeshSkyBoxPipeline = RenderCommand::createGraphicsPipeline(m_MeshSkyBoxPipelineSpecification);
        RegisterPipelineForShaderWatcher("SkyBox", "SkyBox.slang", &m_MeshSkyBoxPipelineSpecification, nullptr, nullptr, &m_MeshSkyBoxPipeline, VanKGraphics);

        // Compute Pipelines creations
        VanKComputePipelineCreateInfo ComputePipelineCreateInfo
        {
            .VanKShader = MeshTaskSubmit
        };

        VanKComputePipelineLayoutCreateInfo ComputePipelineLayoutCreateInfo
        {
            .PushConstants = {PushConstantRange{0, sizeof(meshTasksSubmitPushConstant)}}
        };

        VanKComputePipelineSpecification computePipelineSpecification
        {
            .ComputePipelineCreateInfo = ComputePipelineCreateInfo,
            .ComputePipelineLayoutInfo = ComputePipelineLayoutCreateInfo
        };

        m_ComputeDrawMeshTaskCommandPipelineSpecification = computePipelineSpecification;
        m_ComputeDrawMeshTaskCommandPipeline = RenderCommand::createComputeShaderPipeline(m_ComputeDrawMeshTaskCommandPipelineSpecification);
        RegisterPipelineForShaderWatcher("MeshTaskSubmit", "MeshTaskSubmit.slang", nullptr, &m_ComputeDrawMeshTaskCommandPipelineSpecification, nullptr, &m_ComputeDrawMeshTaskCommandPipeline,
                                         VanKCompute);

        // Raytracing Pipeline creation
        VanKPipelineShaderStageCreateInfo rtShaderStageCreateInfo
        {
            .VanKShader = PathTracer
        };

        VanKPipelineLayoutCreateInfo rtPipelineLayoutCreateInfo
        {
            .PushConstants = {PushConstantRange{0, sizeof(PushConstantRayTrace)}}
        };
        
        // Define the groups to replicate your manual logic perfectly
        std::vector<VanKRayTracingGroup> rtGroups =
        {
            // Group 0: Raygen (Raygen #0)
            { .type = VanKRayTracingGroupType::General, .raygenShader = 0 },
    
            // Group 1: Primary Miss (Miss #0)
            { .type = VanKRayTracingGroupType::General, .missShader = 0 },
    
            // Group 2: Shadow Miss (Miss #1)
            { .type = VanKRayTracingGroupType::General, .missShader = 1 },
    
            // Group 3: Primary Hit (CHIT #0 + AHIT #0)
            { 
                .type = VanKRayTracingGroupType::TrianglesHitGroup, 
                .closestHitShader = 0, 
                .anyHitShader = 0 
            },
    
            // Group 4: Shadow Hit (No CHIT + AHIT #1)
            { 
                .type = VanKRayTracingGroupType::TrianglesHitGroup, 
                .closestHitShader = VANK_SHADER_UNUSED, 
                .anyHitShader = 1 
            }
        };

        VanKRaytracingPipelineSpecification raytracingPipelineSpecification
        {
            .ShaderStageCreateInfo = rtShaderStageCreateInfo,
            .PipelineLayoutInfo = rtPipelineLayoutCreateInfo,
            .groups = rtGroups
        };

        m_RaytracingPipelineSpecification = raytracingPipelineSpecification;
        m_RaytracingPipeline = RenderCommand::createRayTracingPipeline(m_RaytracingPipelineSpecification);
        RegisterPipelineForShaderWatcher("PathTracer", "PathTracer.slang", nullptr, nullptr, &m_RaytracingPipelineSpecification, &m_RaytracingPipeline, VanKRaytracing);

        m_RaytracingVoxelPipelineSpecification = raytracingPipelineSpecification;
        m_RaytracingVoxelPipelineSpecification.ShaderStageCreateInfo.VanKShader = VoxelTracer;
        std::vector<VanKRayTracingGroup> VoxelGroups =
        {
            { .type = VanKRayTracingGroupType::General, .raygenShader = 0 },
            
            { .type = VanKRayTracingGroupType::General, .missShader = 0 },
            
            { .type = VanKRayTracingGroupType::TrianglesHitGroup, .closestHitShader = 0, .anyHitShader = VANK_SHADER_UNUSED, .intersectionShader = VANK_SHADER_UNUSED},
            
            { .type = VanKRayTracingGroupType::ProceduralHitGroup, .closestHitShader = 1, .anyHitShader = VANK_SHADER_UNUSED, .intersectionShader = 0 }
        };
        m_RaytracingVoxelPipelineSpecification.groups = VoxelGroups;
        m_RaytracingVoxelPipeline = RenderCommand::createRayTracingPipeline(m_RaytracingVoxelPipelineSpecification);
        RegisterPipelineForShaderWatcher("VoxelTracer", "VoxelTracer.slang", nullptr, nullptr, &m_RaytracingVoxelPipelineSpecification, &m_RaytracingVoxelPipeline, VanKRaytracing);
        
        WatchShaderFiles(); // has to be last after pipeline creation

        //sampler
        skyboxSampler = // remove make this defualt for all
        {
            .magFilter = VanKFilter::filterLinear,
            .minFilter = VanKFilter::filterLinear,
            .mipmapMode = VanKSamplerMipmapMode::mipmapModeLinear,
            .addressModeU = VanKSamplerAddressMode::addressModeClampToEdge,
            .addressModeV = VanKSamplerAddressMode::addressModeClampToEdge,
            .addressModeW = VanKSamplerAddressMode::addressModeClampToEdge,
            .mipLodBias = 0.0f, // only works if it has enoug miplevels is miplevel is max 1 then making this 10 crashes
            .anisotopyEnable = true,
            .compareEnable = false,
            .compareOp = VanKCompareOp::compareOpAlways,
            .minLod = 0.0f // the higher this is the lower the resolution 0 is max res
        };

        whiteTexture = TextureImporter::LoadTexture2D("");
        pinkTexture = TextureImporter::LoadTexture2D("", {.defaultColor = glm::vec4(1.0f, 0.0f, 1.0f, 1.0f)});
        /*vikingRoom = TextureImporter::LoadTexture2D("assets/textures/viking_room.ktx2", {.FlipTexture = true});
        ChernoLogo = TextureImporter::LoadTexture2D("assets/textures/ChernoLogo.ktx2");*/
        BRDF2DLUT = TextureImporter::LoadTexture2D("E:/dev/VulkanAdventure/vulkanhpptutorial/VulkanTemplate/assets/pbrstuff/lol.ktx2", {.FlipTexture = true, .SamplerInfo = skyboxSampler});

        /*
        cubemap = TextureImporter::LoadTexture2D("E:/dev/VulkanAdventure/vulkanhpptutorial/VulkanTemplate/assets/pbrstuff/cubemap.ktx2", {.SamplerInfo = skyboxSampler});
        irradianceMap = TextureImporter::LoadTexture2D("E:/dev/VulkanAdventure/vulkanhpptutorial/VulkanTemplate/assets/pbrstuff/irradiance.ktx2", {.SamplerInfo = skyboxSampler});
        prefilterMap = TextureImporter::LoadTexture2D("E:/dev/VulkanAdventure/vulkanhpptutorial/VulkanTemplate/assets/pbrstuff/prefilter.ktx2", {.SamplerInfo = skyboxSampler});
        */

        cubemap = TextureImporter::LoadTexture2D("E:/dev/VulkanAdventure/vulkanhpptutorial/VulkanTemplate/assets/pbrstuff/cloudypPureSky/cubeMapSky.ktx2", {.SamplerInfo = skyboxSampler});
        irradianceMap = TextureImporter::LoadTexture2D("E:/dev/VulkanAdventure/vulkanhpptutorial/VulkanTemplate/assets/pbrstuff/cloudypPureSky/cubeMapSkyIrradiance.ktx2",
                                                       {.SamplerInfo = skyboxSampler});
        prefilterMap = TextureImporter::LoadTexture2D("E:/dev/VulkanAdventure/vulkanhpptutorial/VulkanTemplate/assets/pbrstuff/cloudypPureSky/cubeMapSkyPrefilter.ktx2",
                                                      {.SamplerInfo = skyboxSampler});

        /*
        cubemap = TextureImporter::LoadTexture2D("E:/dev/VulkanAdventure/vulkanhpptutorial/VulkanTemplate/assets/pbrstuff/nightSky/nightSkyCube.ktx2", {.SamplerInfo = skyboxSampler});
        irradianceMap = TextureImporter::LoadTexture2D("E:/dev/VulkanAdventure/vulkanhpptutorial/VulkanTemplate/assets/pbrstuff/nightSky/nightSkyIrradiance.ktx2", {.SamplerInfo = skyboxSampler});
        prefilterMap = TextureImporter::LoadTexture2D("E:/dev/VulkanAdventure/vulkanhpptutorial/VulkanTemplate/assets/pbrstuff/nightSky/nightSkyPrefilter.ktx2", {.SamplerInfo = skyboxSampler});
        */


        /*
        rustedIron = TextureImporter::LoadTexture2D("E:/dev/VulkanAdventure/vulkanhpptutorial/VulkanTemplate/assets/rustediron1-alt2-bl/rustediron2_basecolor.ktx2");
        rustedIronMetalRough = TextureImporter::LoadTexture2D("E:/dev/VulkanAdventure/vulkanhpptutorial/VulkanTemplate/assets/rustediron1-alt2-bl/metalRough.ktx2");
        rustedIronNormal = TextureImporter::LoadTexture2D("E:/dev/VulkanAdventure/vulkanhpptutorial/VulkanTemplate/assets/rustediron1-alt2-bl/rustediron2_normal.ktx2");
        */

        /*bool ret = loader.LoadBinaryFromFile(&model, &err, &warn, MODEL_PATH);*/
        //bool ret = loader.LoadASCIIFromFile(&model, &err, &warn, "E:/dev/VulkanAdventure/vulkanhpptutorial/VulkanTemplate/assets/stanford_bunny/stanford_bunny.gltf");
        //bool ret = loader.LoadASCIIFromFile(&model, &err, &warn, "E:/dev/VulkanAdventure/vulkanhpptutorial/VulkanTemplate/assets/Suzanne_monkey/Suzanne.gltf");
        //bool ret = loader.LoadASCIIFromFile(&model, &err, &warn, "E:/dev/VulkanAdventure/vulkanhpptutorial/VulkanTemplate/assets/Sponza/Sponza.gltf");
        /*bool ret = loader.LoadASCIIFromFile(&model, &err, &warn, "E:/dev/VulkanAdventure/vulkanhpptutorial/VulkanTemplate/assets/happy_bhudda/scene.gltf");*/
        /*bool ret = loader.LoadASCIIFromFile(&model, &err, &warn, "E:/dev/VulkanAdventure/vulkanhpptutorial/VulkanTemplate/assets/kitten/kitten.gltf");*/
        //bool ret = loader.LoadASCIIFromFile(&model, &err, &warn, "E:/dev/VulkanAdventure/vulkanhpptutorial/VulkanTemplate/assets/viking_room/viking_room.gltf");

        // this makes no sense anway for 2d i want to use different shader anyway so i can build the quad in mesh shader directly no ?
        std::vector<shaderio::Vertex> quadVertices =
        {
            {{-0.5f, -0.5f, 0.0f}, {0.0f, 0.0f}},
            {{0.5f, -0.5f, 0.0f}, {1.0f, 0.0f}},
            {{0.5f, 0.5f, 0.0f}, {1.0f, 1.0f}},
            {{-0.5f, 0.5f, 0.0f}, {0.0f, 1.0f}}
        };

        std::vector<uint32_t> quadIndices =
        {
            0, 1, 2, // first triangle
            0, 2, 3 // second triangle
        };

        /*ModelHandle bistro = LoadMeshModel("E:/dev/VulkanAdventure/vulkanhpptutorial/VulkanTemplate/assets/bistro/bistro.gltf", pinkTexture->GetTextureIndex());*/
        /*ModelHandle Arcade = LoadMeshModel("E:/dev/VulkanAdventure/vulkanhpptutorial/VulkanTemplate/assets/Arcade/Arcade.gltf", pinkTexture->GetTextureIndex());*/
        //ModelHandle bunny = LoadMeshModel("E:/dev/VulkanAdventure/vulkanhpptutorial/VulkanTemplate/assets/stanford_bunny/stanford_bunny.gltf");
        /*LoadMeshModel("", quadVertices, quadIndices, whiteTexture->GetTextureIndex(), true);*/
        /*ModelHandle viking = LoadMeshModel("E:/dev/VulkanAdventure/vulkanhpptutorial/VulkanTemplate/assets/viking_room/viking_room.gltf");
        materials.back().albedoTexture = vikingRoom->GetTextureIndex();*/
        /*ModelHandle monkey = LoadMeshModel("E:/dev/VulkanAdventure/vulkanhpptutorial/VulkanTemplate/assets/Suzanne_monkey/Suzanne.gltf");*/
        /*ModelHandle sponza = LoadMeshModel("E:/dev/VulkanAdventure/vulkanhpptutorial/VulkanTemplate/assets/Sponza/Sponza.gltf");*/
        /*ModelHandle bhudda = LoadMeshModel("E:/dev/VulkanAdventure/vulkanhpptutorial/VulkanTemplate/assets/happy_bhudda/scene.gltf");*/
        /*ModelHandle damagedHelmet = LoadMeshModel("E:/dev/VulkanAdventure/vulkanhpptutorial/VulkanTemplate/assets/damaged_helmet/DamagedHelmet.gltf");*/
        /*ModelHandle cornellBox = LoadMeshModel("E:/dev/VulkanAdventure/vulkanhpptutorial/VulkanTemplate/assets/cornell_box/scene.gltf");*/
        /*ModelHandle cornellBox = LoadMeshModel("E:/dev/VulkanAdventure/vulkanhpptutorial/VulkanTemplate/assets/CornellBox/CornellBox-Original.gltf");*/
        ModelHandle cornellBox = LoadMeshModel("E:/dev/VulkanAdventure/vulkanhpptutorial/VulkanTemplate/assets/CornellBox/CornellBox-Sphere.gltf");
        /*ModelHandle spheres = LoadMeshModel("E:/dev/VulkanAdventure/vulkanhpptutorial/VulkanTemplate/assets/pbr_spheres/MetalRoughSpheres.gltf");*/
        /*ModelHandle plantOnTable = LoadMeshModel("E:/dev/VulkanAdventure/vulkanhpptutorial/VulkanTemplate/assets/plantOnTable/Untitled.gltf");*/
        /*ModelHandle Cube = LoadMeshModel("E:/dev/VulkanAdventure/vulkanhpptutorial/VulkanTemplate/assets/Cube/Cube.gltf");*/
        /*ModelHandle FlightHelmet = LoadMeshModel("E:/dev/VulkanAdventure/vulkanhpptutorial/VulkanTemplate/assets/FlightHelmet/FlightHelmet.gltf");*/
        /*ModelHandle gun = LoadMeshModel("E:/dev/VulkanAdventure/vulkanhpptutorial/VulkanTemplate/assets/Cerberus_by_Andrew_Maximov/Untitled.gltf");*/
        ModelHandle TransmissionOrderTest = LoadMeshModel("E:/dev/VulkanAdventure/vulkanhpptutorial/VulkanTemplate/assets/TransmissionOrderTest/TransmissionOrderTest.gltf");
        /*ModelHandle TransmissionOrderTest = LoadMeshModel("E:/dev/VulkanAdventure/vulkanhpptutorial/VulkanTemplate/assets/IORTestGrid/IORTestGrid.gltf");*/
        runtimePlant = BuildRuntimeModel(TransmissionOrderTest);
        runtimePlant2 = BuildRuntimeModel(cornellBox);
        //SubmitModelDraw(plantOnTable, glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, 0.0f)) /** glm::rotate(glm::mat4(1.0f), glm::radians(90.0f), glm::vec3(0.0f, 1.0f, 0.0f))*/);
        /*materials.back().albedoTexture = rustedIron->GetTextureIndex();
        materials.back().metallicRoughnessTexture = rustedIronMetalRough->GetTextureIndex();
        materials.back().normalTexture = rustedIronNormal->GetTextureIndex();*/
        /*SubmitModelDraw(bhudda, glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, 0.0f)));*/
        /*SubmitModelDraw(gun, glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, 0.0f)));*/
        /*SubmitModelDraw(viking, glm::translate(glm::mat4(1.0f), glm::vec3(2.0f, 0.0f, 0.0f)));*/
        //SubmitModelDraw(monkey, glm::translate(glm::mat4(1.0f), glm::vec3(-2.0f, 0.0f, 0.0f)));
        uint64_t sceneBuffersize = sizeof(SceneDatas);
        sceneBuffer = m_BufferManager->Create<StorageBuffer>(sceneBuffersize);

        uint64_t cullBuffersize = sizeof(CulledData);
        cullBuffer = m_BufferManager->Create<StorageBuffer>(cullBuffersize);

        uint64_t m_TransferDownlaoadBuffersize = sizeof(CulledData) + sizeof(3840 * 2160 * sizeof(int32_t)); // this is for mouse picking currently only 1x1x1 but future proof for 4k res
        m_TransferDownlaoadBuffer = m_BufferManager->Create<TransferBuffer>(m_TransferDownlaoadBuffersize, VanKTransferBufferUsageDownload);

        uint64_t vertexBuffersize = sizeof(shaderio::Vertex) * geometry.vertices.size();
        /*vertexBuffer = CreateRefs<StorageBuffer>(vertexBuffersize);*/
        vertexBuffer = m_BufferManager->Create<StorageBuffer>(vertexBuffersize);

        uint64_t indexBuffersize = sizeof(uint32_t) * geometry.indices.size();
        indexBuffer = m_BufferManager->Create<StorageBuffer>(indexBuffersize);

        uint64_t meshletVerticesBuffersize = sizeof(uint32_t) * geometry.meshletVertices.size();
        meshletVerticesBuffer = m_BufferManager->Create<StorageBuffer>(meshletVerticesBuffersize);

        uint64_t meshletTrianglesBuffersize = sizeof(uint8_t) * geometry.meshletTriangles.size();
        meshletTrianglesBuffer = m_BufferManager->Create<StorageBuffer>(meshletTrianglesBuffersize);

        uint64_t meshletBuffersize = sizeof(Meshlet) * geometry.meshlets.size();
        meshletBuffer = m_BufferManager->Create<StorageBuffer>(meshletBuffersize);

        uint64_t localMeshTaskSubmitBuffersize = sizeof(VanKDrawMeshTasksIndirectCommand) * 10000;
        localMeshTaskSubmitBuffer = m_BufferManager->Create<StorageBuffer>(localMeshTaskSubmitBuffersize);

        uint64_t meshTaskSubmitBuffersize = sizeof(VanKDrawMeshTasksIndirectCommand) * 10000;
        meshTaskSubmitBuffer = m_BufferManager->Create<IndirectBuffer>(meshTaskSubmitBuffersize);

        uint64_t meshletPrimitiveBuffersize = sizeof(shaderio::MeshletPrimitive) * 10000;
        meshletPrimitiveBuffer = m_BufferManager->Create<StorageBuffer>(meshletPrimitiveBuffersize);

        uint64_t meshDrawBuffersize = sizeof(MeshDraw) * 10000;
        meshDrawBuffer = m_BufferManager->Create<StorageBuffer>(meshDrawBuffersize);

        uint64_t materialBuffersize = sizeof(shaderio::Material) * 10000;
        materialBuffer = m_BufferManager->Create<StorageBuffer>(materialBuffersize);

        uint64_t instanceLutsBuffersize = sizeof(shaderio::InstanceLUT) * 10000;
        instanceLutsBuffer = m_BufferManager->Create<StorageBuffer>(instanceLutsBuffersize);

        uint64_t quadBuffersize = sizeof(QuadData) * 10;
        quadBuffer = m_BufferManager->Create<StorageBuffer>(quadBuffersize);

        uint64_t circleBuffersize = sizeof(CircleData) * 10;
        circleBuffer = m_BufferManager->Create<StorageBuffer>(circleBuffersize);

        uint64_t textBuffersize = sizeof(CircleData) * 10;
        textBuffer = m_BufferManager->Create<StorageBuffer>(textBuffersize);

        uint64_t lineBuffersize = sizeof(LineData) * 10;
        lineBuffer = m_BufferManager->Create<StorageBuffer>(lineBuffersize);

        uint64_t lightsBuffersize = sizeof(Lights) * 10;
        lightsBuffer = m_BufferManager->Create<StorageBuffer>(lightsBuffersize);

        uint64_t transferSize = sceneBuffersize + vertexBuffersize + indexBuffersize + meshletVerticesBuffersize + meshletTrianglesBuffersize + meshletBuffersize +
            localMeshTaskSubmitBuffersize + meshletPrimitiveBuffersize + meshDrawBuffersize + materialBuffersize + instanceLutsBuffersize + quadBuffersize + circleBuffersize +
            textBuffersize + lineBuffersize + lightsBuffersize;
        m_TransferBuffer = m_BufferManager->Create<TransferBuffer>(transferSize, VanKTransferBufferUsageUpload);
    }

    // this is needed because of shaderlibrary holding raii modules and they die last because renderer has it
    //maybe move to vulkanrenderapi backend ?
    void Renderer::Shutdown()
    {
        RenderCommand::waitForGraphicsQueueIdle();

        RenderCommand::DestroyAllPipelines();

        GetShaderLibrary().ShutdownAll();

        m_BufferManager.reset();
    }

    void Renderer::CheckPendingVSyncChange()
    {
        if (!s_VSyncChangeRequested) return;

        RenderCommand::RebuildSwapchain(vSync);

        s_VSyncChangeRequested = false; // reset
    };

    void Renderer::BeginSubmit()
    {
        if (isEditor)
            RenderCommand::BeginFrame(VanK_Render_ImGui);
        else
            RenderCommand::BeginFrame(VanK_Render_Swapchain);

        cmd = RenderCommand::BeginCommandBuffer();
        if (!cmd)
            SDL_Log("AcquireGPUCommandBuffer failed: %s", SDL_GetError());

        quads.clear();
        circles.clear();
        texts.clear();
        lines.clear();
        meshDraws.clear();
        meshTasks.clear();
    }

    void Renderer::EndSubmit()
    {
        /*Flush();*/ // done inside editorlayer on render

        auto& downloadPass = renderGraph.AddPass("DownloadCullingData");
        downloadPass.reads =
        {
            {"CullBuffer", ResourceID::Buffer(m_BufferManager->Get<StorageBuffer>(cullBuffer).get()), ResourceUsage::TransferSrc},
            {"entityImage", ResourceID::Image(entityImage->GetRenderImageIndex()), ResourceUsage::TransferSrc}
        };
        downloadPass.writes =
        {
        };
        downloadPass.execute = []
        {
            if (isEditor)
            {
                std::vector<CulledData> data(1);
                m_BufferManager->Get<TransferBuffer>(m_TransferDownlaoadBuffer)->DownloadFromGPUBuffer(cmd, *m_BufferManager->Get<StorageBuffer>(cullBuffer), data, sizeof(CulledData), 0);
                ImGui::Begin("Mesh");
                ImGui::SeparatorText("Culling Breakdown");
                ImGui::Text("Frustum culled: %u", data[0].frustumCulled);
                ImGui::Text("Backface culled: %u", data[0].backfaceCulled);

                ImGui::SeparatorText("Effective Results");
                ImGui::Text("Total culled: %u", data[0].totalCulled);
                //todo its not the actual rendererd stats its just total so need to substract somehow
                ImGui::Text("Rendered: %zu / %zu", geometry.meshlets.size() - data[0].totalCulled, geometry.meshlets.size());
                ImGui::Text("Total meshlets: %zu", geometry.meshlets.size());
                ImGui::Text("Mode: %s", isRaster ? "Raster" : "Raytrace");
                ImGui::End();
            }
            
            // Entity ID readback Buffer from Entity Image and active pendingPick
            if (m_PendingPick.active)
            {
                m_BufferManager->Get<TransferBuffer>(m_TransferDownlaoadBuffer)->DownloadFromGPUImage(cmd, entityImage->GetRenderImageIndex(), m_PendingPick.x, m_PendingPick.y, 1, 1);
                
                m_PendingPick.active = false;
            }
        };

        //swapchain doesnt work since sceneimage or raytrace image cant blit either because only 1 image possible how do combine hmmmm
        auto& finalRender = renderGraph.AddPass("Swapchain");
        finalRender.reads =
        {
            {"finalImage", ResourceID::Image(renderGraph.GetFinalOutput().renderImageIndex), ResourceUsage::ShaderRead},
        };
        finalRender.writes =
        {
            {
                "SwapChainImage",
                ResourceID::Image(UINT32_MAX),
                ResourceUsage::ColorAttachment,
                ResourceUsage::PresentSrc, VanK_FORMAT_SWAPCHAIN,
                VanK_LOADOP_CLEAR,
                VanK_STOREOP_STORE,
                VanK_FColor{.f = {1.0f, 0.0f, 1.0f, 1.0f}}
            }
        };
        finalRender.execute = []
        {
            if (isEditor)
                RenderCommand::RenderImGui(cmd);
        };

        renderGraph.Build();
        // make a graph send to imgui image to render into viewprot instead of hardocing sceneimage much better i think
        // chatpgt meine fresse sagt mann kann blittingen die hurent sotrage image von raytgracing mutter 
        // mvoe storage image to rendertarget image in my renderer jsut neeed to add storage image flag in texture creation check that out
        /*renderGraph.DumpGraphviz("rendergraph.dot");*/
        renderGraph.Execute(cmd);

        // copy raytrace image into swapchain iamge idk if thats good since i have scene image to how do i combine them both together ?
        // submit rendering is not correct either should it be inside the graph ? idk

        RenderCommand::EndCommandBuffer(cmd);

        RenderCommand::EndFrame();

        CheckPendingVSyncChange();
    }

    void Renderer::Flush()
    {
        /*BeginSubmit();*/

        if (s_IsPipelineReloadFinished.exchange(false))
        {
            IsShaderReloadFinished = false;
            if (s_ShaderWatcher.empty())
                WatchShaderFiles();

            resetFrame();
            EndSubmit();
            ReloadPipelines();
            BeginSubmit();
            return;
        }

        DrawMeshShader();

        /*EndSubmit();*/
    }

    static bool done = false;

    void Renderer::DrawMeshShader()
    {
        ScopeTimer timer("Renderer::DrawMeshShader");
        m_BufferManager->Get<StorageBuffer>(cullBuffer)->Fill(cmd, 0, sizeof(CulledData), 0);

        scenesData.brdflutTexture = BRDF2DLUT->GetTextureIndex();
        scenesData.irradianceTexture = irradianceMap->GetTextureIndex();
        scenesData.prefilteredTexture = prefilterMap->GetTextureIndex();

        //path tracer
        scenesData.skyBoxIndex = cubemap->GetTextureIndex();
        scenesData.useSky = true;
        scenesData.backgroundColor = glm::vec3(1.0f, 0.0f, 1.0f);
        scenesData.frameIndex = s_frameIndex;

        scene.emplace_back(scenesData);
        m_BufferManager->Get<TransferBuffer>(m_TransferBuffer)->Upload(cmd, *m_BufferManager->Get<StorageBuffer>(sceneBuffer), scene, 0);
        scene.clear();

        if (!done)
        {
            //lights
            lights.emplace_back(
                Lights{
                    .position = glm::vec3(0.0f), // unused for directional
                    .intensity = 5.0f, // scaled from GLTF 6830
                    .direction = glm::normalize(glm::quat(0.7903f, -0.5507f, -0.2371f, -0.1258f) * glm::vec3(0.0f, 0.0f, -1.0f)),
                    .type = LightType::eDirectional,
                    .color = glm::vec3(1.0f, 0.95f, 0.85f), // slightly warm sunlight
                    .coneAngle = 0.0f // unused
                }
            );

            m_BufferManager->Get<TransferBuffer>(m_TransferBuffer)->Upload(cmd, *m_BufferManager->Get<StorageBuffer>(lightsBuffer), lights, 0);
            lights.clear();

            m_BufferManager->Get<TransferBuffer>(m_TransferBuffer)->Upload(cmd, *m_BufferManager->Get<StorageBuffer>(vertexBuffer), geometry.vertices, 0, false);

            m_BufferManager->Get<TransferBuffer>(m_TransferBuffer)->Upload(cmd, *m_BufferManager->Get<StorageBuffer>(indexBuffer), geometry.indices, 0, false);

            /*RenderCommand::createBottomLevelAS(*m_BufferManager->Get<StorageBuffer>(vertexBuffer), *m_BufferManager->Get<StorageBuffer>(indexBuffer), geometry.primitives, materials,
                                                        instanceLUTs);*/
            RenderCommand::createBottomLevelASModel(runtimePlant, *m_BufferManager->Get<StorageBuffer>(vertexBuffer), *m_BufferManager->Get<StorageBuffer>(indexBuffer), geometry.primitives, materials);
            RenderCommand::createBottomLevelASModel(runtimePlant2, *m_BufferManager->Get<StorageBuffer>(vertexBuffer), *m_BufferManager->Get<StorageBuffer>(indexBuffer), geometry.primitives, materials);
            glm::mat4 transform = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, 0.0f));
            RenderCommand::createInstanceASModel(runtimePlant, transform, geometry.primitives, instanceLUTs);
           
            glm::mat4 transform2 = glm::translate(glm::mat4(1.0f), glm::vec3(10.0f, 0.0f, 0.0f));
            RenderCommand::createInstanceASModel(runtimePlant2, transform2, geometry.primitives, instanceLUTs);
            RenderCommand::removeInstanceASModel(runtimePlant, 2);
            RenderCommand::createTopLevelAS();
            /*RenderCommand::clearAllTopLevelASInstances(instanceLUTs);*/
            m_BufferManager->Get<TransferBuffer>(m_TransferBuffer)->Upload(cmd, *m_BufferManager->Get<StorageBuffer>(instanceLutsBuffer), instanceLUTs, 0, false);

            m_BufferManager->Get<TransferBuffer>(m_TransferBuffer)->Upload(cmd, *m_BufferManager->Get<StorageBuffer>(meshletVerticesBuffer), geometry.meshletVertices, 0);

            m_BufferManager->Get<TransferBuffer>(m_TransferBuffer)->Upload(cmd, *m_BufferManager->Get<StorageBuffer>(meshletTrianglesBuffer), geometry.meshletTriangles, 0);

            m_BufferManager->Get<TransferBuffer>(m_TransferBuffer)->Upload(cmd, *m_BufferManager->Get<StorageBuffer>(meshletBuffer), geometry.meshlets, 0);

            m_BufferManager->Get<TransferBuffer>(m_TransferBuffer)->Upload(cmd, *m_BufferManager->Get<StorageBuffer>(meshletPrimitiveBuffer), geometry.primitives, 0);

            m_BufferManager->Get<TransferBuffer>(m_TransferBuffer)->Upload(cmd, *m_BufferManager->Get<StorageBuffer>(materialBuffer), materials, 0);

            //----------
            done = true;
        }

        static auto startTime = std::chrono::high_resolution_clock::now();

        auto currentTime = std::chrono::high_resolution_clock::now();
        float time = std::chrono::duration<float>(currentTime - startTime).count();

        glm::mat4 transform = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, 0.0f)) * rotate(glm::mat4(1.0f), time * 0.1f * glm::radians(90.0f), glm::vec3(0.0f, 1.0f, 0.0f));
        /*SubmitModel(runtimePlant, transform);*/
        // has to be done after createAccelerationStructures is called once maybe add a check or so
        /*RenderCommand::updateTopLevelASModel(runtimePlant, transform);*/
       

        m_BufferManager->Get<TransferBuffer>(m_TransferBuffer)->Upload(cmd, *m_BufferManager->Get<StorageBuffer>(meshDrawBuffer), meshDraws, 0);

        m_BufferManager->Get<TransferBuffer>(m_TransferBuffer)->Upload(cmd, *m_BufferManager->Get<StorageBuffer>(localMeshTaskSubmitBuffer), meshTasks, 0);

        //quads
        m_BufferManager->Get<TransferBuffer>(m_TransferBuffer)->Upload(cmd, *m_BufferManager->Get<StorageBuffer>(quadBuffer), quads, 0);
        //circles
        m_BufferManager->Get<TransferBuffer>(m_TransferBuffer)->Upload(cmd, *m_BufferManager->Get<StorageBuffer>(circleBuffer), circles, 0);
        //texts
        m_BufferManager->Get<TransferBuffer>(m_TransferBuffer)->Upload(cmd, *m_BufferManager->Get<StorageBuffer>(textBuffer), texts, 0);
        //lines
        m_BufferManager->Get<TransferBuffer>(m_TransferBuffer)->Upload(cmd, *m_BufferManager->Get<StorageBuffer>(lineBuffer), lines, 0);

        renderGraph.Reset();
        /*if (isRaster)*/
        {
            // between compute and raster is no barrier
            /*{
                auto& compute = renderGraph.AddPass("Compute Mesh Tasks");
                compute.reads = {{"localMeshTaskSubmitBuffer", ResourceID::Buffer(m_BufferManager->Get<StorageBuffer>(localMeshTaskSubmitBuffer).get()), ResourceUsage::ComputeRead}};
                compute.writes = {{"meshTaskSubmitBuffer", ResourceID::Buffer(m_BufferManager->Get<IndirectBuffer>(meshTaskSubmitBuffer).get()), ResourceUsage::ComputeWrite}};
                compute.execute = []
                {
                    GPUScopeTimer computetimer("Compute CommandTask: ", cmd, computeCommandTask);

                    /*
                    VanKComputePass* computePass = RenderCommand::BeginComputePass(cmd, {}, {});
                    #1#

                    RenderCommand::BindPipeline(cmd, VanKPipelineBindPoint::Compute, m_ComputeDrawMeshTaskCommandPipeline);

                    meshTasksSubmitPushConstant pushData
                    {
                        .meshTasksIndirectBufferAddress = m_BufferManager->Get<IndirectBuffer>(meshTaskSubmitBuffer)->GetBufferAddress(),
                        .localMeshTasksIndirectBufferAddress = m_BufferManager->Get<StorageBuffer>(localMeshTaskSubmitBuffer)->GetBufferAddress(),
                    };

                    RenderCommand::PushConstans(cmd, VanKCompute, 0, &pushData, sizeof(meshTasksSubmitPushConstant));

                    RenderCommand::DispatchCompute(cmd, (meshTasks.size() + 64 - 1) / 64, 1, 1); // matches [numthreads(64,1,1)] in shader

                    /*
                    RenderCommand::EndComputePass(computePass);#1#
                };
            }*/

            {
                auto& MeshDraw = renderGraph.AddPass("Mesh Draw");
                MeshDraw.reads =
                {
                    {"meshTaskSubmitBuffer", ResourceID::Buffer(m_BufferManager->Get<IndirectBuffer>(meshTaskSubmitBuffer).get()), ResourceUsage::IndirectRead}
                };
                MeshDraw.writes =
                {
                    // can use this system to create the texture here to automate fully dont forget tho somehow needs to be able to rebuild when viewport changes
                    // check beginrendering i think depth aline is not supported currectlny because of viewport fix it
                    {"sceneImage", ResourceID::Image(sceneImage->GetRenderImageIndex()), ResourceUsage::ResolveAttachment, ResourceUsage::ShaderRead},
                    {
                        "colorImage", ResourceID::Image(colorImage->GetRenderImageIndex()), ResourceUsage::ColorAttachment, {}, VanK_Format_B8G8R8A8Srgb, VanK_LOADOP_CLEAR, VanK_STOREOP_STORE,
                        VanK_FColor{.f = {0.0f, 0.0f, 0.0f, 0.0f}}
                    },
                    {"entityImage", ResourceID::Image(entityImage->GetRenderImageIndex()), ResourceUsage::ResolveAttachment},
                    {
                        "entityColorImage", ResourceID::Image(entityColorImage->GetRenderImageIndex()), ResourceUsage::ColorAttachment, {}, VanK_FORMAT_R32_SINT, VanK_LOADOP_CLEAR, VanK_STOREOP_STORE,
                        VanK_FColor{.i = {-1}}
                    },
                    {
                        "depthImage", ResourceID::Image(depthImage->GetRenderImageIndex()), ResourceUsage::DepthAttachment, {}, VanK_FORMAT_DEPTH_STENCIL, VanK_LOADOP_CLEAR, VanK_STOREOP_STORE,
                        VanK_FColor{.f = {0.0f}}
                    },
                    {"CullBuffer", ResourceID::Buffer(m_BufferManager->Get<StorageBuffer>(cullBuffer).get()), ResourceUsage::TransferDst}
                };

                /*MeshDraw.AddSubpass("PBR", []
                {
                    GPUScopeTimer computetimer("Mesh Render: ", cmd, renderPassMesh);


                    RenderCommand::BindPipeline(cmd, VanKPipelineBindPoint::Graphics, m_MeshPipeline);

                    RenderCommand::BindFragmentSamplers(cmd, NULL, nullptr, NULL, false);

                    RenderCommand::BindRayTracing(cmd, true);

                    TaskMeshPipelinePushConstant pushData
                    {
                        .sceneData = m_BufferManager->Get<StorageBuffer>(sceneBuffer)->GetBufferAddress(),
                        .culledDataBuffer = m_BufferManager->Get<StorageBuffer>(cullBuffer)->GetBufferAddress(),
                        .vertexBuffer = m_BufferManager->Get<StorageBuffer>(vertexBuffer)->GetBufferAddress(),
                        .indexBuffer = m_BufferManager->Get<StorageBuffer>(indexBuffer)->GetBufferAddress(),
                        .meshletVerticesBuffer = m_BufferManager->Get<StorageBuffer>(meshletVerticesBuffer)->GetBufferAddress(),
                        .meshletTrianglesBuffer = m_BufferManager->Get<StorageBuffer>(meshletTrianglesBuffer)->GetBufferAddress(),
                        .meshletBuffer = m_BufferManager->Get<StorageBuffer>(meshletBuffer)->GetBufferAddress(),
                        .meshletPrimitives = m_BufferManager->Get<StorageBuffer>(meshletPrimitiveBuffer)->GetBufferAddress(),
                        .meshDraws = m_BufferManager->Get<StorageBuffer>(meshDrawBuffer)->GetBufferAddress(),
                        .materialBuffer = m_BufferManager->Get<StorageBuffer>(materialBuffer)->GetBufferAddress(),
                        .instanceLutBuffer = m_BufferManager->Get<StorageBuffer>(instanceLutsBuffer)->GetBufferAddress(),
                        .lightsBuffer = m_BufferManager->Get<StorageBuffer>(lightsBuffer)->GetBufferAddress(),
                    };

                    RenderCommand::PushConstans(cmd, VanKMesh, 0, &pushData, sizeof(TaskMeshPipelinePushConstant));

                    //use count instead so gpu deciced how many draw calls once frustum cull for 1 object in compute
                    RenderCommand::DrawMeshTasksIndirect(cmd, *m_BufferManager->Get<IndirectBuffer>(meshTaskSubmitBuffer), 0, meshTasks.size(), sizeof(VanKDrawMeshTasksIndirectCommand));

                    /*meshTasks.clear();#1#
                });*/

                MeshDraw.AddSubpass("Sprites", []
                {
                    // quads/sprites/atlas
                    {
                        RenderCommand::BindPipeline(cmd, VanKPipelineBindPoint::Graphics, m_MeshQuadPipeline);

                        RenderCommand::BindFragmentSamplers(cmd, NULL, nullptr, NULL);

                        PushConstant2D push2D
                        {
                            .numOfElements = quads.size(),
                            .bufferAddress = m_BufferManager->Get<StorageBuffer>(quadBuffer)->GetBufferAddress(),
                            .sceneData = m_BufferManager->Get<StorageBuffer>(sceneBuffer)->GetBufferAddress(),
                        };

                        RenderCommand::PushConstans(cmd, VanKMesh, 0, &push2D, sizeof(PushConstant2D));

                        RenderCommand::DrawMeshTasks(cmd, quads.size(), 1, 1);
                    }
                });

                MeshDraw.AddSubpass("Circles", []
                {
                    //circles
                    {
                        RenderCommand::BindPipeline(cmd, VanKPipelineBindPoint::Graphics, m_MeshCirclePipeline);

                        RenderCommand::BindFragmentSamplers(cmd, NULL, nullptr, NULL);

                        PushConstant2D push2D
                        {
                            .numOfElements = circles.size(),
                            .bufferAddress = m_BufferManager->Get<StorageBuffer>(circleBuffer)->GetBufferAddress(),
                            .sceneData = m_BufferManager->Get<StorageBuffer>(sceneBuffer)->GetBufferAddress(),
                        };

                        RenderCommand::PushConstans(cmd, VanKMesh, 0, &push2D, sizeof(PushConstant2D));

                        RenderCommand::DrawMeshTasks(cmd, circles.size(), 1, 1);
                    }
                });

                MeshDraw.AddSubpass("Texts", []
                {
                    //texts
                    {
                        RenderCommand::BindPipeline(cmd, VanKPipelineBindPoint::Graphics, m_MeshTextPipeline);

                        RenderCommand::BindFragmentSamplers(cmd, NULL, nullptr, NULL);

                        PushConstant2D push2D
                        {
                            .numOfElements = texts.size(),
                            .bufferAddress = m_BufferManager->Get<StorageBuffer>(textBuffer)->GetBufferAddress(),
                            .sceneData = m_BufferManager->Get<StorageBuffer>(sceneBuffer)->GetBufferAddress(),
                        };

                        RenderCommand::PushConstans(cmd, VanKMesh, 0, &push2D, sizeof(PushConstant2D));

                        RenderCommand::DrawMeshTasks(cmd, texts.size(), 1, 1);
                    }
                });

                MeshDraw.AddSubpass("Lines", []
                {
                    //lines
                    {
                        RenderCommand::BindPipeline(cmd, VanKPipelineBindPoint::Graphics, m_MeshLinePipeline);

                        RenderCommand::BindFragmentSamplers(cmd, NULL, nullptr, NULL);

                        PushConstant2D push2D
                        {
                            .numOfElements = lines.size(),
                            .bufferAddress = m_BufferManager->Get<StorageBuffer>(lineBuffer)->GetBufferAddress(),
                            .sceneData = m_BufferManager->Get<StorageBuffer>(sceneBuffer)->GetBufferAddress(),
                        };

                        RenderCommand::PushConstans(cmd, VanKMesh, 0, &push2D, sizeof(PushConstant2D));

                        RenderCommand::DrawMeshTasks(cmd, lines.size(), 1, 1);
                    }
                });

                /*MeshDraw.AddSubpass("SkyBox", []
                {
                    // skybox always last
                    {
                        RenderCommand::BindPipeline(cmd, VanKPipelineBindPoint::Graphics, m_MeshSkyBoxPipeline);

                        RenderCommand::BindFragmentSamplers(cmd, NULL, nullptr, NULL);

                        PushConstantSkyBox pushSkyBox
                        {
                            .MaterialIndex = cubemap->GetTextureIndex(),
                            .sceneData = m_BufferManager->Get<StorageBuffer>(sceneBuffer)->GetBufferAddress(),
                        };

                        RenderCommand::PushConstans(cmd, VanKMesh, 0, &pushSkyBox, sizeof(PushConstantSkyBox));

                        RenderCommand::DrawMeshTasks(cmd, 1, 1, 1);
                    }
                });*/
            } /*renderGraph.SetFinalOutput(sceneImage->GetRenderImageIndex(), sceneImage->getImTextureID());*/
        }
        {
            // i had to change format of image to eR8G8B8A8Unorm othewrwise error will seew what happens
            auto& RayTrace = renderGraph.AddPass("RayTracing");
            RayTrace.reads =
            {

            };
            RayTrace.writes =
            {
                {"rayTraceImage", ResourceID::Image(rayTracingImage->GetRenderImageIndex()), ResourceUsage::StorageWrite, ResourceUsage::ShaderRead}
            };
            RayTrace.execute = []
            {
                /*if (s_frameIndex >= s_maxAccumulationFrames)
                    return;*/

                RenderCommand::BindPipeline(cmd, VanKPipelineBindPoint::Raytracing, m_RaytracingVoxelPipeline);

                RenderCommand::BindFragmentSamplers(cmd, NULL, nullptr, NULL, true);
                //instead of cuurently layouts i could use the layout direclty with m_PipelineResources inside vulkanrendererapi which jsut needs the pipeline to get the layout which is already exposed
                RenderCommand::BindRayTracing(cmd, false, rayTracingImage->GetRenderImageIndex());

                PushConstantRayTrace pushRayTrace
                {
                    .sceneData = m_BufferManager->Get<StorageBuffer>(sceneBuffer)->GetBufferAddress(),
                    .vertexBuffer = m_BufferManager->Get<StorageBuffer>(vertexBuffer)->GetBufferAddress(),
                    .indexBuffer = m_BufferManager->Get<StorageBuffer>(indexBuffer)->GetBufferAddress(),
                    .meshletVerticesBuffer = m_BufferManager->Get<StorageBuffer>(meshletVerticesBuffer)->GetBufferAddress(),
                    .meshletTrianglesBuffer = m_BufferManager->Get<StorageBuffer>(meshletTrianglesBuffer)->GetBufferAddress(),
                    .meshletBuffer = m_BufferManager->Get<StorageBuffer>(meshletBuffer)->GetBufferAddress(),
                    .meshletPrimitives = m_BufferManager->Get<StorageBuffer>(meshletPrimitiveBuffer)->GetBufferAddress(),
                    .meshDraws = m_BufferManager->Get<StorageBuffer>(meshDrawBuffer)->GetBufferAddress(),
                    .materialBuffer = m_BufferManager->Get<StorageBuffer>(materialBuffer)->GetBufferAddress(),
                    .instanceLutBuffer = m_BufferManager->Get<StorageBuffer>(instanceLutsBuffer)->GetBufferAddress(),
                    .lightsBuffer = m_BufferManager->Get<StorageBuffer>(lightsBuffer)->GetBufferAddress(),
                };

                RenderCommand::PushConstans(cmd, VanKRaytracing, 0, &pushRayTrace, sizeof(PushConstantRayTrace));

                RenderCommand::TraceRays(cmd, m_RaytracingVoxelPipeline, rayTracingImage->GetWidth(), rayTracingImage->GetHeight());
            };
        }
        //swapchain doesnt work since sceneimage or raytrace image cant blit either because only 1 image possible how do combine hmmmm
        auto& Composit = renderGraph.AddPass("Composit");
        Composit.reads =
        {
            {"sceneImage", ResourceID::Image(sceneImage->GetRenderImageIndex()), ResourceUsage::ShaderRead},
            {"rayTracingImage", ResourceID::Image(rayTracingImage->GetRenderImageIndex()), ResourceUsage::ShaderRead}
        };
        Composit.writes =
        {
            {
                "finalImage",
                ResourceID::Image(finalImage->GetRenderImageIndex()),
                ResourceUsage::ColorAttachment,
                ResourceUsage::ShaderRead,
                VanK_Format_B8G8R8A8Srgb,
                VanK_LOADOP_CLEAR,
                VanK_STOREOP_STORE,
                VanK_FColor{.f = {0.0f, 1.0f, 0.0f, 1.0f}}
            }
        };
        Composit.execute = []
        {
            RenderCommand::BindPipeline(cmd, VanKPipelineBindPoint::Graphics, m_FinalRenderPipeline);

            RenderCommand::BindRayTracing(cmd, true, {}, true, sceneImage->GetRenderImageIndex(), rayTracingImage->GetRenderImageIndex());

            RenderCommand::DrawMeshTasks(cmd, 1, 1, 1);
        };

        //change this give image make internal indexing
        renderGraph.SetFinalOutput(finalImage->GetRenderImageIndex(), finalImage->getImTextureID());
    }

    struct PipelineReloadEntry
    {
        VanKPipeLine* Pipeline;
        VanKGraphicsPipelineSpecification* graphicsSpec;
        VanKComputePipelineSpecification* computeSpec;
        VanKRaytracingPipelineSpecification* raytracingSpec;
        std::string ShaderKey;
        std::string FileName; // e.g., "GraphicsCubeShader.slang"
        VanKShaderStageFlags flag;
    };

    inline static std::vector<PipelineReloadEntry> s_PipelineReloadEntries;

    void Renderer::RegisterPipelineForShaderWatcher
    (
        const std::string& shaderKey,
        const std::string& fileName,
        VanKGraphicsPipelineSpecification* graphicsSpec,
        VanKComputePipelineSpecification* computeSpec,
        VanKRaytracingPipelineSpecification* raytracingSpec,
        VanKPipeLine* pipeline,
        VanKShaderStageFlags flag
    )
    {
        s_PipelineReloadEntries.push_back({pipeline, graphicsSpec, computeSpec, raytracingSpec, shaderKey, fileName, flag});
    }

    void Renderer::WatchShaderFiles()
    {
        for (const std::string& path : GetShaderLibrary().GetAllShaderPaths())
        {
            s_ShaderWatcher.emplace_back(std::make_unique<filewatch::FileWatch<std::string>>(path,
                                                                                             [](const std::string& file, const filewatch::Event change_type)
                                                                                             {
                                                                                                 if (!IsShaderReloadFinished && change_type == filewatch::Event::modified)
                                                                                                 {
                                                                                                     std::cout << "[FileWatcher] Shader file changed: " << file << '\n';

                                                                                                     IsShaderReloadFinished = true;

                                                                                                     changedFile = file;

                                                                                                     ReloadTimer = Timer();

                                                                                                     Application::Get().SubmitToMainThread([]()
                                                                                                     {
                                                                                                         s_ShaderWatcher.clear();
                                                                                                         s_IsPipelineReloadFinished = true;
                                                                                                     });
                                                                                                 }
                                                                                             }));
        }
    }

    void Renderer::ReloadPipelines()
    {
        VK_CORE_WARN("Reloading took {}ms", ReloadTimer.ElapsedMillis());

        for (auto& entry : s_PipelineReloadEntries)
        {
            if (entry.FileName != changedFile)
                continue;

            RenderCommand::waitForGraphicsQueueIdle();

            RenderCommand::DestroyPipeline(*entry.Pipeline);

            GetShaderLibrary().Remove(entry.ShaderKey);

            auto Shader = GetShaderLibrary().Load(entry.ShaderKey, changedFile);

            if (entry.flag == VanKGraphics)
            {
                entry.graphicsSpec->ShaderStageCreateInfo.VanKShader = Shader;
                *entry.Pipeline = RenderCommand::createGraphicsPipeline(*entry.graphicsSpec);
            }
            else if (entry.flag == VanKCompute)
            {
                entry.computeSpec->ComputePipelineCreateInfo.VanKShader = Shader;
                *entry.Pipeline = RenderCommand::createComputeShaderPipeline(*entry.computeSpec);
            }
            else if (entry.flag == VanKRaytracing)
            {
                entry.raytracingSpec->ShaderStageCreateInfo.VanKShader = Shader;
                *entry.Pipeline = RenderCommand::createRayTracingPipeline(*entry.raytracingSpec);
            }
        }
    }
}
