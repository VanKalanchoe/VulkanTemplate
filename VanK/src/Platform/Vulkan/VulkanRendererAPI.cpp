#include "VulkanRendererAPI.h"

/*--
 * We are using the Vulkan Memory Allocator (VMA) to manage memory.
 * This is a library that helps to allocate memory for Vulkan resources.
-*/
#define VMA_DYNAMIC_VULKAN_FUNCTIONS 1
#define VMA_IMPLEMENTATION
#define VMA_LEAK_LOG_FORMAT(format, ...)                                                                               \
{                                                                                                                    \
printf((format), __VA_ARGS__);                                                                                     \
printf("\n");                                                                                                      \
}
#include "vk_mem_alloc.h"


#include <print>

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_vulkan.h>

#include "VulkanBuffer.h"
#include "VulkanShader.h"

namespace  VanK
{
    VulkanRendererAPI::VulkanRendererAPI() = default;

    VulkanRendererAPI::VulkanRendererAPI(const Config& config) : window(config.window)
    {
        // Set this instance as the static instance
        s_instance = this;

        init();
    }

    VulkanRendererAPI::~VulkanRendererAPI()
    {
        // Clear the static instance if it's this instance
        if (s_instance == this)
        {
            s_instance = nullptr;
        }
        device.waitIdle();
        DestroyAllPipelines();// todo idk where to put this will see
        cleanup();
    }

    VulkanRendererAPI& VulkanRendererAPI::Get()
    {
        if (!s_instance)
        {
            // If no instance is set, this will crash - which is what we want
            // because it means we're trying to use Vulkan before it's initialized
            throw std::runtime_error("VulkanRendererAPI not initialized! Call SetInstance first.");
        }
        return *s_instance;
    }
    
    void VulkanRendererAPI::initVulkan()
    {
        createInstance();
        setupDebugMessenger();
        createSurface();
        pickPhysicalDevice();
        createLogicalDevice();
        createDynamicDispatcher();
        allocator.init(VmaAllocatorCreateInfo
        {
            .physicalDevice = *physicalDevice,
            .device = *device,
            .instance = *instance,
            .vulkanApiVersion = apiVersion
        });
    
        msaaSamples = getMaxUsableSampleCount();
        createSwapChain();
        viewport = swapChainExtent;
        createImageViews();
        createCommandPool();
        createSceneResources();
        createColorResources();
        createDepthResources();
        m_samplerPool.init(device);
        createTexture();
        createTextureSampler();
        createDescriptorPool();
        createDescriptorSets();
        createCommandBuffers();
        createSyncObjects();

        //statistics not important
        createQueryPool();
        createQueryBuffer();
    }

    void VulkanRendererAPI::initImGui()
    {
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGui::StyleColorsDark();

        ImGui_ImplSDL3_InitForVulkan(window);
        static VkFormat imageFormats[] = {static_cast<VkFormat>(swapChainSurfaceFormat.format)};
        ImGui_ImplVulkan_InitInfo initInfo = {
            .Instance = *instance,
            .PhysicalDevice = *physicalDevice,
            .Device = *device,
            .QueueFamily = queueIndex,
            .Queue = *queue,
            .DescriptorPool = *uiDescriptorPool,
            .MinImageCount = 2,
            .ImageCount = MAX_FRAMES_IN_FLIGHT,
            .UseDynamicRendering = true,
            .PipelineRenderingCreateInfo = // Dynamic rendering
            {
                .sType = static_cast<VkStructureType>(vk::StructureType::ePipelineRenderingCreateInfo),
                .colorAttachmentCount = 1,
                .pColorAttachmentFormats = imageFormats
            },
        };

        ImGui_ImplVulkan_Init(&initInfo);

        ImGui::GetIO().ConfigFlags = ImGuiConfigFlags_DockingEnable | ImGuiConfigFlags_ViewportsEnable;

        // Descriptor Set for ImGUI
        uiDescriptorSet.resize(1);
        if ((ImGui::GetCurrentContext() != nullptr) && ImGui::GetIO().BackendPlatformUserData != nullptr)
        {
            for (size_t d = 0; d < 1; ++d) // this is how many color attachments i have for now only color
            {
                uiDescriptorSet[d] = ImGui_ImplVulkan_AddTexture(*linearSampler, *sceneImageView,
                                                                 static_cast<VkImageLayout>(
                                                                     vk::ImageLayout::eShaderReadOnlyOptimal));
            }
        }
    }

    void VulkanRendererAPI::cleanupSwapChain()
    {
        swapChainImages.clear();
        swapChainImageViews.clear();
        swapChain = nullptr;
    }

    void VulkanRendererAPI::cleanup()
    {
        m_samplerPool.deinit();
        allocator.destroyBuffer(queryBuffer); // statistics
        allocator.deinit();

        ImGui_ImplVulkan_Shutdown();
        ImGui_ImplSDL3_Shutdown();
        ImGui::DestroyContext();
    }

    void VulkanRendererAPI::recreateSwapChain()
    {
        device.waitIdle();

        cleanupSwapChain();
        createSwapChain();
        createImageViews();
    }
    
    void VulkanRendererAPI::recreateImages()
    {
        device.waitIdle();
        
        // Recreate offscreen buffers to match viewport size
        createSceneResources();//scene evertyhing drawn into this
        createColorResources();//msaa
        createDepthResources();//depth
        sceneImageInitialized = false;

        // Recreate the ImGui texture to point to the new sceneImageView
        if (!uiDescriptorSet.empty() && uiDescriptorSet[0] != nullptr)
        {
            ImGui_ImplVulkan_RemoveTexture(uiDescriptorSet[0]);
            uiDescriptorSet[0] = nullptr;
        }
        if ((ImGui::GetCurrentContext() != nullptr) && ImGui::GetIO().BackendPlatformUserData != nullptr)
        {
            uiDescriptorSet.resize(1);
            uiDescriptorSet[0] = ImGui_ImplVulkan_AddTexture(
                *linearSampler,
                *sceneImageView,
                static_cast<VkImageLayout>(vk::ImageLayout::eShaderReadOnlyOptimal));
        }
    }

    void VulkanRendererAPI::createInstance()
    {
        constexpr vk::ApplicationInfo appInfo
        {
            .pApplicationName = "Hello Triangle",
            .applicationVersion = VK_MAKE_VERSION(1, 0, 0),
            .pEngineName = "No Engine",
            .engineVersion = VK_MAKE_VERSION(1, 0, 0),
            .apiVersion = vk::ApiVersion14
        };
        apiVersion = vk::ApiVersion14;
    
        // Get the required layers
        std::vector<char const*> requiredLayers;
        if (enableValidationLayers)
        {
            requiredLayers.assign(validationLayers.begin(), validationLayers.end());
        }

        // Check if the required layers are supported by the Vulkan implementation.
        auto layerProperties = context.enumerateInstanceLayerProperties();
        if (std::ranges::any_of(requiredLayers, [&layerProperties](auto const& requiredLayer)
        {
            return std::ranges::none_of(layerProperties,
                                        [requiredLayer](auto const& layerProperty)
                                        {
                                            return strcmp(layerProperty.layerName, requiredLayer) == 0;
                                        });
        }))
        {
            throw std::runtime_error("One or more required layers are not supported!");
        }

        // Get the required extensions.
        auto requiredExtensions = getRequiredExtensions();

        // Check if the required extensions are supported by the Vulkan implementation.
        auto extensionProperties = context.enumerateInstanceExtensionProperties();
        for (auto const& requiredExtension : requiredExtensions)
        {
            if (std::ranges::none_of(extensionProperties,
                                     [requiredExtension](auto const& extensionProperty)
                                     {
                                         return strcmp(extensionProperty.extensionName, requiredExtension) == 0;
                                     }))
            {
                throw std::runtime_error("Required extension not supported: " + std::string(requiredExtension));
            }
        }

        vk::InstanceCreateInfo createInfo
        {
            .pApplicationInfo = &appInfo,
            .enabledLayerCount = static_cast<uint32_t>(requiredLayers.size()),
            .ppEnabledLayerNames = requiredLayers.data(),
            .enabledExtensionCount = static_cast<uint32_t>(requiredExtensions.size()),
            .ppEnabledExtensionNames = requiredExtensions.data(),
        };
        instance = vk::raii::Instance(context, createInfo);
    }

    void VulkanRendererAPI::setupDebugMessenger()
    {
        if (!enableValidationLayers) return;

        vk::DebugUtilsMessageSeverityFlagsEXT severityFlags(
            vk::DebugUtilsMessageSeverityFlagBitsEXT::eVerbose | vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning |
            vk::DebugUtilsMessageSeverityFlagBitsEXT::eError);
        vk::DebugUtilsMessageTypeFlagsEXT messageTypeFlags(
            vk::DebugUtilsMessageTypeFlagBitsEXT::eGeneral | vk::DebugUtilsMessageTypeFlagBitsEXT::ePerformance |
            vk::DebugUtilsMessageTypeFlagBitsEXT::eValidation);
        vk::DebugUtilsMessengerCreateInfoEXT debugUtilsMessengerCreateInfoEXT{
            .messageSeverity = severityFlags,
            .messageType = messageTypeFlags,
            .pfnUserCallback = &debugCallback
        };
        debugMessenger = instance.createDebugUtilsMessengerEXT(debugUtilsMessengerCreateInfoEXT);
    }

    void VulkanRendererAPI::createSurface()
    {
        VkSurfaceKHR _surface;
        if (!SDL_Vulkan_CreateSurface(window, *instance, nullptr, &_surface))
        {
            throw std::runtime_error("failed to create window surface!");
        }
        surface = vk::raii::SurfaceKHR(instance, _surface);
        DBG_VK_NAME(*surface);
    }

    void VulkanRendererAPI::pickPhysicalDevice()
    {
        std::vector<vk::raii::PhysicalDevice> devices = instance.enumeratePhysicalDevices();
        const auto devIter = std::ranges::find_if(devices, [&](auto const& device)
        {
            // Check if the device supports the Vulkan 1.3 API version
            bool supportsVulkan1_3 = device.getProperties().apiVersion >= VK_API_VERSION_1_3;

            // Check if any of the queue families support graphics operations
            auto queueFamilies = device.getQueueFamilyProperties();
            bool supportsGraphics =
                std::ranges::any_of(queueFamilies, [](auto const& qfp)
                {
                    return !!(qfp.queueFlags & vk::QueueFlagBits::eGraphics);
                });

            // Check if all required device extensions are available
            auto availableDeviceExtensions = device.enumerateDeviceExtensionProperties();
            bool supportsAllRequiredExtensions =
                std::ranges::all_of(requiredDeviceExtension,
                                    [&availableDeviceExtensions](auto const& requiredDeviceExtension)
                                    {
                                        return std::ranges::any_of(availableDeviceExtensions,
                                                                   [requiredDeviceExtension](
                                                                   auto const& availableDeviceExtension)
                                                                   {
                                                                       return strcmp(
                                                                           availableDeviceExtension.extensionName,
                                                                           requiredDeviceExtension) == 0;
                                                                   });
                                    });

            auto features = device.template getFeatures2
            <
                vk::PhysicalDeviceFeatures2, vk::PhysicalDeviceVulkan13Features,
                vk::PhysicalDeviceExtendedDynamicStateFeaturesEXT,
                vk::PhysicalDeviceTimelineSemaphoreFeaturesKHR
            >();
            bool supportsRequiredFeatures = features.template get<vk::PhysicalDeviceFeatures2>().features.
                                                     samplerAnisotropy &&
                features.template get<vk::PhysicalDeviceVulkan13Features>().dynamicRendering &&
                features.template get<vk::PhysicalDeviceExtendedDynamicStateFeaturesEXT>().extendedDynamicState &&
                features.template get<vk::PhysicalDeviceTimelineSemaphoreFeaturesKHR>().timelineSemaphore;    

            return supportsVulkan1_3 && supportsGraphics && supportsAllRequiredExtensions && supportsRequiredFeatures;
        });
        if (devIter != devices.end())
        {
            physicalDevice = *devIter;
        }
        else
        {
            throw std::runtime_error("failed to find a suitable GPU!");
        }
    }

