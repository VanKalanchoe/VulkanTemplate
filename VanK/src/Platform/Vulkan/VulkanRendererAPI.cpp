#include "VulkanRendererAPI.h"

/*--
 * We are using the Vulkan Memory Allocator (VMA) to manage memory.
 * This is a library that helps to allocate memory for Vulkan resources.
-*/
#define VMA_DYNAMIC_VULKAN_FUNCTIONS 1
#define VMA_IMPLEMENTATION
#include "vk_mem_alloc.h"
#include "vk_mem_alloc_raii.hpp"
#define VMA_LEAK_LOG_FORMAT(format, ...)                                                                               \
{                                                                                                                    \
printf((format), __VA_ARGS__);                                                                                     \
printf("\n");                                                                                                      \
}
#include <ImGuizmo.h>
#include <iostream>
#include <slang.h>


#include <SDL3/SDL_events.h>
#include <SDL3/SDL_vulkan.h>

#include "shaderIO.h"
#include "VulkanBuffer.h"
#include "VulkanShader.h"
#include "VanK/Core/core.h"
#include "VanK/Core/logger.h"
#include "VanK/Renderer/Texture.h"

namespace VanK
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
        std::cout << "VulkanRendererAPI::~VulkanRendererAPI()" << '\n';
        VulkanRendererAPI::Shutdown(); // ???????
    }

    VulkanRendererAPI& VulkanRendererAPI::Get()
    {
        if (s_instance == nullptr)
        {
            // If no instance is set, this will crash - which is what we want
            // because it means we're trying to use Vulkan before it's initialized
            throw std::runtime_error("VulkanRendererAPI not initialized! Call SetInstance first.");
        }
        return *s_instance;
    }

    void VulkanRendererAPI::Shutdown()
    {
        // Clear the static instance if it's this instance if i call this errors on mass
        /*if (s_instance == this)
        {
            s_instance = nullptr;
        }*/

        s_instance = nullptr;

        device.waitIdle();
        /*DestroyAllPipelines();// todo idk where to put this will see*/
        cleanup();
    }

    void VulkanRendererAPI::initVulkan()
    {
        createInstance();
        setupDebugMessenger();
        createSurface();
        pickPhysicalDevice();
        createLogicalDevice();
        createDynamicDispatcher();
        m_allocator.init(instance, physicalDevice, device);
        msaaSamples = getMaxUsableSampleCount();
        createSwapChain();
        viewport = swapChainExtent;
        createImageViews();
        createCommandPool();
        m_samplerPool.init(device);
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
        /*IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGui::StyleColorsDark();*/

        ImGui_ImplSDL3_InitForVulkan(window);
        static VkFormat imageFormats[] = {static_cast<VkFormat>(swapChainSurfaceFormat.format)};

        ImGui_ImplVulkan_InitInfo initInfo{};
        initInfo.ApiVersion = apiVersion,
            initInfo.Instance = *instance,
            initInfo.PhysicalDevice = *physicalDevice,
            initInfo.Device = *device,
            initInfo.QueueFamily = queueIndex,
            initInfo.Queue = *queue,
            initInfo.DescriptorPool = *uiDescriptorPool,
            initInfo.MinImageCount = 2,
            initInfo.ImageCount = MAX_FRAMES_IN_FLIGHT,
            initInfo.UseDynamicRendering = true,
            initInfo.PipelineInfoMain.PipelineRenderingCreateInfo.sType = static_cast<VkStructureType>(vk::StructureType::ePipelineRenderingCreateInfo);
        initInfo.PipelineInfoMain.PipelineRenderingCreateInfo.colorAttachmentCount = 1;
        initInfo.PipelineInfoMain.PipelineRenderingCreateInfo.pColorAttachmentFormats = imageFormats;

        ImGui_ImplVulkan_Init(&initInfo);

        ImGui::GetIO().ConfigFlags = ImGuiConfigFlags_DockingEnable | ImGuiConfigFlags_ViewportsEnable;
    }

    void VulkanRendererAPI::cleanupSwapChain()
    {
        swapChainImages.clear();
        swapChainImageViews.clear();
        swapChain = nullptr;
    }

    void VulkanRendererAPI::cleanup()
    {
        // Clear the pool on the renderer side so it doesn't hold dangling references
        m_images.clear();

        m_RenderTargetImages.clear();

        // Clean up entity readback buffer
        entityReadbackBuffer.buffer.clear(); // Add this line

        m_samplerPool.deinit();

        queryStatisticsBuffer.buffer.clear(); // statistics

        queryTimeStepBuffer.buffer.clear(); // timestamp

        m_allocator.deinit();
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
        /*device.waitIdle();

        // Recreate offscreen buffers to match viewport size
        createSceneResources(); //scene evertyhing drawn into this
        createColorResources(); //msaa
        createDepthResources(); //depth
        createEntityResources(); //entity
        createEntityColorResources();
        sceneImageInitialized = false;
        entityImageInitialized = false;
        entityColorImageInitialized = false;

        // Recreate the ImGui texture to point to the new sceneImageView
        if (!uiDescriptorSet.empty() && uiDescriptorSet[0] != nullptr)
        {
            uiDescriptorSet.resize(1);
            ImGui_ImplVulkan_RemoveTexture(uiDescriptorSet[0]);
            uiDescriptorSet[0] = nullptr;
        }
        if ((ImGui::GetCurrentContext() != nullptr) && ImGui::GetIO().BackendPlatformUserData != nullptr)
        {
            uiDescriptorSet.resize(1);
            uiDescriptorSet[0] = ImGui_ImplVulkan_AddTexture(
                linearSampler,
                *sceneImageView,
                static_cast<VkImageLayout>(vk::ImageLayout::eShaderReadOnlyOptimal));
        }*/
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
        auto layerProperties = m_context.enumerateInstanceLayerProperties();
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
        auto extensionProperties = m_context.enumerateInstanceExtensionProperties();
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
        instance = vk::raii::Instance(m_context, createInfo);
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
                vk::PhysicalDeviceFeatures2,
                vk::PhysicalDeviceVulkan12Features,
                vk::PhysicalDeviceVulkan13Features,
                vk::PhysicalDeviceExtendedDynamicStateFeaturesEXT,
                vk::PhysicalDeviceTimelineSemaphoreFeaturesKHR,
                vk::PhysicalDeviceMeshShaderFeaturesEXT,
                vk::PhysicalDeviceAccelerationStructureFeaturesKHR,
                vk::PhysicalDeviceRayQueryFeaturesKHR,
                vk::PhysicalDeviceRayTracingPipelineFeaturesKHR
            >();
            bool supportsRequiredFeatures =
                features.template get<vk::PhysicalDeviceFeatures2>().features.samplerAnisotropy &&
                features.template get<vk::PhysicalDeviceVulkan13Features>().dynamicRendering &&
                features.template get<vk::PhysicalDeviceExtendedDynamicStateFeaturesEXT>().extendedDynamicState &&
                features.template get<vk::PhysicalDeviceVulkan12Features>().descriptorBindingSampledImageUpdateAfterBind &&
                features.template get<vk::PhysicalDeviceVulkan12Features>().descriptorBindingPartiallyBound &&
                features.template get<vk::PhysicalDeviceVulkan12Features>().descriptorBindingVariableDescriptorCount &&
                features.template get<vk::PhysicalDeviceVulkan12Features>().runtimeDescriptorArray &&
                features.template get<vk::PhysicalDeviceVulkan12Features>().shaderSampledImageArrayNonUniformIndexing &&
                features.template get<vk::PhysicalDeviceVulkan12Features>().bufferDeviceAddress &&
                features.template get<vk::PhysicalDeviceAccelerationStructureFeaturesKHR>().accelerationStructure &&
                features.template get<vk::PhysicalDeviceRayQueryFeaturesKHR>().rayQuery &&
                features.template get<vk::PhysicalDeviceRayTracingPipelineFeaturesKHR>().rayTracingPipeline &&
                features.template get<vk::PhysicalDeviceTimelineSemaphoreFeaturesKHR>().timelineSemaphore &&
                features.template get<vk::PhysicalDeviceMeshShaderFeaturesEXT>().meshShader;

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
                vk::PhysicalDeviceVulkan14Features,
                vk::PhysicalDeviceExtendedDynamicStateFeaturesEXT,
                vk::PhysicalDeviceMeshShaderFeaturesEXT,
                vk::PhysicalDeviceAccelerationStructureFeaturesKHR,
                vk::PhysicalDeviceRayQueryFeaturesKHR,
                vk::PhysicalDeviceRayTracingPipelineFeaturesKHR
            >
            featureChain =
            {
                {
                    .features = {
                        .independentBlend = true, .sampleRateShading = true, .multiDrawIndirect = true, .wideLines = true, .samplerAnisotropy = true, .pipelineStatisticsQuery = true,
                        .shaderInt64 = true,
                    }
                }, // vk::PhysicalDeviceFeatures2
                {.shaderDrawParameters = true},
                {
                    .drawIndirectCount = true,
                    .shaderInt8 = true,
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
                {.maintenance5 = true, .pushDescriptor = true},
                {.extendedDynamicState = true}, // vk::PhysicalDeviceExtendedDynamicStateFeaturesEXT
                {.taskShader = true, .meshShader = true},
                {.accelerationStructure = true}, // vk::PhysicalDeviceAccelerationStructureFeaturesKHR
                {.rayQuery = true},
                {.rayTracingPipeline = true},
            };

        // create a Device
        float queuePriority = 0.5f;
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

    uint32_t VulkanRendererAPI::AddRenderTargetImageToPool(utils::ImageResource&& imageResource)
    {
        // Add the texture to the image vector
        m_RenderTargetImages.emplace_back(std::move(imageResource));

        // Return the index of the new texture
        return static_cast<uint32_t>(m_RenderTargetImages.size() - 1);
    }

    void VulkanRendererAPI::RemoveRenderTargetImageToPool(uint32_t index)
    {
        // Safety checks
        if (m_RenderTargetImages.empty())
        {
            VK_CORE_WARN("Attempted to remove texture from empty pool");
            return;
        }

        if (index >= m_RenderTargetImages.size())
        {
            VK_CORE_WARN("Attempted to remove texture at invalid index: %u (max: %zu)", index, m_RenderTargetImages.size());
            return;
        }

        // Destroy the texture resource
        m_allocator.destroyImageResource(m_RenderTargetImages[index]);

        // Remove the texture from the image vector
        m_RenderTargetImages[index] = {};

        VK_CORE_INFO("Removed texture at index %u, remaining textures: %zu", index, m_RenderTargetImages.size());
    }

    uint32_t VulkanRendererAPI::AddTextureToPool(utils::ImageResource&& imageResource)
    {
        // Add the texture to the image vector
        m_images.emplace_back(std::move(imageResource));

        // Update the descriptor set to include the new texture
        updateGraphicsDescriptorSet();

        // Return the index of the new texture
        return static_cast<uint32_t>(m_images.size() - 1);
    }

    void VulkanRendererAPI::RemoveTextureFromPool(uint32_t index)
    {
        // Safety checks
        if (m_images.empty())
        {
            VK_CORE_WARN("Attempted to remove texture from empty pool");
            return;
        }

        if (index >= m_images.size())
        {
            VK_CORE_WARN("Attempted to remove texture at invalid index: %u (max: %zu)", index, m_images.size());
            return;
        }

        // Destroy the texture resource
        m_allocator.destroyImageResource(m_images[index]);

        // Remove the texture from the image vector
        m_images[index] = {};
        m_images.erase(m_images.begin() + index);
        // Update the descriptor set to include the new texture
        updateGraphicsDescriptorSet();

        VK_CORE_INFO("Removed texture at index %u, remaining textures: %zu", index, m_images.size());
    }

    VanKPipeLine VulkanRendererAPI::createGraphicsPipeline(VanKGraphicsPipelineSpecification pipelineSpecification)
    {
        vk::raii::Pipeline tempPipeline = VK_NULL_HANDLE;
        vk::raii::PipelineLayout tempPipelineLayout = VK_NULL_HANDLE;

        auto specShader = pipelineSpecification.ShaderStageCreateInfo.VanKShader;
        auto vkShader = dynamic_cast<VulkanShader*>(specShader);

        std::vector<vk::PipelineShaderStageCreateInfo> shaderStages;
        std::vector<std::string> entryNames; // only need to keep names alive
        entryNames.reserve(4); // needs to be reserved or string changes to random bs

        auto addStage = [&](vk::ShaderStageFlagBits stage)
        {
            if (!vkShader->HasStage(stage))
                return;

            entryNames.push_back(vkShader->GetShaderEntryName(stage));

            auto& module = vkShader->GetShaderModule(stage);

            shaderStages.push_back
            ({
                .stage = stage,
                .module = module,
                .pName = entryNames.back().c_str()
            });
        };

        addStage(vk::ShaderStageFlagBits::eVertex);
        addStage(vk::ShaderStageFlagBits::eFragment);
        addStage(vk::ShaderStageFlagBits::eTaskEXT);
        addStage(vk::ShaderStageFlagBits::eMeshEXT);

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
            .polygonMode = ConvertToVkPolygonMode(pipelineSpecification.RasterizationStateCreateInfo.VanKPolygon),
            .frontFace = ConvertToVkFrontFace(pipelineSpecification.RasterizationStateCreateInfo.VanKFrontFace),
            .depthBiasEnable = vk::True,
        };

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
        for (auto ColorBlendAttachment : pipelineSpecification.ColorBlendStateCreateInfo.VanKColorBlendAttachmentState)
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
            vk::DynamicState::eLineWidth,
            vk::DynamicState::eCullMode
        };

        vk::PipelineDynamicStateCreateInfo dynamicState
        {
            .dynamicStateCount = static_cast<uint32_t>(dynamicStates.size()), .pDynamicStates = dynamicStates.data()
        };

        std::array<vk::DescriptorSetLayout, 3> setLayouts =
        {
            *descriptorSetLayout,
            *commonDescriptorSetLayout,
            raytraceDescriptorSetLayout
        };

        std::vector<vk::PushConstantRange> vkPushConstants;
        vkPushConstants.reserve(pipelineSpecification.PipelineLayoutInfo.PushConstants.size());
        vk::ShaderStageFlags stageFlags = (pipelineSpecification.PipelineType == VanK_Mesh) ? ConvertToVkShaderStageFlagBits(VanKMesh) : ConvertToVkShaderStageFlagBits(VanKGraphics);
        for (const auto pushRange : pipelineSpecification.PipelineLayoutInfo.PushConstants)
        {
            vkPushConstants.push_back(vk::PushConstantRange{stageFlags, pushRange.Offset, pushRange.Size});
        }

        vk::PipelineLayoutCreateInfo pipelineLayoutInfo
        {
            .setLayoutCount = setLayouts.size(),
            .pSetLayouts = setLayouts.data(),
            .pushConstantRangeCount = static_cast<uint32_t>(vkPushConstants.size()),
            .pPushConstantRanges = vkPushConstants.data()
        };

        tempPipelineLayout = vk::raii::PipelineLayout(device, pipelineLayoutInfo);
        DBG_VK_NAME(*tempPipelineLayout);

        // Dynamic rendering: provide what the pipeline will render to
        std::vector<vk::Format> colorFormats;
        for (auto format : pipelineSpecification.RenderingCreateInfo.VanKColorAttachmentFormats)
        {
            colorFormats.emplace_back(ConvertToVkColorFormat(format));
        }

        vk::StructureChain<vk::GraphicsPipelineCreateInfo, vk::PipelineRenderingCreateInfo> pipelineCreateInfoChain =
        {
            {
                .stageCount = static_cast<uint32_t>(shaderStages.size()),
                .pStages = shaderStages.data(),
                .pVertexInputState = (pipelineSpecification.PipelineType == VanK_Mesh) ? nullptr : &vertexInputInfo,
                .pInputAssemblyState = (pipelineSpecification.PipelineType == VanK_Mesh) ? nullptr : &inputAssembly,
                .pViewportState = &viewportState,
                .pRasterizationState = &rasterizer,
                .pMultisampleState = &multisampling,
                .pDepthStencilState = &depthStencil,
                .pColorBlendState = &colorBlending,
                .pDynamicState = &dynamicState,
                .layout = tempPipelineLayout,
                .renderPass = nullptr
            },
            {
                .colorAttachmentCount = static_cast<uint32_t>(colorFormats.size()),
                .pColorAttachmentFormats = colorFormats.data(), // &swapChainSurfaceFormat.format
                .depthAttachmentFormat = findDepthFormat()
            }
        };

        tempPipeline = vk::raii::Pipeline(device, nullptr, pipelineCreateInfoChain.get<vk::GraphicsPipelineCreateInfo>());
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

        vk::PipelineShaderStageCreateInfo computeShaderStageInfo
        {
            .stage = vk::ShaderStageFlagBits::eCompute,
            .module = compute,
            .pName = computeEntryName.c_str()
        };

        const std::array<vk::DescriptorSetLayout, 3> computeDescriptorSetLayouts =
        {
            descriptorSetLayout,
            commonDescriptorSetLayout, // <-- This is your new, shared layout
            raytraceDescriptorSetLayout
        };

        std::vector<vk::PushConstantRange> vkPushConstants;
        vkPushConstants.reserve(computePipelineSpecification.ComputePipelineLayoutInfo.PushConstants.size());
        vk::ShaderStageFlags stageFlags = ConvertToVkShaderStageFlagBits(VanKCompute);
        for (const auto pushRange : computePipelineSpecification.ComputePipelineLayoutInfo.PushConstants)
        {
            vkPushConstants.push_back(vk::PushConstantRange{stageFlags, pushRange.Offset, pushRange.Size});
        }

        // The pipeline layout is used to pass data to the pipeline, anything with "layout" in the shader
        const vk::PipelineLayoutCreateInfo pipelineLayoutInfo
        {
            .setLayoutCount = computeDescriptorSetLayouts.size(),
            .pSetLayouts = computeDescriptorSetLayouts.data(),
            .pushConstantRangeCount = static_cast<uint32_t>(vkPushConstants.size()),
            .pPushConstantRanges = vkPushConstants.data(),
        };

        tempPipelineLayout = vk::raii::PipelineLayout(device, pipelineLayoutInfo);
        DBG_VK_NAME(*tempPipelineLayout);

        // Creating the pipeline to run the compute shader
        vk::ComputePipelineCreateInfo pipelineInfo
        {
            .stage = computeShaderStageInfo,
            .layout = tempPipelineLayout
        };
        tempPipeline = vk::raii::Pipeline(device, nullptr, pipelineInfo);
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

    utils::Buffer m_sbtBuffer;

    std::vector<uint8_t> m_shaderHandles;
    vk::StridedDeviceAddressRegionKHR m_raygenRegion{};
    vk::StridedDeviceAddressRegionKHR m_missRegion{};
    vk::StridedDeviceAddressRegionKHR m_hitRegion{};
    vk::StridedDeviceAddressRegionKHR m_callableRegion{};

    void VulkanRendererAPI::createShaderBindingTable(const VkRayTracingPipelineCreateInfoKHR& rtPipelineInfo, vk::raii::Pipeline& rtPipeline)
    {
        m_allocator.destroyBuffer(std::move(m_sbtBuffer)); // Cleanup when re-creating

        vk::StructureChain
            <
                vk::PhysicalDeviceProperties2,
                vk::PhysicalDeviceRayTracingPipelinePropertiesKHR,
                vk::PhysicalDeviceAccelerationStructurePropertiesKHR
            >
            props =
                physicalDevice.getProperties2
                <
                    vk::PhysicalDeviceProperties2,
                    vk::PhysicalDeviceRayTracingPipelinePropertiesKHR,
                    vk::PhysicalDeviceAccelerationStructurePropertiesKHR
                >();

        const auto& rtProps =
            props.get<vk::PhysicalDeviceRayTracingPipelinePropertiesKHR>();

        uint32_t handleSize = rtProps.shaderGroupHandleSize;
        uint32_t handleAlignment = rtProps.shaderGroupHandleAlignment;
        uint32_t baseAlignment = rtProps.shaderGroupBaseAlignment;
        uint32_t groupCount = rtPipelineInfo.groupCount;

        // Get shader group handles
        size_t dataSize = handleSize * groupCount;
        m_shaderHandles.resize(dataSize);
        m_shaderHandles = rtPipeline.getRayTracingShaderGroupHandlesKHR<uint8_t>(0, groupCount, dataSize);

        // Calculate SBT buffer size with proper alignment
        auto alignUp = [](uint32_t size, uint32_t alignment) { return (size + alignment - 1) & ~(alignment - 1); };
        uint32_t raygenSize = alignUp(handleSize, handleAlignment);
        uint32_t missSize = alignUp(handleSize, handleAlignment);
        uint32_t hitSize = alignUp(handleSize, handleAlignment);
        uint32_t callableSize = 0; // No callable shaders in this tutorial

        // Ensure each region starts at a baseAlignment boundary
        uint32_t raygenOffset = 0;
        uint32_t missOffset = alignUp(raygenSize, baseAlignment);
        uint32_t hitOffset = alignUp(missOffset + missSize, baseAlignment);
        uint32_t callableOffset = alignUp(hitOffset + hitSize, baseAlignment);

        size_t bufferSize = callableOffset + callableSize;

        // Create SBT buffer
        m_sbtBuffer = m_allocator.createBuffer
        (
            bufferSize,
            vk::BufferUsageFlagBits2::eShaderBindingTableKHR,
            vma::MemoryUsage::eAutoPreferDevice,
            vma::AllocationCreateFlagBits::eMapped | vma::AllocationCreateFlagBits::eHostAccessRandom
        );

        // Populate SBT buffer
        uint8_t* pData = static_cast<uint8_t*>(m_sbtBuffer.mapping);

        // Ray generation shader (group 0)
        memcpy(pData + raygenOffset, m_shaderHandles.data() + 0 * handleSize, handleSize);
        m_raygenRegion.deviceAddress = m_sbtBuffer.address + raygenOffset;
        m_raygenRegion.stride = raygenSize;
        m_raygenRegion.size = raygenSize;

        // Miss shader (group 1)
        memcpy(pData + missOffset, m_shaderHandles.data() + 1 * handleSize, handleSize);
        m_missRegion.deviceAddress = m_sbtBuffer.address + missOffset;
        m_missRegion.stride = missSize;
        m_missRegion.size = missSize;

        // Hit shader (group 2)
        memcpy(pData + hitOffset, m_shaderHandles.data() + 2 * handleSize, handleSize);
        m_hitRegion.deviceAddress = m_sbtBuffer.address + hitOffset;
        m_hitRegion.stride = hitSize;
        m_hitRegion.size = hitSize;

        // Callable shaders (none in this tutorial)
        m_callableRegion.deviceAddress = 0;
        m_callableRegion.stride = 0;
        m_callableRegion.size = 0;

        LOGI("Shader binding table created and populated \n");
    }

    /*#include "rtbasic.slang.h" */

    VanKPipeLine VulkanRendererAPI::createRayTracingPipeline(VanKRaytracingPipelineSpecification raytracingPipelineSpecification)
    {
        vk::raii::Pipeline tempPipeline = VK_NULL_HANDLE;
        vk::raii::PipelineLayout tempPipelineLayout = VK_NULL_HANDLE;

        // Creating all shaders
        enum StageIndices
        {
            eRaygen,
            eMiss,
            eClosestHit,
            eAnyHit,
            eShaderGroupCount
        };

        auto specShader = raytracingPipelineSpecification.ShaderStageCreateInfo.VanKShader;
        auto vkShader = dynamic_cast<VulkanShader*>(specShader);

        std::vector<vk::PipelineShaderStageCreateInfo> shaderStages;
        std::vector<std::string> entryNames; // only need to keep names alive
        entryNames.reserve(4); // needs to be reserved or string changes to random bs

        std::array<uint32_t, eShaderGroupCount> stageToShaderIndex;
        stageToShaderIndex.fill(VK_SHADER_UNUSED_KHR);

        auto addStage = [&](vk::ShaderStageFlagBits stage, StageIndices eShaderGroupCount)
        {
            if (!vkShader->HasStage(stage))
                return;

            entryNames.push_back(vkShader->GetShaderEntryName(stage));

            auto& module = vkShader->GetShaderModule(stage);

            uint32_t shaderIndex = static_cast<uint32_t>(shaderStages.size());

            shaderStages.push_back
            ({
                .stage = stage,
                .module = module,
                .pName = entryNames.back().c_str()
            });

            stageToShaderIndex[eShaderGroupCount] = shaderIndex;
        };

        addStage(vk::ShaderStageFlagBits::eRaygenKHR, eRaygen);
        addStage(vk::ShaderStageFlagBits::eMissKHR, eMiss);
        addStage(vk::ShaderStageFlagBits::eClosestHitKHR, eClosestHit);
        addStage(vk::ShaderStageFlagBits::eAnyHitKHR, eAnyHit);

        // Shader groups
        vk::RayTracingShaderGroupCreateInfoKHR group{};
        group.anyHitShader = VK_SHADER_UNUSED_KHR;
        group.closestHitShader = VK_SHADER_UNUSED_KHR;
        group.generalShader = VK_SHADER_UNUSED_KHR;
        group.intersectionShader = VK_SHADER_UNUSED_KHR;

        std::vector<vk::RayTracingShaderGroupCreateInfoKHR> shader_groups;
        // Raygen
        group.type = vk::RayTracingShaderGroupTypeKHR::eGeneral;
        group.generalShader = stageToShaderIndex[eRaygen];
        shader_groups.push_back(group);

        // Miss
        group.type = vk::RayTracingShaderGroupTypeKHR::eGeneral;
        group.generalShader = stageToShaderIndex[eMiss];
        shader_groups.push_back(group);

        // Closest Hit Shader
        group.type = vk::RayTracingShaderGroupTypeKHR::eTrianglesHitGroup;
        group.generalShader = VK_SHADER_UNUSED_KHR;
        group.closestHitShader = stageToShaderIndex[eClosestHit];
        group.anyHitShader = stageToShaderIndex[eAnyHit];
        shader_groups.push_back(group);
        
        // Any Hit
        group.type = vk::RayTracingShaderGroupTypeKHR::eGeneral;

        std::array<vk::DescriptorSetLayout, 3> setLayouts =
        {
            *descriptorSetLayout,
            *commonDescriptorSetLayout,
            raytraceDescriptorSetLayout
        };

        std::vector<vk::PushConstantRange> vkPushConstants;
        vkPushConstants.reserve(raytracingPipelineSpecification.PipelineLayoutInfo.PushConstants.size());
        vk::ShaderStageFlags stageFlags = ConvertToVkShaderStageFlagBits(VanKRaytracing);

        for (const auto pushRange : raytracingPipelineSpecification.PipelineLayoutInfo.PushConstants)
        {
            vkPushConstants.push_back(vk::PushConstantRange{stageFlags, pushRange.Offset, pushRange.Size});
        }

        vk::PipelineLayoutCreateInfo pipelineLayoutInfo
        {
            .setLayoutCount = setLayouts.size(),
            .pSetLayouts = setLayouts.data(),
            .pushConstantRangeCount = static_cast<uint32_t>(vkPushConstants.size()),
            .pPushConstantRanges = vkPushConstants.data()
        };

        tempPipelineLayout = vk::raii::PipelineLayout(device, pipelineLayoutInfo);
        DBG_VK_NAME(*tempPipelineLayout);

        vk::StructureChain
            <
                vk::PhysicalDeviceProperties2,
                vk::PhysicalDeviceRayTracingPipelinePropertiesKHR,
                vk::PhysicalDeviceAccelerationStructurePropertiesKHR
            >
            props =
                physicalDevice.getProperties2
                <
                    vk::PhysicalDeviceProperties2,
                    vk::PhysicalDeviceRayTracingPipelinePropertiesKHR,
                    vk::PhysicalDeviceAccelerationStructurePropertiesKHR
                >();

        const auto& rtProps =
            props.get<vk::PhysicalDeviceRayTracingPipelinePropertiesKHR>();

        uint32_t maxRecursion = rtProps.maxRayRecursionDepth;

        vk::RayTracingPipelineCreateInfoKHR rtPipelineInfo;
        rtPipelineInfo.stageCount = static_cast<uint32_t>(shaderStages.size());
        rtPipelineInfo.pStages = shaderStages.data();
        rtPipelineInfo.groupCount = static_cast<uint32_t>(shader_groups.size());
        rtPipelineInfo.pGroups = shader_groups.data();
        rtPipelineInfo.maxPipelineRayRecursionDepth = std::max(3U, maxRecursion);
        rtPipelineInfo.layout = tempPipelineLayout;

        tempPipeline = vk::raii::Pipeline(device, nullptr, nullptr, rtPipelineInfo);
        DBG_VK_NAME(*tempPipeline);

        createShaderBindingTable(rtPipelineInfo, tempPipeline);

        PipelineResource resource;
        resource.pipeline = std::move(tempPipeline);
        resource.layout = std::move(tempPipelineLayout);
        resource.bindPoint = VanKPipelineBindPoint::Raytracing;
        resource.raytracingSpec = raytracingPipelineSpecification;

        auto rawHandle = *resource.pipeline; // raw VkPipeline before moving
        m_currentRaytracingPipelineLayout = *resource.layout;
        m_PipelineResources.emplace(rawHandle, std::move(resource));

        std::cout << "Ray tracing pipeline created successfully\n";

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
        sceneImageInitialized = false;
        entityImageInitialized = false;
        entityColorImageInitialized = false;
        m_hasActiveRenderPass = false;
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
        commandBuffers[frameIndex].reset();

        commandBuffers[frameIndex].begin({});

        //statistics
        commandBuffers[frameIndex].resetQueryPool(queryPoolStatistics, 0, 1);
        commandBuffers[frameIndex].beginQuery(queryPoolStatistics, 0);

        auto cmd = new VanKCommandBuffer_T{&commandBuffers[frameIndex]};

        //timestamp
        timeStampIndex = 0;
        commandBuffers[frameIndex].resetQueryPool(queryPoolTimeStep, 0, maxTimeStamp);
        timestamp.queryIndex = 0;
        StartTimeStamp(cmd, timestamp);

        return cmd;
    }

    void VulkanRendererAPI::EndCommandBuffer(VanKCommandBuffer cmd)
    {
        //statistics
        Unwrap(cmd).endQuery(queryPoolStatistics, 0);

        //timestamp
        StopTimeStamp(cmd, timestamp);

        Unwrap(cmd).end();
    }

    void VulkanRendererAPI::BeginFrame(VanKRenderOption renderOption)
    {
        m_renderOption = renderOption;

        if (m_renderOption == VanK_Render_ImGui)
        {
            ImGui_ImplVulkan_NewFrame();
            ImGui_ImplSDL3_NewFrame();
            ImGui::NewFrame();
            ImGuizmo::BeginFrame();
        }

        // Note: inFlightFences, presentCompleteSemaphores, and commandBuffers are indexed by frameIndex,
        //       while renderFinishedSemaphores is indexed by imageIndex

        auto fenceResult = device.waitForFences(*inFlightFences[frameIndex], vk::True, UINT64_MAX);

        if (fenceResult != vk::Result::eSuccess)
        {
            throw std::runtime_error("failed to wait for fence!");
        }

        auto [result, acquiredImageIndex] = swapChain.acquireNextImage(UINT64_MAX, *presentCompleteSemaphores[frameIndex], nullptr);

        // Due to VULKAN_HPP_HANDLE_ERROR_OUT_OF_DATE_AS_SUCCESS being defined, eErrorOutOfDateKHR can be checked as a result
        // here and does not need to be caught by an exception.
        if (result == vk::Result::eErrorOutOfDateKHR)
        {
            recreateSwapChain();
            return;
        }

        // On other success codes than eSuccess and eSuboptimalKHR we just throw an exception.
        // On any error code, aquireNextImage already threw an exception.
        if (result != vk::Result::eSuccess && result != vk::Result::eSuboptimalKHR)
        {
            assert(result == vk::Result::eTimeout || result == vk::Result::eNotReady);
            throw std::runtime_error("failed to acquire swap chain image!");
        }

        device.resetFences(*inFlightFences[frameIndex]);

        currentResult = result;

        imageIndex = acquiredImageIndex;
    }

    void VulkanRendererAPI::EndFrame()
    {
        if (m_renderOption == VanK_Render_ImGui)
        {
            ImGui::EndFrame();
            if ((ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable) != 0)
            {
                ImGui::UpdatePlatformWindows();
                ImGui::RenderPlatformWindowsDefault();
            }
        }

        vk::PipelineStageFlags waitDestinationStageMask(vk::PipelineStageFlagBits::eColorAttachmentOutput);

        const vk::SubmitInfo submitInfo
        {
            .waitSemaphoreCount = 1,
            .pWaitSemaphores = &*presentCompleteSemaphores[frameIndex],
            .pWaitDstStageMask = &waitDestinationStageMask,
            .commandBufferCount = 1,
            .pCommandBuffers = &*commandBuffers[frameIndex],
            .signalSemaphoreCount = 1,
            .pSignalSemaphores = &*renderFinishedSemaphores[imageIndex]
        };
        queue.submit(submitInfo, *inFlightFences[frameIndex]);

        try
        {
            const vk::PresentInfoKHR presentInfoKHR
            {
                .waitSemaphoreCount = 1,
                .pWaitSemaphores = &*renderFinishedSemaphores[imageIndex],
                .swapchainCount = 1,
                .pSwapchains = &*swapChain,
                .pImageIndices = &imageIndex
            };
            currentResult = queue.presentKHR(presentInfoKHR);

            if (currentResult == vk::Result::eErrorOutOfDateKHR || currentResult == vk::Result::eSuboptimalKHR || framebufferResized)
            {
                framebufferResized = false;
                recreateSwapChain();
            }
            else if (currentResult != vk::Result::eSuccess)
            {
                throw std::runtime_error("failed to present swap chain image!");
            }
        }
        catch (const vk::SystemError& e)
        {
            if (e.code().value() == static_cast<int>(vk::Result::eErrorOutOfDateKHR))
            {
                recreateSwapChain();
                return;
            }
            else
            {
                throw;
            }
        }

        frameIndex = (frameIndex + 1) % MAX_FRAMES_IN_FLIGHT;
        /*downloadQueryStatisticsBuffer();*/
        if (isTimeStapEnabled)
            downloadQueryTimeStampBuffer();
    }

    VanKComputePass* VulkanRendererAPI::BeginComputePass(VanKCommandBuffer cmd, VertexBuffer* vertexBuffer, std::span<Ref<IndirectBuffer>> indirectBuffers, std::span<Ref<IndirectBuffer>> countBuffers)
    {
        auto* result = new VanKComputePass{
            .VanKCommandBuffer = cmd,
            .VanKVertexBuffer = vertexBuffer,
            .VanKIndirectBuffers = indirectBuffers,
            .VanKIndirectCountBuffers = countBuffers
        };

        if (vertexBuffer != nullptr)
        {
            // Add a barrier to make sure nothing was writing to it, before updating its content
            utils::cmdBufferMemoryBarrier
            (
                Unwrap(cmd),
                static_cast<VkBuffer>(vertexBuffer->GetNativeHandle()),
                vk::PipelineStageFlagBits2::eAllCommands, // Wait for everything prior to complete
                vk::PipelineStageFlagBits2::eComputeShader, // Destination: Compute Shader
                {}, // Src Access: None (just wait for visibility)
                vk::AccessFlagBits2::eShaderRead // Dst Access: Compute Shader READ
            );
        }

        for (auto& indirectBuffer : indirectBuffers)
        {
            if (!indirectBuffer) continue;

            // Add a barrier to make sure nothing was writing to it, before updating its content
            utils::cmdBufferMemoryBarrier
            (
                Unwrap(cmd),
                static_cast<VkBuffer>(indirectBuffer->GetNativeHandle()),
                vk::PipelineStageFlagBits2::eAllCommands,
                vk::PipelineStageFlagBits2::eComputeShader,
                {},
                vk::AccessFlagBits2::eShaderWrite // Dst Access: Compute Shader WRITE
            );
        }

        for (auto& countBuffer : countBuffers)
        {
            if (!countBuffer) continue;

            // Add a barrier to make sure nothing was writing to it, before updating its content
            utils::cmdBufferMemoryBarrier
            (
                Unwrap(cmd),
                static_cast<VkBuffer>(countBuffer->GetNativeHandle()),
                vk::PipelineStageFlagBits2::eAllCommands,
                vk::PipelineStageFlagBits2::eComputeShader,
                {},
                vk::AccessFlagBits2::eShaderWrite // Dst Access: Compute Shader WRITE
            );
        }

        return result;
    }

    void VulkanRendererAPI::DispatchCompute(VanKComputePass* computePass, uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ)
    {
        // Execute the compute shader
        // The workgroup is set to 256, and we only have 3 vertex to deal with, so one group is enough
        commandBuffers[frameIndex].dispatch(groupCountX, groupCountY, groupCountZ);
    }

    void VulkanRendererAPI::EndComputePass(VanKComputePass* computePass)
    {
        // A. If Compute ONLY READS (Standard Culling): No barrier needed, as access doesn't change.
        // B. If Compute WROTE (Simulation/Generation):

        if (computePass->VanKVertexBuffer != nullptr)
        {
            // Add barrier to make sure the compute shader is finished before the vertex buffer is used
            utils::cmdBufferMemoryBarrier
            (
                Unwrap(computePass->VanKCommandBuffer),
                static_cast<VkBuffer>(computePass->VanKVertexBuffer->GetNativeHandle()),
                vk::PipelineStageFlagBits2::eComputeShader,
                vk::PipelineStageFlagBits2::eVertexShader,
                vk::AccessFlagBits2::eShaderWrite, // Wait for compute write to finish
                vk::AccessFlagBits2::eShaderRead // Allow vertex shader to read
            );
        }

        for (auto& indirectBuffer : computePass->VanKIndirectBuffers)
        {
            if (!indirectBuffer) continue;

            // Barrier for the Indirect buffer
            utils::cmdBufferMemoryBarrier
            (
                Unwrap(computePass->VanKCommandBuffer),
                static_cast<VkBuffer>(indirectBuffer->GetNativeHandle()),
                vk::PipelineStageFlagBits2::eComputeShader,
                vk::PipelineStageFlagBits2::eDrawIndirect,
                vk::AccessFlagBits2::eShaderWrite, // Src Access: Compute Shader WRITE
                vk::AccessFlagBits2::eIndirectCommandRead // Dst Access: Draw Indirect READ
            );
        }

        for (auto& countBuffer : computePass->VanKIndirectCountBuffers)
        {
            if (!countBuffer) continue;

            // Barrier for the Draw Count buffer
            utils::cmdBufferMemoryBarrier
            (
                Unwrap(computePass->VanKCommandBuffer),
                static_cast<VkBuffer>(countBuffer->GetNativeHandle()),
                vk::PipelineStageFlagBits2::eComputeShader,
                vk::PipelineStageFlagBits2::eDrawIndirect,
                vk::AccessFlagBits2::eShaderWrite, // Src Access: Compute Shader WRITE
                vk::AccessFlagBits2::eIndirectCommandRead // Dst Access: Draw Indirect READ
            );
        }

        delete computePass;
    }

    vk::PipelineStageFlags2 StageFromResourceState(ResourceState state)
    {
        using Stage = ResourceState::Stage;
        switch (state.stage)
        {
        case Stage::TopOfPipe: return vk::PipelineStageFlagBits2::eTopOfPipe;
        case Stage::Compute: return vk::PipelineStageFlagBits2::eComputeShader;
        case Stage::ColorOutput: return vk::PipelineStageFlagBits2::eColorAttachmentOutput;
        case Stage::DepthOutput: return vk::PipelineStageFlagBits2::eAllCommands; // Vulkan has no direct DepthOutput stage
        case Stage::Fragment: return vk::PipelineStageFlagBits2::eFragmentShader;
        case Stage::Transfer: return vk::PipelineStageFlagBits2::eTransfer;
        case Stage::DrawIndirect: return vk::PipelineStageFlagBits2::eDrawIndirect;
        default: return vk::PipelineStageFlagBits2::eAllCommands;
        }
    }

    void VulkanRendererAPI::InsertBarrier(VanKCommandBuffer cmd, ResourceID& id, ResourceState& last, ResourceState& desired)
    {
        if (id.type == ResourceType::Image)
        {
            // Map our abstract ResourceState::Layout to VkImageLayout
            auto toVkLayout = [](ResourceState::Layout l) -> vk::ImageLayout
            {
                switch (l)
                {
                case ResourceState::Layout::Undefined: return vk::ImageLayout::eUndefined;
                case ResourceState::Layout::General: return vk::ImageLayout::eGeneral;
                case ResourceState::Layout::ColorAttachment: return vk::ImageLayout::eColorAttachmentOptimal;
                case ResourceState::Layout::ResolveAttachment: return vk::ImageLayout::eColorAttachmentOptimal;
                case ResourceState::Layout::DepthAttachment: return vk::ImageLayout::eDepthAttachmentOptimal;
                case ResourceState::Layout::ShaderReadOnly: return vk::ImageLayout::eShaderReadOnlyOptimal;
                case ResourceState::Layout::TransferDst: return vk::ImageLayout::eTransferDstOptimal;
                case ResourceState::Layout::TransferSrc: return vk::ImageLayout::eTransferSrcOptimal;
                case ResourceState::Layout::PresentSrc: return vk::ImageLayout::ePresentSrcKHR;
                default: return vk::ImageLayout::eUndefined;
                }
            };

            vk::ImageLayout oldLayout = toVkLayout(last.layout);
            vk::ImageLayout newLayout = toVkLayout(desired.layout);

            if (oldLayout == newLayout)
                return; // no barrier needed

            auto [srcStage, srcAccess] = utils::makePipelineStageAccessTuple(oldLayout);
            auto [dstStage, dstAccess] = utils::makePipelineStageAccessTuple(newLayout);

            // Use your existing helper
            utils::transition_image_layout
            (
                Unwrap(cmd),
                (id.index == UINT32_MAX) ? swapChainImages[imageIndex] : m_RenderTargetImages[id.index].image,
                oldLayout,
                newLayout,
                srcAccess,
                dstAccess,
                srcStage,
                dstStage,
                utils::AspectFromLayout(desired.layout)
            );
        }
        else if (id.type == ResourceType::Buffer)
        {
            auto buffer = static_cast<VkBuffer>(id.buffer->GetNativeHandle());

            vk::PipelineStageFlags2 srcStage = StageFromResourceState(last);
            vk::PipelineStageFlags2 dstStage = StageFromResourceState(desired);

            vk::AccessFlags2 srcAccess = utils::inferAccessMaskFromStage(srcStage, true);
            vk::AccessFlags2 dstAccess = utils::inferAccessMaskFromStage(dstStage, false);

            utils::cmdBufferMemoryBarrier
            (
                Unwrap(cmd),
                buffer,
                srcStage,
                dstStage,
                srcAccess,
                dstAccess
            );
        }
    }

    int32_t VulkanRendererAPI::ReadEntityIDAtPixel(uint32_t imageIndex, uint32_t x, uint32_t y)
    {
        // Clamp coordinates to viewport
        x = std::min(x, viewport.width - 1);
        y = std::min(y, viewport.height - 1);

        // Create readback buffer if needed (only once)
        if (!*entityReadbackBuffer.buffer)
        {
            entityReadbackBuffer = m_allocator.createBuffer(
                sizeof(int32_t), /*sizeof(int32_t) * viewport.width * viewport.height,*/
                vk::BufferUsageFlagBits2::eTransferDst,
                vma::MemoryUsage::eGpuToCpu
            );
        }

        /*
        // Wait for GPU to finish rendering
        queue.waitIdle();*/

        // Transition entity image to transfer source
        /*auto cmd = utils::beginSingleTimeCommands(device, commandPool);*/

        utils::transition_image_layout
        (
            commandBuffers[frameIndex],
            *m_RenderTargetImages[imageIndex].image,
            entityImageInitialized ? vk::ImageLayout::eColorAttachmentOptimal : vk::ImageLayout::eUndefined,
            vk::ImageLayout::eTransferSrcOptimal,
            vk::AccessFlagBits2::eColorAttachmentWrite,
            vk::AccessFlagBits2::eTransferRead,
            vk::PipelineStageFlagBits2::eColorAttachmentOutput,
            vk::PipelineStageFlagBits2::eTransfer,
            vk::ImageAspectFlagBits::eColor
        );

        // Copy image to buffer
        vk::BufferImageCopy copyRegion{
            .bufferOffset = 0,
            .bufferRowLength = 0,
            .bufferImageHeight = 0,
            .imageSubresource = {vk::ImageAspectFlagBits::eColor, 0, 0, 1},
            .imageOffset = {static_cast<int32_t>(x), static_cast<int32_t>(y), 0}, /*.imageOffset = {static_cast<int32_t>(x), static_cast<int32_t>(y), 0},*/
            .imageExtent = {1, 1, 1} /*.imageExtent = {1, 1, 1} */
        };


        commandBuffers[frameIndex].copyImageToBuffer(
            *m_RenderTargetImages[imageIndex].image,
            vk::ImageLayout::eTransferSrcOptimal,
            entityReadbackBuffer.buffer,
            {copyRegion}
        );

        // Transition back
        utils::transition_image_layout
        (
            commandBuffers[frameIndex],
            *m_RenderTargetImages[imageIndex].image,
            vk::ImageLayout::eTransferSrcOptimal,
            vk::ImageLayout::eColorAttachmentOptimal,
            vk::AccessFlagBits2::eTransferRead,
            vk::AccessFlagBits2::eColorAttachmentWrite,
            vk::PipelineStageFlagBits2::eTransfer,
            vk::PipelineStageFlagBits2::eColorAttachmentOutput,
            vk::ImageAspectFlagBits::eColor
        );

        /*utils::endSingleTimeCommands(*cmd, queue);*/

        // Map and read the pixel
        void* mappedData = entityReadbackBuffer.buffer.getAllocation().map();
        int32_t* entityIDs = static_cast<int32_t*>(mappedData);

        /*int32_t entityID = entityIDs[y * viewport.width + x];*/
        int32_t entityID = entityIDs[0];

        entityReadbackBuffer.buffer.getAllocation().unmap();

        return entityID;
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

        vk::PipelineBindPoint vkBindPoint = {};

        switch (pipelineBindPoint)
        {
        case VanKPipelineBindPoint::Graphics: vkBindPoint = vk::PipelineBindPoint::eGraphics;
            break;
        case VanKPipelineBindPoint::Compute: vkBindPoint = vk::PipelineBindPoint::eCompute;
            break;
        case VanKPipelineBindPoint::Raytracing: vkBindPoint = vk::PipelineBindPoint::eRayTracingKHR;
            break;
        default: std::cout << "[VulkanRendererAPI::BindPipeline] Invalid pipeline bind point:" << '\n';
            break;
        }

        Unwrap(cmd).bindPipeline(vkBindPoint, pipelineToBind);

        // Update current pipeline layout for push descriptors / push constants
        if (pipelineBindPoint == VanKPipelineBindPoint::Graphics)
            m_currentGraphicPipelineLayout = layoutToBind;
        else if (pipelineBindPoint == VanKPipelineBindPoint::Compute)
            m_currentComputePipelineLayout = layoutToBind;
        else if (pipelineBindPoint == VanKPipelineBindPoint::Raytracing)
            m_currentRaytracingPipelineLayout = layoutToBind;
    }

#ifndef LAB_TASK_LEVEL
#	define LAB_TASK_LEVEL 11
#endif

#define LAB_TASK_AS_BUILD_AND_BIND 4
#define LAB_TASK_AS_ANIMATION 6
#define LAB_TASK_AS_OPAQUE_FLAG 7
#define LAB_TASK_INSTANCE_LUT 9
#define LAB_TASK_REFLECTIONS 11

    std::vector<vk::raii::Buffer> blasBuffers;
    std::vector<vk::raii::DeviceMemory> blasMemories;
    std::vector<vk::raii::AccelerationStructureKHR> blasHandles;

    std::vector<vk::AccelerationStructureInstanceKHR> instances;
    vk::raii::Buffer instanceBuffer = nullptr;
    vk::raii::DeviceMemory instanceMemory = nullptr;

    vk::raii::Buffer tlasBuffer = nullptr;
    vk::raii::DeviceMemory tlasMemory = nullptr;
    vk::raii::Buffer tlasScratchBuffer = nullptr;
    vk::raii::DeviceMemory tlasScratchMemory = nullptr;
    vk::raii::AccelerationStructureKHR tlas = nullptr;

    /*
    struct InstanceLUT
    {
        uint32_t materialID;
        uint32_t indexBufferOffset;
    };
    std::vector<InstanceLUT> instanceLUTs;
    vk::raii::Buffer         instanceLUTBuffer       = nullptr;
    vk::raii::DeviceMemory   instanceLUTBufferMemory = nullptr;
    */

    void VulkanRendererAPI::BindUniformBuffer(VanKCommandBuffer cmd, VanKPipelineBindPoint bindPoint, UniformBuffer* buffer, uint32_t set, uint32_t binding, uint32_t arrayElement)
    {
        vk::PipelineLayout layout = VK_NULL_HANDLE;

        if (bindPoint == VanKPipelineBindPoint::Graphics) layout = m_currentGraphicPipelineLayout;
        if (bindPoint == VanKPipelineBindPoint::Compute) layout = m_currentComputePipelineLayout;
        if (bindPoint == VanKPipelineBindPoint::Raytracing) layout = m_currentRaytracingPipelineLayout;

        VulkanUniformBuffer* vulkanUBO = dynamic_cast<VulkanUniformBuffer*>(buffer);
        if (!vulkanUBO)
        {
            return;
        }

        const utils::Buffer& vkBuffer = vulkanUBO->GetBuffer();

        // Setting up push descriptor information, we could choose dynamically the buffer to work on
        const vk::DescriptorBufferInfo bufferInfo = {.buffer = vkBuffer.buffer, .offset = 0, .range = vk::WholeSize};

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
        else if (bindPoint == VanKPipelineBindPoint::Graphics)
        {
            stage_flags = vk::ShaderStageFlagBits::eAllGraphics;
        }
        else if (bindPoint == VanKPipelineBindPoint::Raytracing)
        {
            stage_flags = vk::ShaderStageFlagBits::eRaygenKHR | vk::ShaderStageFlagBits::eClosestHitKHR | vk::ShaderStageFlagBits::eAnyHitKHR | vk::ShaderStageFlagBits::eMissKHR;
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

    void VulkanRendererAPI::BeginRendering
    (
        VanKCommandBuffer cmd,
        const VanKColorTargetInfo* color_target_info = {},
        uint32_t num_color_targets = 0,
        VanKDepthStencilTargetInfo depth_stencil_target_info = {}
    )
    {
        DBG_VK_SCOPE(Unwrap(cmd)); // <-- Helps to debug in NSight

        // Depth attachment
        vk::RenderingAttachmentInfo depthAttachment{};
        if (depth_stencil_target_info.format != VanK_FORMAT_INVALID)
        {
            auto& depthImageTarget = m_RenderTargetImages[depth_stencil_target_info.imageIndex];
            vk::ClearDepthStencilValue clearValue{depth_stencil_target_info.clearColor.f[0], 0};
            depthAttachment = vk::RenderingAttachmentInfo
            {
                .imageView = depthImageTarget.view,
                .imageLayout = vk::ImageLayout::eDepthAttachmentOptimal,
                .loadOp = ConvertToVkLoadOp(depth_stencil_target_info.loadOp),
                .storeOp = ConvertToVkStoreOp(depth_stencil_target_info.storeOp),
                .clearValue = clearValue
            };
        }

        // Color attachment
        std::vector<vk::RenderingAttachmentInfo> colorAttachments;
        colorAttachments.reserve(num_color_targets);

        for (uint32_t i = 0; i < num_color_targets; i++)
        {
            const auto& ct = color_target_info[i];

            vk::RenderingAttachmentInfo attachment{};
            if (ct.format == VanK_FORMAT_SWAPCHAIN)
            {
                attachment.imageView = swapChainImageViews[imageIndex];
            }
            else
            {
                auto& colorImageTarget = m_RenderTargetImages[ct.imageIndex];
                attachment.imageView = colorImageTarget.view;

                // Handle resolve images
                if (colorImageTarget.isResolveImage)
                {
                    auto& resolveImageTarget = m_RenderTargetImages[colorImageTarget.resolveTargetID];
                    attachment.resolveImageView = resolveImageTarget.view;
                    attachment.resolveImageLayout = vk::ImageLayout::eColorAttachmentOptimal;

                    // Resolve mode: simple heuristic (could store per-resource if needed)
                    attachment.resolveMode = (ct.format == VanK_FORMAT_R32_SINT)
                                                 ? vk::ResolveModeFlagBits::eSampleZero
                                                 : vk::ResolveModeFlagBits::eAverage;
                }
            }
            attachment.imageLayout = vk::ImageLayout::eColorAttachmentOptimal;
            attachment.loadOp = ConvertToVkLoadOp(ct.loadOp);
            attachment.storeOp = ConvertToVkStoreOp(ct.storeOp);
            attachment.clearValue = ConvertToVkClearColor(ct.clearColor, ConvertToVkFormat(ct.format));

            colorAttachments.push_back(attachment);
        }

        vk::Extent2D renderExtent{};

        if (color_target_info[0].format == VanK_FORMAT_SWAPCHAIN)
        {
            renderExtent = swapChainExtent;
        }
        else
        {
            auto& firstColor = m_RenderTargetImages[color_target_info[0].imageIndex];

            renderExtent =
            {
                firstColor.extent.width,
                firstColor.extent.height
            };
        }

        vk::RenderingInfo renderingInfo =
        {
            .renderArea = {.offset = {0, 0}, .extent = renderExtent},
            .layerCount = 1,
            .colorAttachmentCount = static_cast<uint32_t>(colorAttachments.size()),
            .pColorAttachments = colorAttachments.data(),
            .pDepthAttachment = (depth_stencil_target_info.format != VanK_FORMAT_INVALID) ? &depthAttachment : nullptr
        };

        Unwrap(cmd).beginRendering(renderingInfo);
    }

    void VulkanRendererAPI::SetViewport(VanKCommandBuffer cmd, uint32_t viewportCount, VanKViewport vanKViewports)
    {
        vk::Viewport vk_viewport{vanKViewports.x, vanKViewports.y, vanKViewports.width, vanKViewports.height, vanKViewports.minDepth, vanKViewports.maxDepth};

        // Wrap the single viewport in an ArrayProxy (RAII-friendly)
        std::vector viewports(viewportCount, vk_viewport);

        Unwrap(cmd).setViewportWithCount(viewports);
    }

    void VulkanRendererAPI::SetScissor(VanKCommandBuffer cmd, uint32_t scissorCount, VankRect scissor)
    {
        vk::Rect2D vk_scissor{vk::Offset2D(scissor.x, scissor.y), {scissor.width, scissor.height}};

        // Wrap the single scissor in an ArrayProxy (RAII-friendly)
        std::vector scissors(scissorCount, vk_scissor);

        Unwrap(cmd).setScissorWithCount(scissors);
    }

    void VulkanRendererAPI::SetLineWidth(VanKCommandBuffer cmd, float lineWidth)
    {
        Unwrap(cmd).setLineWidth(lineWidth);
    }

    void VulkanRendererAPI::SetCullMode(VanKCommandBuffer cmd, VanKCullModeFlags cullMode)
    {
        Unwrap(cmd).setCullMode(ConvertToVkCullMode(cullMode));
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
        case VanKIndexElementSize::Uint16: vkIndexType = vk::IndexType::eUint16;
            break;
        case VanKIndexElementSize::Uint32: vkIndexType = vk::IndexType::eUint32;
            break;
        }

        Unwrap(cmd).bindIndexBuffer(buffer, 0, vkIndexType);
    }

    void VulkanRendererAPI::PushConstans(VanKCommandBuffer cmd, VanKShaderStageFlags stageFlags, uint32_t slot, const void* data, uint32_t dataSize)
    {
        vk::PipelineLayout layout = VK_NULL_HANDLE;

        vk::ShaderStageFlags flag = ConvertToVkShaderStageFlagBits(stageFlags);

        if (flag & (vk::ShaderStageFlagBits::eTaskEXT | vk::ShaderStageFlagBits::eMeshEXT | vk::ShaderStageFlagBits::eFragment))
        {
            layout = m_currentGraphicPipelineLayout;
        }
        else if (flag & vk::ShaderStageFlagBits::eCompute)
        {
            layout = m_currentComputePipelineLayout;
        }
        else if (flag & (vk::ShaderStageFlagBits::eRaygenKHR | vk::ShaderStageFlagBits::eClosestHitKHR | vk::ShaderStageFlagBits::eAnyHitKHR | vk::ShaderStageFlagBits::eMissKHR))
        {
            layout = m_currentRaytracingPipelineLayout;
        }

        if (!layout)
        {
            std::cout << "ERROR: PushConstants layout is NULL for raytracing!" << std::endl;
            return;
        }

        vk::PushConstantsInfo pushConstantsInfo
        {
            .layout = layout,
            .stageFlags = flag,
            .offset = slot,
            .size = dataSize,
            .pValues = data,
        };

        Unwrap(cmd).pushConstants2(pushConstantsInfo);
    }

    void VulkanRendererAPI::Draw(VanKCommandBuffer cmd, uint32_t vertexCount, uint32_t instanceCount, uint32_t firstVertex, uint32_t firstInstance)
    {
        Unwrap(cmd).draw(vertexCount, instanceCount, firstVertex, firstInstance);
    }

    void VulkanRendererAPI::DrawIndexed(VanKCommandBuffer cmd, uint32_t indexCount, uint32_t instanceCount, uint32_t firstIndex, int32_t vertexOffset, uint32_t firstInstance)
    {
        Unwrap(cmd).drawIndexed(indexCount, instanceCount, firstIndex, vertexOffset, firstInstance);
    }

    void VulkanRendererAPI::DrawIndirectCount(VanKCommandBuffer cmd, IndirectBuffer& indirectBuffer, uint32_t indirectBufferOffset, IndirectBuffer& countBuffer, uint32_t countBufferOffset,
                                              uint32_t maxDrawCount, uint32_t stride)
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

        Unwrap(cmd).drawIndirectCount(bufferIndirect, indirectBufferOffset, bufferCount, countBufferOffset, maxDrawCount, stride);
    }

    void VulkanRendererAPI::DrawIndexedIndirectCount(VanKCommandBuffer cmd, IndirectBuffer& indirectBuffer, uint32_t indirectBufferOffset, IndirectBuffer& countBuffer, uint32_t countBufferOffset,
                                                     uint32_t maxDrawCount, uint32_t stride)
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

    void VulkanRendererAPI::DrawMeshTasks(VanKCommandBuffer cmd, uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ)
    {
        Unwrap(cmd).drawMeshTasksEXT(groupCountX, groupCountY, groupCountZ);
    }

    void VulkanRendererAPI::DrawMeshTasksIndirect(VanKCommandBuffer cmd, IndirectBuffer& indirectBuffer, uint32_t indirectBufferOffset, uint32_t maxDrawCount, uint32_t stride)
    {
        if (stride < sizeof(vk::DrawMeshTasksIndirectCommandEXT))
            throw std::runtime_error("drawMeshTasksIndirectCount: stride too small");

        // Cast to VulkanVertexBuffer
        const VulkanIndirectBuffer* vulkanIB = dynamic_cast<const VulkanIndirectBuffer*>(&indirectBuffer);
        if (!vulkanIB)
            throw std::runtime_error("drawMeshTasksIndirectCount: indirectBuffer is not a VulkanIndirectBuffer");

        const utils::Buffer& vkBuffer = vulkanIB->GetBuffer();
        vk::Buffer bufferIndirect = vkBuffer.buffer; // The actual VkBuffer

        Unwrap(cmd).drawMeshTasksIndirectEXT(bufferIndirect, indirectBufferOffset, maxDrawCount, stride);
    }

    void VulkanRendererAPI::DrawMeshTasksIndirectCount(VanKCommandBuffer cmd, IndirectBuffer& indirectBuffer, uint32_t indirectBufferOffset, IndirectBuffer& countBuffer, uint32_t countBufferOffset,
                                                       uint32_t maxDrawCount, uint32_t stride)
    {
        if (stride < sizeof(vk::DrawMeshTasksIndirectCommandEXT))
            throw std::runtime_error("drawMeshTasksIndirectCount: stride too small");

        // Cast to VulkanVertexBuffer
        const VulkanIndirectBuffer* vulkanIB = dynamic_cast<const VulkanIndirectBuffer*>(&indirectBuffer);
        if (!vulkanIB)
            throw std::runtime_error("drawMeshTasksIndirectCount: indirectBuffer is not a VulkanIndirectBuffer");

        const utils::Buffer& vkBuffer = vulkanIB->GetBuffer();
        vk::Buffer bufferIndirect = vkBuffer.buffer; // The actual VkBuffer

        // Cast to VulkanVertexBuffer
        const VulkanIndirectBuffer* vulkanCB = dynamic_cast<const VulkanIndirectBuffer*>(&countBuffer);
        if (!vulkanCB)
            throw std::runtime_error("drawMeshTasksIndirectCount: countBuffer is not a VulkanIndirectBuffer");

        const utils::Buffer& vkBufferCount = vulkanCB->GetBuffer();
        vk::Buffer bufferCount = vkBufferCount.buffer; // The actual VkBuffer

        Unwrap(cmd).drawMeshTasksIndirectCountEXT(bufferIndirect, indirectBufferOffset, bufferCount, countBufferOffset, maxDrawCount, stride);
    }

    void VulkanRendererAPI::TraceRays(VanKCommandBuffer cmd, uint32_t width, uint32_t height)
    {
        Unwrap(cmd).traceRaysKHR(m_raygenRegion, m_missRegion, m_hitRegion, m_callableRegion, width, height, 1);
    }

    void VulkanRendererAPI::EndRendering(VanKCommandBuffer cmd)
    {
        Unwrap(cmd).endRendering();
        sceneImageInitialized = false;
        entityImageInitialized = false;
        entityColorImageInitialized = false;
        m_hasActiveRenderPass = false;
    }

    void VulkanRendererAPI::RenderImGui(VanKCommandBuffer cmd)
    {
        ImGui::Render(); // This is creating the data to draw the UI (not on GPU yet)

        ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), *Unwrap(cmd));
    }

    void VulkanRendererAPI::SubmitRendering(VanKCommandBuffer cmd, uint32_t renderTargetImage)
    {
        if (m_renderOption == VanK_Render_ImGui)
        {
            // Transition the swapchain image to COLOR_ATTACHMENT_OPTIMAL
            utils::transition_image_layout
            (
                Unwrap(cmd),
                swapChainImages[imageIndex],
                sceneImageInitialized ? vk::ImageLayout::ePresentSrcKHR : vk::ImageLayout::eUndefined,
                vk::ImageLayout::eColorAttachmentOptimal,
                {}, // srcAccessMask (no need to wait for previous operations)
                vk::AccessFlagBits2::eColorAttachmentWrite, // dstAccessMask
                vk::PipelineStageFlagBits2::eTopOfPipe, // srcStage
                vk::PipelineStageFlagBits2::eColorAttachmentOutput, // dstStage,
                vk::ImageAspectFlagBits::eColor
            );

            // Second pass: draw ImGui to swapchain image
            vk::RenderingAttachmentInfo swapColorAttachment =
            {
                .imageView = swapChainImageViews[imageIndex],
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

            ImGui::Render(); // This is creating the data to draw the UI (not on GPU yet)

            ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), *Unwrap(cmd));

            Unwrap(cmd).endRendering();

            // Transition the swapchain image to PRESENT_SRC
            utils::transition_image_layout
            (
                Unwrap(cmd),
                swapChainImages[imageIndex],
                vk::ImageLayout::eColorAttachmentOptimal,
                vk::ImageLayout::ePresentSrcKHR,
                vk::AccessFlagBits2::eColorAttachmentWrite, // srcAccessMask
                {}, // dstAccessMask
                vk::PipelineStageFlagBits2::eColorAttachmentOutput, // srcStage
                vk::PipelineStageFlagBits2::eBottomOfPipe, // dstStage,
                vk::ImageAspectFlagBits::eColor
            );

            return;
        }

        if (m_renderOption == VanK_Render_Swapchain)
        {
            // Transition the swapchain image to COLOR_ATTACHMENT_OPTIMAL
            utils::transition_image_layout
            (
                Unwrap(cmd),
                swapChainImages[imageIndex],
                sceneImageInitialized ? vk::ImageLayout::ePresentSrcKHR : vk::ImageLayout::eUndefined,
                vk::ImageLayout::eColorAttachmentOptimal,
                {}, // srcAccessMask (no need to wait for previous operations)
                vk::AccessFlagBits2::eColorAttachmentWrite, // dstAccessMask
                vk::PipelineStageFlagBits2::eTopOfPipe, // srcStage
                vk::PipelineStageFlagBits2::eColorAttachmentOutput, // dstStage,
                vk::ImageAspectFlagBits::eColor
            );

            if (renderTargetImage != UINT32_MAX)
            {
                // Transition images before blit
                utils::transition_image_layout
                (
                    Unwrap(cmd),
                    m_RenderTargetImages[renderTargetImage].image,
                    vk::ImageLayout::eShaderReadOnlyOptimal,
                    vk::ImageLayout::eTransferSrcOptimal,
                    vk::AccessFlagBits2::eShaderRead,
                    vk::AccessFlagBits2::eTransferRead,
                    vk::PipelineStageFlagBits2::eFragmentShader,
                    vk::PipelineStageFlagBits2::eTransfer,
                    vk::ImageAspectFlagBits::eColor
                );

                utils::transition_image_layout
                (
                    Unwrap(cmd),
                    swapChainImages[imageIndex],
                    vk::ImageLayout::eColorAttachmentOptimal,
                    vk::ImageLayout::eTransferDstOptimal,
                    {},
                    vk::AccessFlagBits2::eTransferWrite,
                    vk::PipelineStageFlagBits2::eBottomOfPipe,
                    vk::PipelineStageFlagBits2::eTransfer,
                    vk::ImageAspectFlagBits::eColor
                );

                vk::ImageSubresourceLayers subresource;
                subresource.aspectMask = vk::ImageAspectFlagBits::eColor;
                subresource.baseArrayLayer = 0;
                subresource.layerCount = 1;
                subresource.mipLevel = 0;

                vk::ImageBlit2 blitRegion{};
                blitRegion.srcSubresource = subresource;
                blitRegion.srcOffsets[0] = {0, 0, 0};
                blitRegion.srcOffsets[1] = {static_cast<int32_t>(m_RenderTargetImages[renderTargetImage].extent.width), static_cast<int32_t>(m_RenderTargetImages[renderTargetImage].extent.height), 1};
                blitRegion.dstSubresource = subresource;
                blitRegion.dstOffsets[0] = {0, 0, 0};
                blitRegion.dstOffsets[1] = {static_cast<int32_t>(swapChainExtent.width), static_cast<int32_t>(swapChainExtent.height), 1};

                vk::BlitImageInfo2 blitInfo{};
                blitInfo.srcImage = m_RenderTargetImages[renderTargetImage].image;
                blitInfo.srcImageLayout = vk::ImageLayout::eTransferSrcOptimal;
                blitInfo.dstImage = swapChainImages[imageIndex];
                blitInfo.dstImageLayout = vk::ImageLayout::eTransferDstOptimal;
                blitInfo.regionCount = 1;
                blitInfo.pRegions = &blitRegion;
                blitInfo.filter = vk::Filter::eNearest;

                Unwrap(cmd).blitImage2(blitInfo);

                // After rendering, transition the images to appropriate layouts

                // Transition images before blit
                utils::transition_image_layout
                (
                    Unwrap(cmd),
                    m_RenderTargetImages[renderTargetImage].image,
                    vk::ImageLayout::eTransferSrcOptimal,
                    vk::ImageLayout::eShaderReadOnlyOptimal,
                    vk::AccessFlagBits2::eTransferRead,
                    vk::AccessFlagBits2::eColorAttachmentWrite,
                    vk::PipelineStageFlagBits2::eTransfer,
                    vk::PipelineStageFlagBits2::eColorAttachmentOutput,
                    vk::ImageAspectFlagBits::eColor
                );

                // Transition the swapchain image to PRESENT_SRC
                utils::transition_image_layout
                (
                    Unwrap(cmd),
                    swapChainImages[imageIndex],
                    vk::ImageLayout::eTransferDstOptimal,
                    vk::ImageLayout::ePresentSrcKHR,
                    vk::AccessFlagBits2::eTransferWrite, // srcAccessMask
                    {}, // dstAccessMask
                    vk::PipelineStageFlagBits2::eTransfer, // srcStage
                    vk::PipelineStageFlagBits2::eBottomOfPipe, // dstStage
                    vk::ImageAspectFlagBits::eColor
                );
            }
            else
            {
                VK_CORE_ERROR("SubmitRendering Swapchain no renderTargetImage provided fallback to direct SwapChain");

                vk::ClearColorValue clearColor{std::array{0.5f, 0.5f, 0.5f, 1.0f}};

                vk::ImageSubresourceRange range;
                range.aspectMask = vk::ImageAspectFlagBits::eColor;
                range.baseMipLevel = 0;
                range.levelCount = 1;
                range.baseArrayLayer = 0;
                range.layerCount = 1;

                utils::transition_image_layout
                (
                    Unwrap(cmd),
                    swapChainImages[imageIndex],
                    vk::ImageLayout::eColorAttachmentOptimal,
                    vk::ImageLayout::eTransferDstOptimal,
                    {},
                    vk::AccessFlagBits2::eTransferWrite,
                    vk::PipelineStageFlagBits2::eBottomOfPipe,
                    vk::PipelineStageFlagBits2::eTransfer,
                    vk::ImageAspectFlagBits::eColor
                );

                vk::ImageSubresourceRange ranges[] = {range};

                Unwrap(cmd).clearColorImage
                (
                    swapChainImages[imageIndex],
                    vk::ImageLayout::eTransferDstOptimal,
                    clearColor, // pass by reference, not pointer
                    ranges // ArrayProxy will implicitly wrap the array
                );

                utils::transition_image_layout(
                    Unwrap(cmd),
                    swapChainImages[imageIndex],
                    vk::ImageLayout::eTransferDstOptimal,
                    vk::ImageLayout::ePresentSrcKHR,
                    vk::AccessFlagBits2::eTransferWrite,
                    {},
                    vk::PipelineStageFlagBits2::eTransfer,
                    vk::PipelineStageFlagBits2::eBottomOfPipe,
                    vk::ImageAspectFlagBits::eColor
                );
            }
        }
    }

    void VulkanRendererAPI::BindFragmentSamplers(VanKCommandBuffer cmd, uint32_t firstSlot, const TextureSamplerBinding* samplers, uint32_t num_bindings, bool isRayTracing)
    {
        //layout could be raytracing no or should BindRayTracing handle textures to ?
        // Ensure the descriptor set exists and the first handle is valid
        if (descriptorSets.empty())
        {
            std::cout << "Descriptor set array is empty!" << '\n';
            return;
        }

        //might have to change this i tryed putting updatedescriptor set here but idk do i need this in bindless ?
        // TextureSamplerBinding is empty check rendererapi strcut
        /*-- 
         * Binding the resources passed to the shader, using the descriptor set (holds the texture) 
         * There are two descriptor layouts, one for the texture and one for the scene information,
         * but only the texture is a set, the scene information is a push descriptor.
        -*/
        vk::DescriptorSet rawDescriptorSet = *descriptorSets[0];
        if (!*descriptorSets[0] || rawDescriptorSet == VK_NULL_HANDLE)
        {
            std::cout << "Descriptor set raw handle is invalid!" << '\n';
            return;
        }
        vk::BindDescriptorSetsInfoKHR bindDescriptorSetsInfo =
        {
            .stageFlags = (isRayTracing) ? vk::ShaderStageFlagBits::eRaygenKHR | vk::ShaderStageFlagBits::eClosestHitKHR | vk::ShaderStageFlagBits::eAnyHitKHR | vk::ShaderStageFlagBits::eMissKHR : vk::ShaderStageFlagBits::eAllGraphics | vk::ShaderStageFlagBits::eTaskEXT | vk::ShaderStageFlagBits::eMeshEXT,
            .layout = (isRayTracing) ? m_currentRaytracingPipelineLayout : m_currentGraphicPipelineLayout,
            .firstSet = 0,
            .descriptorSetCount = 1,
            .pDescriptorSets = &rawDescriptorSet,
        };
        Unwrap(cmd).bindDescriptorSets2(bindDescriptorSetsInfo);
    }

    void VulkanRendererAPI::BindRayTracing
    (
        VanKCommandBuffer cmd,
        bool useRayQuery,
        uint32_t renderTargetImageIndex,
        bool isfinalRenderPass,
        uint32_t rasterRenderTargetImageIndex,
        uint32_t rayTraceRenderTargetImageIndex
    )
    {
        // Ensure the descriptor set exists and the first handle is valid
        if (raytraceDescriptorSet.empty())
        {
            std::cout << "Descriptor set array is empty!" << '\n';
            return;
        }

        std::vector<vk::WriteDescriptorSet> descriptorWrites;
        descriptorWrites.reserve(2);

        if (!isfinalRenderPass)
        {
            vk::WriteDescriptorSetAccelerationStructureKHR asInfo
            {
                .accelerationStructureCount = 1,
                .pAccelerationStructures = {&*tlas}
            };

            vk::WriteDescriptorSet asWrite
            {
                .pNext = &asInfo,
                .dstSet = raytraceDescriptorSet[0],
                .dstBinding = 0,
                .dstArrayElement = 0,
                .descriptorCount = 1,
                .descriptorType = vk::DescriptorType::eAccelerationStructureKHR
            };
            descriptorWrites.emplace_back(asWrite);

            if (!useRayQuery)
            {
                vk::DescriptorImageInfo imageInfo
                {
                    .sampler = VK_NULL_HANDLE,
                    .imageView = m_RenderTargetImages[renderTargetImageIndex].view,
                    .imageLayout = vk::ImageLayout::eGeneral
                };

                vk::WriteDescriptorSet imageWrite
                {
                    .dstSet = raytraceDescriptorSet[0],
                    .dstBinding = 1,
                    .descriptorCount = 1,
                    .descriptorType = vk::DescriptorType::eStorageImage,
                    .pImageInfo = &imageInfo
                };
                descriptorWrites.emplace_back(imageWrite);
            }
        }
        else
        {
            if (rasterRenderTargetImageIndex != UINT32_MAX)
            {
                vk::DescriptorImageInfo imageInfo
                {
                    .sampler = m_RenderTargetImages[rasterRenderTargetImageIndex].sampler,
                    .imageView = m_RenderTargetImages[rasterRenderTargetImageIndex].view,
                    .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal
                };

                vk::WriteDescriptorSet imageWrite
                {
                    .dstSet = raytraceDescriptorSet[0],
                    .dstBinding = 2,
                    .descriptorCount = 1,
                    .descriptorType = vk::DescriptorType::eCombinedImageSampler,
                    .pImageInfo = &imageInfo
                };
                descriptorWrites.emplace_back(imageWrite);
            }

            if (rayTraceRenderTargetImageIndex != UINT32_MAX)
            {
                vk::DescriptorImageInfo imageInfo2
                {
                    .sampler = m_RenderTargetImages[rayTraceRenderTargetImageIndex].sampler,
                    .imageView = m_RenderTargetImages[rayTraceRenderTargetImageIndex].view,
                    .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal
                };

                vk::WriteDescriptorSet imageWrite2
                {
                    .dstSet = raytraceDescriptorSet[0],
                    .dstBinding = 3,
                    .descriptorCount = 1,
                    .descriptorType = vk::DescriptorType::eCombinedImageSampler,
                    .pImageInfo = &imageInfo2
                };

                descriptorWrites.emplace_back(imageWrite2);
            }
        }
        device.updateDescriptorSets(descriptorWrites, {});

        //might have to change this i tryed putting updatedescriptor set here but idk do i need this in bindless ?
        // TextureSamplerBinding is empty check rendererapi strcut
        /*-- 
         * Binding the resources passed to the shader, using the descriptor set (holds the texture) 
         * There are two descriptor layouts, one for the texture and one for the scene information,
         * but only the texture is a set, the scene information is a push descriptor.
        -*/
        vk::DescriptorSet rawDescriptorSetRay = *raytraceDescriptorSet[0];
        if (!*raytraceDescriptorSet[0] || rawDescriptorSetRay == VK_NULL_HANDLE)
        {
            std::cout << "Descriptor set raw handle is invalid!" << '\n';
            return;
        }
        vk::BindDescriptorSetsInfoKHR bindDescriptorSetsInforay =
        {
            .stageFlags = (useRayQuery) ? vk::ShaderStageFlagBits::eAllGraphics | vk::ShaderStageFlagBits::eTaskEXT | vk::ShaderStageFlagBits::eMeshEXT : vk::ShaderStageFlagBits::eRaygenKHR | vk::ShaderStageFlagBits::eClosestHitKHR | vk::ShaderStageFlagBits::eMissKHR,
            .layout = (useRayQuery) ? m_currentGraphicPipelineLayout : m_currentRaytracingPipelineLayout,
            .firstSet = 2,
            .descriptorSetCount = 1,
            .pDescriptorSets = &rawDescriptorSetRay,
        };
        Unwrap(cmd).bindDescriptorSets2(bindDescriptorSetsInforay);
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

    void VulkanRendererAPI::generateMipmaps(vk::raii::Image& image, vk::Format imageFormat, int32_t texWidth, int32_t texHeight,
                                            uint32_t mipLevels, uint32_t layerCount)
    {
        // Check if image format supports linear blit-ing
        vk::FormatProperties formatProperties = physicalDevice.getFormatProperties(imageFormat);

        if (!(formatProperties.optimalTilingFeatures & vk::FormatFeatureFlagBits::eSampledImageFilterLinear))
        {
            throw std::runtime_error("texture image format does not support linear blitting!");
        }

        std::unique_ptr<vk::raii::CommandBuffer> commandBuffer = utils::beginSingleTimeCommands(device, commandPool);

        vk::ImageMemoryBarrier barrier = {
            .srcAccessMask = vk::AccessFlagBits::eTransferWrite, .dstAccessMask = vk::AccessFlagBits::eTransferRead,
            .oldLayout = vk::ImageLayout::eTransferDstOptimal, .newLayout = vk::ImageLayout::eTransferSrcOptimal,
            .srcQueueFamilyIndex = vk::QueueFamilyIgnored, .dstQueueFamilyIndex = vk::QueueFamilyIgnored, .image = image
        };
        barrier.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount = layerCount;
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
            blit.srcSubresource = vk::ImageSubresourceLayers(vk::ImageAspectFlagBits::eColor, i - 1, 0, layerCount);
            blit.dstSubresource = vk::ImageSubresourceLayers(vk::ImageAspectFlagBits::eColor, i, 0, layerCount);

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

        utils::endSingleTimeCommands(*commandBuffer, queue);
    }

    vk::SampleCountFlagBits VulkanRendererAPI::getMaxUsableSampleCount() const
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

    vk::raii::ImageView VulkanRendererAPI::createImageView(vk::raii::Image& image, vk::Format format,
                                                           vk::ImageAspectFlags aspectFlags,
                                                           uint32_t mipLevels, uint32_t layerCount, vk::ImageViewType viewType)
    {
        vk::ImageViewCreateInfo viewInfo
        {
            .image = image,
            .viewType = viewType,
            .format = format,
            .subresourceRange = {aspectFlags, 0, mipLevels, 0, layerCount}
        };
        return vk::raii::ImageView(device, viewInfo);
    }

    void VulkanRendererAPI::createBuffer(vk::DeviceSize size, vk::BufferUsageFlags usage, vk::MemoryPropertyFlags properties, vk::raii::Buffer& buffer, vk::raii::DeviceMemory& bufferMemory)
    {
        vk::BufferCreateInfo bufferInfo{
            .size = size,
            .usage = usage,
            .sharingMode = vk::SharingMode::eExclusive
        };
        buffer = vk::raii::Buffer(device, bufferInfo);
        vk::MemoryRequirements memRequirements = buffer.getMemoryRequirements();
        vk::MemoryAllocateInfo allocInfo{
            .allocationSize = memRequirements.size,
            .memoryTypeIndex = findMemoryType(memRequirements.memoryTypeBits, properties)
        };
        vk::MemoryAllocateFlagsInfo allocFlagsInfo{};
        if (usage & vk::BufferUsageFlagBits::eShaderDeviceAddress)
        {
            allocFlagsInfo.flags = vk::MemoryAllocateFlagBits::eDeviceAddress;
            allocInfo.pNext = &allocFlagsInfo;
        }
        bufferMemory = vk::raii::DeviceMemory(device, allocInfo);
        buffer.bindMemory(bufferMemory, 0);
    }

    void VulkanRendererAPI::createAccelerationStructures
    (
        const StorageBuffer& vertexBuffer,
        const StorageBuffer& indexBuffer,
        std::vector<shaderio::MeshletPrimitive>& primitives,
        std::vector<shaderio::Material>& materials,
        std::vector<shaderio::InstanceLUT>& instanceLUTs,
        uint32_t renderTargetImageIndex
    )
    {
#if LAB_TASK_LEVEL >= LAB_TASK_AS_BUILD_AND_BIND
        /*vk::BufferDeviceAddressInfo vai{.buffer = *vertexBuffer};*/
        vk::DeviceAddress vertexAddr = vertexBuffer.GetBufferAddress();
        /*vk::BufferDeviceAddressInfo iai{.buffer = *indexBuffer};*/
        vk::DeviceAddress indexAddr = indexBuffer.GetBufferAddress();
        std::cout << std::dec << "vertaddr: " << vertexAddr << " indexaddr: " << indexAddr << std::endl;

        instances.reserve(primitives.size());
        blasBuffers.reserve(primitives.size());
        blasMemories.reserve(primitives.size());
        blasHandles.reserve(primitives.size());

        vk::TransformMatrixKHR identity{};
        identity.matrix = std::array<std::array<float, 4>, 3>{
            {
                std::array<float, 4>{1.f, 0.f, 0.f, 0.f},
                std::array<float, 4>{0.f, 1.f, 0.f, 0.f},
                std::array<float, 4>{0.f, 0.f, 1.f, 0.f}
            }
        };

        // TASK02: Build a bottom level acceleration structure for each submesh
        for (size_t i = 0; i < primitives.size(); ++i)
        {
            const auto& submesh = primitives[i];
            const auto& mat = materials[submesh.materialIndex];

            // Prepare the geometry data
            auto trianglesData = vk::AccelerationStructureGeometryTrianglesDataKHR{
                .vertexFormat = vk::Format::eR32G32B32Sfloat,
                .vertexData = vertexAddr,
                .vertexStride = sizeof(shaderio::Vertex),
                .maxVertex = submesh.maxVertex,
                .indexType = vk::IndexType::eUint32,
                .indexData = indexAddr + submesh.indexOffset * sizeof(uint32_t)
            };

            vk::AccelerationStructureGeometryDataKHR geometryData(trianglesData);

            vk::AccelerationStructureGeometryKHR blasGeometry{
                .geometryType = vk::GeometryTypeKHR::eTriangles,
                .geometry = geometryData,
                /*.flags = vk::GeometryFlagBitsKHR::eOpaque*/
            };
#	if LAB_TASK_LEVEL >= LAB_TASK_AS_OPAQUE_FLAG
            // TASK07
            blasGeometry.flags = (mat.transparent) ? vk::GeometryFlagBitsKHR::eNoDuplicateAnyHitInvocation : vk::GeometryFlagBitsKHR::eOpaque;
#	endif        // LAB_TASK_LEVEL >= LAB_TASK_AS_OPAQUE_FLAG

            vk::AccelerationStructureBuildGeometryInfoKHR blasBuildGeometryInfo{
                .type = vk::AccelerationStructureTypeKHR::eBottomLevel,
                .mode = vk::BuildAccelerationStructureModeKHR::eBuild,
                .geometryCount = 1,
                .pGeometries = &blasGeometry,
            };

            // Query the memory sizes that will be needed for this BLAS
            auto primitiveCount = static_cast<uint32_t>(submesh.indexCount / 3);

            vk::AccelerationStructureBuildSizesInfoKHR blasBuildSizes =
                device.getAccelerationStructureBuildSizesKHR(
                    vk::AccelerationStructureBuildTypeKHR::eDevice,
                    blasBuildGeometryInfo,
                    {primitiveCount});

            // Create a scratch buffer for the BLAS, this will hold temporary data
            // during the build process
            vk::raii::Buffer scratchBuffer = nullptr;
            vk::raii::DeviceMemory scratchMemory = nullptr;
            createBuffer(blasBuildSizes.buildScratchSize,
                         vk::BufferUsageFlagBits::eStorageBuffer |
                         vk::BufferUsageFlagBits::eShaderDeviceAddress,
                         vk::MemoryPropertyFlagBits::eDeviceLocal,
                         scratchBuffer, scratchMemory);

            // Save the scratch buffer address in the build info structure
            vk::BufferDeviceAddressInfo scratchAddressInfo{.buffer = *scratchBuffer};
            vk::DeviceAddress scratchAddr = device.getBufferAddressKHR(scratchAddressInfo);
            blasBuildGeometryInfo.scratchData.deviceAddress = scratchAddr;

            // Create a buffer for the BLAS itself now that we now the required size
            vk::raii::Buffer blasBuffer = nullptr;
            vk::raii::DeviceMemory blasMemory = nullptr;
            blasBuffers.emplace_back(std::move(blasBuffer));
            blasMemories.emplace_back(std::move(blasMemory));
            createBuffer(blasBuildSizes.accelerationStructureSize,
                         vk::BufferUsageFlagBits::eAccelerationStructureStorageKHR |
                         vk::BufferUsageFlagBits::eShaderDeviceAddress |
                         vk::BufferUsageFlagBits::eAccelerationStructureBuildInputReadOnlyKHR,
                         vk::MemoryPropertyFlagBits::eDeviceLocal,
                         blasBuffers[i], blasMemories[i]);

            // Create and store the BLAS handle
            vk::AccelerationStructureCreateInfoKHR blasCreateInfo{
                .buffer = blasBuffers[i],
                .offset = 0,
                .size = blasBuildSizes.accelerationStructureSize,
                .type = vk::AccelerationStructureTypeKHR::eBottomLevel,
            };

            blasHandles.emplace_back(device.createAccelerationStructureKHR(blasCreateInfo));

            // Save the BLAS handle in the build info structure
            blasBuildGeometryInfo.dstAccelerationStructure = blasHandles[i];

            // Prepare the build range for the BLAS
            vk::AccelerationStructureBuildRangeInfoKHR blasRangeInfo{
                .primitiveCount = primitiveCount,
                .primitiveOffset = 0,
                .firstVertex = submesh.firstVertex,
                .transformOffset = 0
            };

            // Build the BLAS
            auto cmd = utils::beginSingleTimeCommands(device, commandPool);
            cmd->buildAccelerationStructuresKHR({blasBuildGeometryInfo}, {&blasRangeInfo});
            utils::endSingleTimeCommands(*cmd, queue);

            // TASK03: Create a BLAS instance for the TLAS
            vk::AccelerationStructureDeviceAddressInfoKHR addrInfo{
                .accelerationStructure = *blasHandles[i]
            };
            vk::DeviceAddress blasDeviceAddr = device.getAccelerationStructureAddressKHR(addrInfo);

            vk::AccelerationStructureInstanceKHR instance{
                .transform = identity,
                .mask = 0xFF,
                .accelerationStructureReference = blasDeviceAddr
            };

            instances.push_back(instance);

#	if LAB_TASK_LEVEL >= LAB_TASK_INSTANCE_LUT
            // TASK09: store the instance look-up table entry
            instances[i].instanceCustomIndex = static_cast<uint32_t>(i);

            instanceLUTs.push_back({static_cast<uint32_t>(submesh.materialIndex), submesh.indexOffset, submesh.firstVertex});
#	endif        // LAB_TASK_LEVEL >= LAB_TASK_INSTANCE_LUT
        }

        // TASK03: Prepare the instance data buffer
        vk::DeviceSize instBufferSize = sizeof(instances[0]) * instances.size();
        createBuffer(instBufferSize,
                     vk::BufferUsageFlagBits::eShaderDeviceAddress |
                     vk::BufferUsageFlagBits::eTransferDst |
                     vk::BufferUsageFlagBits::eAccelerationStructureBuildInputReadOnlyKHR,
                     vk::MemoryPropertyFlagBits::eHostVisible |
                     vk::MemoryPropertyFlagBits::eHostCoherent,
                     instanceBuffer, instanceMemory);

        void* ptr = instanceMemory.mapMemory(0, instBufferSize);
        memcpy(ptr, instances.data(), instBufferSize);
        instanceMemory.unmapMemory();

        vk::BufferDeviceAddressInfo instanceAddrInfo{.buffer = instanceBuffer};
        vk::DeviceAddress instanceAddr = device.getBufferAddressKHR(instanceAddrInfo);

        // Prepare the geometry (instance) data
        auto instancesData = vk::AccelerationStructureGeometryInstancesDataKHR{
            .arrayOfPointers = vk::False,
            .data = instanceAddr
        };

        vk::AccelerationStructureGeometryDataKHR geometryData(instancesData);

        vk::AccelerationStructureGeometryKHR tlasGeometry{
            .geometryType = vk::GeometryTypeKHR::eInstances,
            .geometry = geometryData
        };

        vk::AccelerationStructureBuildGeometryInfoKHR tlasBuildGeometryInfo{
            .type = vk::AccelerationStructureTypeKHR::eTopLevel,
            .mode = vk::BuildAccelerationStructureModeKHR::eBuild,
            .geometryCount = 1,
            .pGeometries = &tlasGeometry
        };

#	if LAB_TASK_LEVEL >= LAB_TASK_AS_ANIMATION
        tlasBuildGeometryInfo.flags = vk::BuildAccelerationStructureFlagBitsKHR::eAllowUpdate;
#	endif        // LAB_TASK_LEVEL >= LAB_TASK_AS_ANIMATION

        // Query the memory sizes that will be needed for this TLAS
        auto primitiveCount = static_cast<uint32_t>(instances.size());

        vk::AccelerationStructureBuildSizesInfoKHR tlasBuildSizes =
            device.getAccelerationStructureBuildSizesKHR(
                vk::AccelerationStructureBuildTypeKHR::eDevice,
                tlasBuildGeometryInfo,
                {primitiveCount});

        // Create a scratch buffer for the TLAS, this will hold temporary data
        // during the build process
        createBuffer(
            tlasBuildSizes.buildScratchSize,
            vk::BufferUsageFlagBits::eStorageBuffer |
            vk::BufferUsageFlagBits::eShaderDeviceAddress,
            vk::MemoryPropertyFlagBits::eDeviceLocal,
            tlasScratchBuffer, tlasScratchMemory);

        // Save the scratch buffer address in the build info structure
        vk::BufferDeviceAddressInfo scratchAddressInfo{.buffer = *tlasScratchBuffer};
        vk::DeviceAddress scratchAddr = device.getBufferAddressKHR(scratchAddressInfo);
        tlasBuildGeometryInfo.scratchData.deviceAddress = scratchAddr;

        // Create a buffer for the TLAS itself now that we now the required size
        createBuffer(
            tlasBuildSizes.accelerationStructureSize,
            vk::BufferUsageFlagBits::eAccelerationStructureStorageKHR |
            vk::BufferUsageFlagBits::eShaderDeviceAddress |
            vk::BufferUsageFlagBits::eAccelerationStructureBuildInputReadOnlyKHR,
            vk::MemoryPropertyFlagBits::eDeviceLocal,
            tlasBuffer, tlasMemory);

        // Create and store the TLAS handle
        vk::AccelerationStructureCreateInfoKHR tlasCreateInfo{
            .buffer = tlasBuffer,
            .offset = 0,
            .size = tlasBuildSizes.accelerationStructureSize,
            .type = vk::AccelerationStructureTypeKHR::eTopLevel,
        };

        tlas = device.createAccelerationStructureKHR(tlasCreateInfo);

        // Save the TLAS handle in the build info structure
        tlasBuildGeometryInfo.dstAccelerationStructure = tlas;

        // Prepare the build range for the TLAS
        vk::AccelerationStructureBuildRangeInfoKHR tlasRangeInfo{
            .primitiveCount = primitiveCount,
            .primitiveOffset = 0,
            .firstVertex = 0,
            .transformOffset = 0
        };

        // Build the TLAS
        auto cmd = utils::beginSingleTimeCommands(device, commandPool);

        cmd->buildAccelerationStructuresKHR({tlasBuildGeometryInfo}, {&tlasRangeInfo});

        utils::endSingleTimeCommands(*cmd, queue);
#endif        // LAB_TASK_LEVEL >= LAB_TASK_AS_BUILD_AND_BIND
    }

    void VulkanRendererAPI::copyBuffer(vk::raii::Buffer& srcBuffer, vk::raii::Buffer& dstBuffer, vk::DeviceSize size)
    {
        vk::CommandBufferAllocateInfo allocInfo{.commandPool = commandPool, .level = vk::CommandBufferLevel::ePrimary, .commandBufferCount = 1};
        vk::raii::CommandBuffer commandCopyBuffer = std::move(device.allocateCommandBuffers(allocInfo).front());
        commandCopyBuffer.begin(vk::CommandBufferBeginInfo{.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit});
        commandCopyBuffer.copyBuffer(*srcBuffer, *dstBuffer, vk::BufferCopy{.size = size});
        commandCopyBuffer.end();
        queue.submit(vk::SubmitInfo{.commandBufferCount = 1, .pCommandBuffers = &*commandCopyBuffer}, nullptr);
        queue.waitIdle();
    }

#if LAB_TASK_LEVEL >= LAB_TASK_AS_ANIMATION
    void VulkanRendererAPI::updateTopLevelAS(const glm::mat4& model)
    {
        vk::TransformMatrixKHR tm{};
        auto& M = model;
        tm.matrix = std::array<std::array<float, 4>, 3>{
            {
                std::array<float, 4>{M[0][0], M[1][0], M[2][0], M[3][0]},
                std::array<float, 4>{M[0][1], M[1][1], M[2][1], M[3][1]},
                std::array<float, 4>{M[0][2], M[1][2], M[2][2], M[3][2]}
            }
        };

        // TASK06: update the instances to use the new transform matrix.
        for (auto& instance : instances)
        {
            instance.setTransform(tm);
        }

        auto primitiveCount = static_cast<uint32_t>(instances.size());
        vk::DeviceSize instBufferSize = sizeof(instances[0]) * primitiveCount;

        vk::raii::Buffer stagingBuffer({});
        vk::raii::DeviceMemory stagingBufferMemory({});
        createBuffer(instBufferSize, vk::BufferUsageFlagBits::eTransferSrc, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent, stagingBuffer, stagingBufferMemory);

        void* dataStaging = stagingBufferMemory.mapMemory(0, instBufferSize);
        memcpy(dataStaging, instances.data(), instBufferSize);
        stagingBufferMemory.unmapMemory();

        copyBuffer(stagingBuffer, instanceBuffer, instBufferSize);

        vk::BufferDeviceAddressInfo instanceAddrInfo{.buffer = instanceBuffer};
        vk::DeviceAddress instanceAddr = device.getBufferAddressKHR(instanceAddrInfo);

        // Prepare the geometry (instance) data
        auto instancesData = vk::AccelerationStructureGeometryInstancesDataKHR{
            .arrayOfPointers = vk::False,
            .data = instanceAddr
        };

        vk::AccelerationStructureGeometryDataKHR geometryData(instancesData);

        vk::AccelerationStructureGeometryKHR tlasGeometry{
            .geometryType = vk::GeometryTypeKHR::eInstances,
            .geometry = geometryData
        };

        // TASK06: Note the new parameters to re-build the TLAS in-place
        vk::AccelerationStructureBuildGeometryInfoKHR tlasBuildGeometryInfo{
            .type = vk::AccelerationStructureTypeKHR::eTopLevel,
            .flags = vk::BuildAccelerationStructureFlagBitsKHR::eAllowUpdate,
            .mode = vk::BuildAccelerationStructureModeKHR::eUpdate,
            .srcAccelerationStructure = tlas,
            .dstAccelerationStructure = tlas,
            .geometryCount = 1,
            .pGeometries = &tlasGeometry
        };

        vk::BufferDeviceAddressInfo scratchAddressInfo{.buffer = *tlasScratchBuffer};
        vk::DeviceAddress scratchAddr = device.getBufferAddressKHR(scratchAddressInfo);
        tlasBuildGeometryInfo.scratchData.deviceAddress = scratchAddr;

        // Prepare the build range for the TLAS
        vk::AccelerationStructureBuildRangeInfoKHR tlasRangeInfo{
            .primitiveCount = primitiveCount,
            .primitiveOffset = 0,
            .firstVertex = 0,
            .transformOffset = 0
        };

        // Re-build the TLAS
        auto cmd = utils::beginSingleTimeCommands(device, commandPool);

        // Pre-build barrier
        vk::MemoryBarrier2 preBarrier
        {
            .srcStageMask = vk::PipelineStageFlagBits2::eAccelerationStructureBuildKHR | vk::PipelineStageFlagBits2::eTransfer | vk::PipelineStageFlagBits2::eFragmentShader,
            .srcAccessMask = vk::AccessFlagBits2::eAccelerationStructureWriteKHR | vk::AccessFlagBits2::eTransferWrite | vk::AccessFlagBits2::eShaderRead,
            .dstStageMask = vk::PipelineStageFlagBits2::eAccelerationStructureBuildKHR,
            .dstAccessMask = vk::AccessFlagBits2::eAccelerationStructureReadKHR | vk::AccessFlagBits2::eAccelerationStructureWriteKHR
        };

        vk::DependencyInfo preDependencyInfo
        {
            .dependencyFlags = {},
            .memoryBarrierCount = 1,
            .pMemoryBarriers = &preBarrier,
        };

        cmd->pipelineBarrier2(preDependencyInfo);

        cmd->buildAccelerationStructuresKHR({tlasBuildGeometryInfo}, {&tlasRangeInfo});

        // Post-build barrier
        vk::MemoryBarrier2 postBarrier
        {
            .srcStageMask = vk::PipelineStageFlagBits2::eAccelerationStructureBuildKHR,
            .srcAccessMask = vk::AccessFlagBits2::eAccelerationStructureWriteKHR,
            .dstStageMask = vk::PipelineStageFlagBits2::eAccelerationStructureBuildKHR | vk::PipelineStageFlagBits2::eFragmentShader,
            .dstAccessMask = vk::AccessFlagBits2::eAccelerationStructureReadKHR | vk::AccessFlagBits2::eShaderRead
        };

        vk::DependencyInfo postDependencyInfo
        {
            .dependencyFlags = {},
            .memoryBarrierCount = 1,
            .pMemoryBarriers = &postBarrier,
        };

        cmd->pipelineBarrier2(postDependencyInfo);

        utils::endSingleTimeCommands(*cmd, queue);
    }
#endif        // LAB_TASK_LEVEL >= LAB_TASK_AS_ANIMATION

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

        // Pool for TLAS descriptor set
        {
            std::array poolSizes =
            {
                vk::DescriptorPoolSize(vk::DescriptorType::eAccelerationStructureKHR, MAX_FRAMES_IN_FLIGHT),
                vk::DescriptorPoolSize(vk::DescriptorType::eStorageImage, MAX_FRAMES_IN_FLIGHT),
                vk::DescriptorPoolSize(vk::DescriptorType::eCombinedImageSampler, MAX_FRAMES_IN_FLIGHT),
                vk::DescriptorPoolSize(vk::DescriptorType::eCombinedImageSampler, MAX_FRAMES_IN_FLIGHT),
            };

            vk::DescriptorPoolCreateInfo poolInfo{
                .flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet | vk::DescriptorPoolCreateFlagBits::eUpdateAfterBind,
                .maxSets = MAX_FRAMES_IN_FLIGHT, // Only one TLAS set is enough
                .poolSizeCount = static_cast<uint32_t>(poolSizes.size()),
                .pPoolSizes = poolSizes.data()
            };
            raytraceDescriptorPool = vk::raii::DescriptorPool(device, poolInfo);
            DBG_VK_NAME(*raytraceDescriptorPool);
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
                        .stageFlags = vk::ShaderStageFlagBits::eAllGraphics | vk::ShaderStageFlagBits::eRaygenKHR | vk::ShaderStageFlagBits::eClosestHitKHR | vk::ShaderStageFlagBits::eAnyHitKHR | vk::ShaderStageFlagBits::eMissKHR
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
            std::vector<vk::DescriptorSetLayout> layouts = {*descriptorSetLayout};
            // Allocate the descriptor set, needed only for larger descriptor sets
            vk::DescriptorSetAllocateInfo allocInfo = {
                .descriptorPool = descriptorPool,
                .descriptorSetCount = 1,
                .pSetLayouts = layouts.data(),
            };
            descriptorSets = device.allocateDescriptorSets(allocInfo);
            DBG_VK_NAME(*descriptorSets.back());
            if (!*descriptorSets[0])
            {
                std::cerr << "Descriptor set allocation failed! Handle is VK_NULL_HANDLE.\n";
            }
        }

        // Second this is another set which will be pushed
        {
            std::array layoutBindings =
            {
                vk::DescriptorSetLayoutBinding(0, vk::DescriptorType::eUniformBuffer, 1,
                                               vk::ShaderStageFlagBits::eAllGraphics | vk::ShaderStageFlagBits::eCompute | vk::ShaderStageFlagBits::eRaygenKHR | vk::ShaderStageFlagBits::eClosestHitKHR
                                               | vk::ShaderStageFlagBits::eAnyHitKHR| vk::ShaderStageFlagBits::eMissKHR, nullptr),
            };

            vk::DescriptorSetLayoutCreateInfo descriptorSetLayoutInfo
            {
                .flags = vk::DescriptorSetLayoutCreateFlagBits::ePushDescriptor,
                .bindingCount = uint32_t(layoutBindings.size()),
                .pBindings = layoutBindings.data(),
            };
            commonDescriptorSetLayout = device.createDescriptorSetLayout(descriptorSetLayoutInfo);
            DBG_VK_NAME(*commonDescriptorSetLayout);
        }

        // acceleration structure
        {
            std::array layoutBindings =
            {
                //fragmnet for rayQuerys
                vk::DescriptorSetLayoutBinding(0, vk::DescriptorType::eAccelerationStructureKHR, 1,
                                               vk::ShaderStageFlagBits::eFragment | vk::ShaderStageFlagBits::eRaygenKHR | vk::ShaderStageFlagBits::eClosestHitKHR | vk::ShaderStageFlagBits::eAnyHitKHR | vk::ShaderStageFlagBits::eMissKHR,
                                               nullptr),
                vk::DescriptorSetLayoutBinding(1, vk::DescriptorType::eStorageImage, 1, vk::ShaderStageFlagBits::eFragment | vk::ShaderStageFlagBits::eRaygenKHR | vk::ShaderStageFlagBits::eCompute,
                                               nullptr),
                vk::DescriptorSetLayoutBinding(2, vk::DescriptorType::eCombinedImageSampler, 1,
                                               vk::ShaderStageFlagBits::eFragment | vk::ShaderStageFlagBits::eRaygenKHR | vk::ShaderStageFlagBits::eCompute,
                                               nullptr),
                vk::DescriptorSetLayoutBinding(3, vk::DescriptorType::eCombinedImageSampler, 1,
                                               vk::ShaderStageFlagBits::eFragment | vk::ShaderStageFlagBits::eRaygenKHR | vk::ShaderStageFlagBits::eCompute,
                                               nullptr)
            };

            std::array<vk::DescriptorBindingFlags, 4> flags = {
                static_cast<vk::DescriptorBindingFlags>(0), // binding 0
                static_cast<vk::DescriptorBindingFlags>(0), // binding 1
                vk::DescriptorBindingFlagBits::eUpdateAfterBind, // binding 2
                vk::DescriptorBindingFlagBits::eUpdateAfterBind // binding 3
            };

            vk::DescriptorSetLayoutBindingFlagsCreateInfo bindingFlagsCI{
                .bindingCount = uint32_t(flags.size()),
                .pBindingFlags = flags.data()
            };

            vk::DescriptorSetLayoutCreateInfo descriptorSetLayoutInfo
            {
                .pNext = &bindingFlagsCI,
                .flags = vk::DescriptorSetLayoutCreateFlagBits::eUpdateAfterBindPool,
                .bindingCount = uint32_t(layoutBindings.size()),
                .pBindings = layoutBindings.data(),
            };
            raytraceDescriptorSetLayout = device.createDescriptorSetLayout(descriptorSetLayoutInfo);
            DBG_VK_NAME(*raytraceDescriptorSetLayout);
            std::vector<vk::DescriptorSetLayout> layouts = {*raytraceDescriptorSetLayout};
            // Allocate the descriptor set, needed only for larger descriptor sets
            vk::DescriptorSetAllocateInfo allocInfo = {
                .descriptorPool = raytraceDescriptorPool,
                .descriptorSetCount = 1,
                .pSetLayouts = layouts.data(),
            };
            raytraceDescriptorSet = device.allocateDescriptorSets(allocInfo);
            DBG_VK_NAME(*raytraceDescriptorSet.back());
            if (!*raytraceDescriptorSet[0])
            {
                std::cerr << "Descriptor set allocation failed! Handle is VK_NULL_HANDLE.\n";
            }
        }
        /*updateGraphicsDescriptorSet();*/
    }

    void VulkanRendererAPI::updateGraphicsDescriptorSet()
    {
        // Don't update descriptor set if there are no textures
        if (m_images.empty())
        {
            LOGW("updateGraphicsDescriptorSet: No textures to update, skipping descriptor set update");
            return;
        }

        // Prepare imageInfos vector automatically sized to m_image's size
        std::vector<vk::DescriptorImageInfo> imageInfos;
        imageInfos.reserve(m_images.size()); // reserve for efficiency

        // The image info
        for (auto& m_image : m_images)
        {
            imageInfos.push_back({
                .sampler = m_image.sampler, // currently the same sampler maybe add them indivual in the future
                .imageView = m_image.view,
                .imageLayout = m_image.layout,
            });
        }

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

    void VulkanRendererAPI::createSyncObjects()
    {
        assert(presentCompleteSemaphores.empty() && renderFinishedSemaphores.empty() && inFlightFences.empty());

        for (size_t i = 0; i < swapChainImages.size(); i++)
        {
            renderFinishedSemaphores.emplace_back(device, vk::SemaphoreCreateInfo());
        }

        for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
        {
            presentCompleteSemaphores.emplace_back(device, vk::SemaphoreCreateInfo());
            inFlightFences.emplace_back(device, vk::FenceCreateInfo{.flags = vk::FenceCreateFlagBits::eSignaled});
        }
    }

    void VulkanRendererAPI::createQueryPool()
    {
        // stastistics
        {
            vk::QueryPoolCreateInfo poolInfo
            {
                .queryType = vk::QueryType::ePipelineStatistics,
                .queryCount = 1,
                .pipelineStatistics =
                /*vk::QueryPipelineStatisticFlagBits::eInputAssemblyVertices |
                vk::QueryPipelineStatisticFlagBits::eInputAssemblyPrimitives |
                vk::QueryPipelineStatisticFlagBits::eVertexShaderInvocations |
                vk::QueryPipelineStatisticFlagBits::eFragmentShaderInvocations |
                vk::QueryPipelineStatisticFlagBits::eComputeShaderInvocations |*/
                vk::QueryPipelineStatisticFlagBits::eClippingInvocations /*|
                    vk::QueryPipelineStatisticFlagBits::eClippingPrimitives |
                    vk::QueryPipelineStatisticFlagBits::eTaskShaderInvocationsEXT |
                    vk::QueryPipelineStatisticFlagBits::eMeshShaderInvocationsEXT*/
            };

            queryPoolStatistics = vk::raii::QueryPool(device, poolInfo);
            DBG_VK_NAME(*queryPoolStatistics);
        }

        // timestamp
        {
            vk::QueryPoolCreateInfo poolInfo
            {
                .queryType = vk::QueryType::eTimestamp,
                .queryCount = maxTimeStamp,
            };

            queryPoolTimeStep = vk::raii::QueryPool(device, poolInfo);
            DBG_VK_NAME(*queryPoolTimeStep);
        }
    }

    void VulkanRendererAPI::createQueryBuffer()
    {
        // 7 pipelineStatistics, 1 querycount
        queryStatisticsBuffer = m_allocator.createBuffer(sizeof(uint64_t) * 1 * 1, vk::BufferUsageFlagBits2::eTransferDst, vma::MemoryUsage::eGpuToCpu);

        // timestamp, 2 quercount
        queryTimeStepBuffer = m_allocator.createBuffer(sizeof(uint64_t) * 1 * maxTimeStamp, vk::BufferUsageFlagBits2::eTransferDst, vma::MemoryUsage::eGpuToCpu);
    }

    void VulkanRendererAPI::downloadQueryStatisticsBuffer()
    {
        auto cmd = utils::beginSingleTimeCommands(device, commandPool);

        cmd->copyQueryPoolResults(queryPoolStatistics, 0, 1, queryStatisticsBuffer.buffer, 0, 0, vk::QueryResultFlagBits::e64/* | vk::QueryResultFlagBits::eWait*/);

        utils::endSingleTimeCommands(*cmd, queue);

        // Map and copy data to the staging buffer
        try
        {
            void* mappedData = queryStatisticsBuffer.buffer.getAllocation().map();
            // No need to explicitly unmap; vma::raii::Allocation unmaps on destruction

            uint64_t* stats = reinterpret_cast<uint64_t*>(mappedData);

            /*
            std::cout << std::dec;
            std::cout << "Input assembly vertices: "        << stats[0] << "\n";
            std::cout << "Input assembly primitives: "      << stats[1] << "\n";
            std::cout << "Vertex shader invocations: "      << stats[2] << "\n";
            std::cout << "Clipping invocations: "           << stats[3] << "\n";
            std::cout << "Clipping primitives: "            << stats[4] << "\n";
            std::cout << "Fragment shader invocations: "    << stats[5] << "\n";
            std::cout << "Compute shader invocations: "     << stats[6] << "\n";*/

            pipeStats.clippingInvocations = stats[0];

            queryStatisticsBuffer.buffer.getAllocation().unmap();
        }
        catch ([[maybe_unused]] const vk::SystemError& err)
        {
            VK_CORE_ERROR("Failed to map staging buffer memory!");
            // You could throw or handle the error here
        }
    }

    void VulkanRendererAPI::StartTimeStamp(VanKCommandBuffer cmd, VanKTimestampPass& pass)
    {
        uint32_t base = timeStampIndex;

        timeStampIndex += 2;

        Unwrap(cmd).writeTimestamp2(vk::PipelineStageFlagBits2::eAllCommands, queryPoolTimeStep, base + 0);

        pass.queryIndex = base;

        activeTimestamps.emplace_back(&pass);
    }

    void VulkanRendererAPI::StopTimeStamp(VanKCommandBuffer cmd, VanKTimestampPass& pass)
    {
        Unwrap(cmd).writeTimestamp2(vk::PipelineStageFlagBits2::eAllCommands, queryPoolTimeStep, pass.queryIndex + 1);
    }

    void VulkanRendererAPI::downloadQueryTimeStampBuffer() // todo use vankcommandbuffer because its in a pass anyway
    {
        auto cmd = utils::beginSingleTimeCommands(device, commandPool);

        cmd->copyQueryPoolResults(queryPoolTimeStep, 0, timeStampIndex, queryTimeStepBuffer.buffer, 0, sizeof(uint64_t), vk::QueryResultFlagBits::e64 | vk::QueryResultFlagBits::eWait);

        utils::endSingleTimeCommands(*cmd, queue);

        // Map and copy data to the staging buffer
        try
        {
            void* mappedData = queryTimeStepBuffer.buffer.getAllocation().map();
            // No need to explicitly unmap; vma::raii::Allocation unmaps on destruction

            uint64_t* stats = reinterpret_cast<uint64_t*>(mappedData);

            /*timestamp.begin = stats[timestamp.queryIndex + 0];
            timestamp.end = stats[timestamp.queryIndex + 1];*/

            for (auto* passPtr : activeTimestamps)
            {
                passPtr->begin = stats[passPtr->queryIndex];
                passPtr->end = stats[passPtr->queryIndex + 1];
            }

            activeTimestamps.clear();

            queryTimeStepBuffer.buffer.getAllocation().unmap();
        }
        catch ([[maybe_unused]] const vk::SystemError& err)
        {
            VK_CORE_ERROR("Failed to map staging buffer memory!");
            // You could throw or handle the error here
        }
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
