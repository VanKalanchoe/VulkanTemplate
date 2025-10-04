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

    struct Renderer3DData
    {
        struct CameraData
        {
            alignas(16) glm::mat4 view;
            alignas(16) glm::mat4 proj;
            uint64_t vertexAddress;
            uint64_t indexAddress;
            uint64_t indirectAddress;
            uint64_t countAddress;
            uint32_t numVertices;
            uint32_t numindicies;
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
                    shaderio::Vertex vertex{};

                    const float* pos = reinterpret_cast<const float*>(&posBuffer.data[posBufferView.byteOffset + posAccessor
                        .byteOffset + i * 12]);
                    vertex.pos = {pos[0], pos[1], pos[2]};

                    if (hasTexCoords)
                    {
                        const float* texCoord = reinterpret_cast<const float*>(&texCoordBuffer->data[texCoordBufferView->
                            byteOffset + texCoordAccessor->byteOffset + i * 8]);
                        vertex.texCoord = {texCoord[0], texCoord[1]};
                    }
                    else
                    {
                        vertex.texCoord = {0.0f, 0.0f};
                    }

                    vertex.color = {1.0f, 1.0f, 1.0f, 1.0f};

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
    
    void Renderer::Init(Window& window)
    {
        RendererAPI::Config config;
        config.window = window.getWindowHandle();

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
                .blendEnable = false,
                .srcColorBlendFactor = VanK_BLEND_FACTOR_SRC_ALPHA,
                .dstColorBlendFactor = VanK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
                .colorBlendOp = VanK_BLEND_OP_ADD,
                .srcAlphaBlendFactor = VanK_BLEND_FACTOR_SRC_ALPHA,
                .dstAlphaBlendFactor = VanK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
                .alphaBlendOp = VanK_BLEND_OP_ADD,
                .colorWriteMask = VanK_COLOR_COMPONENT_R_BIT | VanK_COLOR_COMPONENT_G_BIT | VanK_COLOR_COMPONENT_B_BIT | VanK_COLOR_COMPONENT_A_BIT,
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
            .VanKColorAttachmentFormats = {VanK_Format_B8G8R8A8Srgb}
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

        /*loadModel();*/
        // Clear and reserve to avoid reallocations
        vertices.clear();
        vertices.reserve(24);

        // Front face (+Z)
        vertices.push_back({{-0.5f, -0.5f,  0.5f}, {1,1,1,1}, {0,0}});
        vertices.push_back({{ 0.5f, -0.5f,  0.5f}, {1,1,1,1}, {1,0}});
        vertices.push_back({{ 0.5f,  0.5f,  0.5f}, {1,1,1,1}, {1,1}});
        vertices.push_back({{-0.5f,  0.5f,  0.5f}, {1,1,1,1}, {0,1}});

        // Back face (-Z)
        vertices.push_back({{ 0.5f, -0.5f, -0.5f}, {1,1,1,1}, {0,0}});
        vertices.push_back({{-0.5f, -0.5f, -0.5f}, {1,1,1,1}, {1,0}});
        vertices.push_back({{-0.5f,  0.5f, -0.5f}, {1,1,1,1}, {1,1}});
        vertices.push_back({{ 0.5f,  0.5f, -0.5f}, {1,1,1,1}, {0,1}});

        // Top face (+Y)
        vertices.push_back({{-0.5f,  0.5f,  0.5f}, {1,1,1,1}, {0,0}});
        vertices.push_back({{ 0.5f,  0.5f,  0.5f}, {1,1,1,1}, {1,0}});
        vertices.push_back({{ 0.5f,  0.5f, -0.5f}, {1,1,1,1}, {1,1}});
        vertices.push_back({{-0.5f,  0.5f, -0.5f}, {1,1,1,1}, {0,1}});

        // Bottom face (-Y)
        vertices.push_back({{-0.5f, -0.5f, -0.5f}, {1,1,1,1}, {0,0}});
        vertices.push_back({{ 0.5f, -0.5f, -0.5f}, {1,1,1,1}, {1,0}});
        vertices.push_back({{ 0.5f, -0.5f,  0.5f}, {1,1,1,1}, {1,1}});
        vertices.push_back({{-0.5f, -0.5f,  0.5f}, {1,1,1,1}, {0,1}});

        // Right face (+X)
        vertices.push_back({{ 0.5f, -0.5f,  0.5f}, {1,1,1,1}, {0,0}});
        vertices.push_back({{ 0.5f, -0.5f, -0.5f}, {1,1,1,1}, {1,0}});
        vertices.push_back({{ 0.5f,  0.5f, -0.5f}, {1,1,1,1}, {1,1}});
        vertices.push_back({{ 0.5f,  0.5f,  0.5f}, {1,1,1,1}, {0,1}});

        // Left face (-X)
        vertices.push_back({{-0.5f, -0.5f, -0.5f}, {1,1,1,1}, {0,0}});
        vertices.push_back({{-0.5f, -0.5f,  0.5f}, {1,1,1,1}, {1,0}});
        vertices.push_back({{-0.5f,  0.5f,  0.5f}, {1,1,1,1}, {1,1}});
        vertices.push_back({{-0.5f,  0.5f, -0.5f}, {1,1,1,1}, {0,1}});

        // 12 triangles (2 per face) → 36 indices
        indices = {
            0, 1, 2, 2, 3, 0,       // front
            4, 5, 6, 6, 7, 4,       // back
            8, 9,10,10,11, 8,       // top
           12,13,14,14,15,12,       // bottom
           16,17,18,18,19,16,       // right
           20,21,22,22,23,20        // left
        };

        size_t vertexBufferSize = sizeof(vertices[0]) * vertices.size();
        vertexMesh.reset(VertexBuffer::Create(vertexBufferSize));

        size_t indexBufferSize = sizeof(indices[0]) * indices.size();
        indexMesh.reset(IndexBuffer::Create(indexBufferSize));

        uint32_t maxDraws = 1;
        size_t indirectBufferSize = sizeof(shaderio::DrawIndexedIndirectCommand) * maxDraws;
        indirectBuffer.reset(IndirectBuffer::Create(indirectBufferSize));

        size_t countBufferSize = sizeof(uint32_t);
        countBuffer.reset(IndirectBuffer::Create(countBufferSize));

        size_t transferSize = vertexBufferSize + indexBufferSize + indirectBufferSize + countBufferSize;
        transferRing.reset(TransferBuffer::Create(transferSize, VanKTransferBufferUsageUpload));
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

        transferRing.reset();

        indirectBuffer.reset();

        countBuffer.reset();

        vertexMesh.reset();
        
        indexMesh.reset();
    }

    void Renderer::BeginSubmit()
    {
        RenderCommand::BeginFrame();
        
        cmd = RenderCommand::BeginCommandBuffer();
        if (!cmd)
            SDL_Log("AcquireGPUCommandBuffer failed: %s", SDL_GetError());
    }

    void Renderer::EndSubmit()
    {
        RenderCommand::EndCommandBuffer(cmd);
        RenderCommand::EndFrame();
    }
    static auto lastTime = std::chrono::high_resolution_clock::now();
    static int frameCount = 0;
    static float fps = 0.0f;
    void Renderer::DrawFrame()
    {
        if (windowMinimized)
            return;
        
        UploadBufferToGpuWithTransferRing(cmd, transferRing, vertexMesh, vertices, shaderio::Vertex, 0);
        UploadBufferToGpuWithTransferRing(cmd, transferRing, indexMesh, indices, uint32_t, 0);

        /*std::vector<VanKDrawIndexedIndirectCommand> drawCommands(1);

        for (uint32_t i = 0; i < 1; i++)
        {
            drawCommands[i].indexCount   = indices.size();
            drawCommands[i].instanceCount= 1;
            drawCommands[i].firstIndex   = 0;
            drawCommands[i].vertexOffset = 0;
            drawCommands[i].firstInstance = i;
        }
        UploadBufferToGpuWithTransferRing(cmd, transferRing, indirectBuffer, drawCommands, VanKDrawIndexedIndirectCommand, 0);

        uint32_t drawCount = 1;
        std::vector<uint32_t> countVec = { drawCount };
        UploadBufferToGpuWithTransferRing(cmd, transferRing, countBuffer, countVec, uint32_t, 0);*/
        
        frameCount++;
        auto now = std::chrono::high_resolution_clock::now();
        float elapsed = std::chrono::duration<float>(now - lastTime).count();
        if (elapsed >= 1.0f)
        {
            fps = frameCount / elapsed;
            frameCount = 0;
            lastTime = now;
            std::cout << "FPS: " << fps << std::endl;
        }
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

        ImGui_ImplVulkan_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();

        /*--
          * IMGUI Docking
          * Create a dockspace and dock the viewport and settings window.
          * The central node is named "Viewport", which can be used later with Begin("Viewport")
          * to render the final image.
         -*/
        const ImGuiDockNodeFlags dockFlags = ImGuiDockNodeFlags_PassthruCentralNode |
            ImGuiDockNodeFlags_NoDockingInCentralNode;
        ImGuiID dockID = ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport(), dockFlags);
        // Docking layout, must be done only if it doesn't exist
        if (!ImGui::DockBuilderGetNode(dockID)->IsSplitNode() && !ImGui::FindWindowByName("Viewport"))
        {
            ImGui::DockBuilderDockWindow("Viewport", dockID); // Dock "Viewport" to  central node
            ImGui::DockBuilderGetCentralNode(dockID)->LocalFlags |= ImGuiDockNodeFlags_NoTabBar;
            // Remove "Tab" from the central node
            ImGuiID leftID = ImGui::DockBuilderSplitNode(dockID, ImGuiDir_Left, 0.2f, nullptr, &dockID);
            // Split the central node
            ImGui::DockBuilderDockWindow("Settings", leftID); // Dock "Settings" to the left node
        }
        // [optional] Show the menu bar
        if (ImGui::BeginMainMenuBar())
        {
            if (ImGui::BeginMenu("File"))
            {
                if (ImGui::MenuItem("vSync", "", &vSync))
                {
                    RenderCommand::RebuildSwapchain(vSync); // Recreate the swapchain with the new vSync setting
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Exit"))
                {
                    SDL_Event quitEvent;
                    SDL_zero(quitEvent); // Zero-initialize
                    quitEvent.type = SDL_EVENT_QUIT; // Set event type
                    SDL_PushEvent(&quitEvent);
                }
                ImGui::EndMenu();
            }
            ImGui::EndMainMenuBar();
        }

        // We define "viewport" with no padding an retrieve the rendering area
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        ImGui::Begin("Viewport");
        ImVec2 windowSize = ImGui::GetContentRegionAvail();
        ImGui::End();
        ImGui::PopStyleVar();

        if (ImGui::Begin("Viewport"))
        {
            ImVec2 viewportSize = ImGui::GetContentRegionAvail();
            m_ViewportSize = { std::max(1u, static_cast<uint32_t>(viewportSize.x)),
                         std::max(1u, static_cast<uint32_t>(viewportSize.y)) };

            if (m_ViewportSize.width != lastViewportExtent.width || m_ViewportSize.height != lastViewportExtent.height)
            {
                
                lastViewportExtent.width = m_ViewportSize.width;
                lastViewportExtent.height = m_ViewportSize.height;

                RenderCommand::setViewportSize(m_ViewportSize);
            }
            
            // !!! This is where the GBuffer image is displayed !!!
            ImGui::Image(RenderCommand::getImTextureID(0), viewportSize);

            // Adding overlay text on the upper left corner
            ImGui::SetCursorPos(ImVec2(0, 0));
            ImGui::Text("FPS: %.1f", ImGui::GetIO().Framerate);
        }
        ImGui::End();

        ImGui::Render(); // This is creating the data to draw the UI (not on GPU yet)
        
        static auto startTime = std::chrono::high_resolution_clock::now();
        static auto lastFrameTime = startTime;
        auto currentTime = std::chrono::high_resolution_clock::now();
        float time = std::chrono::duration<float>(currentTime - startTime).count();
        float deltaTime = std::chrono::duration<float>(currentTime - lastFrameTime).count();
        lastFrameTime = currentTime;

        // Camera and projection matrices (shared by all objects)
        glm::mat4 view = glm::lookAt(glm::vec3(2.0f, 2.0f, 6.0f), glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
        glm::mat4 proj = glm::perspective(glm::radians(45.0f),
                                          static_cast<float>(m_ViewportSize.width) / static_cast<float>(m_ViewportSize.
                                              height), 0.1f, 20.0f);
        proj[1][1] *= -1;
        
        s_Data.camData.view = view;
        s_Data.camData.proj = proj;
        s_Data.camData.vertexAddress = vertexMesh->GetBufferAddress();
        s_Data.camData.indexAddress = indexMesh->GetBufferAddress();
        s_Data.camData.indirectAddress = indirectBuffer->GetBufferAddress();
        s_Data.camData.countAddress = countBuffer->GetBufferAddress();
        s_Data.camData.numVertices = static_cast<uint32_t>(vertices.size());
        s_Data.camData.numindicies = static_cast<uint32_t>(indices.size());
        uniformScene->Update(cmd, &s_Data.camData, sizeof(s_Data.camData));
        RenderCommand::BindUniformBuffer(cmd, VanKPipelineBindPoint::Graphics, uniformScene.get(), 1, 0, 0);
        RenderCommand::BindUniformBuffer(cmd, VanKPipelineBindPoint::Compute, uniformScene.get(), 1, 0, 0);
        
        VanKComputePass* computePass = RenderCommand::BeginComputePass(cmd, vertexMesh.get());
        
        RenderCommand::BindPipeline(cmd, VanKPipelineBindPoint::Compute, m_ComputeDrawIndirectPipeline);
        
        RenderCommand::DispatchCompute(computePass, 1, 1, 1);

        RenderCommand::EndComputePass(computePass);
        {
            std::vector<VanKColorTargetInfo> colorAttachments;
            colorAttachments.emplace_back(VanK_Format_B8G8R8A8Srgb, VanK_LOADOP_CLEAR, VanK_STOREOP_STORE, VanK_FColor{.f = {0.1f, 0.1f, 0.1f, 1.0f}});

            VanKDepthStencilTargetInfo depthStencilTargetInfo = {.loadOp = VanK_LOADOP_CLEAR, .storeOp = VanK_STOREOP_STORE, .clearColor = VanK_FColor{.f = {1.0f, 0}}};
            
            RenderCommand::BeginRendering(cmd, colorAttachments.data(), colorAttachments.size(), depthStencilTargetInfo, VanK_Render_None);
            
            RenderCommand::BindPipeline(cmd, VanKPipelineBindPoint::Graphics, m_GraphicsDebugPipeline);
            
            VanKViewport viewPort = { 0, 0, m_ViewportSize.width, m_ViewportSize.height, 0, 1 };
            RenderCommand::SetViewport(cmd, 1, viewPort);

            VankRect rect = { 0, 0, m_ViewportSize.width, m_ViewportSize.height };
            RenderCommand::SetScissor(cmd, 1, rect);
            
            RenderCommand::BindFragmentSamplers(cmd, NULL, nullptr, NULL);
            
            /*RenderCommand::BindVertexBuffer(cmd, 0, *vertexMesh, 1);*/

            RenderCommand::BindIndexBuffer(cmd, *indexMesh, VanKIndexElementSize::Uint32);

            /*RenderCommand::DrawIndexed(cmd, indices.size(), 1, 0, 0, 0);*/
            RenderCommand::DrawIndexedIndirectCount(cmd, *indirectBuffer, 0, *countBuffer, 0, 1, sizeof(shaderio::DrawIndexedIndirectCommand));

            RenderCommand::EndRendering(cmd);
        }
        
        {
            RenderCommand::BeginRendering(cmd, {}, {}, {}, VanK_Render_ImGui);
            
            RenderCommand::EndRendering(cmd);
        }
        
        ImGui::EndFrame();
        if ((ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable) != 0)
        {
            ImGui::UpdatePlatformWindows();
            ImGui::RenderPlatformWindowsDefault();
        }
    }

    void Renderer::Flush()
    {
        BeginSubmit();
        DrawFrame();
        EndSubmit();
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