    void VulkanRendererAPI::createLogicalDevice()
    {
        std::vector<vk::QueueFamilyProperties> queueFamilyProperties = physicalDevice.getQueueFamilyProperties();

        // get the first index into queueFamilyProperties which supports both graphics and present
        for (uint32_t qfpIndex = 0; qfpIndex < queueFamilyProperties.size(); qfpIndex++)
        {
            if ((queueFamilyProperties[qfpIndex].queueFlags & vk::QueueFlagBits::eGraphics &&
                queueFamilyProperties[qfpIndex].queueFlags & vk::QueueFlagBits::eCompute) &&
                physicalDevice.getSurfaceSupportKHR(qfpIndex, *surface))
            {
                // found a queue family that supports both graphics and present
                queueIndex = qfpIndex;
                break;
            }
        }
        if (queueIndex == ~0)
        {
            throw std::runtime_error("Could not find a queue for graphics and present -> terminating");
        }

        // query for Vulkan 1.3 features
        vk::StructureChain
        <
            vk::PhysicalDeviceFeatures2,
            vk::PhysicalDeviceVulkan11Features,
            vk::PhysicalDeviceVulkan12Features,
            vk::PhysicalDeviceVulkan13Features,
            vk::PhysicalDeviceExtendedDynamicStateFeaturesEXT,
            vk::PhysicalDeviceMaintenance5Features
        >
        featureChain =
        {
            {.features = {.sampleRateShading = true, .samplerAnisotropy = true, .pipelineStatisticsQuery = true, .shaderInt64 = true}}, // vk::PhysicalDeviceFeatures2
            {.shaderDrawParameters = true},
            {
                .drawIndirectCount = true,
                .descriptorIndexing = true, 
                .shaderSampledImageArrayNonUniformIndexing = true,
                .descriptorBindingSampledImageUpdateAfterBind = true,
                .descriptorBindingUpdateUnusedWhilePending = true,
                .descriptorBindingPartiallyBound = true,
                .descriptorBindingVariableDescriptorCount = true,
                .runtimeDescriptorArray = true,
                .scalarBlockLayout = true,
                .timelineSemaphore = true,
                .bufferDeviceAddress = true
            },
            {.synchronization2 = true, .dynamicRendering = true}, // vk::PhysicalDeviceVulkan13Features
            {.extendedDynamicState = true}, // vk::PhysicalDeviceExtendedDynamicStateFeaturesEXT
            {.maintenance5 = true},
        };

        // create a Device
        float queuePriority = 0.0f;
        vk::DeviceQueueCreateInfo deviceQueueCreateInfo{
            .queueFamilyIndex = queueIndex, .queueCount = 1, .pQueuePriorities = &queuePriority
        };
        vk::DeviceCreateInfo deviceCreateInfo{
            .pNext = &featureChain.get(),
            .queueCreateInfoCount = 1,
            .pQueueCreateInfos = &deviceQueueCreateInfo,
            .enabledExtensionCount = static_cast<uint32_t>(requiredDeviceExtension.size()),
            .ppEnabledExtensionNames = requiredDeviceExtension.data()
        };

        device = vk::raii::Device(physicalDevice, deviceCreateInfo);
        DBG_VK_NAME(*device);

        debugUtilInitialize(device);
        
        queue = vk::raii::Queue(device, queueIndex, 0);
        DBG_VK_NAME(*queue);
    }

    void VulkanRendererAPI::createDynamicDispatcher()
    {
        /*//Use your own initial function pointer of type PFN_vkGetInstanceProcAddr: provided by SDL3
        PFN_vkGetInstanceProcAddr vkGetInstanceProcAddr = reinterpret_cast<PFN_vkGetInstanceProcAddr>(SDL_Vulkan_GetVkGetInstanceProcAddr());
        VULKAN_HPP_DEFAULT_DISPATCHER.init(vkGetInstanceProcAddr);

        //initialize it with a vk::Instance to get all the other function pointers:
        VULKAN_HPP_DEFAULT_DISPATCHER.init(instance);*/
    }

    void VulkanRendererAPI::createSwapChain()
    {
        auto surfaceCapabilities = physicalDevice.getSurfaceCapabilitiesKHR(*surface);
        swapChainExtent = chooseSwapExtent(surfaceCapabilities);
        
        swapChainSurfaceFormat = chooseSwapSurfaceFormat(physicalDevice.getSurfaceFormatsKHR(*surface));
        
        vk::SwapchainCreateInfoKHR swapChainCreateInfo{
            .surface = *surface,
            .minImageCount = chooseSwapMinImageCount(surfaceCapabilities),
            .imageFormat = swapChainSurfaceFormat.format,
            .imageColorSpace = swapChainSurfaceFormat.colorSpace,
            .imageExtent = swapChainExtent,
            .imageArrayLayers = 1,
            .imageUsage = vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eTransferDst,
            .imageSharingMode = vk::SharingMode::eExclusive,
            .preTransform = surfaceCapabilities.currentTransform,
            .compositeAlpha = vk::CompositeAlphaFlagBitsKHR::eOpaque,
            .presentMode = chooseSwapPresentMode(physicalDevice.getSurfacePresentModesKHR(*surface), vSync),
            .clipped = true
        };
        swapChain = vk::raii::SwapchainKHR(device, swapChainCreateInfo);
        DBG_VK_NAME(*swapChain);
        
        swapChainImages = swapChain.getImages();
    }

