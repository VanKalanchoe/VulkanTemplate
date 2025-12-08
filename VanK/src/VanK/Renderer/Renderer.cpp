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
        std::vector<shaderio::InstancedStorageData> storageInstancesPtr;
        shaderio::SceneInfo SceneData;
    };
    static Renderer3DData s_Data;
    
    const std::string MODEL_PATH = "../build/VanK/models/viking_room.glb";
    
    void Renderer::loadModel()
    {
        // Use tinygltf to load the model instead of tinyobjloader
        tinygltf::Model model;
        tinygltf::TinyGLTF loader;
        std::string err;
        std::string warn;

        bool ret = loader.LoadBinaryFromFile(&model, &err, &warn, MODEL_PATH);

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

        vertices.clear();
        indices.clear();

        // Process all meshes in the model
        for (const auto& mesh : model.meshes)
        {
            for (const auto& primitive : mesh.primitives)
            {
                // Get indices
                const tinygltf::Accessor& indexAccessor = model.accessors[primitive.indices];
                const tinygltf::BufferView& indexBufferView = model.bufferViews[indexAccessor.bufferView];
                const tinygltf::Buffer& indexBuffer = model.buffers[indexBufferView.buffer];

                // Get vertex positions
                const tinygltf::Accessor& posAccessor = model.accessors[primitive.attributes.at("POSITION")];
                const tinygltf::BufferView& posBufferView = model.bufferViews[posAccessor.bufferView];
                const tinygltf::Buffer& posBuffer = model.buffers[posBufferView.buffer];

                // Get texture coordinates if available
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

                uint32_t baseVertex = static_cast<uint32_t>(vertices.size());

                for (size_t i = 0; i < posAccessor.count; i++)
                {
                    shaderio::InstancedVertexData vertex{};

                    const float* pos = reinterpret_cast<const float*>(&posBuffer.data[posBufferView.byteOffset + posAccessor
                        .byteOffset + i * 12]);
                    vertex.position = {pos[0], pos[1], pos[2]};

                    if (hasTexCoords)
                    {
                        const float* texCoord = reinterpret_cast<const float*>(&texCoordBuffer->data[texCoordBufferView->
                            byteOffset + texCoordAccessor->byteOffset + i * 8]);
                        vertex.texcoords = {texCoord[0], texCoord[1]};
                    }
                    else
                    {
                        vertex.texcoords = {0.0f, 0.0f};
                    }

                    /*vertex.color = {1.0f, 1.0f, 1.0f, 1.0f};*/

                    vertices.push_back(vertex);
                }

                const unsigned char* indexData = &indexBuffer.data[indexBufferView.byteOffset + indexAccessor.byteOffset];
                size_t indexCount = indexAccessor.count;
                size_t indexStride = 0;

                // Determine index stride based on component type
                if (indexAccessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT)
                {
                    indexStride = sizeof(uint16_t);
                }
                else if (indexAccessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT)
                {
                    indexStride = sizeof(uint32_t);
                }
                else if (indexAccessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE)
                {
                    indexStride = sizeof(uint8_t);
                }
                else
                {
                    throw std::runtime_error("Unsupported index component type");
                }

                indices.reserve(indices.size() + indexCount);

                for (size_t i = 0; i < indexCount; i++)
                {
                    uint32_t index = 0;

                    if (indexAccessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT)
                    {
                        index = *reinterpret_cast<const uint16_t*>(indexData + i * indexStride);
                    }
                    else if (indexAccessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT)
                    {
                        index = *reinterpret_cast<const uint32_t*>(indexData + i * indexStride);
                    }
                    else if (indexAccessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE)
                    {
                        index = *reinterpret_cast<const uint8_t*>(indexData + i * indexStride);
                    }

                    indices.push_back(baseVertex + index);
                }
            }
        }
    }
    
    void Renderer::BeginScene(const Camera& camera, const glm::mat4& transform)
    {
        /*glm::mat4 viewProj = camera.GetProjection() * glm::inverse(transform);*/
        glm::mat4 View = glm::inverse(transform);
        glm::mat4 Proj = camera.GetProjection();
        
        s_Data.SceneData.view = View;
        s_Data.SceneData.proj = Proj;
    }
    
    void Renderer::BeginScene(const EditorCamera& camera)
    {
        glm::mat4 View = camera.GetViewMatrix();
        glm::mat4 Proj = camera.GetProjection();
        
        s_Data.SceneData.view = View;
        s_Data.SceneData.proj = Proj;
    }

    void Renderer::EndScene()
    {
        Geometry::SetFrameInstances("cube", s_Data.storageInstancesPtr);
        Flush();
        s_Data.storageInstancesPtr.clear();
    }

    void Renderer::DrawQuad(const glm::mat4& transform, const glm::vec4& color, int entityID)
    {
        shaderio::InstancedStorageData storage;
        storage.Model = transform;
        storage.albedoMap = whiteTexture->GetTextureIndex();
        storage.albedo = color;
        storage.EntityID = entityID;
        
        s_Data.storageInstancesPtr.emplace_back(storage);
    }

    void Renderer::DrawQuad(const glm::mat4& transform, const Ref<Texture2D>& texture, float tilingFactor, const glm::vec4& tintColor, int entityID)
    {
        shaderio::InstancedStorageData storage;
        storage.Model = transform;
        storage.albedoMap = texture->GetTextureIndex();
        storage.albedo = tintColor;
        storage.EntityID = entityID;
        
        s_Data.storageInstancesPtr.emplace_back(storage);
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
    
    void Renderer::Init(Window& window)
    {
        RendererAPI::Config config;
        config.window = window.getWindowHandle();
        m_window = window.getWindowHandle();
        RenderCommand::SetConfig(config);
        RenderCommand::Init();

        // Shader creation
        auto DebugShader = GetShaderLibrary().Load("DebugShader", "shader.slang");
        auto DrawIndirectShader = GetShaderLibrary().Load("DrawIndirectShader", "DrawIndirectShader.slang");

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
            .VanKShader = DebugShader,
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
            .VanKCullMode = VanK_CULL_MODE_BACK_BIT, // todo change this for performance reason i think back or front test
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
            .VanKdepthCompareOp = VanK_COMPARE_OP_LESS
        };

        VanKPipelineRenderingCreateInfo RenderingCreateInfo
        {
            .VanKColorAttachmentFormats = {VanK_Format_B8G8R8A8Srgb, VanK_FORMAT_R32_SINT}
        };

        VanKGraphicsPipelineSpecification GraphicsPipelineSpecification
        {
            .ShaderStageCreateInfo = ShaderStageCreateInfo,
            .VertexInputStateCreateInfo = VertexInputStateCreateInfo,
            .InputAssemblyStateCreateInfo = InputAssemblyStateCreateInfo,
            .RasterizationStateCreateInfo = RasterizationStateCreateInfo,
            .ColorBlendStateCreateInfo = ColorBlendStateCreateInfo,
            .MultisampleStateCreateInfo = MultisampleStateCreateInfo,
            .DepthStateInfo = DepthStencilStateCreateInfo,
            .RenderingCreateInfo = RenderingCreateInfo,
        };

        m_GraphicsDebugPipelineSpecification = GraphicsPipelineSpecification;

        m_GraphicsDebugPipeline = RenderCommand::createGraphicsPipeline(m_GraphicsDebugPipelineSpecification);
        RegisterPipelineForShaderWatcher("DebugShader", "shader.slang", &m_GraphicsDebugPipelineSpecification, nullptr, &m_GraphicsDebugPipeline, VanKGraphics);
        
        // Compute Pipelines creations
        VanKComputePipelineCreateInfo ComputePipelineCreateInfo
        {
            .VanKShader = DrawIndirectShader
        };
        
        VanKComputePipelineSpecification computePipelineSpecification
        {
            .ComputePipelineCreateInfo = ComputePipelineCreateInfo
        };
        
        m_ComputeDrawIndirectPipelineSpecification = computePipelineSpecification;
        
        m_ComputeDrawIndirectPipeline = RenderCommand::createComputeShaderPipeline(m_ComputeDrawIndirectPipelineSpecification);
        RegisterPipelineForShaderWatcher("DrawIndirectShader", "DrawIndirectShader.slang", nullptr, &m_ComputeDrawIndirectPipelineSpecification, &m_ComputeDrawIndirectPipeline, VanKCompute);

        WatchShaderFiles(); // has to be after rednerer2d init othwerise it cant watch it beacuse not created shaders

        uniformScene.reset(UniformBuffer::Create(sizeof(s_Data.SceneData)));

        whiteTexture = TextureImporter::LoadTexture2D("");
        vikingRoom = TextureImporter::LoadTexture2D("../build/VanK/textures/viking_room.ktx2");
        vikingRoom2 = TextureImporter::LoadTexture2D("../build/VanK/textures/viking_room2.ktx2");
        ChernoLogo = TextureImporter::LoadTexture2D("../build/VanK/textures/ChernoLogo.ktx2");
        
        loadModel();
        
        Geometry::AppendGeometry("model", vertices, indices);
        Geometry::AppendGeometry("cube", GeometryData::quadVertices, GeometryData::quadIndices);
        
        /*// Grid parameters (matching your DrawFrame values)
        const int gridWidth = 3;
        const float spacing = 2.0f;
        const float cubeOffsetX = 1.0f; // Offset for the cube group

        // Assuming mesh 0 is "model" and mesh 1 is "cube" based on the creation order:
        // Geometry::AppendGeometry("model", ...);
        // Geometry::AppendGeometry("cube", ...);
        
        // Check total number of meshes
        if (Geometry::GetMeshes().size() < 2) return; 

        // --- 1. Generate Transforms for "model" (Mesh 0) ---
        // The "model" will be placed in a 3x3 grid (9 instances total)
        std::vector<shaderio::InstancedStorageData> modelStorageData;
        uint32_t modelInstanceCount = 9; // Let's create a 3x3 grid (9 instances)
        
        for (uint32_t j = 0; j < modelInstanceCount; j++)
        {
            int row = j / gridWidth;
            int col = j % gridWidth;

            float x = col * spacing;
            float y = 0.0f;
            float z = row * spacing;

            shaderio::InstancedStorageData data{};
            // The first mesh has no offset
            data.Model = glm::translate(glm::mat4(1.0f), glm::vec3(x, y, z));
            data.albedoMap = vikingRoom->GetTextureIndex(); // Use the model texture

            modelStorageData.push_back(data);
        }
        Geometry::AppendGeometryData("model", modelStorageData);

        // --- 2. Generate Transforms for "cube" (Mesh 1) ---
        // The "cube" will also be placed in a 3x3 grid (9 instances total)
        std::vector<shaderio::InstancedStorageData> cubeStorageData;
        uint32_t cubeInstanceCount = 9; // Let's use the same instance count
        
        for (uint32_t j = 0; j < cubeInstanceCount; j++)
        {
            int row = j / gridWidth;
            int col = j % gridWidth;

            float x = col * spacing;
            float y = 0.0f;
            float z = row * spacing;

            // ⭐ Apply the offset here during creation
            x += cubeOffsetX; 

            shaderio::InstancedStorageData data{};
            data.Model = glm::translate(glm::mat4(1.0f), glm::vec3(x, y, z));
            data.albedoMap = ChernoLogo->GetTextureIndex(); // Use the cube texture

            cubeStorageData.push_back(data);
        }
        Geometry::AppendGeometryData("cube", cubeStorageData);
        std::vector<shaderio::InstancedStorageData> cubeStorageData2;
        shaderio::InstancedStorageData data2;
        data2.Model = glm::translate(glm::mat4(1.0f), glm::vec3(-1, 0, 0));
        data2.albedoMap = whiteTexture->GetTextureIndex();
        cubeStorageData2.emplace_back(data2);
        Geometry::AppendGeometryData("cube", cubeStorageData2);*/

        size_t countBufferSize = sizeof(uint32_t);
        m_CountBuffer.reset(IndirectBuffer::Create(countBufferSize));
        // 4            4        156         152                   152
        //draw calls, meshes, instances, actualy instances, draws saved by instancing
        //pipeline statatistics imputassemblyvertices/primitives vertexshaderinvocation clippinginvocation clipping primitives fragmentshaderinvocations computershaderinvocatinon
    }
    
    // this is needed because of shaderlibrary holding raii modules and they die last because renderer has it
    //maybe move to vulkanrenderapi backend ?
    void Renderer::Shutdown() 
    {
        RenderCommand::waitForGraphicsQueueIdle();

        RenderCommand::DestroyAllPipelines();

        GetShaderLibrary().ShutdownAll();

        uniformScene.reset();

        m_TransferRingBuffer.reset();

        m_IndirectBuffer.reset();

        m_CountBuffer.reset();

        m_InstancedVertexBuffer.reset();
        
        m_InstancedIndexBuffer.reset();

        m_InstancedStorageBuffer.reset();
        
        m_MeshInfoBuffer.reset();
    }

    void Renderer::BeginSubmit()
    {
        if (isEditor)
            RenderCommand::BeginFrame(VanK_Render_ImGui);
        else
            RenderCommand::BeginFrame(VanK_Render_Swapchain);
        
        cmd = RenderCommand::BeginCommandBuffer();
        if (!cmd)
            SDL_Log("AcquireGPUCommandBuffer failed: %s", SDL_GetError());
    }

    void Renderer::EndSubmit()
    {
        RenderCommand::SubmitRendering(cmd);
        
        RenderCommand::EndCommandBuffer(cmd);
        
        RenderCommand::EndFrame();
    }
    
    void Renderer::Flush()
    {
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
        /*std::vector<shaderio::InstancedStorageData> cubeInstances;

        // first cube
        shaderio::InstancedStorageData a;
        a.Model = glm::translate(glm::mat4(1.0f), glm::vec3(-4, 0, 0));
        a.albedoMap = whiteTexture->GetTextureIndex();
        cubeInstances.push_back(a);

        // second cube
        shaderio::InstancedStorageData b;
        b.Model = glm::translate(glm::mat4(1.0f), glm::vec3(-2, 0, 0));
        b.albedoMap = ChernoLogo->GetTextureIndex();
        cubeInstances.push_back(b);

        Geometry::SetFrameInstances("cube", cubeInstances);*/
        /*Geometry::AppendGeometryData("cube", cubeStorageData2);*/
        DrawFrame();
        /*cubeInstances.clear();*/
        /*cubeStorageData.clear();*/
        /*Geometry::ClearInstances("cube");*/
        /*EndSubmit();*/
    }
    
    void Renderer::DrawFrame()
    {
        if (Geometry::GetTotalInstances() == 0) 
        {
            // maybe add vkCmdClearColorImage() instead 
            std::vector<VanKColorTargetInfo> colorAttachments;
            colorAttachments.emplace_back(VanK_Format_B8G8R8A8Srgb, VanK_LOADOP_CLEAR, VanK_STOREOP_STORE, VanK_FColor{.f = {0.1f, 0.1f, 0.1f, 1.0f}});
            colorAttachments.emplace_back(VanK_Format_B8G8R8A8Srgb, VanK_LOADOP_CLEAR, VanK_STOREOP_STORE, VanK_FColor{.i = -1});

            VanKDepthStencilTargetInfo depthStencilTargetInfo = {.loadOp = VanK_LOADOP_CLEAR, .storeOp = VanK_STOREOP_STORE, .clearColor = VanK_FColor{.f = {1.0f, 0}}};
            
            RenderCommand::BeginRendering(cmd, colorAttachments.data(), colorAttachments.size(), depthStencilTargetInfo);
            
            RenderCommand::EndRendering(cmd);
            return;
        }
        
        size_t vertexBufferSize = sizeof(shaderio::InstancedVertexData) * Geometry::GetVertices().size();
        if (!m_InstancedVertexBuffer || m_InstancedVertexBuffer->GetSize() < vertexBufferSize)
            m_InstancedVertexBuffer.reset(VertexBuffer::Create(vertexBufferSize));
        
        size_t indexBufferSize = sizeof(shaderio::InstancedIndexData) * Geometry::GetIndices().size();
        if (!m_InstancedIndexBuffer || m_InstancedIndexBuffer->GetSize() < indexBufferSize)
            m_InstancedIndexBuffer.reset(IndexBuffer::Create(indexBufferSize));
        
        size_t indirectBufferSize = sizeof(shaderio::DrawIndexedIndirectCommand) * Geometry::GetMeshes().size();
        if (!m_IndirectBuffer || m_IndirectBuffer->GetSize() < indirectBufferSize)
            m_IndirectBuffer.reset(IndirectBuffer::Create(indirectBufferSize));
        
        size_t storageBufferSize = sizeof(shaderio::InstancedStorageData) * Geometry::GetTotalInstances();
        if (!m_InstancedStorageBuffer || m_InstancedStorageBuffer->GetSize() < storageBufferSize)
            m_InstancedStorageBuffer.reset(StorageBuffer::Create(storageBufferSize));
        
        size_t meshInfoBufferSize = sizeof(shaderio::MeshInfo) * Geometry::GetMeshes().size();
        if (!m_MeshInfoBuffer || m_MeshInfoBuffer->GetSize() < meshInfoBufferSize)
            m_MeshInfoBuffer.reset(StorageBuffer::Create(meshInfoBufferSize));
        
        size_t transferSize = vertexBufferSize + indexBufferSize + indirectBufferSize + storageBufferSize + meshInfoBufferSize;
        if (!m_TransferRingBuffer || m_TransferRingBuffer->GetSize() < transferSize)
            m_TransferRingBuffer.reset(TransferBuffer::Create(transferSize, VanKTransferBufferUsageUpload));
        
    
        UploadBufferToGpuWithTransferRing(cmd, m_TransferRingBuffer, m_InstancedVertexBuffer, Geometry::GetVertices(), shaderio::InstancedVertexData, 0);

        UploadBufferToGpuWithTransferRing(cmd, m_TransferRingBuffer, m_InstancedIndexBuffer, Geometry::GetIndices(), uint32_t, 0);

        UploadBufferToGpuWithTransferRing(cmd, m_TransferRingBuffer, m_InstancedStorageBuffer, Geometry::GetStorageData(), shaderio::InstancedStorageData, 0);
            
        UploadBufferToGpuWithTransferRing(cmd, m_TransferRingBuffer, m_MeshInfoBuffer, Geometry::GetMeshes(), shaderio::MeshInfo, 0);
        
        s_Data.SceneData.vertexAddress = m_InstancedVertexBuffer->GetBufferAddress();
        s_Data.SceneData.indirectAddress = m_IndirectBuffer->GetBufferAddress();
        s_Data.SceneData.storageAddress = m_InstancedStorageBuffer->GetBufferAddress();
        s_Data.SceneData.countAddress = m_CountBuffer->GetBufferAddress();
        s_Data.SceneData.meshInfoAddress = m_MeshInfoBuffer->GetBufferAddress();
        s_Data.SceneData.numMeshes = static_cast<uint32_t>(Geometry::GetMeshes().size());
        
        uniformScene->Update(cmd, &s_Data.SceneData, sizeof(s_Data.SceneData));
        
        RenderCommand::BindUniformBuffer(cmd, VanKPipelineBindPoint::Graphics, uniformScene.get(), 1, 0, 0);
        RenderCommand::BindUniformBuffer(cmd, VanKPipelineBindPoint::Compute, uniformScene.get(), 1, 0, 0);
        
        {
            VanKComputePass* computePass = RenderCommand::BeginComputePass(cmd, m_InstancedVertexBuffer.get(), m_IndirectBuffer.get(), m_CountBuffer.get());
        
            RenderCommand::BindPipeline(cmd, VanKPipelineBindPoint::Compute, m_ComputeDrawIndirectPipeline);
            
            RenderCommand::DispatchCompute(computePass, (Geometry::GetMeshes().size() + 64 - 1) / 64, 1, 1); // matches [numthreads(64,1,1)] in shader

            RenderCommand::EndComputePass(computePass);
        }
        
        {
            std::vector<VanKColorTargetInfo> colorAttachments;
            colorAttachments.emplace_back(VanK_Format_B8G8R8A8Srgb, VanK_LOADOP_CLEAR, VanK_STOREOP_STORE, VanK_FColor{.f = {0.1f, 0.1f, 0.1f, 1.0f}});
            colorAttachments.emplace_back(VanK_Format_B8G8R8A8Srgb, VanK_LOADOP_CLEAR, VanK_STOREOP_STORE, VanK_FColor{.i = -1});

            VanKDepthStencilTargetInfo depthStencilTargetInfo = {.loadOp = VanK_LOADOP_CLEAR, .storeOp = VanK_STOREOP_STORE, .clearColor = VanK_FColor{.f = {1.0f, 0}}};
            
            RenderCommand::BeginRendering(cmd, colorAttachments.data(), colorAttachments.size(), depthStencilTargetInfo);
            
            RenderCommand::BindPipeline(cmd, VanKPipelineBindPoint::Graphics, m_GraphicsDebugPipeline);
            
            VanKViewport viewPort = { 0, 0, m_ViewportSize.width, m_ViewportSize.height, 0, 1 };
            RenderCommand::SetViewport(cmd, 1, viewPort);

            VankRect rect = { 0, 0, m_ViewportSize.width, m_ViewportSize.height };
            RenderCommand::SetScissor(cmd, 1, rect);
            
            RenderCommand::BindFragmentSamplers(cmd, NULL, nullptr, NULL);

            RenderCommand::BindIndexBuffer(cmd, *m_InstancedIndexBuffer, VanKIndexElementSize::Uint32);
            
            RenderCommand::DrawIndexedIndirectCount(cmd, *m_IndirectBuffer, 0, *m_CountBuffer, 0, static_cast<uint32_t>(Geometry::GetMeshes().size()), sizeof(shaderio::DrawIndexedIndirectCommand));

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
        s_PipelineReloadEntries.push_back({ pipeline, graphicsSpec, computeSpec, shaderKey, fileName, flag });
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
