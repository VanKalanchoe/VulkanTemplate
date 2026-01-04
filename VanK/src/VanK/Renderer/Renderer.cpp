#include "Renderer.h"

#include <imgui.h>
#include <imgui_internal.h>
#include "backends/imgui_impl_sdl3.h"
#include "backends/imgui_impl_vulkan.h"

#include <SDL3/SDL_log.h>

#include "VanK/Core/Application.h"
#include "VanK/Core/Log.h"
#include "VanK/Core/Timer.h"
#define TINYGLTF_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <tiny_gltf.h>

#include <meshoptimizer.h>
#include <glm/gtc/type_ptr.inl>

#include "MSDFData.h"
#include "VanK/Asset/AssetManager.h"

namespace VanK
{
    static std::vector<std::unique_ptr<filewatch::FileWatch<std::string>>> s_ShaderWatcher;
    static std::atomic<bool> s_IsPipelineReloadFinished = false;
    bool IsShaderReloadFinished = false;
    std::string changedFile;
    Timer ReloadTimer;

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
        uint64_t meshletVerticesBuffer;
        uint64_t meshletTrianglesBuffer;
        uint64_t meshletBuffer;
        uint64_t meshletPrimitives;
        uint64_t meshDraws;
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
    };

    std::vector<SceneDatas> scene;
    SceneDatas scenesData{};
    SceneDatas frozenSceneData{};

    struct CulledData
    {
        uint32_t frustumCulled{0};
        uint32_t backfaceCulled{0};
        uint32_t totalCulled{0};
    };

    struct Vertex
    {
        glm::vec3 position{0.0f};
        glm::vec2 texcoords;
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

    struct MeshletPrimitive
    {
        uint32_t meshletOffset{0};
        uint32_t meshletCount{0};
        uint32_t materialIndex{0};
        uint32_t padding1{0};
        // {3} center, {1} radius
        glm::vec4 boundingSphere{};
    };

    struct ModelHandle
    {
        uint64_t firstPrimitive;
        uint64_t primitiveCount;
    };

    struct ExtractedMeshletModel
    {
        std::string name{};
        bool bSuccessfullyLoaded{false};

        /*std::vector<Vertex> vertices{};
        std::vector<uint32_t> meshletVertices{};
        std::vector<uint8_t> meshletTriangles{};
        std::vector<Meshlet> meshlets{};*/

        /*MeshletPrimitive primitive{};*/
        Transform transform{};
    };

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

    std::vector<VanKDrawMeshTasksIndirectCommand> meshTasks;

    struct MeshDraw
    {
        // because this can change every frame needs to be uploaded every frame
        glm::mat4 modelMatrix;
        uint64_t primitiveID;
    };

    std::vector<MeshDraw> meshDraws;

    struct Geometry
    {
        // only needs to be updated once a new model is added so no need for upload every frame
        std::vector<Vertex> vertices{};
        std::vector<uint32_t> meshletVertices{};
        std::vector<uint8_t> meshletTriangles{};
        std::vector<Meshlet> meshlets{};
        std::vector<MeshletPrimitive> primitives{};
    };

    Geometry geometry;

    glm::mat4 GetNodeTransform(const tinygltf::Node& node)
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
    std::vector<Vertex> primitiveVertices{};
    std::vector<uint32_t> primitiveIndices{};
    void TraverseNode(
        const tinygltf::Model& model,
        int nodeIndex,
        const glm::mat4& parentTransform,
        std::vector<Vertex>& verticesOut,
        std::vector<uint32_t>& indicesOut
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

                for (size_t i = 0; i < posAccessor.count; i++)
                {
                    const float* pos = reinterpret_cast<const float*>(bufferStart + i * stride);
                    glm::vec4 p(pos[0], pos[1], pos[2], 1.0f);

                    Vertex v{};
                    v.position = glm::vec3(worldTransform * p); // <--- APPLY WORLD TRANSFORM HERE

                    if (hasTexCoords)
                    {
                        const float* texCoord = reinterpret_cast<const float*>(&texCoordBuffer->data[texCoordBufferView->byteOffset + texCoordAccessor->byteOffset + i * 8]);
                        v.texcoords = {texCoord[0], texCoord[1]};
                    }
                    else
                    {
                        v.texcoords = {0.0f, 0.0f};
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
            sizeof(Vertex)
        );

        std::vector<Vertex> remappedVertices(vertexCount);
        meshopt_remapVertexBuffer(remappedVertices.data(), primitiveVertices.data(), primitiveVertices.size(), sizeof(Vertex), remap.data());
        std::vector<uint32_t> remappedIndices(primitiveIndices.size());
        meshopt_remapIndexBuffer(remappedIndices.data(), primitiveIndices.data(), primitiveIndices.size(), remap.data());

        meshopt_optimizeVertexCache(remappedIndices.data(), remappedIndices.data(), primitiveIndices.size(), vertexCount);
        meshopt_optimizeVertexFetch(remappedVertices.data(), remappedIndices.data(), primitiveIndices.size(), remappedVertices.data(), vertexCount, sizeof(Vertex));

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
                                              reinterpret_cast<const float*>(primitiveVertices.data()), primitiveVertices.size(), sizeof(Vertex),
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
            sizeof(Vertex), // vertex stride
            nullptr, // no per-vertex radii
            0
        );

        MeshletPrimitive prim{};
        prim.meshletOffset = static_cast<uint32_t>(geometry.meshlets.size());
        prim.meshletCount = static_cast<uint32_t>(meshlets.size());
        prim.boundingSphere = glm::vec4(
            primitiveBounds.center[0],
            primitiveBounds.center[1],
            primitiveBounds.center[2],
            primitiveBounds.radius
        );

        /*if (materialIndex != UINT32_MAX)
            prim.materialIndex = materialIndex;
        else*/
            prim.materialIndex = 0;

        // small hack so for 2d frustum still works otherwise i have to disable it idk sounds more expansive or ignore 
        // but idk if that could cause glitches in the future
        /*if (FrustumFor2D)
            prim.boundingSphere.w += 0.001f;*/

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
                sizeof(Vertex)
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
            TraverseNode(model, child, worldTransform, verticesOut, indicesOut);
        }
    }


    ModelHandle LoadMeshModel(std::string path, std::vector<Vertex> vertices = {}, std::vector<uint32_t> indices = {}, uint32_t materialIndex = UINT32_MAX, bool FrustumFor2D = false)
    {
        uint64_t firstPrimitive = geometry.primitives.size();
        
        if (!path.empty())
        {
            // Use tinygltf to load the model instead of tinyobjloader
            tinygltf::Model model;
            tinygltf::TinyGLTF loader;
            std::string err;
            std::string warn;

            bool ret = loader.LoadASCIIFromFile(&model, &err, &warn, path);

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
                    TraverseNode(model, nodeIndex, glm::mat4(1.0f), primitiveVertices, primitiveIndices);
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
        scenesData.view = glm::scale(glm::mat4(1.0f), glm::vec3(1.0f, 1.0f, -1.0f)) * glm::inverse(transform);;
        scenesData.proj = camera.GetProjection();
        scenesData.viewProj = camera.GetProjection() * scenesData.view;
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
        scenesData.cameraWorldPos = {camera.GetPosition(), 0.0f};
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

    std::vector<QuadData> quads;

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

    std::vector<CircleData> circles;

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

    std::vector<TextData> texts;

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

    std::vector<LineData> lines;

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

    static void SubmitModelDraw(
        const ModelHandle& model,
        const glm::mat4& transform)
    {
        for (uint64_t i = 0; i < model.primitiveCount; ++i)
        {
            uint64_t primitiveId = model.firstPrimitive + i;
            const auto& prim = geometry.primitives[primitiveId];

            meshDraws.emplace_back(MeshDraw{transform, primitiveId});
            meshTasks.emplace_back(VanKDrawMeshTasksIndirectCommand{(geometry.primitives[primitiveId].meshletCount + 64 - 1) / 64, 1, 1});
        }
    }

    void Renderer::Init(Window& window)
    {
        RendererAPI::Config config;
        config.window = window.getWindowHandle();
        m_window = window.getWindowHandle();
        RenderCommand::SetConfig(config);
        RenderCommand::Init();

        // Shader creation
        auto MeshTaskSubmit = GetShaderLibrary().Load("MeshTaskSubmit", "MeshTaskSubmit.slang");
        auto MeshShader = GetShaderLibrary().Load("MeshShader", "MeshShader.slang");
        auto MeshQuad = GetShaderLibrary().Load("MeshQuad", "MeshQuad.slang");
        auto MeshCircle = GetShaderLibrary().Load("MeshCircle", "MeshCircle.slang");
        auto MeshText = GetShaderLibrary().Load("MeshText", "MeshText.slang");
        auto MeshLine = GetShaderLibrary().Load("MeshLine", "MeshLine.slang");

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
            .depthTestEnable = true,
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

        m_MeshPipelineSpecification = GraphicsPipelineSpecification;
        m_MeshPipelineSpecification.PipelineType = VanK_Mesh;
        m_MeshPipelineSpecification.ShaderStageCreateInfo.VanKShader = MeshShader;
        m_MeshPipelineSpecification.PipelineLayoutInfo.PushConstants = {PushConstantRange{0, sizeof(TaskMeshPipelinePushConstant)}};
        m_MeshPipeline = RenderCommand::createGraphicsPipeline(m_MeshPipelineSpecification);
        RegisterPipelineForShaderWatcher("MeshShader", "MeshShader.slang", &m_MeshPipelineSpecification, nullptr, &m_MeshPipeline, VanKGraphics);

        m_MeshQuadPipelineSpecification = GraphicsPipelineSpecification;
        m_MeshQuadPipelineSpecification.PipelineType = VanK_Mesh;
        m_MeshQuadPipelineSpecification.ShaderStageCreateInfo.VanKShader = MeshQuad;
        m_MeshQuadPipelineSpecification.PipelineLayoutInfo.PushConstants = {PushConstantRange{0, sizeof(PushConstant2D)}};
        m_MeshQuadPipeline = RenderCommand::createGraphicsPipeline(m_MeshQuadPipelineSpecification);
        RegisterPipelineForShaderWatcher("MeshQuad", "MeshQuad.slang", &m_MeshQuadPipelineSpecification, nullptr, &m_MeshQuadPipeline, VanKGraphics);

        m_MeshCirclePipelineSpecification = GraphicsPipelineSpecification;
        m_MeshCirclePipelineSpecification.PipelineType = VanK_Mesh;
        m_MeshCirclePipelineSpecification.ShaderStageCreateInfo.VanKShader = MeshCircle;
        m_MeshCirclePipelineSpecification.PipelineLayoutInfo.PushConstants = {PushConstantRange{0, sizeof(PushConstant2D)}};
        m_MeshCirclePipeline = RenderCommand::createGraphicsPipeline(m_MeshCirclePipelineSpecification);
        RegisterPipelineForShaderWatcher("MeshCircle", "MeshCircle.slang", &m_MeshCirclePipelineSpecification, nullptr, &m_MeshCirclePipeline, VanKGraphics);

        m_MeshTextPipelineSpecification = GraphicsPipelineSpecification;
        m_MeshTextPipelineSpecification.PipelineType = VanK_Mesh;
        m_MeshTextPipelineSpecification.ShaderStageCreateInfo.VanKShader = MeshText;
        m_MeshTextPipelineSpecification.PipelineLayoutInfo.PushConstants = {PushConstantRange{0, sizeof(PushConstant2D)}};
        m_MeshTextPipeline = RenderCommand::createGraphicsPipeline(m_MeshTextPipelineSpecification);
        RegisterPipelineForShaderWatcher("MeshText", "MeshText.slang", &m_MeshTextPipelineSpecification, nullptr, &m_MeshTextPipeline, VanKGraphics);

        m_MeshLinePipelineSpecification = GraphicsPipelineSpecification;
        m_MeshLinePipelineSpecification.PipelineType = VanK_Mesh;
        m_MeshLinePipelineSpecification.ShaderStageCreateInfo.VanKShader = MeshLine;
        m_MeshLinePipelineSpecification.PipelineLayoutInfo.PushConstants = {PushConstantRange{0, sizeof(PushConstant2D)}};
        m_MeshLinePipeline = RenderCommand::createGraphicsPipeline(m_MeshLinePipelineSpecification);
        RegisterPipelineForShaderWatcher("MeshLine", "MeshLine.slang", &m_MeshLinePipelineSpecification, nullptr, &m_MeshLinePipeline, VanKGraphics);

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
        RegisterPipelineForShaderWatcher("MeshTaskSubmit", "MeshTaskSubmit.slang", nullptr, &m_ComputeDrawMeshTaskCommandPipelineSpecification, &m_ComputeDrawMeshTaskCommandPipeline, VanKCompute);

        WatchShaderFiles(); // has to be last after pipeline creation

        whiteTexture = TextureImporter::LoadTexture2D("");
        vikingRoom = TextureImporter::LoadTexture2D("../build/VanK/textures/viking_room.ktx2");
        ChernoLogo = TextureImporter::LoadTexture2D("../build/VanK/textures/ChernoLogo.ktx2");

        /*bool ret = loader.LoadBinaryFromFile(&model, &err, &warn, MODEL_PATH);*/
        //bool ret = loader.LoadASCIIFromFile(&model, &err, &warn, "E:/dev/VulkanAdventure/vulkanhpptutorial/VulkanTemplate/assets/stanford_bunny/stanford_bunny.gltf");
        //bool ret = loader.LoadASCIIFromFile(&model, &err, &warn, "E:/dev/VulkanAdventure/vulkanhpptutorial/VulkanTemplate/assets/Suzanne_monkey/Suzanne.gltf");
        //bool ret = loader.LoadASCIIFromFile(&model, &err, &warn, "E:/dev/VulkanAdventure/vulkanhpptutorial/VulkanTemplate/assets/Sponza/Sponza.gltf");
        /*bool ret = loader.LoadASCIIFromFile(&model, &err, &warn, "E:/dev/VulkanAdventure/vulkanhpptutorial/VulkanTemplate/assets/happy_bhudda/scene.gltf");*/
        /*bool ret = loader.LoadASCIIFromFile(&model, &err, &warn, "E:/dev/VulkanAdventure/vulkanhpptutorial/VulkanTemplate/assets/kitten/kitten.gltf");*/
        //bool ret = loader.LoadASCIIFromFile(&model, &err, &warn, "E:/dev/VulkanAdventure/vulkanhpptutorial/VulkanTemplate/assets/viking_room/viking_room.gltf");

        // this makes no sense anway for 2d i want to use different shader anyway so i can build the quad in mesh shader directly no ?
        std::vector<Vertex> quadVertices =
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

        ModelHandle bistro = LoadMeshModel("E:/dev/VulkanAdventure/vulkanhpptutorial/VulkanTemplate/assets/bistro/bistro.gltf");
        /*ModelHandle bunny = LoadMeshModel("E:/dev/VulkanAdventure/vulkanhpptutorial/VulkanTemplate/assets/stanford_bunny/stanford_bunny.gltf");*/
        /*LoadMeshModel("", quadVertices, quadIndices, whiteTexture->GetTextureIndex(), true);*/
        /*ModelHandle viking = LoadMeshModel("E:/dev/VulkanAdventure/vulkanhpptutorial/VulkanTemplate/assets/viking_room/viking_room.gltf");*/
        /*ModelHandle monkey = LoadMeshModel("E:/dev/VulkanAdventure/vulkanhpptutorial/VulkanTemplate/assets/Suzanne_monkey/Suzanne.gltf");*/

        SubmitModelDraw(bistro, glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, 0.0f)));
        /*SubmitModelDraw(viking, glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, 0.0f)));*/
        
        uint64_t sceneBuffersize = sizeof(SceneDatas);
        sceneBuffer.reset(StorageBuffer::Create(sceneBuffersize));

        uint64_t cullBuffersize = sizeof(CulledData);
        cullBuffer.reset(StorageBuffer::Create(cullBuffersize));

        uint64_t m_TransferDownlaoadBuffersize = sizeof(CulledData);
        m_TransferDownlaoadBuffer.reset(TransferBuffer::Create(m_TransferDownlaoadBuffersize, VanKTransferBufferUsageDownload));

        uint64_t vertexBuffersize = sizeof(Vertex) * geometry.vertices.size();
        vertexBuffer.reset(StorageBuffer::Create(vertexBuffersize));

        uint64_t meshletVerticesBuffersize = sizeof(uint32_t) * geometry.meshletVertices.size();
        meshletVerticesBuffer.reset(StorageBuffer::Create(meshletVerticesBuffersize));

        uint64_t meshletTrianglesBuffersize = sizeof(uint8_t) * geometry.meshletTriangles.size();
        meshletTrianglesBuffer.reset(StorageBuffer::Create(meshletTrianglesBuffersize));

        uint64_t meshletBuffersize = sizeof(Meshlet) * geometry.meshlets.size();
        meshletBuffer.reset(StorageBuffer::Create(meshletBuffersize));

        uint64_t localMeshTaskSubmitBuffersize = sizeof(VanKDrawMeshTasksIndirectCommand) * 10000;
        localMeshTaskSubmitBuffer.reset(StorageBuffer::Create(localMeshTaskSubmitBuffersize));

        uint64_t meshTaskSubmitBuffersize = sizeof(VanKDrawMeshTasksIndirectCommand) * 10000;
        meshTaskSubmitBuffer.reset(IndirectBuffer::Create(meshTaskSubmitBuffersize));

        uint64_t meshletPrimitiveBuffersize = sizeof(MeshletPrimitive) * 10000;
        meshletPrimitiveBuffer.reset(StorageBuffer::Create(meshletPrimitiveBuffersize));

        uint64_t meshDrawBuffersize = sizeof(MeshDraw) * 10000;
        meshDrawBuffer.reset(StorageBuffer::Create(meshDrawBuffersize));

        uint64_t quadBuffersize = sizeof(QuadData) * 10;
        quadBuffer.reset(StorageBuffer::Create(quadBuffersize));

        uint64_t circleBuffersize = sizeof(CircleData) * 10;
        circleBuffer.reset(StorageBuffer::Create(circleBuffersize));

        uint64_t textBuffersize = sizeof(CircleData) * 10;
        textBuffer.reset(StorageBuffer::Create(textBuffersize));

        uint64_t lineBuffersize = sizeof(LineData) * 10;
        lineBuffer.reset(StorageBuffer::Create(lineBuffersize));

        uint64_t transferSize = sceneBuffersize + vertexBuffersize + meshletVerticesBuffersize + meshletTrianglesBuffersize + meshletBuffersize +
            localMeshTaskSubmitBuffersize + meshletPrimitiveBuffersize + meshDrawBuffersize + quadBuffersize + circleBuffersize + textBuffersize +
            lineBuffersize;
        m_TransferBuffer.reset(TransferBuffer::Create(transferSize, VanKTransferBufferUsageUpload));
    }

    // this is needed because of shaderlibrary holding raii modules and they die last because renderer has it
    //maybe move to vulkanrenderapi backend ?
    void Renderer::Shutdown()
    {
        RenderCommand::waitForGraphicsQueueIdle();

        RenderCommand::DestroyAllPipelines();

        GetShaderLibrary().ShutdownAll();

        m_TransferBuffer.reset();

        sceneBuffer.reset();

        cullBuffer.reset();

        m_TransferDownlaoadBuffer.reset();

        vertexBuffer.reset();

        meshletVerticesBuffer.reset();

        meshletTrianglesBuffer.reset();

        meshletBuffer.reset();

        localMeshTaskSubmitBuffer.reset();

        meshTaskSubmitBuffer.reset();

        meshDrawBuffer.reset();

        meshletPrimitiveBuffer.reset();

        quadBuffer.reset();

        circleBuffer.reset();

        textBuffer.reset();

        lineBuffer.reset();
    }

    void Renderer::CheckPendingVSyncChange()
    {
        if (!s_VSyncChangeRequested) return;

        RenderCommand::RebuildSwapchain(vSync);

        s_VSyncChangeRequested = false; // reset
    };
    static bool s_FlushedThisFrame = false;

    void Renderer::BeginSubmit()
    {
        s_FlushedThisFrame = false;

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
    }

    void Renderer::EndSubmit()
    {
        Flush();

        uint64_t offset = 0;
        void* mapPtr = m_TransferDownlaoadBuffer->MapTransferBuffer(sizeof(CulledData), 0, offset);
        m_TransferDownlaoadBuffer->DownloadFromGPUBuffer(cmd, {0}, {cullBuffer.get(), 0, sizeof(CulledData)});
        CulledData data;
        std::memcpy(&data, mapPtr, sizeof(CulledData));
        m_TransferDownlaoadBuffer->UnMapTransferBuffer();
        ImGui::Begin("Mesh");
        ImGui::SeparatorText("Culling Breakdown");
        ImGui::Text("Frustum culled: %u", data.frustumCulled);
        ImGui::Text("Backface culled: %u", data.backfaceCulled);

        ImGui::SeparatorText("Effective Results");
        ImGui::Text("Total culled: %u", data.totalCulled);
        //todo its not the actual rendererd stats
        ImGui::Text("Rendered: %zu / %zu", geometry.meshlets.size() - data.totalCulled, geometry.meshlets.size());
        ImGui::Text("Total meshlets: %zu", geometry.meshlets.size());
        ImGui::End();

        RenderCommand::SubmitRendering(cmd);

        RenderCommand::EndCommandBuffer(cmd);

        RenderCommand::EndFrame();

        CheckPendingVSyncChange();
    }

    void Renderer::Flush()
    {
        VK_CORE_ASSERT(!s_FlushedThisFrame, "Flush called more than once per frame");
        s_FlushedThisFrame = true;

        /*BeginSubmit();*/

        if (s_IsPipelineReloadFinished.exchange(false))
        {
            IsShaderReloadFinished = false;
            if (s_ShaderWatcher.empty())
                WatchShaderFiles();

            EndSubmit();
            ReloadPipelines();
            BeginSubmit();
            return;
        }

        DrawMeshShader();

        /*EndSubmit();*/
    }
    
    bool done = false;
    void Renderer::DrawMeshShader()
    {
        ScopeTimer timer("Renderer::DrawMeshShader");

        cullBuffer->Fill(cmd, 0, sizeof(CulledData), 0);

        scene.emplace_back(scenesData);
        UploadBufferToGpuWithTransferRing(cmd, m_TransferBuffer, sceneBuffer, scene, SceneDatas, 0);
        scene.clear();

        if (!done)
        {
            UploadBufferToGpuWithTransferRing(cmd, m_TransferBuffer, vertexBuffer, geometry.vertices, Vertex, 0);
            UploadBufferToGpuWithTransferRing(cmd, m_TransferBuffer, meshletVerticesBuffer, geometry.meshletVertices, uint32_t, 0);
            UploadBufferToGpuWithTransferRing(cmd, m_TransferBuffer, meshletTrianglesBuffer, geometry.meshletTriangles, uint8_t, 0);
            UploadBufferToGpuWithTransferRing(cmd, m_TransferBuffer, meshletBuffer, geometry.meshlets, Meshlet, 0);
            UploadBufferToGpuWithTransferRing(cmd, m_TransferBuffer, meshletPrimitiveBuffer, geometry.primitives, MeshletPrimitive, 0);

            //multiple meshes---
            UploadBufferToGpuWithTransferRing(cmd, m_TransferBuffer, meshDrawBuffer, meshDraws, MeshDraw, 0);
            /*meshDraws.clear();*/
            UploadBufferToGpuWithTransferRing(cmd, m_TransferBuffer, localMeshTaskSubmitBuffer, meshTasks, VanKDrawMeshTasksIndirectCommand, 0);
            //----------
            done = true;
        }
        //quads
        UploadBufferToGpuWithTransferRing(cmd, m_TransferBuffer, quadBuffer, quads, QuadData, 0);

        //circles
        UploadBufferToGpuWithTransferRing(cmd, m_TransferBuffer, circleBuffer, circles, CircleData, 0);

        //texts
        UploadBufferToGpuWithTransferRing(cmd, m_TransferBuffer, textBuffer, texts, TextData, 0);

        //lines
        UploadBufferToGpuWithTransferRing(cmd, m_TransferBuffer, lineBuffer, lines, LineData, 0);

        {
            GPUScopeTimer computetimer("Compute CommandTask: ", cmd, computeCommandTask);
            /*RenderCommand::StartTimeStamp(cmd, lol);*/
            
            VanKComputePass* computePass = RenderCommand::BeginComputePass(cmd, {}, std::span(&meshTaskSubmitBuffer, 1));

            RenderCommand::BindPipeline(cmd, VanKPipelineBindPoint::Compute, m_ComputeDrawMeshTaskCommandPipeline);

            meshTasksSubmitPushConstant pushData
            {
                .meshTasksIndirectBufferAddress = meshTaskSubmitBuffer->GetBufferAddress(),
                .localMeshTasksIndirectBufferAddress = localMeshTaskSubmitBuffer->GetBufferAddress(),
            };

            RenderCommand::PushConstans(cmd, VanKCompute, 0, &pushData, sizeof(meshTasksSubmitPushConstant));

            RenderCommand::DispatchCompute(computePass, (meshTasks.size() + 64 - 1) / 64, 1, 1); // matches [numthreads(64,1,1)] in shader

            RenderCommand::EndComputePass(computePass);
            
            /*RenderCommand::StopTimeStamp(cmd, lol);*/
        }

        {
            GPUScopeTimer computetimer("Mesh Render: ", cmd, renderPassMesh);
            std::vector<VanKColorTargetInfo> colorAttachments;
            colorAttachments.emplace_back(VanK_Format_B8G8R8A8Srgb, VanK_LOADOP_CLEAR, VanK_STOREOP_STORE, VanK_FColor{.f = {0.1f, 0.1f, 0.1f, 1.0f}});
            colorAttachments.emplace_back(VanK_FORMAT_R32_SINT, VanK_LOADOP_CLEAR, VanK_STOREOP_STORE, VanK_FColor{.i = {-1}});

            VanKDepthStencilTargetInfo depthStencilTargetInfo = {.loadOp = VanK_LOADOP_CLEAR, .storeOp = VanK_STOREOP_STORE, .clearColor = VanK_FColor{.f = {0.0f, 0}}};

            RenderCommand::BeginRendering(cmd, colorAttachments.data(), colorAttachments.size(), depthStencilTargetInfo);

            VanKViewport viewPort = {0, static_cast<float>(m_ViewportSize.height), static_cast<float>(m_ViewportSize.width), -static_cast<float>(m_ViewportSize.height), 0, 1};
            RenderCommand::SetViewport(cmd, 1, viewPort);

            VankRect rect = {0, 0, m_ViewportSize.width, m_ViewportSize.height};
            RenderCommand::SetScissor(cmd, 1, rect);

            RenderCommand::SetLineWidth(cmd, m_LineWidth);

            RenderCommand::SetCullMode(cmd, cullMode);

            RenderCommand::BindPipeline(cmd, VanKPipelineBindPoint::Graphics, m_MeshPipeline);

            RenderCommand::BindFragmentSamplers(cmd, NULL, nullptr, NULL);

            TaskMeshPipelinePushConstant pushData
            {
                .sceneData = sceneBuffer->GetBufferAddress(),
                .culledDataBuffer = cullBuffer->GetBufferAddress(),
                .vertexBuffer = vertexBuffer->GetBufferAddress(),
                .meshletVerticesBuffer = meshletVerticesBuffer->GetBufferAddress(),
                .meshletTrianglesBuffer = meshletTrianglesBuffer->GetBufferAddress(),
                .meshletBuffer = meshletBuffer->GetBufferAddress(),
                .meshletPrimitives = meshletPrimitiveBuffer->GetBufferAddress(),
                .meshDraws = meshDrawBuffer->GetBufferAddress()
            };

            RenderCommand::PushConstans(cmd, VanKMesh, 0, &pushData, sizeof(TaskMeshPipelinePushConstant));

            //use count instead so gpu deciced how many draw calls once frustum cull for 1 object in compute
            RenderCommand::DrawMeshTasksIndirect(cmd, *meshTaskSubmitBuffer, 0, meshTasks.size(), sizeof(VanKDrawMeshTasksIndirectCommand));

            /*meshTasks.clear();*/

            // quads/sprites/atlas
            {
                RenderCommand::BindPipeline(cmd, VanKPipelineBindPoint::Graphics, m_MeshQuadPipeline);

                RenderCommand::BindFragmentSamplers(cmd, NULL, nullptr, NULL);

                PushConstant2D push2D
                {
                    .numOfElements = quads.size(),
                    .bufferAddress = quadBuffer->GetBufferAddress(),
                    .sceneData = sceneBuffer->GetBufferAddress(),
                };

                RenderCommand::PushConstans(cmd, VanKMesh, 0, &push2D, sizeof(PushConstant2D));

                RenderCommand::DrawMeshTasks(cmd, quads.size(), 1, 1);
            }

            //circles
            {
                RenderCommand::BindPipeline(cmd, VanKPipelineBindPoint::Graphics, m_MeshCirclePipeline);

                RenderCommand::BindFragmentSamplers(cmd, NULL, nullptr, NULL);

                PushConstant2D push2D
                {
                    .numOfElements = circles.size(),
                    .bufferAddress = circleBuffer->GetBufferAddress(),
                    .sceneData = sceneBuffer->GetBufferAddress(),
                };

                RenderCommand::PushConstans(cmd, VanKMesh, 0, &push2D, sizeof(PushConstant2D));

                RenderCommand::DrawMeshTasks(cmd, circles.size(), 1, 1);
            }

            //texts
            {
                RenderCommand::BindPipeline(cmd, VanKPipelineBindPoint::Graphics, m_MeshTextPipeline);

                RenderCommand::BindFragmentSamplers(cmd, NULL, nullptr, NULL);

                PushConstant2D push2D
                {
                    .numOfElements = texts.size(),
                    .bufferAddress = textBuffer->GetBufferAddress(),
                    .sceneData = sceneBuffer->GetBufferAddress(),
                };

                RenderCommand::PushConstans(cmd, VanKMesh, 0, &push2D, sizeof(PushConstant2D));

                RenderCommand::DrawMeshTasks(cmd, texts.size(), 1, 1);
            }

            //lines
            {
                RenderCommand::BindPipeline(cmd, VanKPipelineBindPoint::Graphics, m_MeshLinePipeline);

                RenderCommand::BindFragmentSamplers(cmd, NULL, nullptr, NULL);

                PushConstant2D push2D
                {
                    .numOfElements = lines.size(),
                    .bufferAddress = lineBuffer->GetBufferAddress(),
                    .sceneData = sceneBuffer->GetBufferAddress(),
                };

                RenderCommand::PushConstans(cmd, VanKMesh, 0, &push2D, sizeof(PushConstant2D));

                RenderCommand::DrawMeshTasks(cmd, lines.size(), 1, 1);
            }

            RenderCommand::EndRendering(cmd);
        }
    }

    struct PipelineReloadEntry
    {
        VanKPipeLine* Pipeline;
        VanKGraphicsPipelineSpecification* graphicsSpec;
        VanKComputePipelineSpecification* computeSpec;
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
        VanKPipeLine* pipeline,
        VanKShaderStageFlags flag
    )
    {
        s_PipelineReloadEntries.push_back({pipeline, graphicsSpec, computeSpec, shaderKey, fileName, flag});
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
            else
            {
                entry.computeSpec->ComputePipelineCreateInfo.VanKShader = Shader;
                *entry.Pipeline = RenderCommand::createComputeShaderPipeline(*entry.computeSpec);
            }
        }
    }
}