    //change this ? from chapter image view
    void VulkanRendererAPI::createImageViews()
    {
        assert(swapChainImageViews.empty());

        vk::ImageViewCreateInfo imageViewCreateInfo{
            .viewType = vk::ImageViewType::e2D,
            .format = swapChainSurfaceFormat.format,
            .subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1}
        };
        for (auto& image : swapChainImages)
        {
            imageViewCreateInfo.image = image;
            DBG_VK_NAME(image);
            vk::raii::ImageView imageView(device, imageViewCreateInfo);
            DBG_VK_NAME(*imageView);
            swapChainImageViews.emplace_back(std::move(imageView));
        }
    }

    struct VertexInputDescription
    {
        std::vector<vk::VertexInputBindingDescription> bindings;
        std::vector<vk::VertexInputAttributeDescription> attributes;
    };

    static vk::Format ShaderDataTypeToVulkanFormat(ShaderDataType type)
    {
        switch (type)
        {
        case ShaderDataType::Float: return vk::Format::eR32Sfloat;
        case ShaderDataType::Float2: return vk::Format::eR32G32Sfloat;
        case ShaderDataType::Float3: return vk::Format::eR32G32B32Sfloat;
        case ShaderDataType::Float4: return vk::Format::eR32G32B32A32Sfloat;
        case ShaderDataType::Int: return vk::Format::eR32Sint;
        case ShaderDataType::Int2: return vk::Format::eR32G32Sint;
        case ShaderDataType::Int3: return vk::Format::eR32G32B32Sint;
        case ShaderDataType::Int4: return vk::Format::eR32G32B32A32Sint;
        case ShaderDataType::Bool: return vk::Format::eR8Uint; // or VK_FORMAT_R32_UINT if you want 4 bytes
            // Add Mat3/Mat4 if you want to support them as arrays of vec3/vec4
        default: return vk::Format::eUndefined;
        }
    }

    inline VertexInputDescription BufferLayoutToVertexInput(const BufferLayout& layout, uint32_t binding = 0)
    {
        VertexInputDescription desc;
        desc.bindings.push_back({
            binding,
            layout.GetStride(),
            vk::VertexInputRate::eVertex
        });

        uint32_t location = 0;
        for (const auto& element : layout.GetElements())
        {
            desc.attributes.push_back({
                location++,
                binding,
                ShaderDataTypeToVulkanFormat(element.Type),
                element.Offset
            });
        }
        return desc;
    }

    uint32_t VulkanRendererAPI::AddTextureToPool(utils::ImageResource&& imageResource)
    {
        // Add the texture to the image vector
        images.emplace_back(std::move(imageResource));

        // Update the descriptor set to include the new texture

        // Return the index of the new texture
        return static_cast<uint32_t>(images.size() - 1);
    }

    void VulkanRendererAPI::RemoveTextureFromPool(uint32_t index)
    {
        // Safety checks
        if (images.empty())
        {
            VK_CORE_WARN("Attempted to remove texture from empty pool");
            return;
        }
        
        if (index >= images.size())
        {
            VK_CORE_WARN("Attempted to remove texture at invalid index: %u (max: %zu)", index, images.size());
            return;
        }

        VK_CORE_INFO("Removed texture at index %u, remaining textures: %zu", index, images.size());
    }

    VanKPipeLine VulkanRendererAPI::createGraphicsPipeline(VanKGraphicsPipelineSpecification pipelineSpecification)
    {
        vk::raii::Pipeline tempPipeline = VK_NULL_HANDLE;
        vk::raii::PipelineLayout tempPipelineLayout = VK_NULL_HANDLE;
        
        auto specShader = pipelineSpecification.ShaderStageCreateInfo.VanKShader;
        auto vkShader = dynamic_cast<VulkanShader*>(specShader);
        
        std::string vertShaderEntryName = vkShader->GetShaderEntryName(vk::ShaderStageFlagBits::eVertex);
        std::string fragShaderEntryName = vkShader->GetShaderEntryName(vk::ShaderStageFlagBits::eFragment);
        auto& vertShaderModule = vkShader->GetShaderModule(vk::ShaderStageFlagBits::eVertex);
        auto& fragShaderModule = vkShader->GetShaderModule(vk::ShaderStageFlagBits::eFragment);

        vk::PipelineShaderStageCreateInfo vertShaderStageInfo
        {
            .stage = vk::ShaderStageFlagBits::eVertex,
            .module = vertShaderModule,
            .pName = vertShaderEntryName.c_str()
        };
        vk::PipelineShaderStageCreateInfo fragShaderStageInfo
        {
            .stage = vk::ShaderStageFlagBits::eFragment,
            .module = fragShaderModule,
            .pName = fragShaderEntryName.c_str(),
        };
        
        vk::PipelineShaderStageCreateInfo shaderStages[] = {vertShaderStageInfo, fragShaderStageInfo};

        VertexInputDescription vertexInput; // Keep this alive until vkCreateGraphicsPipelines finishes
        
        vk::PipelineVertexInputStateCreateInfo vertexInputInfo{};
        // Describe the layout of the Vertex in the Buffer, which is passed to the vertex shader
        if (!pipelineSpecification.VertexInputStateCreateInfo.VanKBufferLayout.GetElements().empty())
        {
            vertexInput = BufferLayoutToVertexInput(pipelineSpecification.VertexInputStateCreateInfo.VanKBufferLayout);
            
            vertexInputInfo.vertexBindingDescriptionCount = static_cast<uint32_t>(vertexInput.bindings.size());
            vertexInputInfo.pVertexBindingDescriptions = vertexInput.bindings.data();
            vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(vertexInput.attributes.size());
            vertexInputInfo.pVertexAttributeDescriptions = vertexInput.attributes.data();
        }
        else
        {
            // Explicitly no vertex input
            vertexInputInfo.vertexBindingDescriptionCount = 0;
            vertexInputInfo.pVertexBindingDescriptions = nullptr;
            vertexInputInfo.vertexAttributeDescriptionCount = 0;
            vertexInputInfo.pVertexAttributeDescriptions = nullptr;
        }
        
        vk::PipelineInputAssemblyStateCreateInfo inputAssembly
        {
            .topology = ConvertToVkPrimitiveTopology(pipelineSpecification.InputAssemblyStateCreateInfo.VanKPrimitive),
            .primitiveRestartEnable = vk::False
        };
        
        vk::PipelineViewportStateCreateInfo viewportState
        {
            .viewportCount = 0,
            .pViewports = nullptr,
            .scissorCount = 0,
            .pScissors = nullptr
        };
        
        // The rasterizer is used to convert the primitives into fragments, and how it will appear
        vk::PipelineRasterizationStateCreateInfo rasterizer
        {
            .depthClampEnable = vk::False,
            .rasterizerDiscardEnable = vk::False,
            .polygonMode = ConvertToVkPolygonMode(pipelineSpecification.RasterizationStateCreateInfo.VanKPolygon),
            .cullMode = ConvertToVkCullMode(pipelineSpecification.RasterizationStateCreateInfo.VanKCullMode),
            .frontFace = ConvertToVkFrontFace(pipelineSpecification.RasterizationStateCreateInfo.VanKFrontFace),
            .depthBiasEnable = vk::False
        };
        
        rasterizer.lineWidth = 1.0f; // dont needed dynamic now
        vk::PipelineMultisampleStateCreateInfo multisampling // todo expose this as api
        {
            .rasterizationSamples = pipelineSpecification.MultisampleStateCreateInfo.sampleCount == VanK_SAMPLE_COUNT_1_BIT ? vk::SampleCountFlagBits::e1 : msaaSamples,
            .sampleShadingEnable = pipelineSpecification.MultisampleStateCreateInfo.sampleShadingEnable,
            .minSampleShading = pipelineSpecification.MultisampleStateCreateInfo.minSampleShading, // min fraction for sample shading; closer to one is smoother
        };
        
        // Instruct how the depth buffer will be used
        vk::PipelineDepthStencilStateCreateInfo depthStencil
        {
            .depthTestEnable = pipelineSpecification.DepthStateInfo.depthTestEnable,
            .depthWriteEnable = pipelineSpecification.DepthStateInfo.depthWriteEnable,
            .depthCompareOp = ConvertToVkCompareOp(pipelineSpecification.DepthStateInfo.VanKdepthCompareOp),
            .depthBoundsTestEnable = vk::False,
            .stencilTestEnable = vk::False
        };
        
        /*--
         * The color blending is used to blend the color of the fragment with the color already in the framebuffer (all channel)
         * Here we enable blending, such that the alpha channel is used to blend the color with the color already in the framebuffer.
         * The texture will have part transparent.
         *
         * Without blending, everything can be set to 0, except colorWriteMask, which needs to be set.
        */
        std::vector<vk::PipelineColorBlendAttachmentState> colorBlendAttachments;
        for ( auto ColorBlendAttachment : pipelineSpecification.ColorBlendStateCreateInfo.VanKColorBlendAttachmentState)
        {
            vk::PipelineColorBlendAttachmentState tempcolorBlendAttachment{
                .blendEnable = ColorBlendAttachment.blendEnable, // No blending for entity ID buffer
                .srcColorBlendFactor = ConvertToVkBlendFactor(ColorBlendAttachment.srcColorBlendFactor),
                .dstColorBlendFactor = ConvertToVkBlendFactor(ColorBlendAttachment.dstColorBlendFactor),
                .colorBlendOp = ConvertToVkBlendOp(ColorBlendAttachment.colorBlendOp),
                .srcAlphaBlendFactor = ConvertToVkBlendFactor(ColorBlendAttachment.srcAlphaBlendFactor),
                .dstAlphaBlendFactor = ConvertToVkBlendFactor(ColorBlendAttachment.dstAlphaBlendFactor),
                .alphaBlendOp = ConvertToVkBlendOp(ColorBlendAttachment.alphaBlendOp),
                .colorWriteMask = ConvertToVkcolorWriteMask(ColorBlendAttachment.colorWriteMask), // Only R is used for R32_SINT
            };
            colorBlendAttachments.emplace_back(tempcolorBlendAttachment);
        }

        vk::PipelineColorBlendStateCreateInfo colorBlending
        {
             .logicOpEnable = vk::False, // No logic operation
             .logicOp = vk::LogicOp::eCopy, // Don't care
             .attachmentCount = uint32_t(colorBlendAttachments.size()),
             .pAttachments = colorBlendAttachments.data()
        };

        std::vector dynamicStates =
        {
            vk::DynamicState::eViewportWithCount,
            vk::DynamicState::eScissorWithCount,
            vk::DynamicState::eLineWidth
        };
        
        vk::PipelineDynamicStateCreateInfo dynamicState
        {
            .dynamicStateCount = static_cast<uint32_t>(dynamicStates.size()), .pDynamicStates = dynamicStates.data()
        };

        std::array<vk::DescriptorSetLayout, 2> setLayouts =
        {
            *descriptorSetLayout,
            *commonDescriptorSetLayout
        };

        vk::PipelineLayoutCreateInfo pipelineLayoutInfo
        {
            .setLayoutCount = setLayouts.size(),
            .pSetLayouts = setLayouts.data(),
            .pushConstantRangeCount = 0
        };

        tempPipelineLayout = vk::raii::PipelineLayout(device, pipelineLayoutInfo);
        DBG_VK_NAME(*tempPipelineLayout);
        
        // Dynamic rendering: provide what the pipeline will render to
        std::vector<vk::Format> colorFormats;
        for (auto format : pipelineSpecification.RenderingCreateInfo.VanKColorAttachmentFormats)
        {
            colorFormats.emplace_back(ConvertToVkColorFormat(format));
        }
        
        vk::PipelineRenderingCreateInfo pipelineRenderingCreateInfo
        {
            .colorAttachmentCount = static_cast<uint32_t>(colorFormats.size()),
            .pColorAttachmentFormats = colorFormats.data(), // &swapChainSurfaceFormat.format
            .depthAttachmentFormat = findDepthFormat()
        };
        
        vk::GraphicsPipelineCreateInfo pipelineInfo
        {
            .pNext = &pipelineRenderingCreateInfo,
            .stageCount = static_cast<uint32_t>(std::size(shaderStages)),
            .pStages = shaderStages,
            .pVertexInputState = &vertexInputInfo,
            .pInputAssemblyState = &inputAssembly,
            .pViewportState = &viewportState,
            .pRasterizationState = &rasterizer,
            .pMultisampleState = &multisampling,
            .pDepthStencilState = &depthStencil,
            .pColorBlendState = &colorBlending,
            .pDynamicState = &dynamicState,
            .layout = tempPipelineLayout,
            .renderPass = nullptr
        };

        tempPipeline = vk::raii::Pipeline(device, nullptr, pipelineInfo);
        DBG_VK_NAME(*tempPipeline);
        
        PipelineResource resource;
        resource.pipeline = std::move(tempPipeline);
        resource.layout = std::move(tempPipelineLayout);
        resource.bindPoint = VanKPipelineBindPoint::Graphics;
        resource.spec = pipelineSpecification;

        auto rawHandle = *resource.pipeline; // raw VkPipeline before moving
        m_currentGraphicPipelineLayout = *resource.layout;
        m_PipelineResources.emplace(rawHandle, std::move(resource));
        
        return Wrap(rawHandle);
    }

    VanKPipeLine VulkanRendererAPI::createComputeShaderPipeline(VanKComputePipelineSpecification computePipelineSpecification)
    {
        vk::raii::Pipeline tempPipeline = VK_NULL_HANDLE;
        vk::raii::PipelineLayout tempPipelineLayout = VK_NULL_HANDLE;
        
        auto specShader = dynamic_cast<VulkanShader*>(computePipelineSpecification.ComputePipelineCreateInfo.VanKShader);
        std::string computeEntryName = specShader->GetShaderEntryName(vk::ShaderStageFlagBits::eCompute);
        auto& compute = specShader->GetShaderModule(vk::ShaderStageFlagBits::eCompute);

        // Create the pipeline layout used by the compute shader
        /*
        const std::array<VkPushConstantRange, 1> pushRanges =
        {
            {{.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT, .offset = 0, .size = sizeof(shaderio::PushConstantCompute)}}
        };
        */

        vk::PipelineShaderStageCreateInfo computeShaderStageInfo
        {
            .stage = vk::ShaderStageFlagBits::eCompute,
            .module = compute,
            .pName = computeEntryName.c_str()
        };

        const std::array<vk::DescriptorSetLayout, 2> computeDescriptorSetLayouts =
        {
            descriptorSetLayout,
            commonDescriptorSetLayout // <-- This is your new, shared layout
        };

        // The pipeline layout is used to pass data to the pipeline, anything with "layout" in the shader
        const vk::PipelineLayoutCreateInfo pipelineLayoutInfo
        {
            .setLayoutCount = uint32_t(computeDescriptorSetLayouts.size()),
            .pSetLayouts = computeDescriptorSetLayouts.data(),
            /*.pushConstantRangeCount = uint32_t(pushRanges.size()),
            .pPushConstantRanges = pushRanges.data(),*/
        };
        tempPipelineLayout = vk::raii::PipelineLayout( device, pipelineLayoutInfo );
        DBG_VK_NAME(*tempPipelineLayout);

        // Creating the pipeline to run the compute shader
        vk::ComputePipelineCreateInfo pipelineInfo
        {
            .stage = computeShaderStageInfo,
            .layout = tempPipelineLayout
        };
        tempPipeline = vk::raii::Pipeline( device, nullptr, pipelineInfo );
        DBG_VK_NAME(*tempPipeline);
        
        PipelineResource resource;
        resource.pipeline = std::move(tempPipeline);
        resource.layout = std::move(tempPipelineLayout);
        resource.bindPoint = VanKPipelineBindPoint::Compute;
        resource.computeSpec = computePipelineSpecification;

        auto rawHandle = *resource.pipeline; // raw VkPipeline before moving
        m_currentComputePipelineLayout = *resource.layout;
        m_PipelineResources.emplace(rawHandle, std::move(resource));

        return Wrap(rawHandle);
    }

    void VulkanRendererAPI::DestroyAllPipelines()
    {
        // Clear the map completely
        m_PipelineResources.clear();
    }

    /*--
     * Destroy all resources and the Vulkan context
   -*/

    void VulkanRendererAPI::DestroyPipeline(VanKPipeLine pipeline)
    {
        auto it = m_PipelineResources.find(Unwrap(pipeline));
        if (it != m_PipelineResources.end())
        {
            // not needed because of RAII
            /*if (it->second.pipeline != VK_NULL_HANDLE)
            {
                VkDevice device = m_context.getDevice();
                
                vkDestroyPipeline(device, it->second.pipeline, nullptr);
                it->second.pipeline = VK_NULL_HANDLE;
                
                vkDestroyPipelineLayout(device, it->second.layout, nullptr);
                it->second.layout = VK_NULL_HANDLE;
            }*/

            m_PipelineResources.erase(it);
        }
    }

    VanKCommandBuffer VulkanRendererAPI::BeginCommandBuffer()
    {
        commandBuffers[currentFrame].reset();
        
        commandBuffers[currentFrame].begin({});

        //statistics
        commandBuffers[currentFrame].resetQueryPool(queryPool, 0, 1);
        commandBuffers[currentFrame].beginQuery(queryPool, 0);
        
        auto cmd = new VanKCommandBuffer_T{&commandBuffers[currentFrame]};
        
        return cmd;
    }
    
    void VulkanRendererAPI::EndCommandBuffer(VanKCommandBuffer cmd)
    {
        Unwrap(cmd).endQuery(queryPool, 0);
        Unwrap(cmd).end();
    }

    void VulkanRendererAPI::BeginFrame()
    {
        while (vk::Result::eTimeout == device.waitForFences(*inFlightFences[currentFrame], vk::True, UINT64_MAX));
        auto [result, imageIndex] = swapChain.acquireNextImage(UINT64_MAX, *presentCompleteSemaphores[currentFrame],
                                                               nullptr);

        currentResult = result;
        
        currentImageIndex = imageIndex;
        
        if (result == vk::Result::eErrorOutOfDateKHR)
        {
            recreateSwapChain();
            return;
        }
        
        if (result != vk::Result::eSuccess && result != vk::Result::eSuboptimalKHR)
        {
            throw std::runtime_error("failed to acquire swap chain image!");
        }
        
        device.resetFences(*inFlightFences[currentFrame]);
    }

    void VulkanRendererAPI::EndFrame()
    {
        vk::PipelineStageFlags waitDestinationStageMask(vk::PipelineStageFlagBits::eColorAttachmentOutput);
        const vk::SubmitInfo submitInfo{
            .waitSemaphoreCount = 1,
            .pWaitSemaphores = &*presentCompleteSemaphores[currentFrame],
            .pWaitDstStageMask = &waitDestinationStageMask,
            .commandBufferCount = 1,
            .pCommandBuffers = &*commandBuffers[currentFrame],
            .signalSemaphoreCount = 1,
            .pSignalSemaphores = &*renderFinishedSemaphores[currentImageIndex]
        };
        queue.submit(submitInfo, *inFlightFences[currentFrame]);

        const vk::PresentInfoKHR presentInfoKHR{
            .waitSemaphoreCount = 1,
            .pWaitSemaphores = &*renderFinishedSemaphores[currentImageIndex],
            .swapchainCount = 1,
            .pSwapchains = &*swapChain,
            .pImageIndices = &currentImageIndex
        };
        VkResult rawResult = vkQueuePresentKHR(*queue, reinterpret_cast<const VkPresentInfoKHR*>(&presentInfoKHR));
        currentResult = static_cast<vk::Result>(rawResult);
        //result = queue.presentKHR(presentInfoKHR); when resizing in hpp is fixed then use this https://github.com/KhronosGroup/Vulkan-Tutorial/issues/73
        if (currentResult == vk::Result::eErrorOutOfDateKHR || currentResult == vk::Result::eSuboptimalKHR || framebufferResized)
        {
            framebufferResized = false;
            recreateSwapChain();
        }
        else if (currentResult != vk::Result::eSuccess)
        {
            throw std::runtime_error("failed to present swap chain image!");
        }
        currentFrame = (currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
        /*downloadQueryBuffer();*/
    }

    VanKComputePass* VulkanRendererAPI::BeginComputePass(VanKCommandBuffer cmd, VertexBuffer* buffer)
    {
        auto* result = new VanKComputePass
        {
            .VanKCommandBuffer = cmd,
            .VanKVertexBuffer = buffer
        };

        if (buffer != nullptr)
        {
            // Add a barrier to make sure nothing was writing to it, before updating its content
            utils::cmdBufferMemoryBarrier
            (
                Unwrap(cmd),
                static_cast<VkBuffer>(buffer->GetNativeHandle()),
                vk::PipelineStageFlagBits2::eVertexShader | vk::PipelineStageFlagBits2::eFragmentShader | vk::PipelineStageFlagBits2::eComputeShader,
                vk::PipelineStageFlagBits2::eTransfer
            );
        }
        
        return result;
    }

    void VulkanRendererAPI::DispatchCompute(VanKComputePass* computePass, uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ)
    {
        // Execute the compute shader
        // The workgroup is set to 256, and we only have 3 vertex to deal with, so one group is enough
        commandBuffers[currentFrame].dispatch(groupCountX, groupCountY, groupCountZ);
    }

    void VulkanRendererAPI::EndComputePass(VanKComputePass* computePass)
    {
        if (computePass->VanKVertexBuffer != nullptr)
        {
            // Add barrier to make sure the compute shader is finished before the vertex buffer is used
            utils::cmdBufferMemoryBarrier
            (
                Unwrap(computePass->VanKCommandBuffer),
                static_cast<VkBuffer>(computePass->VanKVertexBuffer->GetNativeHandle()),
                vk::PipelineStageFlagBits2::eComputeShader,
                vk::PipelineStageFlagBits2::eVertexShader
            );
        }
        
        delete computePass;
    }

    void VulkanRendererAPI::BindPipeline(VanKCommandBuffer cmd, VanKPipelineBindPoint pipelineBindPoint, VanKPipeLine pipeline)
    {
        auto it = m_PipelineResources.find(Unwrap(pipeline));
        if (it == m_PipelineResources.end())
        {
            VK_CORE_ERROR("BindPipeline: pipeline not found in resources");
            return;
        }

        vk::Pipeline pipelineToBind = it->second.pipeline;
        vk::PipelineLayout layoutToBind = it->second.layout;

        if (!pipelineToBind || !layoutToBind)
        {
            VK_CORE_ERROR("BindPipeline: pipeline or layout is VK_NULL_HANDLE");
            return;
        }

        vk::PipelineBindPoint vkBindPoint = (pipelineBindPoint == VanKPipelineBindPoint::Graphics) ? vk::PipelineBindPoint::eGraphics : vk::PipelineBindPoint::eCompute;

        Unwrap(cmd).bindPipeline(vkBindPoint, pipelineToBind);

        // Update current pipeline layout for push descriptors / push constants
        if (pipelineBindPoint == VanKPipelineBindPoint::Graphics)
            m_currentGraphicPipelineLayout = layoutToBind;
        else
            m_currentComputePipelineLayout = layoutToBind;
    }

    void VulkanRendererAPI::BindUniformBuffer(VanKCommandBuffer cmd, VanKPipelineBindPoint bindPoint, UniformBuffer* buffer, uint32_t set, uint32_t binding, uint32_t arrayElement)
    {
        vk::PipelineLayout layout = VK_NULL_HANDLE;

        if (bindPoint == VanKPipelineBindPoint::Graphics) layout = m_currentGraphicPipelineLayout;
        if (bindPoint == VanKPipelineBindPoint::Compute) layout = m_currentComputePipelineLayout;
    
        VulkanUniformBuffer* vulkanUBO = dynamic_cast<VulkanUniformBuffer*>(buffer);
        if (!vulkanUBO)
        {
            return;
        }

        const utils::Buffer& vkBuffer = vulkanUBO->GetBuffer();
    
        // Setting up push descriptor information, we could choose dynamically the buffer to work on
        const vk::DescriptorBufferInfo bufferInfo = { .buffer = vkBuffer.buffer, .offset = 0, .range = vk::WholeSize };

        std::array<vk::WriteDescriptorSet, 1> writeDescriptorSets;
        writeDescriptorSets[0] = vk::WriteDescriptorSet{};
        writeDescriptorSets[0].dstSet = nullptr;
        writeDescriptorSets[0].dstBinding = binding;
        writeDescriptorSets[0].dstArrayElement = arrayElement;
        writeDescriptorSets[0].descriptorCount = 1;
        writeDescriptorSets[0].descriptorType = vk::DescriptorType::eUniformBuffer;
        writeDescriptorSets[0].pBufferInfo = &bufferInfo;

        vk::ShaderStageFlags stage_flags;

        if (bindPoint == VanKPipelineBindPoint::Compute)
        {
            stage_flags = vk::ShaderStageFlagBits::eCompute;
        }
        else
        {
            stage_flags = vk::ShaderStageFlagBits::eAllGraphics;
        }
        
        // Push layout information with updated data
        const vk::PushDescriptorSetInfoKHR pushDescriptorSetInfo
        {
            .stageFlags = stage_flags,
            .layout = layout,
            .set = set, // <--- Second set layout(set=1, binding=...) in the fragment shader
            .descriptorWriteCount = writeDescriptorSets.size(),
            .pDescriptorWrites = writeDescriptorSets.data(),
        };
    
        // This is a push descriptor, allowing synchronization and dynamically changing data
        Unwrap(cmd).pushDescriptorSet2(pushDescriptorSetInfo);
    }

    void VulkanRendererAPI::BeginRendering(VanKCommandBuffer cmd, const VanKColorTargetInfo* color_target_info = {}, uint32_t num_color_targets = 0, VanKDepthStencilTargetInfo depth_stencil_target_info = {}, VanKRenderOption render_option = VanK_Render_None)
    {
        DBG_VK_SCOPE(Unwrap(cmd));  // <-- Helps to debug in NSight

        m_renderOption = render_option;
        
        if (render_option == VanK_Render_None)
        {
            // Before starting rendering, transition the images to the appropriate layouts
            // Transition the multisampled color image to COLOR_ATTACHMENT_OPTIMAL
            transition_image_layout_custom
            (
                colorImage,
                vk::ImageLayout::eUndefined,
                vk::ImageLayout::eColorAttachmentOptimal,
                {},
                vk::AccessFlagBits2::eColorAttachmentWrite,
                vk::PipelineStageFlagBits2::eTopOfPipe,
                vk::PipelineStageFlagBits2::eColorAttachmentOutput,
                vk::ImageAspectFlagBits::eColor
            );

            // Transition the depth image to DEPTH_ATTACHMENT_OPTIMAL
            transition_image_layout_custom
            (
                depthImage,
                vk::ImageLayout::eUndefined,
                vk::ImageLayout::eDepthAttachmentOptimal,
                {},
                vk::AccessFlagBits2::eDepthStencilAttachmentWrite,
                vk::PipelineStageFlagBits2::eTopOfPipe,
                vk::PipelineStageFlagBits2::eEarlyFragmentTests,
                vk::ImageAspectFlagBits::eDepth
            );

            // 3) Bootstrap or re-transition resolve target (sceneImage) for FIRST pass
            transition_image_layout_custom
            (
                sceneImage,
                sceneImageInitialized ? vk::ImageLayout::eShaderReadOnlyOptimal : vk::ImageLayout::eUndefined,
                vk::ImageLayout::eColorAttachmentOptimal,
                sceneImageInitialized ? vk::AccessFlags2(vk::AccessFlagBits2::eShaderRead) : vk::AccessFlags2{},
                vk::AccessFlags2(vk::AccessFlagBits2::eColorAttachmentWrite),
                sceneImageInitialized ? vk::PipelineStageFlagBits2::eFragmentShader : vk::PipelineStageFlagBits2::eTopOfPipe,
                vk::PipelineStageFlagBits2::eColorAttachmentOutput,
                vk::ImageAspectFlagBits::eColor
            );
        
            // First pass: render scene into MSAA color with resolve to single-sample sceneImage
            vk::ClearValue clearColor = vk::ClearColorValue(0.0f, 0.0f, 0.0f, 1.0f);
            vk::ClearValue clearDepth = vk::ClearDepthStencilValue(1.0f, 0);

            // Color attachment (multisampled) with resolve attachment
            vk::RenderingAttachmentInfo colorAttachment =
            {
                .imageView = colorImageView,
                .imageLayout = vk::ImageLayout::eColorAttachmentOptimal,
                .resolveMode = vk::ResolveModeFlagBits::eAverage,
                .resolveImageView = sceneImageView,
                .resolveImageLayout = vk::ImageLayout::eColorAttachmentOptimal,
                .loadOp = vk::AttachmentLoadOp::eClear,
                .storeOp = vk::AttachmentStoreOp::eStore,
                .clearValue = clearColor
            };

            // Depth attachment
            vk::RenderingAttachmentInfo depthAttachment =
            {
                .imageView = depthImageView,
                .imageLayout = vk::ImageLayout::eDepthAttachmentOptimal,
                .loadOp = vk::AttachmentLoadOp::eClear,
                .storeOp = vk::AttachmentStoreOp::eDontCare,
                .clearValue = clearDepth
            };

            vk::RenderingInfo renderingInfo =
            {
                .renderArea = {.offset = {0, 0}, .extent = viewport},
                .layerCount = 1,
                .colorAttachmentCount = 1,
                .pColorAttachments = &colorAttachment,
                .pDepthAttachment = &depthAttachment
            };
        
            Unwrap(cmd).beginRendering(renderingInfo);
        }
        
        if (render_option == VanK_Render_ImGui || render_option == VanK_Render_Swapchain)
        {
            // Transition the swapchain image to COLOR_ATTACHMENT_OPTIMAL
            transition_image_layout
            (
                currentImageIndex,
                vk::ImageLayout::eUndefined,
                vk::ImageLayout::eColorAttachmentOptimal,
                {}, // srcAccessMask (no need to wait for previous operations)
                vk::AccessFlagBits2::eColorAttachmentWrite, // dstAccessMask
                vk::PipelineStageFlagBits2::eTopOfPipe, // srcStage
                vk::PipelineStageFlagBits2::eColorAttachmentOutput // dstStage
            );
        
            // Transition sceneImage -> SHADER_READ_ONLY_OPTIMAL for sampling in ImGui
            transition_image_layout_custom(
                sceneImage,
                vk::ImageLayout::eColorAttachmentOptimal,
                vk::ImageLayout::eShaderReadOnlyOptimal,
                vk::AccessFlags2(vk::AccessFlagBits2::eColorAttachmentWrite),
                vk::AccessFlags2(vk::AccessFlagBits2::eShaderRead),
                vk::PipelineStageFlagBits2::eColorAttachmentOutput,
                vk::PipelineStageFlagBits2::eFragmentShader,
                vk::ImageAspectFlagBits::eColor
                );
            sceneImageInitialized = true;

            // Second pass: draw ImGui to swapchain image
            vk::RenderingAttachmentInfo swapColorAttachment =
            {
                .imageView = swapChainImageViews[currentImageIndex],
                .imageLayout = vk::ImageLayout::eColorAttachmentOptimal,
                .loadOp = vk::AttachmentLoadOp::eClear,
                .storeOp = vk::AttachmentStoreOp::eStore
            };
        
            vk::RenderingInfo renderingInfo2 =
            {
                .renderArea = {.offset = {0, 0}, .extent = swapChainExtent},
                .layerCount = 1,
                .colorAttachmentCount = 1,
                .pColorAttachments = &swapColorAttachment
            };

            Unwrap(cmd).beginRendering(renderingInfo2);
        }
    }

    void VulkanRendererAPI::SetViewport(VanKCommandBuffer cmd, uint32_t viewportCount, VanKViewport viewport)
    {
        vk::Viewport vkViewport{ viewport.x, viewport.y, (float)viewport.width, (float)viewport.height, viewport.minDepth, viewport.maxDepth };

        // Wrap the single viewport in an ArrayProxy (RAII-friendly)
        std::vector<vk::Viewport> viewports(viewportCount, vkViewport);
        
        Unwrap(cmd).setViewportWithCount(viewports);
    }

    void VulkanRendererAPI::SetScissor(VanKCommandBuffer cmd, uint32_t scissorCount, VankRect scissor)
    {
        vk::Rect2D vkScissor( vk::Offset2D(scissor.x, scissor.y), {scissor.width, scissor.height} );

        // Wrap the single scissor in an ArrayProxy (RAII-friendly)
        std::vector<vk::Rect2D> scissors(scissorCount, vkScissor);
        
        Unwrap(cmd).setScissorWithCount(scissors);
    }

    void VulkanRendererAPI::BindVertexBuffer(VanKCommandBuffer cmd, uint32_t first_slot, const VertexBuffer& vertexBuffer, uint32_t num_bindings)
    {
        if (num_bindings < 1)
        {
            VK_CORE_ERROR("num_bindings is less then 1 check: {}", num_bindings);
        }
        
        // Cast to VulkanVertexBuffer
        const VulkanVertexBuffer* vulkanVB = dynamic_cast<const VulkanVertexBuffer*>(&vertexBuffer);
        if (!vulkanVB)
        {
            // Handle error: wrong buffer type
            return; 
        }

        const utils::Buffer& vkBuffer = vulkanVB->GetBuffer();
        std::vector<vk::Buffer> buffers(num_bindings, vkBuffer.buffer); // The actual VkBuffer

        std::vector<vk::DeviceSize> offsets(num_bindings, 0);
        
        Unwrap(cmd).bindVertexBuffers(first_slot, buffers, offsets);
    }

    void VulkanRendererAPI::BindIndexBuffer(VanKCommandBuffer cmd, const IndexBuffer& indexBuffer, VanKIndexElementSize elementSize)
    {
        // Cast to VulkanVertexBuffer
        const VulkanIndexBuffer* vulkanIB = dynamic_cast<const VulkanIndexBuffer*>(&indexBuffer);
        if (!vulkanIB)
        {
            // Handle error: wrong buffer type
            return;
        }

        const utils::Buffer& vkBuffer = vulkanIB->GetBuffer();
        vk::Buffer buffer = vkBuffer.buffer; // The actual VkBuffer

        vk::IndexType vkIndexType = vk::IndexType::eNoneKHR;
        switch (elementSize)
        {
        case VanKIndexElementSize::Uint16: vkIndexType = vk::IndexType::eUint16; break;
        case VanKIndexElementSize::Uint32: vkIndexType = vk::IndexType::eUint32; break;
        }

        Unwrap(cmd).bindIndexBuffer(buffer, 0, vkIndexType);
    }

    void VulkanRendererAPI::Draw(VanKCommandBuffer cmd, uint32_t vertexCount, uint32_t instanceCount, uint32_t firstVertex, uint32_t firstInstance)
    {
        Unwrap(cmd).draw(vertexCount, instanceCount, firstVertex, firstInstance);
    }

    void VulkanRendererAPI::DrawIndexed(VanKCommandBuffer cmd, uint32_t indexCount, uint32_t instanceCount, uint32_t firstIndex, int32_t vertexOffset, uint32_t firstInstance)
    {
        Unwrap(cmd).drawIndexed(indexCount, instanceCount, firstIndex, vertexOffset, firstInstance);
    }

    void VulkanRendererAPI::DrawIndexedIndirectCount(VanKCommandBuffer cmd, IndirectBuffer& indirectBuffer, uint32_t indirectBufferOffset, IndirectBuffer& countBuffer, uint32_t countBufferOffset, uint32_t maxDrawCount, uint32_t stride)
    {
        if (stride < sizeof(vk::DrawIndexedIndirectCommand))
            throw std::runtime_error("drawIndexedIndirectCount: stride too small");
        
        // Cast to VulkanVertexBuffer
        const VulkanIndirectBuffer* vulkanIB = dynamic_cast<const VulkanIndirectBuffer*>(&indirectBuffer);
        if (!vulkanIB)
            throw std::runtime_error("DrawIndexedIndirectCount: indirectBuffer is not a VulkanIndirectBuffer");

        const utils::Buffer& vkBuffer = vulkanIB->GetBuffer();
        vk::Buffer bufferIndirect = vkBuffer.buffer; // The actual VkBuffer

        // Cast to VulkanVertexBuffer
        const VulkanIndirectBuffer* vulkanCB = dynamic_cast<const VulkanIndirectBuffer*>(&countBuffer);
        if (!vulkanCB)
            throw std::runtime_error("DrawIndexedIndirectCount: countBuffer is not a VulkanIndirectBuffer");

        const utils::Buffer& vkBufferCount = vulkanCB->GetBuffer();
        vk::Buffer bufferCount = vkBufferCount.buffer; // The actual VkBuffer
        
        Unwrap(cmd).drawIndexedIndirectCount(bufferIndirect, indirectBufferOffset, bufferCount, countBufferOffset, maxDrawCount, stride);
    }

   
            /*transition_image_layout
            (
                currentImageIndex,
                vk::ImageLayout::eUndefined,
                vk::ImageLayout::eColorAttachmentOptimal,
                {}, // srcAccessMask (no need to wait for previous operations)
                vk::AccessFlagBits2::eColorAttachmentWrite, // dstAccessMask
                vk::PipelineStageFlagBits2::eTopOfPipe, // srcStage
                vk::PipelineStageFlagBits2::eColorAttachmentOutput // dstStage
            );
        
            // Transition sceneImage -> SHADER_READ_ONLY_OPTIMAL for sampling in ImGui
            transition_image_layout_custom(
                sceneImage,
                vk::ImageLayout::eColorAttachmentOptimal,
                vk::ImageLayout::eShaderReadOnlyOptimal,
                vk::AccessFlags2(vk::AccessFlagBits2::eColorAttachmentWrite),
                vk::AccessFlags2(vk::AccessFlagBits2::eShaderRead),
                vk::PipelineStageFlagBits2::eColorAttachmentOutput,
                vk::PipelineStageFlagBits2::eFragmentShader,
                vk::ImageAspectFlagBits::eColor);
            sceneImageInitialized = true;*/

    void VulkanRendererAPI::EndRendering(VanKCommandBuffer cmd)
    {
        if (m_renderOption == VanK_Render_None)
        {
            Unwrap(cmd).endRendering();
            
            return;
        }
        
        if (m_renderOption == VanK_Render_ImGui)
        {
            ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), *Unwrap(cmd));

            Unwrap(cmd).endRendering();
            
            // Transition the swapchain image to PRESENT_SRC
            transition_image_layout
            (
                currentImageIndex,
                vk::ImageLayout::eColorAttachmentOptimal,
                vk::ImageLayout::ePresentSrcKHR,
                vk::AccessFlagBits2::eColorAttachmentWrite, // srcAccessMask
                {}, // dstAccessMask
                vk::PipelineStageFlagBits2::eColorAttachmentOutput, // srcStage
                vk::PipelineStageFlagBits2::eBottomOfPipe // dstStage
            );

            return;
        }
        
        if (m_renderOption == VanK_Render_Swapchain)
        {
            Unwrap(cmd).endRendering();
            
            // Transition images before blit
            transition_image_layout_custom
            (
                sceneImage,
                vk::ImageLayout::eShaderReadOnlyOptimal,
                vk::ImageLayout::eTransferSrcOptimal,
                vk::AccessFlagBits2::eShaderRead,
                vk::AccessFlagBits2::eTransferRead,
                vk::PipelineStageFlagBits2::eFragmentShader,
                vk::PipelineStageFlagBits2::eTransfer,
                vk::ImageAspectFlagBits::eColor
            );

            transition_image_layout
            (
                currentImageIndex,
                vk::ImageLayout::eColorAttachmentOptimal,
                vk::ImageLayout::eTransferDstOptimal,
                {},
                vk::AccessFlagBits2::eTransferWrite,
                vk::PipelineStageFlagBits2::eBottomOfPipe,
                vk::PipelineStageFlagBits2::eTransfer
            );
            
            vk::ImageSubresourceLayers subresource{};
            subresource.aspectMask = vk::ImageAspectFlagBits::eColor;
            subresource.baseArrayLayer = 0;
            subresource.layerCount = 1;
            subresource.mipLevel = 0;
            
            vk::ImageBlit2 blitRegion{};
            blitRegion.srcSubresource = subresource;
            blitRegion.srcOffsets[0] = {0, 0, 0};
            blitRegion.srcOffsets[1] = { static_cast<int32_t>(viewport.width), static_cast<int32_t>(viewport.height), 1 };
            blitRegion.dstSubresource = subresource;
            blitRegion.dstOffsets[0] = {0, 0, 0};
            blitRegion.dstOffsets[1] = { static_cast<int32_t>(swapChainExtent.width), static_cast<int32_t>(swapChainExtent.height), 1 };
            
            vk::BlitImageInfo2 blitInfo{};
            blitInfo.srcImage = sceneImage;
            blitInfo.srcImageLayout = vk::ImageLayout::eTransferSrcOptimal;
            blitInfo.dstImage = swapChainImages[currentImageIndex];
            blitInfo.dstImageLayout = vk::ImageLayout::eTransferDstOptimal;
            blitInfo.regionCount = 1;
            blitInfo.pRegions = &blitRegion;
            blitInfo.filter = vk::Filter::eNearest;
            
            Unwrap(cmd).blitImage2(blitInfo);

            // After rendering, transition the images to appropriate layouts

            // Transition images before blit
            transition_image_layout_custom(
                sceneImage,
                vk::ImageLayout::eTransferSrcOptimal,
                vk::ImageLayout::eShaderReadOnlyOptimal,
                vk::AccessFlagBits2::eTransferRead,
                vk::AccessFlagBits2::eColorAttachmentWrite,
                vk::PipelineStageFlagBits2::eTransfer,
                vk::PipelineStageFlagBits2::eColorAttachmentOutput,
                vk::ImageAspectFlagBits::eColor
            );
        
            // Transition the swapchain image to PRESENT_SRC
            transition_image_layout
            (
                currentImageIndex,
                vk::ImageLayout::eTransferDstOptimal,
                vk::ImageLayout::ePresentSrcKHR,
                vk::AccessFlagBits2::eTransferWrite, // srcAccessMask
                {}, // dstAccessMask
                vk::PipelineStageFlagBits2::eTransfer, // srcStage
                vk::PipelineStageFlagBits2::eBottomOfPipe // dstStage
            );
        }
    }

    void VulkanRendererAPI::BindFragmentSamplers(VanKCommandBuffer cmd, uint32_t firstSlot, const TextureSamplerBinding* samplers, uint32_t num_bindings)
    {
        //might have to change this i tryed putting updatedescriptor set here but idk do i need this in bindless ?
        // TextureSamplerBinding is empty check rendererapi strcut
        /*-- 
         * Binding the resources passed to the shader, using the descriptor set (holds the texture) 
         * There are two descriptor layouts, one for the texture and one for the scene information,
         * but only the texture is a set, the scene information is a push descriptor.
        -*/
        vk::DescriptorSet rawDescriptorSet = *descriptorSets[0];
        vk::BindDescriptorSetsInfoKHR bindDescriptorSetsInfo =
        {
            .stageFlags = vk::ShaderStageFlagBits::eAllGraphics,
            .layout = m_currentGraphicPipelineLayout,
            .firstSet = 0,
            .descriptorSetCount = 1,
            .pDescriptorSets = &rawDescriptorSet,
        };
        Unwrap(cmd).bindDescriptorSets2(bindDescriptorSetsInfo);
    }

    void VulkanRendererAPI::waitForGraphicsQueueIdle()
    {
        queue.waitIdle();
    }

    void VulkanRendererAPI::createCommandPool()
    {
        vk::CommandPoolCreateInfo poolInfo{
            .flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer,
            .queueFamilyIndex = queueIndex
        };
        commandPool = vk::raii::CommandPool(device, poolInfo);
        DBG_VK_NAME(*commandPool);
    }

    void VulkanRendererAPI::createSceneResources()
    {
        vk::Format colorFormat = swapChainSurfaceFormat.format;
        // single-sampled and SAMPLED
        createImage(
            viewport.width, viewport.height,
            1, vk::SampleCountFlagBits::e1, colorFormat,
            vk::ImageTiling::eOptimal,
            vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferSrc,
            vk::MemoryPropertyFlagBits::eDeviceLocal,
            sceneImage, sceneImageMemory);

        DBG_VK_NAME(*sceneImage);
        
        sceneImageView = createImageView(sceneImage, colorFormat, vk::ImageAspectFlagBits::eColor, 1);
        DBG_VK_NAME(*sceneImageView);
    }

    void VulkanRendererAPI::createColorResources()
    {
        vk::Format colorFormat = swapChainSurfaceFormat.format;

        createImage(viewport.width, viewport.height, 1, msaaSamples, colorFormat,
                    vk::ImageTiling::eOptimal,
                    vk::ImageUsageFlagBits::eTransientAttachment | vk::ImageUsageFlagBits::eColorAttachment,
                    vk::MemoryPropertyFlagBits::eDeviceLocal, colorImage, colorImageMemory);
        DBG_VK_NAME(*colorImage);
        
        colorImageView = createImageView(colorImage, colorFormat, vk::ImageAspectFlagBits::eColor, 1);
        DBG_VK_NAME(*colorImageView);
    }

    void VulkanRendererAPI::createDepthResources()
    {
        vk::Format depthFormat = findDepthFormat();

        createImage(viewport.width, viewport.height, 1, msaaSamples, depthFormat,
                    vk::ImageTiling::eOptimal,
                    vk::ImageUsageFlagBits::eDepthStencilAttachment, vk::MemoryPropertyFlagBits::eDeviceLocal,
                    depthImage, depthImageMemory);
        DBG_VK_NAME(*depthImage);
        
        depthImageView = createImageView(depthImage, depthFormat, vk::ImageAspectFlagBits::eDepth, 1);
        DBG_VK_NAME(*depthImageView);
    }

    vk::Format VulkanRendererAPI::findSupportedFormat(const std::vector<vk::Format>& candidates, vk::ImageTiling tiling,
                                             vk::FormatFeatureFlags features) const
    {
        for (const auto format : candidates)
        {
            vk::FormatProperties props = physicalDevice.getFormatProperties(format);

            if (tiling == vk::ImageTiling::eLinear && (props.linearTilingFeatures & features) == features)
            {
                return format;
            }
            if (tiling == vk::ImageTiling::eOptimal && (props.optimalTilingFeatures & features) == features)
            {
                return format;
            }
        }

        throw std::runtime_error("failed to find supported format!");
    }

    [[nodiscard]] vk::Format VulkanRendererAPI::findDepthFormat() const
    {
        return findSupportedFormat(
            {vk::Format::eD32Sfloat, vk::Format::eD32SfloatS8Uint, vk::Format::eD24UnormS8Uint},
            vk::ImageTiling::eOptimal,
            vk::FormatFeatureFlagBits::eDepthStencilAttachment
        );
    }

    bool VulkanRendererAPI::hasStencilComponent(vk::Format format)
    {
        return format == vk::Format::eD32SfloatS8Uint || format == vk::Format::eD24UnormS8Uint;
    }

    void VulkanRendererAPI::createTexture()
    {
        ktxTexture* kTexture;
        KTX_error_code result = ktxTexture_CreateFromNamedFile(
            TEXTURE_PATH.c_str(),
            KTX_TEXTURE_CREATE_LOAD_IMAGE_DATA_BIT,
            &kTexture);

        if (result != KTX_SUCCESS)
        {
            throw std::runtime_error("failed to load ktx texture image!");
        }

        // Get texture dimensions and data
        uint32_t texWidth = kTexture->baseWidth;
        uint32_t texHeight = kTexture->baseHeight;
        /*ktx_size_t imageSize = ktxTexture_GetImageSize(kTexture, 0);*/
        ktx_size_t imageSize = ktxTexture_GetDataSize(kTexture); // total size of all mip levels
        ktx_uint8_t* ktxTextureData = ktxTexture_GetData(kTexture);

        // Create staging buffer
        utils::Buffer stagingBuffer = allocator.createStagingBuffer(std::span(ktxTextureData, imageSize));

        // Determine the Vulkan format from KTX format
        vk::Format textureFormat;

        // Check if the KTX texture has a format
        if (kTexture->classId == ktxTexture2_c)
        {
            // For KTX2 files, we can get the format directly
            auto* ktx2 = reinterpret_cast<ktxTexture2*>(kTexture);
            textureFormat = static_cast<vk::Format>(ktx2->vkFormat);
            if (textureFormat == vk::Format::eUndefined)
            {
                // If the format is undefined, fall back to a reasonable default
                textureFormat = vk::Format::eR8G8B8A8Unorm;
            }
        }
        else
        {
            // For KTX1 files or if we can't determine the format, use a reasonable default
            textureFormat = vk::Format::eR8G8B8A8Unorm;
        }

        textureImageFormat = textureFormat;

        if (kTexture->numLevels > 1)
        {
            mipLevels = kTexture->numLevels;
            // Create the texture image
            createImage(texWidth, texHeight, mipLevels, vk::SampleCountFlagBits::e1, textureFormat,
                        vk::ImageTiling::eOptimal,
                        vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eTransferDst |
                        vk::ImageUsageFlagBits::eSampled, vk::MemoryPropertyFlagBits::eDeviceLocal, textureImage,
                        textureImageMemory);

            DBG_VK_NAME(*textureImage);

            // Copy data from staging buffer to texture image
            transitionImageLayout(textureImage, vk::ImageLayout::eUndefined, vk::ImageLayout::eTransferDstOptimal,
                                  mipLevels);

            std::unique_ptr<vk::raii::CommandBuffer> commandBuffer = VulkanRendererAPI::beginSingleTimeCommands();
            // Copy each mip level
            for (uint32_t i = 0; i < mipLevels; i++)
            {
                ktx_size_t offset;
                KTX_error_code result = ktxTexture_GetImageOffset(kTexture, i, 0, 0, &offset);
                uint32_t mipWidth = std::max(1u, texWidth >> i);
                uint32_t mipHeight = std::max(1u, texHeight >> i);
                
                allocator.copyBufferToImage(commandBuffer, stagingBuffer,textureImage, static_cast<uint32_t>(mipWidth), static_cast<uint32_t>(mipHeight), offset, i);
            }
            endSingleTimeCommands(*commandBuffer);
        
            transitionImageLayout(textureImage, vk::ImageLayout::eTransferDstOptimal,
                                  vk::ImageLayout::eShaderReadOnlyOptimal, mipLevels);
        }
        else
        {
            std::unique_ptr<vk::raii::CommandBuffer> commandBuffer = VulkanRendererAPI::beginSingleTimeCommands();
            mipLevels = static_cast<uint32_t>(std::floor(std::log2(std::max(texWidth, texHeight)))) + 1;
            // Create the texture image
            createImage(texWidth, texHeight, mipLevels, vk::SampleCountFlagBits::e1, textureFormat,
                        vk::ImageTiling::eOptimal,
                        vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eTransferDst |
                        vk::ImageUsageFlagBits::eSampled, vk::MemoryPropertyFlagBits::eDeviceLocal, textureImage,
                        textureImageMemory);

            DBG_VK_NAME(*textureImage);

            // Copy data from staging buffer to texture image
            transitionImageLayout(textureImage, vk::ImageLayout::eUndefined, vk::ImageLayout::eTransferDstOptimal, mipLevels);
            
            allocator.copyBufferToImage(commandBuffer, stagingBuffer, textureImage, static_cast<uint32_t>(texWidth), static_cast<uint32_t>(texHeight));
            endSingleTimeCommands(*commandBuffer);
            generateMipmaps(textureImage, textureFormat, texWidth, texHeight, mipLevels);
        }

        textureImageView = createImageView(textureImage, textureImageFormat, vk::ImageAspectFlagBits::eColor, mipLevels);
        DBG_VK_NAME(*textureImageView);
        /*utils::ImageResource resource{};
        resource.image = *textureImage;

        images.emplace_back();*/
        
        allocator.freeStagingBuffers();
        // Cleanup KTX resources
        ktxTexture_Destroy(kTexture);
    }

    void VulkanRendererAPI::generateMipmaps(vk::raii::Image& image, vk::Format imageFormat, int32_t texWidth, int32_t texHeight,
                                   uint32_t mipLevels)
    {
        // Check if image format supports linear blit-ing
        vk::FormatProperties formatProperties = physicalDevice.getFormatProperties(imageFormat);

        if (!(formatProperties.optimalTilingFeatures & vk::FormatFeatureFlagBits::eSampledImageFilterLinear))
        {
            throw std::runtime_error("texture image format does not support linear blitting!");
        }

        std::unique_ptr<vk::raii::CommandBuffer> commandBuffer = beginSingleTimeCommands();

        vk::ImageMemoryBarrier barrier = {
            .srcAccessMask = vk::AccessFlagBits::eTransferWrite, .dstAccessMask = vk::AccessFlagBits::eTransferRead,
            .oldLayout = vk::ImageLayout::eTransferDstOptimal, .newLayout = vk::ImageLayout::eTransferSrcOptimal,
            .srcQueueFamilyIndex = vk::QueueFamilyIgnored, .dstQueueFamilyIndex = vk::QueueFamilyIgnored, .image = image
        };
        barrier.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount = 1;
        barrier.subresourceRange.levelCount = 1;

        int32_t mipWidth = texWidth;
        int32_t mipHeight = texHeight;

        for (uint32_t i = 1; i < mipLevels; i++)
        {
            barrier.subresourceRange.baseMipLevel = i - 1;
            barrier.oldLayout = vk::ImageLayout::eTransferDstOptimal;
            barrier.newLayout = vk::ImageLayout::eTransferSrcOptimal;
            barrier.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
            barrier.dstAccessMask = vk::AccessFlagBits::eTransferRead;

            commandBuffer->pipelineBarrier(vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eTransfer,
                                           {}, {}, {}, barrier);

            vk::ArrayWrapper1D<vk::Offset3D, 2> offsets, dstOffsets;
            offsets[0] = vk::Offset3D(0, 0, 0);
            offsets[1] = vk::Offset3D(mipWidth, mipHeight, 1);
            dstOffsets[0] = vk::Offset3D(0, 0, 0);
            dstOffsets[1] = vk::Offset3D(mipWidth > 1 ? mipWidth / 2 : 1, mipHeight > 1 ? mipHeight / 2 : 1, 1);
            vk::ImageBlit blit = {
                .srcSubresource = {}, .srcOffsets = offsets,
                .dstSubresource = {}, .dstOffsets = dstOffsets
            };
            blit.srcSubresource = vk::ImageSubresourceLayers(vk::ImageAspectFlagBits::eColor, i - 1, 0, 1);
            blit.dstSubresource = vk::ImageSubresourceLayers(vk::ImageAspectFlagBits::eColor, i, 0, 1);

            commandBuffer->blitImage(image, vk::ImageLayout::eTransferSrcOptimal, image,
                                     vk::ImageLayout::eTransferDstOptimal, {blit}, vk::Filter::eLinear);

            barrier.oldLayout = vk::ImageLayout::eTransferSrcOptimal;
            barrier.newLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
            barrier.srcAccessMask = vk::AccessFlagBits::eTransferRead;
            barrier.dstAccessMask = vk::AccessFlagBits::eShaderRead;

            commandBuffer->pipelineBarrier(vk::PipelineStageFlagBits::eTransfer,
                                           vk::PipelineStageFlagBits::eFragmentShader, {}, {}, {}, barrier);

            if (mipWidth > 1) mipWidth /= 2;
            if (mipHeight > 1) mipHeight /= 2;
        }

        barrier.subresourceRange.baseMipLevel = mipLevels - 1;
        barrier.oldLayout = vk::ImageLayout::eTransferDstOptimal;
        barrier.newLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
        barrier.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
        barrier.dstAccessMask = vk::AccessFlagBits::eShaderRead;

        commandBuffer->pipelineBarrier(vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eFragmentShader,
                                       {}, {}, {}, barrier);

        endSingleTimeCommands(*commandBuffer);
    }

    vk::SampleCountFlagBits VulkanRendererAPI::getMaxUsableSampleCount()
    {
        vk::PhysicalDeviceProperties physicalDeviceProperties = physicalDevice.getProperties();

        vk::SampleCountFlags counts = physicalDeviceProperties.limits.framebufferColorSampleCounts &
            physicalDeviceProperties.limits.framebufferDepthSampleCounts;
        if (counts & vk::SampleCountFlagBits::e64) { return vk::SampleCountFlagBits::e64; }
        if (counts & vk::SampleCountFlagBits::e32) { return vk::SampleCountFlagBits::e32; }
        if (counts & vk::SampleCountFlagBits::e16) { return vk::SampleCountFlagBits::e16; }
        if (counts & vk::SampleCountFlagBits::e8) { return vk::SampleCountFlagBits::e8; }
        if (counts & vk::SampleCountFlagBits::e4) { return vk::SampleCountFlagBits::e4; }
        if (counts & vk::SampleCountFlagBits::e2) { return vk::SampleCountFlagBits::e2; }

        return vk::SampleCountFlagBits::e1;
    }

    void VulkanRendererAPI::createTextureSampler()
    {
        vk::PhysicalDeviceProperties properties = physicalDevice.getProperties();
        vk::SamplerCreateInfo samplerInfo
        {
            .magFilter = vk::Filter::eLinear,
            .minFilter = vk::Filter::eLinear,
            .mipmapMode = vk::SamplerMipmapMode::eLinear,
            .addressModeU = vk::SamplerAddressMode::eRepeat,
            .addressModeV = vk::SamplerAddressMode::eRepeat,
            .addressModeW = vk::SamplerAddressMode::eRepeat,
            .mipLodBias = 0.0f,
            .anisotropyEnable = vk::True,
            .maxAnisotropy = properties.limits.maxSamplerAnisotropy,
            .compareEnable = vk::False,
            .compareOp = vk::CompareOp::eAlways,
            .minLod = 0.0f, // the higher this is the lower the resolution 0 is max res
            .maxLod = VK_LOD_CLAMP_NONE
        };
        textureSampler = vk::raii::Sampler(device, samplerInfo);
    }

    vk::raii::ImageView VulkanRendererAPI::createImageView(vk::raii::Image& image, vk::Format format,
                                                  vk::ImageAspectFlags aspectFlags,
                                                  uint32_t mipLevels)
    {
        vk::ImageViewCreateInfo viewInfo
        {
            .image = image,
            .viewType = vk::ImageViewType::e2D,
            .format = format,
            .subresourceRange = {aspectFlags, 0, mipLevels, 0, 1}
        };
        return vk::raii::ImageView(device, viewInfo);
    }

    void VulkanRendererAPI::createImage(uint32_t width, uint32_t height, uint32_t mipLevels, vk::SampleCountFlagBits numSamples,
                               vk::Format format, vk::ImageTiling tiling,
                               vk::ImageUsageFlags usage, vk::MemoryPropertyFlags properties, vk::raii::Image& image,
                               vk::raii::DeviceMemory& imageMemory)
    {
        vk::ImageCreateInfo imageInfo
        {
            .imageType = vk::ImageType::e2D, .format = format,
            .extent = {width, height, 1}, .mipLevels = mipLevels, .arrayLayers = 1,
            .samples = numSamples, .tiling = tiling,
            .usage = usage, .sharingMode = vk::SharingMode::eExclusive
        };

        image = vk::raii::Image(device, imageInfo);

        vk::MemoryRequirements memRequirements = image.getMemoryRequirements();
        vk::MemoryAllocateInfo allocInfo{
            .allocationSize = memRequirements.size,
            .memoryTypeIndex = findMemoryType(memRequirements.memoryTypeBits, properties)
        };
        imageMemory = vk::raii::DeviceMemory(device, allocInfo);
        image.bindMemory(imageMemory, 0);
    }

    void VulkanRendererAPI::transitionImageLayout(const vk::raii::Image& image, vk::ImageLayout oldLayout, vk::ImageLayout newLayout,
                                         uint32_t mipLevels)
    {
        auto commandBuffer = beginSingleTimeCommands();

        vk::ImageMemoryBarrier barrier{
            .oldLayout = oldLayout, .newLayout = newLayout,
            .image = image,
            .subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, mipLevels, 0, 1}
        };

        vk::PipelineStageFlags sourceStage;
        vk::PipelineStageFlags destinationStage;

        if (oldLayout == vk::ImageLayout::eUndefined && newLayout == vk::ImageLayout::eTransferDstOptimal)
        {
            barrier.srcAccessMask = {};
            barrier.dstAccessMask = vk::AccessFlagBits::eTransferWrite;

            sourceStage = vk::PipelineStageFlagBits::eTopOfPipe;
            destinationStage = vk::PipelineStageFlagBits::eTransfer;
        }
        else if (oldLayout == vk::ImageLayout::eTransferDstOptimal && newLayout ==
            vk::ImageLayout::eShaderReadOnlyOptimal)
        {
            barrier.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
            barrier.dstAccessMask = vk::AccessFlagBits::eShaderRead;

            sourceStage = vk::PipelineStageFlagBits::eTransfer;
            destinationStage = vk::PipelineStageFlagBits::eFragmentShader;
        }
        else
        {
            throw std::invalid_argument("unsupported layout transition!");
        }
        commandBuffer->pipelineBarrier(sourceStage, destinationStage, {}, {}, nullptr, barrier);
        endSingleTimeCommands(*commandBuffer);
    }

    void VulkanRendererAPI::createDescriptorPool()
    {
        {
            // We need MAX_OBJECTS * MAX_FRAMES_IN_FLIGHT descriptor sets
            uint32_t m_maxTextures = 10000;
            const uint32_t safegardSize = 2;
            uint32_t maxDescriptorSets = std::min(1000U, physicalDevice.getProperties().limits.maxDescriptorSetUniformBuffers - safegardSize);
            m_maxTextures = std::min(m_maxTextures, physicalDevice.getProperties().limits.maxDescriptorSetSampledImages - safegardSize);
            std::array poolSize
            {
                vk::DescriptorPoolSize(vk::DescriptorType::eCombinedImageSampler, m_maxTextures)
            };
        
            vk::DescriptorPoolCreateInfo poolInfo
            {
                .flags = vk::DescriptorPoolCreateFlagBits::eUpdateAfterBind | vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet,
                .maxSets = maxDescriptorSets,
                .poolSizeCount = static_cast<uint32_t>(poolSize.size()),
                .pPoolSizes = poolSize.data()
            };
            descriptorPool = vk::raii::DescriptorPool(device, poolInfo);
            DBG_VK_NAME(*descriptorPool);
        }
        // This is the descriptor pool for the ImGui UI, which is used to display the textures and other resources (GBuffers).
        {
            auto deviceProperties = physicalDevice.getProperties();
            // ImGui creates a descriptor set for each single texture. Therefore the pool size must be large enough to hold all textures of all sets.
            uint32_t uiPoolSize = std::min(20U, deviceProperties.limits.maxDescriptorSetSampledImages);
            uint32_t maxDescriptorSets = std::min(uiPoolSize, deviceProperties.limits.maxDescriptorSetUniformBuffers);
            vk::DescriptorPoolSize poolSize = {vk::DescriptorType::eCombinedImageSampler, uiPoolSize};
            vk::DescriptorPoolCreateInfo poolInfo
            {
                .flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet,
                .maxSets = maxDescriptorSets,
                .poolSizeCount = 1,
                .pPoolSizes = &poolSize,
            };
            uiDescriptorPool = vk::raii::DescriptorPool(device, poolInfo);
            DBG_VK_NAME(*uiDescriptorPool);
        }
    }

    void VulkanRendererAPI::createDescriptorSets()
    {
        // First describe the layout of the texture descriptor, what and how many
        {
            uint32_t numTextures = 10000; // We don't need to set the exact number of texture the scene have.

            // In comment, the layout for a storage buffer, which is not used in this sample, but rather a push descriptor (below)
            std::array<vk::DescriptorSetLayoutBinding, 1> layoutBindings{
                    {
                        {
                            .binding = 0,
                            .descriptorType = vk::DescriptorType::eCombinedImageSampler,
                            .descriptorCount = numTextures,
                            .stageFlags = vk::ShaderStageFlagBits::eAllGraphics
                        },
                
                        // This is if we would add another binding for the scene info, but instead we make another set, see below
                        // {.binding = 1, .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, .descriptorCount = 1, .stageFlags = VK_SHADER_STAGE_ALL_GRAPHICS},
                    }
            };

            std::array<vk::DescriptorBindingFlags, 1> flags = {
                // Flags for binding 0 (texture array):
                vk::DescriptorBindingFlagBits::eUpdateAfterBind | // Can update while in use
                vk::DescriptorBindingFlagBits::eUpdateUnusedWhilePending | // Can update unused entries
                vk::DescriptorBindingFlagBits::ePartiallyBound // Not all array elements need to be valid (0,2,3 vs 0,1,2,3)

                // Flags for binding 1 (scene info buffer):
                // VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT  // flags for storage buffer binding
            };
            
            vk::DescriptorSetLayoutBindingFlagsCreateInfo bindingFlags{
                .bindingCount = uint32_t(layoutBindings.size()), // matches our number of bindings
                .pBindingFlags = flags.data(), // the flags for each binding
            };

            vk::DescriptorSetLayoutCreateInfo descriptorSetLayoutInfo{
                .pNext = &bindingFlags,
                .flags = vk::DescriptorSetLayoutCreateFlagBits::eUpdateAfterBindPool,
                // Allows to update the descriptor set after it has been bound
                .bindingCount = uint32_t(layoutBindings.size()),
                .pBindings = layoutBindings.data(),
            };
            descriptorSetLayout = device.createDescriptorSetLayout(descriptorSetLayoutInfo);
            DBG_VK_NAME(*descriptorSetLayout);
            std::vector<vk::DescriptorSetLayout> layouts = { *descriptorSetLayout };
            // Allocate the descriptor set, needed only for larger descriptor sets
            vk::DescriptorSetAllocateInfo allocInfo = {
                .descriptorPool = descriptorPool,
                .descriptorSetCount = 1,
                .pSetLayouts = layouts.data(),
            };
            descriptorSets = device.allocateDescriptorSets(allocInfo);
            DBG_VK_NAME(*descriptorSets.back());
        }

        // Second this is another set which will be pushed
        {
            // This is the scene buffer information
            std::array<vk::DescriptorSetLayoutBinding, 1> layoutBindings{
                {
                    {
                        .binding = 0,
                        .descriptorType = vk::DescriptorType::eUniformBuffer,
                        .descriptorCount = 1,
                        .stageFlags = vk::ShaderStageFlagBits::eAllGraphics | vk::ShaderStageFlagBits::eCompute
                    }
                }
            };
            vk::DescriptorSetLayoutCreateInfo descriptorSetLayoutInfo{
                .flags = vk::DescriptorSetLayoutCreateFlagBits::ePushDescriptor,
                .bindingCount = uint32_t(layoutBindings.size()),
                .pBindings = layoutBindings.data(),
            };
            commonDescriptorSetLayout = device.createDescriptorSetLayout(descriptorSetLayoutInfo);
            DBG_VK_NAME(*commonDescriptorSetLayout);
        }
        updateGraphicsDescriptorSet();
    }

    void VulkanRendererAPI::updateGraphicsDescriptorSet()
    {
        // The sampler used for the texture
        vk::raii::Sampler sampler = m_samplerPool.acquireSampler({
            .magFilter = vk::Filter::eLinear,
            .minFilter = vk::Filter::eLinear,
            .mipmapMode = vk::SamplerMipmapMode::eLinear,
            .addressModeU = vk::SamplerAddressMode::eRepeat,
            .addressModeV = vk::SamplerAddressMode::eRepeat,
            .addressModeW = vk::SamplerAddressMode::eRepeat,
            .maxLod = vk::LodClampNone,
        });
        DBG_VK_NAME(*sampler);
    
        // Prepare imageInfos vector automatically sized to m_image's size
        std::vector<vk::DescriptorImageInfo> imageInfos;
        imageInfos.reserve(1); // reserve for efficiency

        // The image info
        for (size_t i = 0; i < 1; ++i)
        {
            imageInfos.push_back({
                .sampler = sampler,
                .imageView = textureImageView,
                .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
            });
        }

        std::vector<VkDescriptorSet> descHandles;
        for (auto& ds : descriptorSets)
            descHandles.push_back(*ds);

        std::array<vk::WriteDescriptorSet, 1> writeDescriptorSets;
        writeDescriptorSets[0] = vk::WriteDescriptorSet{};
        writeDescriptorSets[0].dstSet = descriptorSets[0]; // single handle
        writeDescriptorSets[0].dstBinding = 0;
        writeDescriptorSets[0].dstArrayElement = 0;
        writeDescriptorSets[0].descriptorCount = static_cast<uint32_t>(imageInfos.size());
        writeDescriptorSets[0].descriptorType = vk::DescriptorType::eCombinedImageSampler;
        writeDescriptorSets[0].pImageInfo = imageInfos.data();

        // This is if the scene info buffer if part of the descriptor set layout (we have it in a separate set/layout)
        // VkDescriptorBufferInfo bufferInfo = {.buffer = m_sceneInfoBuffer.buffer, .offset = 0, .range = VK_WHOLE_SIZE};
        // writeDescriptorSets.push_back({
        //     .sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        //     .dstSet          = m_textureDescriptorSet,  // Not set, this is a push descriptor
        //     .dstBinding      = 1,                       // layout(binding = 1) in the fragment shader
        //     .dstArrayElement = 0,                       // If we were to use an array of images
        //     .descriptorCount = 1,
        //     .descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
        //     .pBufferInfo     = &bufferInfo,
        // });

        /*-- 
         * With the flags set it ACTUALLY allows:
         *  - You can update after binding to a command buffer but before submitting.
         *  - You can update while the descriptor set is bound in another thread.
         *  - You don't invalidate the command buffer when you update.
         *  - Multiple threads can update different descriptors at the same time
         * What it does NOT allow:
         *  - Update while the GPU is actively reading it in a shader
         *  - Skipping proper synchronization between CPU updates and GPU reads
         *  - Simultaneous updates to the same descriptor
         * Since this is called before starting to render, we don't need to worry about the first two.
        -*/
        device.updateDescriptorSets(writeDescriptorSets, {});
    }

    std::unique_ptr<vk::raii::CommandBuffer> VulkanRendererAPI::beginSingleTimeCommands()
    {
        vk::CommandBufferAllocateInfo allocInfo{
            .commandPool = *commandPool,
            .level = vk::CommandBufferLevel::ePrimary,
            .commandBufferCount = 1
        };
        std::unique_ptr<vk::raii::CommandBuffer> commandBuffer = std::make_unique<vk::raii::CommandBuffer>(
            std::move(vk::raii::CommandBuffers(device, allocInfo).front()));

        vk::CommandBufferBeginInfo beginInfo{
            .flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit
        };
        commandBuffer->begin(beginInfo);

        return commandBuffer;
    }

    void VulkanRendererAPI::endSingleTimeCommands(const vk::raii::CommandBuffer& commandBuffer) const
    {
        commandBuffer.end();

        vk::SubmitInfo submitInfo{.commandBufferCount = 1, .pCommandBuffers = &*commandBuffer};
        queue.submit(submitInfo, nullptr);
        queue.waitIdle();
    }
    
    uint32_t VulkanRendererAPI::findMemoryType(uint32_t typeFilter, vk::MemoryPropertyFlags properties)
    {
        vk::PhysicalDeviceMemoryProperties memProperties = physicalDevice.getMemoryProperties();

        for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++)
        {
            if ((typeFilter & (1 << i)) && (memProperties.memoryTypes[i].propertyFlags & properties) == properties)
            {
                return i;
            }
        }

        throw std::runtime_error("failed to find suitable memory type!");
    }

    void VulkanRendererAPI::createCommandBuffers()
    {
        commandBuffers.clear();
        vk::CommandBufferAllocateInfo allocInfo{
            .commandPool = *commandPool, .level = vk::CommandBufferLevel::ePrimary,
            .commandBufferCount = MAX_FRAMES_IN_FLIGHT
        };
        commandBuffers = vk::raii::CommandBuffers(device, allocInfo);
        for (auto& commandBuffer : commandBuffers)
        {
            DBG_VK_NAME(*commandBuffer);
        }
    }

    void VulkanRendererAPI::transition_image_layout(
        uint32_t imageIndex,
        vk::ImageLayout old_layout,
        vk::ImageLayout new_layout,
        vk::AccessFlags2 src_access_mask,
        vk::AccessFlags2 dst_access_mask,
        vk::PipelineStageFlags2 src_stage_mask,
        vk::PipelineStageFlags2 dst_stage_mask
    )
    {
        vk::ImageMemoryBarrier2 barrier = {
            .srcStageMask = src_stage_mask,
            .srcAccessMask = src_access_mask,
            .dstStageMask = dst_stage_mask,
            .dstAccessMask = dst_access_mask,
            .oldLayout = old_layout,
            .newLayout = new_layout,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = swapChainImages[imageIndex],
            .subresourceRange = {
                .aspectMask = vk::ImageAspectFlagBits::eColor,
                .baseMipLevel = 0,
                .levelCount = 1,
                .baseArrayLayer = 0,
                .layerCount = 1
            }
        };
        vk::DependencyInfo dependency_info = {
            .dependencyFlags = {},
            .imageMemoryBarrierCount = 1,
            .pImageMemoryBarriers = &barrier
        };
        commandBuffers[currentFrame].pipelineBarrier2(dependency_info);
    }

    void VulkanRendererAPI::transition_image_layout_custom(
        vk::raii::Image& image,
        vk::ImageLayout old_layout,
        vk::ImageLayout new_layout,
        vk::AccessFlags2 src_access_mask,
        vk::AccessFlags2 dst_access_mask,
        vk::PipelineStageFlags2 src_stage_mask,
        vk::PipelineStageFlags2 dst_stage_mask,
        vk::ImageAspectFlags aspect_mask
    )
    {
        vk::ImageMemoryBarrier2 barrier = {
            .srcStageMask = src_stage_mask,
            .srcAccessMask = src_access_mask,
            .dstStageMask = dst_stage_mask,
            .dstAccessMask = dst_access_mask,
            .oldLayout = old_layout,
            .newLayout = new_layout,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = image,
            .subresourceRange = {
                .aspectMask = aspect_mask,
                .baseMipLevel = 0,
                .levelCount = 1,
                .baseArrayLayer = 0,
                .layerCount = 1
            }
        };
        vk::DependencyInfo dependency_info = {
            .dependencyFlags = {},
            .imageMemoryBarrierCount = 1,
            .pImageMemoryBarriers = &barrier
        };
        commandBuffers[currentFrame].pipelineBarrier2(dependency_info);
    }

    void VulkanRendererAPI::createSyncObjects()
    {
        presentCompleteSemaphores.clear();
        renderFinishedSemaphores.clear();
        inFlightFences.clear();

        for (size_t i = 0; i < swapChainImages.size(); i++)
        {
            presentCompleteSemaphores.emplace_back(device, vk::SemaphoreCreateInfo());
            DBG_VK_NAME(*presentCompleteSemaphores.back());
            renderFinishedSemaphores.emplace_back(device, vk::SemaphoreCreateInfo());
            DBG_VK_NAME(*renderFinishedSemaphores.back());
        }

        for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
        {
            inFlightFences.emplace_back(device, vk::FenceCreateInfo{.flags = vk::FenceCreateFlagBits::eSignaled});
            DBG_VK_NAME(*inFlightFences.back());
        }
    }

    void VulkanRendererAPI::createQueryPool()
    {
        vk::QueryPoolCreateInfo poolInfo
        {
            .queryType = vk::QueryType::ePipelineStatistics,
            .queryCount = 1,
            .pipelineStatistics =
                vk::QueryPipelineStatisticFlagBits::eInputAssemblyVertices |
                vk::QueryPipelineStatisticFlagBits::eInputAssemblyPrimitives |
                vk::QueryPipelineStatisticFlagBits::eVertexShaderInvocations |
                vk::QueryPipelineStatisticFlagBits::eFragmentShaderInvocations |
                vk::QueryPipelineStatisticFlagBits::eComputeShaderInvocations |
                vk::QueryPipelineStatisticFlagBits::eClippingInvocations |
                vk::QueryPipelineStatisticFlagBits::eClippingPrimitives
        };

        queryPool = vk::raii::QueryPool(device, poolInfo);
        DBG_VK_NAME(*queryPool);
    }

    void VulkanRendererAPI::createQueryBuffer()
    {
        // 7 pipelineStatistics, 1 querycount
        queryBuffer = allocator.createBuffer(sizeof(uint64_t) * 7 * 1, vk::BufferUsageFlagBits2::eTransferDst, VMA_MEMORY_USAGE_GPU_TO_CPU);
    }

    void VulkanRendererAPI::downloadQueryBuffer()
    {
        auto cmd = beginSingleTimeCommands();
        
        cmd->copyQueryPoolResults(queryPool, 0, 1, queryBuffer.buffer, 0, 0, vk::QueryResultFlagBits::e64 | vk::QueryResultFlagBits::eWait);

        endSingleTimeCommands(*cmd);

        void* mappedData = nullptr;
        vmaMapMemory(allocator, queryBuffer.allocation, &mappedData);

        uint64_t* stats = reinterpret_cast<uint64_t*>(mappedData);

        std::cout << std::dec;
        std::cout << "Input assembly vertices: "        << stats[0] << "\n";
        std::cout << "Input assembly primitives: "      << stats[1] << "\n";
        std::cout << "Vertex shader invocations: "      << stats[2] << "\n";
        std::cout << "Clipping invocations: "           << stats[3] << "\n";
        std::cout << "Clipping primitives: "            << stats[4] << "\n";
        std::cout << "Fragment shader invocations: "    << stats[5] << "\n";
        std::cout << "Compute shader invocations: "     << stats[6] << "\n";

        vmaUnmapMemory(allocator, queryBuffer.allocation);
    }
    
    uint32_t VulkanRendererAPI::chooseSwapMinImageCount(vk::SurfaceCapabilitiesKHR const& surfaceCapabilities)
    {
        auto minImageCount = std::max(3u, surfaceCapabilities.minImageCount);
        if ((0 < surfaceCapabilities.maxImageCount) && (surfaceCapabilities.maxImageCount < minImageCount))
        {
            minImageCount = surfaceCapabilities.maxImageCount;
        }
        return minImageCount;
    }

    vk::SurfaceFormatKHR VulkanRendererAPI::chooseSwapSurfaceFormat(const std::vector<vk::SurfaceFormatKHR>& availableFormats)
    {
        assert(!availableFormats.empty());
        const auto formatIt = std::ranges::find_if(
            availableFormats,
            [](const auto& format)
            {
                return format.format == vk::Format::eB8G8R8A8Srgb && format.colorSpace == vk::ColorSpaceKHR::eSrgbNonlinear;
            });
        return formatIt != availableFormats.end() ? *formatIt : availableFormats[0];
    }

    vk::PresentModeKHR VulkanRendererAPI::chooseSwapPresentMode(const std::vector<vk::PresentModeKHR>& availablePresentModes,
                                                       bool vsync)
    {
        if (vsync)
            return vk::PresentModeKHR::eFifo; // guaranteed available

        if (std::ranges::any_of(availablePresentModes,
                                [](auto mode) { return mode == vk::PresentModeKHR::eMailbox; }))
            return vk::PresentModeKHR::eMailbox;

        if (std::ranges::any_of(availablePresentModes,
                                [](auto mode) { return mode == vk::PresentModeKHR::eImmediate; }))
            return vk::PresentModeKHR::eImmediate;

        return vk::PresentModeKHR::eFifo;
    }

    vk::Extent2D VulkanRendererAPI::chooseSwapExtent(const vk::SurfaceCapabilitiesKHR& capabilities)
    {
        if (capabilities.currentExtent.width != std::numeric_limits<uint32_t>::max())
        {
            return capabilities.currentExtent;
        }
        int width, height;
        SDL_GetWindowSize(window, &width, &height);
        
        return {
            std::clamp<uint32_t>(width, capabilities.minImageExtent.width, capabilities.maxImageExtent.width),
            std::clamp<uint32_t>(height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height)
        };
    }

    std::vector<const char*> VulkanRendererAPI::getRequiredExtensions()
    {
        uint32_t sdlExtensionCount = 0;
        auto sdlExtensions = SDL_Vulkan_GetInstanceExtensions(&sdlExtensionCount);

        std::vector extensions(sdlExtensions, sdlExtensions + sdlExtensionCount);
        if (enableValidationLayers)
        {
            extensions.push_back(vk::EXTDebugUtilsExtensionName);
        }

        return extensions;
    }

    VKAPI_ATTR vk::Bool32 VKAPI_CALL VulkanRendererAPI::debugCallback(vk::DebugUtilsMessageSeverityFlagBitsEXT severity,
                                                             vk::DebugUtilsMessageTypeFlagsEXT type,
                                                             const vk::DebugUtilsMessengerCallbackDataEXT* pCallbackData,
                                                             void*)
    {
        std::cerr << "validation layer: type " << to_string(type) << " msg: " << pCallbackData->pMessage << std::endl;

        return vk::False;
    }

    std::vector<char> VulkanRendererAPI::readFile(const std::string& filename)
    {
        std::ifstream file(filename, std::ios::ate | std::ios::binary);
        if (!file.is_open())
        {
            throw std::runtime_error("failed to open file!");
        }
        std::vector<char> buffer(file.tellg());
        file.seekg(0, std::ios::beg);
        file.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        file.close();
        return buffer;
    }
}
