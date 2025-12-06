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

namespace VanK
{
    static std::vector<std::unique_ptr<filewatch::FileWatch<std::string>>> s_ShaderWatcher;
    static std::atomic<bool> s_IsPipelineReloadFinished = false;
    bool IsShaderReloadFinished = false;
    std::string changedFile;
    Timer ReloadTimer;

    struct MeshInfo
    {
        uint32_t indexCount;    // number of indices for this mesh
        uint32_t instanceCount; // how many instances of this mesh you want
        uint32_t firstIndex;    // starting index in the global index buffer
        uint32_t vertexOffset;  // vertex base offset
        uint32_t firstInstance; // optional, for indirect draw
    };
    
    std::vector<MeshInfo> meshes;
    
    struct Renderer3DData
    {
        struct CameraData
        {
            alignas(16) glm::mat4 view;
            alignas(16) glm::mat4 proj;
            uint64_t vertexAddress;
            uint64_t indirectAddress;
            uint64_t countAddress;
            uint64_t storageAddress;
            uint32_t numMeshes;
            MeshInfo meshes[100]; // fixed size array or dynamic with SSBO
        };
        CameraData camData;
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
    }
    
    void Renderer::BeginScene(const EditorCamera& camera)
    {
        glm::mat4 View = camera.GetViewMatrix();
        glm::mat4 Proj = camera.GetProjection();
        
        s_Data.camData.view = View;
        s_Data.camData.proj = Proj;
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

        uniformScene.reset(UniformBuffer::Create(sizeof(s_Data.camData)));

        loadModel();
        
        // Load GLTF mesh
        MeshInfo gltfMesh{};
        gltfMesh.firstIndex = 0; // initial index buffer offset
        gltfMesh.indexCount = indices.size();
        gltfMesh.vertexOffset = 0;
        gltfMesh.instanceCount = 10; // e.g., 1 copies of GLTF mesh
        gltfMesh.firstInstance = 0;
        meshes.push_back(gltfMesh);

        uint32_t baseVertex = vertices.size();
        std::cout << "baseVertex: " << baseVertex << std::endl;
        vertices.insert(vertices.end(), GeometryData::cubeVertices.begin(), GeometryData::cubeVertices.end());
        
        uint32_t baseIndex = indices.size();
        for (uint32_t idx : GeometryData::cubeIndices)
            indices.push_back(baseVertex + idx);
        
        uint32_t firstInstance = 0;
        if (!meshes.empty()) {
            const auto& lastMesh = meshes.back();
            firstInstance = lastMesh.firstInstance + lastMesh.instanceCount;
        }
        
        MeshInfo cubeMesh{};
        cubeMesh.firstIndex = baseIndex; // after GLTF indices appended
        cubeMesh.indexCount = static_cast<uint32_t>(GeometryData::cubeIndices.size());;
        cubeMesh.vertexOffset = baseVertex; // where cube vertices start
        cubeMesh.instanceCount = 2;         // 5 cubes
        cubeMesh.firstInstance = firstInstance;
        meshes.push_back(cubeMesh);
        
        /*vertices = GeometryData::cubeVertices;
        indices = GeometryData::cubeIndices;*/
        
        //fix upload in geometry.cpp maybe stagingbuffer will see
        size_t vertexBufferSize = sizeof(vertices[0]) * vertices.size();
        m_InstancedVertexBuffer.reset(VertexBuffer::Create(vertexBufferSize));

        size_t indexBufferSize = sizeof(indices[0]) * indices.size();
        m_InstancedIndexBuffer.reset(IndexBuffer::Create(indexBufferSize));

        uint32_t maxDraws = static_cast<uint32_t>(meshes.size());
        size_t indirectBufferSize = sizeof(shaderio::DrawIndexedIndirectCommand) * maxDraws;
        indirectBuffer.reset(IndirectBuffer::Create(indirectBufferSize));

        size_t countBufferSize = sizeof(uint32_t);
        countBuffer.reset(IndirectBuffer::Create(countBufferSize));
        
        //needs to be dynamic because it can change per mesh even if they are the same 
        uint32_t totalInstances = 0;
        for (const auto& mesh : meshes)
            totalInstances += mesh.instanceCount;
        size_t storageBufferSize = sizeof(shaderio::InstancedStorageData) * totalInstances;
        m_InstancedStorageBuffer.reset(StorageBuffer::Create(storageBufferSize));

        //maybe for storagebuffer it has to be seprate idk how resizing works will see
        size_t transferSize = vertexBufferSize + indexBufferSize + indirectBufferSize + countBufferSize + storageBufferSize;
        m_TransferRingBuffer.reset(TransferBuffer::Create(transferSize, VanKTransferBufferUsageUpload));
        // 4            4        156         152                   152
        //draw calls, meshes, instances, actualy instances, draws saved by instancing
        //pipeline statatistics imputassemblyvertices/primitives vertexshaderinvocation clippinginvocation clipping primitives fragmentshaderinvocations computershaderinvocatinon

        texture = TextureImporter::LoadTexture2D("");
        vikingRoom = TextureImporter::LoadTexture2D("../build/VanK/textures/viking_room.ktx2");
        vikingRoom2 = TextureImporter::LoadTexture2D("../build/VanK/textures/viking_room2.ktx2");
        ChernoLogo = TextureImporter::LoadTexture2D("../build/VanK/textures/ChernoLogo.ktx2");
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

        indirectBuffer.reset();

        countBuffer.reset();

        m_InstancedVertexBuffer.reset();
        
        m_InstancedIndexBuffer.reset();

        m_InstancedStorageBuffer.reset();
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
    
    void Renderer::DrawFrame()
    {
        std::vector<shaderio::InstancedStorageData> storageData;
        
        // Example transforms for each mesh
        std::vector<glm::vec3> translations = {
            glm::vec3(0.0f, 0.0f, 0.0f),
            glm::vec3(2.0f, 0.0f, 0.0f),
            glm::vec3(-2.0f, 0.0f, 0.0f)
        };

        // Loop over meshes + each instance
        int gridWidth = 3;      // how many per row
        float spacing = 2.0f;

        size_t instanceCounter = 0;
        for (size_t i = 0; i < meshes.size(); i++)
        {
            for (uint32_t j = 0; j < meshes[i].instanceCount; j++)
            {
                int row = instanceCounter / gridWidth;
                int col = instanceCounter % gridWidth;

                float x = col * spacing;
                float y = 0.0f;
                float z = row * spacing;

                shaderio::InstancedStorageData data{};
                data.Model = glm::translate(glm::mat4(1.0f), glm::vec3(x, y, z));

                // Assign textures
                if (instanceCounter == 0)
                    data.albedoMap = vikingRoom->GetTextureIndex();
                else if (instanceCounter == 1)
                    data.albedoMap = vikingRoom2->GetTextureIndex();
                else
                    data.albedoMap = ChernoLogo->GetTextureIndex();

                storageData.push_back(data);
                instanceCounter++;
            }
        }
        
        UploadBufferToGpuWithTransferRing(cmd, m_TransferRingBuffer, m_InstancedVertexBuffer, vertices, shaderio::InstancedVertexData, 0);
        UploadBufferToGpuWithTransferRing(cmd, m_TransferRingBuffer, m_InstancedIndexBuffer, indices, uint32_t, 0);
        UploadBufferToGpuWithTransferRing(cmd, m_TransferRingBuffer, m_InstancedStorageBuffer, storageData, shaderio::InstancedStorageData, 0);
        
        s_Data.camData.vertexAddress = m_InstancedVertexBuffer->GetBufferAddress();
        s_Data.camData.indirectAddress = indirectBuffer->GetBufferAddress();
        s_Data.camData.countAddress = countBuffer->GetBufferAddress();
        s_Data.camData.storageAddress = m_InstancedStorageBuffer->GetBufferAddress();
        s_Data.camData.numMeshes = static_cast<uint32_t>(meshes.size());
        memcpy(s_Data.camData.meshes, meshes.data(), meshes.size() * sizeof(MeshInfo));
        uniformScene->Update(cmd, &s_Data.camData, sizeof(s_Data.camData));
        RenderCommand::BindUniformBuffer(cmd, VanKPipelineBindPoint::Graphics, uniformScene.get(), 1, 0, 0);
        RenderCommand::BindUniformBuffer(cmd, VanKPipelineBindPoint::Compute, uniformScene.get(), 1, 0, 0);
        
        VanKComputePass* computePass = RenderCommand::BeginComputePass(cmd, m_InstancedVertexBuffer.get());
        
        RenderCommand::BindPipeline(cmd, VanKPipelineBindPoint::Compute, m_ComputeDrawIndirectPipeline);
        uint32_t numMeshes = static_cast<uint32_t>(meshes.size());
        uint32_t threadsPerGroup = 64; // matches [numthreads(64,1,1)]
        uint32_t dispatchCount = (numMeshes + threadsPerGroup - 1) / threadsPerGroup;
        RenderCommand::DispatchCompute(computePass, dispatchCount, 1, 1);

        RenderCommand::EndComputePass(computePass);
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
            
            /*RenderCommand::BindVertexBuffer(cmd, 0, *vertexMesh, 1);*/

            RenderCommand::BindIndexBuffer(cmd, *m_InstancedIndexBuffer, VanKIndexElementSize::Uint32);

            /*RenderCommand::DrawIndexed(cmd, indices.size(), 1, 0, 0, 0);*/
            RenderCommand::DrawIndexedIndirectCount(cmd, *indirectBuffer, 0, *countBuffer, 0, static_cast<uint32_t>(meshes.size()), sizeof(shaderio::DrawIndexedIndirectCommand));

            RenderCommand::EndRendering(cmd);
        }
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
        DrawFrame();
        /*EndSubmit();*/
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
