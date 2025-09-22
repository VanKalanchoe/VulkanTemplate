#include <iostream>
#include <fstream>
#include <stdexcept>
#include <vector>
#include <cstring>
#include <cstdlib>
#include <memory>
#include <algorithm>
#include <limits>
#include <array>
#include <assert.h>
#include <chrono>
#include <thread>
#include <unordered_map>

#ifdef __INTELLISENSE__
#include <vulkan/vulkan_raii.hpp>
#else
import vulkan_hpp;
#endif

#include <vulkan/vk_platform.h>

#define SDL_MAIN_USE_CALLBACKS
#include <SDL3/SDL_main.h>
#include <SDL3/SDL_vulkan.h>
#include <vulkan/vulkan.h>
#include <SDL3/SDL_log.h>

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#define GLM_ENABLE_EXPERIMENTAL
#define GLM_FORCE_CXX11
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/hash.hpp>

#include <ktx.h>

#define TINYGLTF_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <tiny_gltf.h>

// Some graphic user interface (GUI) using Dear ImGui
#include "backends/imgui_impl_SDL3.h"
#include "backends/imgui_impl_vulkan.h"
#include "imgui.h"
#include "imgui_internal.h"  // For Docking

constexpr uint32_t WIDTH = 800;
constexpr uint32_t HEIGHT = 600;
constexpr int MAX_FRAMES_IN_FLIGHT = 2;
const std::string MODEL_PATH = "models/viking_room.glb";
const std::string TEXTURE_PATH = "textures/viking_room.ktx2";
// Define the number of objects to render
constexpr int MAX_OBJECTS = 3;

const std::vector validationLayers =
{
    "VK_LAYER_KHRONOS_validation"
};

#ifdef NDEBUG
constexpr bool enableValidationLayers = false;
#else
constexpr bool enableValidationLayers = true;
#endif

/*-- 
 * Combines hash values using the FNV-1a based algorithm 
-*/
static std::size_t hashCombine(std::size_t seed, auto const& value)
{
    return seed ^ (std::hash<std::decay_t<decltype(value)>>{}(value) + 0x9e3779b9 + (seed << 6) + (seed >> 2));
}

#include <cassert>

class SamplerPool
{
public:
    SamplerPool() = default;
    ~SamplerPool() = default;

    void init(vk::raii::Device& device)
    {
        m_device = &device;
    }

    void deinit()
    {
        m_samplerMap.clear();
    }

    vk::raii::Sampler acquireSampler(const vk::SamplerCreateInfo& createInfo)
    {
        auto it = m_samplerMap.find(createInfo);
        if (it != m_samplerMap.end()) {
            return std::move(it->second); // return existing
        }

        // Construct sampler directly in the map
        auto [newIt, inserted] = m_samplerMap.try_emplace(
            createInfo, vk::raii::Sampler(*m_device, createInfo)
        );
        return std::move(newIt->second);
    }

    void releaseSampler(const vk::raii::Sampler& sampler)
    {
        for (auto it = m_samplerMap.begin(); it != m_samplerMap.end();)
        {
            if (&(it->second) == &sampler)
            {
                it = m_samplerMap.erase(it);
            }
            else
            {
                ++it;
            }
        }
    }

private:
    vk::raii::Device* m_device = nullptr;;

    struct SamplerCreateInfoHash
    {
        std::size_t operator()(const vk::SamplerCreateInfo& info) const
        {
            std::size_t seed = 0;
            auto hashCombine = [&](auto value)
            {
                seed ^= std::hash<decltype(value)>{}(value) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
            };
            hashCombine(info.magFilter);
            hashCombine(info.minFilter);
            hashCombine(info.mipmapMode);
            hashCombine(info.addressModeU);
            hashCombine(info.addressModeV);
            hashCombine(info.addressModeW);
            hashCombine(info.mipLodBias);
            hashCombine(info.anisotropyEnable);
            hashCombine(info.maxAnisotropy);
            hashCombine(info.compareEnable);
            hashCombine(info.compareOp);
            hashCombine(info.minLod);
            hashCombine(info.maxLod);
            hashCombine(info.borderColor);
            hashCombine(info.unnormalizedCoordinates);
            return seed;
        }
    };

    struct SamplerCreateInfoEqual
    {
        bool operator()(const vk::SamplerCreateInfo& lhs, const vk::SamplerCreateInfo& rhs) const
        {
            return std::memcmp(&lhs, &rhs, sizeof(vk::SamplerCreateInfo)) == 0;
        }
    };

    std::unordered_map<vk::SamplerCreateInfo, vk::raii::Sampler, SamplerCreateInfoHash, SamplerCreateInfoEqual>
    m_samplerMap;

    vk::raii::Sampler createSampler(const vk::SamplerCreateInfo& createInfo)
    {
        return vk::raii::Sampler(*m_device, createInfo);
    }
};


struct Vertex
{
    glm::vec3 pos;
    glm::vec3 color;
    glm::vec2 texCoord;

    static vk::VertexInputBindingDescription getBindingDescription()
    {
        return {0, sizeof(Vertex), vk::VertexInputRate::eVertex};
    }

    static std::array<vk::VertexInputAttributeDescription, 3> getAttributeDescriptions()
    {
        return {
            vk::VertexInputAttributeDescription(0, 0, vk::Format::eR32G32B32Sfloat, offsetof(Vertex, pos)),
            vk::VertexInputAttributeDescription(1, 0, vk::Format::eR32G32B32Sfloat, offsetof(Vertex, color)),
            vk::VertexInputAttributeDescription(2, 0, vk::Format::eR32G32Sfloat, offsetof(Vertex, texCoord))
        };
    }

    bool operator==(const Vertex& other) const
    {
        return pos == other.pos && color == other.color && texCoord == other.texCoord;
    }
};

template <>
struct std::hash<Vertex>
{
    size_t operator()(Vertex const& vertex) const noexcept
    {
        return ((hash<glm::vec3>()(vertex.pos) ^ (hash<glm::vec3>()(vertex.color) << 1)) >> 1) ^ (hash<
            glm::vec2>()(vertex.texCoord) << 1);
    }
};

// Define a structure to hold per-object data
struct GameObject
{
    // Transform properties
    glm::vec3 position = {0.0f, 0.0f, 0.0f};
    glm::vec3 rotation = {0.0f, 0.0f, 0.0f};
    glm::vec3 scale = {1.0f, 1.0f, 1.0f};

    // Uniform buffer for this object (one per frame in flight)
    std::vector<vk::raii::Buffer> uniformBuffers;
    std::vector<vk::raii::DeviceMemory> uniformBuffersMemory;
    std::vector<void*> uniformBuffersMapped;

    // Descriptor sets for this object (one per frame in flight)
    std::vector<vk::raii::DescriptorSet> descriptorSets;

    // Calculate model matrix based on position, rotation, and scale
    glm::mat4 getModelMatrix() const
    {
        glm::mat4 model = glm::mat4(1.0f);
        model = glm::translate(model, position);
        model = glm::rotate(model, rotation.x, glm::vec3(1.0f, 0.0f, 0.0f));
        model = glm::rotate(model, rotation.y, glm::vec3(0.0f, 1.0f, 0.0f));
        model = glm::rotate(model, rotation.z, glm::vec3(0.0f, 0.0f, 1.0f));
        model = glm::scale(model, scale);
        return model;
    }
};

struct UniformBufferObject
{
    alignas(16) glm::mat4 model;
    alignas(16) glm::mat4 view;
    alignas(16) glm::mat4 proj;
};

/*const std::vector<Vertex> vertices = {
    {{-0.5f, -0.5f, 0.0f}, {1.0f, 0.0f, 0.0f}, {0.0f, 0.0f}},
    {{0.5f, -0.5f, 0.0f}, {0.0f, 1.0f, 0.0f}, {1.0f, 0.0f}},
    {{0.5f, 0.5f, 0.0f}, {0.0f, 0.0f, 1.0f}, {1.0f, 1.0f}},
    {{-0.5f, 0.5f, 0.0f}, {1.0f, 1.0f, 1.0f}, {0.0f, 1.0f}},

    {{-0.5f, -0.5f, -0.5f}, {1.0f, 0.0f, 0.0f}, {0.0f, 0.0f}},
    {{0.5f, -0.5f, -0.5f}, {0.0f, 1.0f, 0.0f}, {1.0f, 0.0f}},
    {{0.5f, 0.5f, -0.5f}, {0.0f, 0.0f, 1.0f}, {1.0f, 1.0f}},
    {{-0.5f, 0.5f, -0.5f}, {1.0f, 1.0f, 1.0f}, {0.0f, 1.0f}}
};

const std::vector<uint16_t> indices = {
    0, 1, 2, 2, 3, 0,
    4, 5, 6, 6, 7, 4
};*/
static auto lastTime = std::chrono::high_resolution_clock::now();
static int frameCount = 0;
static float fps = 0.0f;
class HelloTriangleApplication
{
public:
    void init()
    {
        initWindow();
        initVulkan();
        m_samplerPool.init(device);
        // Acquiring the sampler which will be used for displaying the GBuffer
        const vk::SamplerCreateInfo info{.magFilter = vk::Filter::eLinear, .minFilter = vk::Filter::eLinear};
        linearSampler = m_samplerPool.acquireSampler(info);
        initImGui();
    }

    void render()
    {
        mainLoop();
    }

    void shutdown()
    {
        device.waitIdle();
        cleanup();
    }

    void setFramebufferResized(bool resized) { framebufferResized = resized; }
    void setWindowMinimized(bool minimized) { windowMinimized = minimized; };

private:
    SDL_Window* window = nullptr;
    vk::raii::Context context;
    vk::raii::Instance instance = nullptr;
    vk::raii::DebugUtilsMessengerEXT debugMessenger = nullptr;
    vk::raii::SurfaceKHR surface = nullptr;
    vk::raii::PhysicalDevice physicalDevice = nullptr;
    vk::SampleCountFlagBits msaaSamples = vk::SampleCountFlagBits::e1;
    vk::raii::Device device = nullptr;
    uint32_t queueIndex = ~0;
    vk::raii::Queue queue = nullptr;
    vk::raii::SwapchainKHR swapChain = nullptr;
    std::vector<vk::Image> swapChainImages;
    vk::SurfaceFormatKHR swapChainSurfaceFormat;
    vk::Extent2D swapChainExtent;
    std::vector<vk::raii::ImageView> swapChainImageViews;

    vk::raii::DescriptorSetLayout descriptorSetLayout = nullptr;
    vk::raii::PipelineLayout pipelineLayout = nullptr;
    vk::raii::Pipeline graphicsPipeline = nullptr;

    vk::raii::Image sceneImage = nullptr;
    vk::raii::DeviceMemory sceneImageMemory = nullptr;
    vk::raii::ImageView sceneImageView = nullptr;

    vk::raii::Image colorImage = nullptr;
    vk::raii::DeviceMemory colorImageMemory = nullptr;
    vk::raii::ImageView colorImageView = nullptr;

    vk::raii::Image depthImage = nullptr;
    vk::raii::DeviceMemory depthImageMemory = nullptr;
    vk::raii::ImageView depthImageView = nullptr;

    uint32_t mipLevels = 0;
    vk::raii::Image textureImage = nullptr;
    vk::raii::DeviceMemory textureImageMemory = nullptr;
    vk::raii::ImageView textureImageView = nullptr;
    SamplerPool m_samplerPool;
    vk::raii::Sampler linearSampler = nullptr;
    vk::raii::Sampler textureSampler = nullptr;
    vk::Format textureImageFormat = vk::Format::eUndefined;

    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    vk::raii::Buffer vertexBuffer = nullptr;
    vk::raii::DeviceMemory vertexBufferMemory = nullptr;
    vk::raii::Buffer indexBuffer = nullptr;
    vk::raii::DeviceMemory indexBufferMemory = nullptr;

    // Array of game objects to render
    std::array<GameObject, MAX_OBJECTS> gameObjects;

    std::vector<vk::raii::Buffer> uniformBuffers;
    std::vector<vk::raii::DeviceMemory> uniformBuffersMemory;
    std::vector<void*> uniformBuffersMapped;

    vk::raii::DescriptorPool descriptorPool = nullptr;
    vk::raii::DescriptorPool uiDescriptorPool = nullptr; // imgui 
    std::vector<vk::raii::DescriptorSet> descriptorSets;
    std::vector<VkDescriptorSet> uiDescriptorSet{}; // imgui

    vk::raii::CommandPool commandPool = nullptr;
    std::vector<vk::raii::CommandBuffer> commandBuffers;

    std::vector<vk::raii::Semaphore> presentCompleteSemaphores;
    std::vector<vk::raii::Semaphore> renderFinishedSemaphores;
    std::vector<vk::raii::Fence> inFlightFences;
    uint32_t currentFrame = 0;

    bool framebufferResized = false;
    bool windowMinimized = false;
    bool vSync = false;
    bool sceneImageInitialized = false;

    std::vector<const char*> requiredDeviceExtension =
    {
        vk::KHRSwapchainExtensionName,
        vk::KHRSpirv14ExtensionName,
        vk::KHRSynchronization2ExtensionName,
        vk::KHRCreateRenderpass2ExtensionName
    };

    SDL_AppResult initWindow()
    {
        if (!SDL_Vulkan_LoadLibrary(nullptr))
        {
            SDL_Log("Vulkan support is available");
        }
        else
        {
            SDL_Log("Vulkan support is NOT available: %s", SDL_GetError());
        }

        if (!SDL_Init(SDL_INIT_VIDEO))
        {
            SDL_Log("Couldn't initialize SDL: %s", SDL_GetError());
            return SDL_APP_FAILURE;
        }

        window = SDL_CreateWindow("Vulkan", WIDTH, HEIGHT,
                                  SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);

        if (!window)
        {
            SDL_Log("Couldn't create window: %s", SDL_GetError());
            return SDL_APP_FAILURE;
        }

        SDL_SetWindowMinimumSize(window, 640, 480);

        return SDL_APP_CONTINUE;
    }

    SDL_AppResult initVulkan()
    {
        createInstance();
        setupDebugMessenger();
        createSurface();
        pickPhysicalDevice();
        msaaSamples = getMaxUsableSampleCount();
        createLogicalDevice();
        createSwapChain();
        createImageViews();
        createDescriptorSetLayout();
        createGraphicsPipeline();
        createCommandPool();
        createSceneResources();
        createColorResources();
        createDepthResources();
        createTextureImage();
        createTextureImageView();
        createTextureSampler();
        loadModel();
        createVertexBuffer();
        createIndexBuffer();
        setupGameObjects();
        createUniformBuffers();
        createDescriptorPool();
        createDescriptorSets();
        createCommandBuffers();
        createSyncObjects();

        return SDL_APP_CONTINUE;
    }

    SDL_AppResult initImGui()
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
                                                                 static_cast<VkImageLayout>(vk::ImageLayout::eShaderReadOnlyOptimal));
            }
        }

        return SDL_APP_CONTINUE;
    }

    void mainLoop()
    {
        if (windowMinimized)
            return;

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
                    recreateSwapChain(); // Recreate the swapchain with the new vSync setting
                ImGui::Separator();
                if (ImGui::MenuItem("Exit"))
                {
                    SDL_Event quitEvent;
                    SDL_zero(quitEvent);                     // Zero-initialize
                    quitEvent.type = SDL_EVENT_QUIT;         // Set event type
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

        /*// Verify if the viewport has a new size and resize the G-Buffer accordingly.
        const VkExtent2D viewportSize = {uint32_t(windowSize.x), uint32_t(windowSize.y)};
        if (m_viewportSize.width != viewportSize.width || m_viewportSize.height != viewportSize.height)
        {
            onViewportSizeChange(viewportSize);
        }*/

        drawFrame();

        // Update and Render additional Platform Windows (floating windows)
        ImGui::EndFrame();
        if ((ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable) != 0)
        {
            ImGui::UpdatePlatformWindows();
            ImGui::RenderPlatformWindowsDefault();
        }
    }

    void cleanupSwapChain()
    {
        swapChainImages.clear();
        swapChainImageViews.clear();
        swapChain = nullptr;
    }

    void cleanup()
    {
        // Clean up resources in each GameObject
        for (auto& gameObject : gameObjects)
        {
            // Unmap memory
            for (size_t i = 0; i < gameObject.uniformBuffersMemory.size(); i++)
            {
                if (gameObject.uniformBuffersMapped[i] != nullptr)
                {
                    gameObject.uniformBuffersMemory[i].unmapMemory();
                }
            }

            // Clear vectors to release resources
            gameObject.uniformBuffers.clear();
            gameObject.uniformBuffersMemory.clear();
            gameObject.uniformBuffersMapped.clear();
            gameObject.descriptorSets.clear();
        }
        
        m_samplerPool.deinit();
        
        ImGui_ImplVulkan_Shutdown();
        ImGui_ImplSDL3_Shutdown();
        ImGui::DestroyContext();

        SDL_DestroyWindow(window);
    }

    void recreateSwapChain()
    {
        device.waitIdle();

        cleanupSwapChain();
        createSwapChain();
        createImageViews();
        createSceneResources();
        createColorResources();
        createDepthResources();
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

    void createInstance()
    {
        constexpr vk::ApplicationInfo appInfo
        {
            .pApplicationName = "Hello Triangle",
            .applicationVersion = VK_MAKE_VERSION(1, 0, 0),
            .pEngineName = "No Engine",
            .engineVersion = VK_MAKE_VERSION(1, 0, 0),
            .apiVersion = vk::ApiVersion14
        };

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

    void setupDebugMessenger()
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
    
    void createSurface()
    {
        VkSurfaceKHR _surface;
        if (!SDL_Vulkan_CreateSurface(window, *instance, nullptr, &_surface))
        {
            throw std::runtime_error("failed to create window surface!");
        }
        surface = vk::raii::SurfaceKHR(instance, _surface);
    }

    void pickPhysicalDevice()
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

            auto features = device.template getFeatures2<
                vk::PhysicalDeviceFeatures2, vk::PhysicalDeviceVulkan13Features,
                vk::PhysicalDeviceExtendedDynamicStateFeaturesEXT>();
            bool supportsRequiredFeatures = features.template get<vk::PhysicalDeviceFeatures2>().features.
                                                     samplerAnisotropy &&
                features.template get<vk::PhysicalDeviceVulkan13Features>().dynamicRendering &&
                features.template get<vk::PhysicalDeviceExtendedDynamicStateFeaturesEXT>().extendedDynamicState;

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

    void createLogicalDevice()
    {
        std::vector<vk::QueueFamilyProperties> queueFamilyProperties = physicalDevice.getQueueFamilyProperties();

        // get the first index into queueFamilyProperties which supports both graphics and present
        for (uint32_t qfpIndex = 0; qfpIndex < queueFamilyProperties.size(); qfpIndex++)
        {
            if ((queueFamilyProperties[qfpIndex].queueFlags & vk::QueueFlagBits::eGraphics) &&
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
        vk::StructureChain<vk::PhysicalDeviceFeatures2, vk::PhysicalDeviceVulkan11Features // added myself
                           , vk::PhysicalDeviceVulkan13Features,
                           vk::PhysicalDeviceExtendedDynamicStateFeaturesEXT> featureChain =
        {
            {.features = {.sampleRateShading = true, .samplerAnisotropy = true}}, // vk::PhysicalDeviceFeatures2
            {.shaderDrawParameters = true}, // <-- enable // added myself
            {.synchronization2 = true, .dynamicRendering = true}, // vk::PhysicalDeviceVulkan13Features
            {.extendedDynamicState = true} // vk::PhysicalDeviceExtendedDynamicStateFeaturesEXT
        };

        // create a Device
        float queuePriority = 0.0f;
        vk::DeviceQueueCreateInfo deviceQueueCreateInfo{
            .queueFamilyIndex = queueIndex, .queueCount = 1, .pQueuePriorities = &queuePriority
        };
        vk::DeviceCreateInfo deviceCreateInfo{
            .pNext = &featureChain.get<vk::PhysicalDeviceFeatures2>(),
            .queueCreateInfoCount = 1,
            .pQueueCreateInfos = &deviceQueueCreateInfo,
            .enabledExtensionCount = static_cast<uint32_t>(requiredDeviceExtension.size()),
            .ppEnabledExtensionNames = requiredDeviceExtension.data()
        };

        device = vk::raii::Device(physicalDevice, deviceCreateInfo);
        queue = vk::raii::Queue(device, queueIndex, 0);
    }

    void createSwapChain() {
        auto surfaceCapabilities = physicalDevice.getSurfaceCapabilitiesKHR( *surface );
        swapChainExtent          = chooseSwapExtent( surfaceCapabilities );
        swapChainSurfaceFormat   = chooseSwapSurfaceFormat( physicalDevice.getSurfaceFormatsKHR( *surface ) );
        vk::SwapchainCreateInfoKHR swapChainCreateInfo{ .surface          = *surface,
                                                        .minImageCount    = chooseSwapMinImageCount( surfaceCapabilities ),
                                                        .imageFormat      = swapChainSurfaceFormat.format,
                                                        .imageColorSpace  = swapChainSurfaceFormat.colorSpace,
                                                        .imageExtent      = swapChainExtent,
                                                        .imageArrayLayers = 1,
                                                        .imageUsage       = vk::ImageUsageFlagBits::eColorAttachment,
                                                        .imageSharingMode = vk::SharingMode::eExclusive,
                                                        .preTransform     = surfaceCapabilities.currentTransform,
                                                        .compositeAlpha   = vk::CompositeAlphaFlagBitsKHR::eOpaque,
                                                        .presentMode      = chooseSwapPresentMode( physicalDevice.getSurfacePresentModesKHR( *surface ) , vSync),
                                                        .clipped          = true };
        std::cout << "Present mode chosen: " << static_cast<int>(swapChainCreateInfo.presentMode) << std::endl;

        swapChain = vk::raii::SwapchainKHR(device, swapChainCreateInfo);
        swapChainImages = swapChain.getImages();
    }

    //change this ? from chapter image view
    void createImageViews() {
        assert(swapChainImageViews.empty());

        vk::ImageViewCreateInfo imageViewCreateInfo{
            .viewType = vk::ImageViewType::e2D,
            .format = swapChainSurfaceFormat.format,
            .subresourceRange = { vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1 }
        };
        for ( auto image : swapChainImages )
        {
            imageViewCreateInfo.image = image;
            swapChainImageViews.emplace_back( device, imageViewCreateInfo );
        }
    }

    void createDescriptorSetLayout() {
        std::array bindings = {
            vk::DescriptorSetLayoutBinding( 0, vk::DescriptorType::eUniformBuffer, 1, vk::ShaderStageFlagBits::eVertex, nullptr),
            vk::DescriptorSetLayoutBinding( 1, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eFragment, nullptr)
        };

        vk::DescriptorSetLayoutCreateInfo layoutInfo{ .bindingCount = static_cast<uint32_t>(bindings.size()), .pBindings = bindings.data() };
        descriptorSetLayout = vk::raii::DescriptorSetLayout(device, layoutInfo);
    }


    void createGraphicsPipeline()
    {
        vk::raii::ShaderModule shaderModule = createShaderModule(readFile("shaders/slang.spv"));

        vk::PipelineShaderStageCreateInfo vertShaderStageInfo{
            .stage = vk::ShaderStageFlagBits::eVertex, .module = shaderModule, .pName = "vertMain"
        };
        vk::PipelineShaderStageCreateInfo fragShaderStageInfo{
            .stage = vk::ShaderStageFlagBits::eFragment, .module = shaderModule, .pName = "fragMain"
        };
        vk::PipelineShaderStageCreateInfo shaderStages[] = {vertShaderStageInfo, fragShaderStageInfo};

        auto bindingDescription = Vertex::getBindingDescription();
        auto attributeDescriptions = Vertex::getAttributeDescriptions();
        vk::PipelineVertexInputStateCreateInfo vertexInputInfo{
            .vertexBindingDescriptionCount = 1,
            .pVertexBindingDescriptions = &bindingDescription,
            .vertexAttributeDescriptionCount = static_cast<uint32_t>(attributeDescriptions.size()),
            .pVertexAttributeDescriptions = attributeDescriptions.data()
        };
        vk::PipelineInputAssemblyStateCreateInfo inputAssembly{
            .topology = vk::PrimitiveTopology::eTriangleList,
            .primitiveRestartEnable = vk::False
        };
        vk::PipelineViewportStateCreateInfo viewportState{
            .viewportCount = 1,
            .scissorCount = 1
        };
        vk::PipelineRasterizationStateCreateInfo rasterizer{
            .depthClampEnable = vk::False,
            .rasterizerDiscardEnable = vk::False,
            .polygonMode = vk::PolygonMode::eFill,
            .cullMode = vk::CullModeFlagBits::eBack,
            .frontFace = vk::FrontFace::eCounterClockwise,
            .depthBiasEnable = vk::False
        };
        rasterizer.lineWidth = 1.0f;
        vk::PipelineMultisampleStateCreateInfo multisampling{
            .rasterizationSamples = msaaSamples,
            .sampleShadingEnable = vk::True,
            .minSampleShading = 0.2f, // min fraction for sample shading; closer to one is smoother
        };
        vk::PipelineDepthStencilStateCreateInfo depthStencil{
            .depthTestEnable = vk::True,
            .depthWriteEnable = vk::True,
            .depthCompareOp = vk::CompareOp::eLess,
            .depthBoundsTestEnable = vk::False,
            .stencilTestEnable = vk::False
        };
        vk::PipelineColorBlendAttachmentState colorBlendAttachment;
        colorBlendAttachment.colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
            vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA;
        colorBlendAttachment.blendEnable = vk::False;

        vk::PipelineColorBlendStateCreateInfo colorBlending{
            .logicOpEnable = vk::False,
            .logicOp = vk::LogicOp::eCopy,
            .attachmentCount = 1,
            .pAttachments = &colorBlendAttachment
        };

        std::vector dynamicStates = {
            vk::DynamicState::eViewport,
            vk::DynamicState::eScissor
        };
        vk::PipelineDynamicStateCreateInfo dynamicState{
            .dynamicStateCount = static_cast<uint32_t>(dynamicStates.size()), .pDynamicStates = dynamicStates.data()
        };

        vk::PipelineLayoutCreateInfo pipelineLayoutInfo{
            .setLayoutCount = 1, .pSetLayouts = &*descriptorSetLayout, .pushConstantRangeCount = 0
        };

        pipelineLayout = vk::raii::PipelineLayout(device, pipelineLayoutInfo);

        vk::Format depthFormat = findDepthFormat();
        vk::PipelineRenderingCreateInfo pipelineRenderingCreateInfo{
            .colorAttachmentCount = 1,
            .pColorAttachmentFormats = &swapChainSurfaceFormat.format,
            .depthAttachmentFormat = depthFormat
        };
        vk::GraphicsPipelineCreateInfo pipelineInfo{
            .pNext = &pipelineRenderingCreateInfo,
            .stageCount = 2,
            .pStages = shaderStages,
            .pVertexInputState = &vertexInputInfo,
            .pInputAssemblyState = &inputAssembly,
            .pViewportState = &viewportState,
            .pRasterizationState = &rasterizer,
            .pMultisampleState = &multisampling,
            .pDepthStencilState = &depthStencil,
            .pColorBlendState = &colorBlending,
            .pDynamicState = &dynamicState,
            .layout = pipelineLayout,
            .renderPass = nullptr
        };

        graphicsPipeline = vk::raii::Pipeline(device, nullptr, pipelineInfo);
    }

    void createCommandPool() {
        vk::CommandPoolCreateInfo poolInfo{
            .flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer,
            .queueFamilyIndex = queueIndex
        };
        commandPool = vk::raii::CommandPool(device, poolInfo);
    }

    void createSceneResources()
    {
        vk::Format colorFormat = swapChainSurfaceFormat.format;
        // single-sampled and SAMPLED
        createImage(
            swapChainExtent.width, swapChainExtent.height,
            1, vk::SampleCountFlagBits::e1, colorFormat,
            vk::ImageTiling::eOptimal,
            vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled,
            vk::MemoryPropertyFlagBits::eDeviceLocal,
            sceneImage, sceneImageMemory);
        sceneImageView = createImageView(sceneImage, colorFormat, vk::ImageAspectFlagBits::eColor, 1);
    }
    
    void createColorResources()
    {
        vk::Format colorFormat = swapChainSurfaceFormat.format;

        createImage(swapChainExtent.width, swapChainExtent.height, 1, msaaSamples, colorFormat,
                    vk::ImageTiling::eOptimal,
                    vk::ImageUsageFlagBits::eTransientAttachment | vk::ImageUsageFlagBits::eColorAttachment,
                    vk::MemoryPropertyFlagBits::eDeviceLocal, colorImage, colorImageMemory);
        colorImageView = createImageView(colorImage, colorFormat, vk::ImageAspectFlagBits::eColor, 1);
    }

    void createDepthResources()
    {
        vk::Format depthFormat = findDepthFormat();

        createImage(swapChainExtent.width, swapChainExtent.height, 1, msaaSamples, depthFormat,
                    vk::ImageTiling::eOptimal,
                    vk::ImageUsageFlagBits::eDepthStencilAttachment, vk::MemoryPropertyFlagBits::eDeviceLocal,
                    depthImage, depthImageMemory);
        depthImageView = createImageView(depthImage, depthFormat, vk::ImageAspectFlagBits::eDepth, 1);
    }

    vk::Format findSupportedFormat(const std::vector<vk::Format>& candidates, vk::ImageTiling tiling, vk::FormatFeatureFlags features) const {
        for (const auto format : candidates) {
            vk::FormatProperties props = physicalDevice.getFormatProperties(format);

            if (tiling == vk::ImageTiling::eLinear && (props.linearTilingFeatures & features) == features) {
                return format;
            }
            if (tiling == vk::ImageTiling::eOptimal && (props.optimalTilingFeatures & features) == features) {
                return format;
            }
        }

        throw std::runtime_error("failed to find supported format!");
    }

    [[nodiscard]] vk::Format findDepthFormat() const {
        return findSupportedFormat(
        {vk::Format::eD32Sfloat, vk::Format::eD32SfloatS8Uint, vk::Format::eD24UnormS8Uint},
            vk::ImageTiling::eOptimal,
            vk::FormatFeatureFlagBits::eDepthStencilAttachment
        );
    }

    static bool hasStencilComponent(vk::Format format) {
        return format == vk::Format::eD32SfloatS8Uint || format == vk::Format::eD24UnormS8Uint;
    }

    void createTextureImage()
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
        ktx_size_t imageSize = ktxTexture_GetImageSize(kTexture, 0);
        ktx_uint8_t* ktxTextureData = ktxTexture_GetData(kTexture);

        // Create staging buffer
        vk::raii::Buffer stagingBuffer({});
        vk::raii::DeviceMemory stagingBufferMemory({});
        createBuffer(imageSize, vk::BufferUsageFlagBits::eTransferSrc,
                     vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent,
                     stagingBuffer, stagingBufferMemory);

        // Copy texture data to staging buffer
        void* data = stagingBufferMemory.mapMemory(0, imageSize);
        memcpy(data, ktxTextureData, imageSize);
        stagingBufferMemory.unmapMemory();

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

            // Copy data from staging buffer to texture image
            transitionImageLayout(textureImage, vk::ImageLayout::eUndefined, vk::ImageLayout::eTransferDstOptimal,
                                  mipLevels);

            // Copy each mip level
            for (uint32_t i = 0; i < mipLevels; i++)
            {
                ktx_size_t offset;
                KTX_error_code result = ktxTexture_GetImageOffset(kTexture, i, 0, 0, &offset);
                uint32_t mipWidth = std::max(1u, texWidth >> i);
                uint32_t mipHeight = std::max(1u, texHeight >> i);
                copyBufferToImage(stagingBuffer, textureImage, static_cast<uint32_t>(mipWidth),
                                  static_cast<uint32_t>(mipHeight), offset, mipLevels);
            }

            transitionImageLayout(textureImage, vk::ImageLayout::eTransferDstOptimal,
                                  vk::ImageLayout::eShaderReadOnlyOptimal, mipLevels);
        }
        else
        {
            mipLevels = static_cast<uint32_t>(std::floor(std::log2(std::max(texWidth, texHeight)))) + 1;
            // Create the texture image
            createImage(texWidth, texHeight, mipLevels, vk::SampleCountFlagBits::e1, textureFormat,
                        vk::ImageTiling::eOptimal,
                        vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eTransferDst |
                        vk::ImageUsageFlagBits::eSampled, vk::MemoryPropertyFlagBits::eDeviceLocal, textureImage,
                        textureImageMemory);

            // Copy data from staging buffer to texture image
            transitionImageLayout(textureImage, vk::ImageLayout::eUndefined, vk::ImageLayout::eTransferDstOptimal,
                                  mipLevels);

            copyBufferToImage(stagingBuffer, textureImage, static_cast<uint32_t>(texWidth),
                              static_cast<uint32_t>(texHeight));

            generateMipmaps(textureImage, textureFormat, texWidth, texHeight, mipLevels);
        }

        // Cleanup KTX resources
        ktxTexture_Destroy(kTexture);
    }

    void generateMipmaps(vk::raii::Image& image, vk::Format imageFormat, int32_t texWidth, int32_t texHeight,
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

    vk::SampleCountFlagBits getMaxUsableSampleCount()
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

    void createTextureImageView()
    {
        textureImageView = createImageView(textureImage, textureImageFormat, vk::ImageAspectFlagBits::eColor,
                                           mipLevels);
    }

    void createTextureSampler()
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

    vk::raii::ImageView createImageView(vk::raii::Image& image, vk::Format format, vk::ImageAspectFlags aspectFlags,
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

    void createImage(uint32_t width, uint32_t height, uint32_t mipLevels, vk::SampleCountFlagBits numSamples,
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

    void transitionImageLayout(const vk::raii::Image& image, vk::ImageLayout oldLayout, vk::ImageLayout newLayout,
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

    void copyBufferToImage(const vk::raii::Buffer& buffer, vk::raii::Image& image, uint32_t width, uint32_t height,
                           uint64_t offset = 0, uint32_t mipLevel = 0)
    {
        std::unique_ptr<vk::raii::CommandBuffer> commandBuffer = beginSingleTimeCommands();
        vk::BufferImageCopy region
        {
            .bufferOffset = offset, .bufferRowLength = 0, .bufferImageHeight = 0,
            .imageSubresource = {vk::ImageAspectFlagBits::eColor, mipLevel, 0, 1},
            .imageOffset = {0, 0, 0},
            .imageExtent = {width, height, 1}
        };
        commandBuffer->copyBufferToImage(buffer, image, vk::ImageLayout::eTransferDstOptimal, {region});
        endSingleTimeCommands(*commandBuffer);
    }

    void loadModel() {
        // Use tinygltf to load the model instead of tinyobjloader
        tinygltf::Model model;
        tinygltf::TinyGLTF loader;
        std::string err;
        std::string warn;

        bool ret = loader.LoadBinaryFromFile(&model, &err, &warn, MODEL_PATH);

        if (!warn.empty()) {
            std::cout << "glTF warning: " << warn << std::endl;
        }

        if (!err.empty()) {
            std::cout << "glTF error: " << err << std::endl;
        }

        if (!ret) {
            throw std::runtime_error("Failed to load glTF model");
        }

        vertices.clear();
        indices.clear();

        // Process all meshes in the model
        for (const auto& mesh : model.meshes) {
            for (const auto& primitive : mesh.primitives) {
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

                if (hasTexCoords) {
                    texCoordAccessor = &model.accessors[primitive.attributes.at("TEXCOORD_0")];
                    texCoordBufferView = &model.bufferViews[texCoordAccessor->bufferView];
                    texCoordBuffer = &model.buffers[texCoordBufferView->buffer];
                }

                uint32_t baseVertex = static_cast<uint32_t>(vertices.size());

                for (size_t i = 0; i < posAccessor.count; i++) {
                    Vertex vertex{};

                    const float* pos = reinterpret_cast<const float*>(&posBuffer.data[posBufferView.byteOffset + posAccessor.byteOffset + i * 12]);
                    vertex.pos = {pos[0], pos[1], pos[2]};

                    if (hasTexCoords) {
                        const float* texCoord = reinterpret_cast<const float*>(&texCoordBuffer->data[texCoordBufferView->byteOffset + texCoordAccessor->byteOffset + i * 8]);
                        vertex.texCoord = {texCoord[0], texCoord[1]};
                    } else {
                        vertex.texCoord = {0.0f, 0.0f};
                    }

                    vertex.color = {1.0f, 1.0f, 1.0f};

                    vertices.push_back(vertex);
                }

                const unsigned char* indexData = &indexBuffer.data[indexBufferView.byteOffset + indexAccessor.byteOffset];
                size_t indexCount = indexAccessor.count;
                size_t indexStride = 0;

                // Determine index stride based on component type
                if (indexAccessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT) {
                    indexStride = sizeof(uint16_t);
                } else if (indexAccessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT) {
                    indexStride = sizeof(uint32_t);
                } else if (indexAccessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE) {
                    indexStride = sizeof(uint8_t);
                } else {
                    throw std::runtime_error("Unsupported index component type");
                }

                indices.reserve(indices.size() + indexCount);

                for (size_t i = 0; i < indexCount; i++) {
                    uint32_t index = 0;

                    if (indexAccessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT) {
                        index = *reinterpret_cast<const uint16_t*>(indexData + i * indexStride);
                    } else if (indexAccessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT) {
                        index = *reinterpret_cast<const uint32_t*>(indexData + i * indexStride);
                    } else if (indexAccessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE) {
                        index = *reinterpret_cast<const uint8_t*>(indexData + i * indexStride);
                    }

                    indices.push_back(baseVertex + index);
                }
            }
        }
    }

    //change this to vma because of max allocation  maxMemoryAllocationCount  physical device limit,
    void createVertexBuffer() {
        vk::DeviceSize bufferSize = sizeof(vertices[0]) * vertices.size();
        vk::raii::Buffer stagingBuffer({});
        vk::raii::DeviceMemory stagingBufferMemory({});
        createBuffer(bufferSize, vk::BufferUsageFlagBits::eTransferSrc, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent, stagingBuffer, stagingBufferMemory);

        void* dataStaging = stagingBufferMemory.mapMemory(0, bufferSize);
        memcpy(dataStaging, vertices.data(), bufferSize);
        stagingBufferMemory.unmapMemory();

        createBuffer(bufferSize, vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eVertexBuffer, vk::MemoryPropertyFlagBits::eDeviceLocal, vertexBuffer, vertexBufferMemory);

        copyBuffer(stagingBuffer, vertexBuffer, bufferSize);
    }

    void createIndexBuffer() {
        vk::DeviceSize bufferSize = sizeof(indices[0]) * indices.size();

        vk::raii::Buffer stagingBuffer({});
        vk::raii::DeviceMemory stagingBufferMemory({});
        createBuffer(bufferSize, vk::BufferUsageFlagBits::eTransferSrc, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent, stagingBuffer, stagingBufferMemory);

        void* data = stagingBufferMemory.mapMemory(0, bufferSize);
        memcpy(data, indices.data(), bufferSize);
        stagingBufferMemory.unmapMemory();

        createBuffer(bufferSize, vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eIndexBuffer, vk::MemoryPropertyFlagBits::eDeviceLocal, indexBuffer, indexBufferMemory);

        copyBuffer(stagingBuffer, indexBuffer, bufferSize);
    }

    // Initialize the game objects with different positions, rotations, and scales
    void setupGameObjects() {
        // Object 1 - Center
        gameObjects[0].position = {0.0f, 0.0f, 0.0f};
        gameObjects[0].rotation = {0.0f, glm::radians(-90.0f), 0.0f};
        gameObjects[0].scale = {1.0f, 1.0f, 1.0f};

        // Object 2 - Left
        gameObjects[1].position = {-2.0f, 0.0f, -1.0f};
        gameObjects[1].rotation = {0.0f, glm::radians(-45.0f), 0.0f};
        gameObjects[1].scale = {0.75f, 0.75f, 0.75f};

        // Object 3 - Right
        gameObjects[2].position = {2.0f, 0.0f, -1.0f};
        gameObjects[2].rotation = {0.0f, glm::radians(45.0f), 0.0f};
        gameObjects[2].scale = {0.75f, 0.75f, 0.75f};
    }

    // Create uniform buffers for each object
    void createUniformBuffers() {
        // For each game object
        for (auto& gameObject : gameObjects) {
            gameObject.uniformBuffers.clear();
            gameObject.uniformBuffersMemory.clear();
            gameObject.uniformBuffersMapped.clear();

            // Create uniform buffers for each frame in flight
            for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
                vk::DeviceSize bufferSize = sizeof(UniformBufferObject);
                vk::raii::Buffer buffer({});
                vk::raii::DeviceMemory bufferMem({});
                createBuffer(bufferSize, vk::BufferUsageFlagBits::eUniformBuffer, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent, buffer, bufferMem);
                gameObject.uniformBuffers.emplace_back(std::move(buffer));
                gameObject.uniformBuffersMemory.emplace_back(std::move(bufferMem));
                gameObject.uniformBuffersMapped.emplace_back(gameObject.uniformBuffersMemory[i].mapMemory(0, bufferSize));
            }
        }
    }

    void createDescriptorPool()
    {
        {
            // We need MAX_OBJECTS * MAX_FRAMES_IN_FLIGHT descriptor sets
            std::array poolSize
            {
                vk::DescriptorPoolSize(vk::DescriptorType::eUniformBuffer, MAX_OBJECTS * MAX_FRAMES_IN_FLIGHT),
                vk::DescriptorPoolSize(vk::DescriptorType::eCombinedImageSampler, MAX_OBJECTS * MAX_FRAMES_IN_FLIGHT)
            };
            vk::DescriptorPoolCreateInfo poolInfo
            {
                .flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet,
                .maxSets = MAX_OBJECTS * MAX_FRAMES_IN_FLIGHT,
                .poolSizeCount = static_cast<uint32_t>(poolSize.size()),
                .pPoolSizes = poolSize.data()
            };
            descriptorPool = vk::raii::DescriptorPool(device, poolInfo);
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
        }
    }

    void createDescriptorSets() {
        // For each game object
        for (auto& gameObject : gameObjects) {
            // Create descriptor sets for each frame in flight
            std::vector<vk::DescriptorSetLayout> layouts(MAX_FRAMES_IN_FLIGHT, *descriptorSetLayout);
            vk::DescriptorSetAllocateInfo allocInfo{
                .descriptorPool = *descriptorPool,
                .descriptorSetCount = static_cast<uint32_t>(layouts.size()),
                .pSetLayouts = layouts.data()
            };

            gameObject.descriptorSets.clear();
            gameObject.descriptorSets = device.allocateDescriptorSets(allocInfo);

            for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
                vk::DescriptorBufferInfo bufferInfo{
                    .buffer = *gameObject.uniformBuffers[i],
                    .offset = 0,
                    .range = sizeof(UniformBufferObject)
                };
                vk::DescriptorImageInfo imageInfo{
                    .sampler = *textureSampler,
                    .imageView = *textureImageView,
                    .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal
                };
                std::array descriptorWrites{
                    vk::WriteDescriptorSet{
                        .dstSet = *gameObject.descriptorSets[i],
                        .dstBinding = 0,
                        .dstArrayElement = 0,
                        .descriptorCount = 1,
                        .descriptorType = vk::DescriptorType::eUniformBuffer,
                        .pBufferInfo = &bufferInfo
                    },
                    vk::WriteDescriptorSet{
                        .dstSet = *gameObject.descriptorSets[i],
                        .dstBinding = 1,
                        .dstArrayElement = 0,
                        .descriptorCount = 1,
                        .descriptorType = vk::DescriptorType::eCombinedImageSampler,
                        .pImageInfo = &imageInfo
                    }
                };
                device.updateDescriptorSets(descriptorWrites, {});
            }
        }
    }

    void createBuffer(vk::DeviceSize size, vk::BufferUsageFlags usage, vk::MemoryPropertyFlags properties, vk::raii::Buffer& buffer, vk::raii::DeviceMemory& bufferMemory) {
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
        bufferMemory = vk::raii::DeviceMemory(device, allocInfo);
        buffer.bindMemory(*bufferMemory, 0);
    }

    std::unique_ptr<vk::raii::CommandBuffer> beginSingleTimeCommands() {
        vk::CommandBufferAllocateInfo allocInfo{
            .commandPool = *commandPool,
            .level = vk::CommandBufferLevel::ePrimary,
            .commandBufferCount = 1
        };
        std::unique_ptr<vk::raii::CommandBuffer> commandBuffer = std::make_unique<vk::raii::CommandBuffer>(std::move(vk::raii::CommandBuffers(device, allocInfo).front()));

        vk::CommandBufferBeginInfo beginInfo{
            .flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit
        };
        commandBuffer->begin(beginInfo);

        return commandBuffer;
    }

    void endSingleTimeCommands(const vk::raii::CommandBuffer& commandBuffer) const {
        commandBuffer.end();

        vk::SubmitInfo submitInfo{ .commandBufferCount = 1, .pCommandBuffers = &*commandBuffer };
        queue.submit(submitInfo, nullptr);
        queue.waitIdle();
    }

    //change this to use the above functions check images chapter or 1 before idk
    void copyBuffer(vk::raii::Buffer & srcBuffer, vk::raii::Buffer & dstBuffer, vk::DeviceSize size) {
        vk::CommandBufferAllocateInfo allocInfo{ .commandPool = *commandPool, .level = vk::CommandBufferLevel::ePrimary, .commandBufferCount = 1 };
        vk::raii::CommandBuffer commandCopyBuffer = std::move(device.allocateCommandBuffers(allocInfo).front());
        commandCopyBuffer.begin(vk::CommandBufferBeginInfo{ .flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit });
        commandCopyBuffer.copyBuffer(*srcBuffer, *dstBuffer, vk::BufferCopy{ .size = size });
        commandCopyBuffer.end();
        queue.submit(vk::SubmitInfo{ .commandBufferCount = 1, .pCommandBuffers = &*commandCopyBuffer }, nullptr);
        queue.waitIdle();
    }

    uint32_t findMemoryType(uint32_t typeFilter, vk::MemoryPropertyFlags properties) {
        vk::PhysicalDeviceMemoryProperties memProperties = physicalDevice.getMemoryProperties();

        for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
            if ((typeFilter & (1 << i)) && (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
                return i;
            }
        }

        throw std::runtime_error("failed to find suitable memory type!");
    }
    
    void createCommandBuffers() {
        commandBuffers.clear();
        vk::CommandBufferAllocateInfo allocInfo{ .commandPool = *commandPool, .level = vk::CommandBufferLevel::ePrimary,
                                                 .commandBufferCount = MAX_FRAMES_IN_FLIGHT };
        commandBuffers = vk::raii::CommandBuffers(device, allocInfo);
    }

    void recordCommandBuffer(uint32_t imageIndex)
    {
        commandBuffers[currentFrame].begin({});
        // Before starting rendering, transition the images to the appropriate layouts
        // Transition the swapchain image to COLOR_ATTACHMENT_OPTIMAL
        transition_image_layout(
            imageIndex,
            vk::ImageLayout::eUndefined,
            vk::ImageLayout::eColorAttachmentOptimal,
            {}, // srcAccessMask (no need to wait for previous operations)
            vk::AccessFlagBits2::eColorAttachmentWrite, // dstAccessMask
            vk::PipelineStageFlagBits2::eTopOfPipe, // srcStage
            vk::PipelineStageFlagBits2::eColorAttachmentOutput // dstStage
        );
  
        // Transition the multisampled color image to COLOR_ATTACHMENT_OPTIMAL
        transition_image_layout_custom(
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
        transition_image_layout_custom(
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
        transition_image_layout_custom(
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
        vk::RenderingAttachmentInfo colorAttachment = {
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
        vk::RenderingAttachmentInfo depthAttachment = {
            .imageView = depthImageView,
            .imageLayout = vk::ImageLayout::eDepthAttachmentOptimal,
            .loadOp = vk::AttachmentLoadOp::eClear,
            .storeOp = vk::AttachmentStoreOp::eDontCare,
            .clearValue = clearDepth
        };

        vk::RenderingInfo renderingInfo = {
            .renderArea = {.offset = {0, 0}, .extent = swapChainExtent},
            .layerCount = 1,
            .colorAttachmentCount = 1,
            .pColorAttachments = &colorAttachment,
            .pDepthAttachment = &depthAttachment
        };
        commandBuffers[currentFrame].beginRendering(renderingInfo);
        commandBuffers[currentFrame].bindPipeline(vk::PipelineBindPoint::eGraphics, *graphicsPipeline);
        commandBuffers[currentFrame].setViewport(0, vk::Viewport(0.0f, 0.0f, static_cast<float>(swapChainExtent.width),
                                                                 static_cast<float>(swapChainExtent.height), 0.0f,
                                                                 1.0f));
        commandBuffers[currentFrame].setScissor(0, vk::Rect2D(vk::Offset2D(0, 0), swapChainExtent));
        commandBuffers[currentFrame].bindVertexBuffers(0, *vertexBuffer, {0});
        commandBuffers[currentFrame].bindIndexBuffer(*indexBuffer, 0, vk::IndexType::eUint32);
        /*commandBuffers[currentFrame].bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipelineLayout, 0, *descriptorSets[currentFrame], nullptr);*/
        // Draw each object with its own descriptor set
        for (const auto& gameObject : gameObjects)
        {
            // Bind the descriptor set for this object
            commandBuffers[currentFrame].bindDescriptorSets(
                vk::PipelineBindPoint::eGraphics,
                *pipelineLayout,
                0,
                *gameObject.descriptorSets[currentFrame],
                nullptr
            );

            // Draw the object
            commandBuffers[currentFrame].drawIndexed(indices.size(), 1, 0, 0, 0);
        }
        commandBuffers[currentFrame].endRendering();
        
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
        sceneImageInitialized = true;
        
        // Second pass: draw ImGui to swapchain image
        vk::RenderingAttachmentInfo swapColorAttachment = {
            .imageView = swapChainImageViews[imageIndex],
            .imageLayout = vk::ImageLayout::eColorAttachmentOptimal,
            .loadOp = vk::AttachmentLoadOp::eClear,
            .storeOp = vk::AttachmentStoreOp::eStore
        };
        vk::RenderingInfo renderingInfo2 = {
            .renderArea = {.offset = {0, 0}, .extent = swapChainExtent},
            .layerCount = 1,
            .colorAttachmentCount = 1,
            .pColorAttachments = &swapColorAttachment
        };

        commandBuffers[currentFrame].beginRendering(renderingInfo2);
        ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), *commandBuffers[currentFrame]);
        commandBuffers[currentFrame].endRendering();
        
        // After rendering, transition the images to appropriate layouts

        // Transition the swapchain image to PRESENT_SRC
        transition_image_layout(
            imageIndex,
            vk::ImageLayout::eColorAttachmentOptimal,
            vk::ImageLayout::ePresentSrcKHR,
            vk::AccessFlagBits2::eColorAttachmentWrite, // srcAccessMask
            {}, // dstAccessMask
            vk::PipelineStageFlagBits2::eColorAttachmentOutput, // srcStage
            vk::PipelineStageFlagBits2::eBottomOfPipe // dstStage
        );
        commandBuffers[currentFrame].end();
    }

    void transition_image_layout(
        uint32_t imageIndex,
        vk::ImageLayout old_layout,
        vk::ImageLayout new_layout,
        vk::AccessFlags2 src_access_mask,
        vk::AccessFlags2 dst_access_mask,
        vk::PipelineStageFlags2 src_stage_mask,
        vk::PipelineStageFlags2 dst_stage_mask
        ) {
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

    void transition_image_layout_custom(
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

    void createSyncObjects() {
        presentCompleteSemaphores.clear();
        renderFinishedSemaphores.clear();
        inFlightFences.clear();

        for (size_t i = 0; i < swapChainImages.size(); i++) {
            presentCompleteSemaphores.emplace_back(device, vk::SemaphoreCreateInfo());
            renderFinishedSemaphores.emplace_back(device, vk::SemaphoreCreateInfo());
        }

        for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
            inFlightFences.emplace_back(device, vk::FenceCreateInfo{ .flags = vk::FenceCreateFlagBits::eSignaled });
        }
    }

    void updateUniformBuffers() {
        static auto startTime = std::chrono::high_resolution_clock::now();
        static auto lastFrameTime = startTime;
        auto currentTime = std::chrono::high_resolution_clock::now();
        float time = std::chrono::duration<float>(currentTime - startTime).count();
        float deltaTime = std::chrono::duration<float>(currentTime - lastFrameTime).count();
        lastFrameTime = currentTime;

        // Camera and projection matrices (shared by all objects)
        glm::mat4 view = glm::lookAt(glm::vec3(2.0f, 2.0f, 6.0f), glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
        glm::mat4 proj = glm::perspective(glm::radians(45.0f), static_cast<float>(swapChainExtent.width) / static_cast<float>(swapChainExtent.height), 0.1f, 20.0f);
        proj[1][1] *= -1;

        // Update uniform buffers for each object
        for (auto& gameObject : gameObjects) {
            // Apply continuous rotation to the object based on frame time
            const float rotationSpeed = 0.5f; // Rotation speed in radians per second
            gameObject.rotation.y += rotationSpeed * deltaTime; // Slow rotation around Y axis scaled by frame time

            // Get the model matrix for this object
            glm::mat4 model = gameObject.getModelMatrix();

            // Create and update the UBO
            UniformBufferObject ubo{
                .model = model,
                .view = view,
                .proj = proj
            };

            // Copy the UBO data to the mapped memory
            memcpy(gameObject.uniformBuffersMapped[currentFrame], &ubo, sizeof(ubo));
        }
    }

    void drawFrame()
    {
        while (vk::Result::eTimeout == device.waitForFences(*inFlightFences[currentFrame], vk::True, UINT64_MAX));
        auto [result, imageIndex] = swapChain.acquireNextImage(UINT64_MAX, *presentCompleteSemaphores[currentFrame], nullptr);

        if (result == vk::Result::eErrorOutOfDateKHR) {
            recreateSwapChain();
            return;
        }
        if (result != vk::Result::eSuccess && result != vk::Result::eSuboptimalKHR) {
            throw std::runtime_error("failed to acquire swap chain image!");
        }

        /*-- The ImGui code -*/

        /*-- 
         * The rendering of the scene is done using dynamic rendering with a G-Buffer (see recordGraphicCommands).
         * The target image will be rendered/displayed using ImGui.
         * Its placement will cover the entire viewport (ImGui draws a quad with the texture we provide),
         * and the image will be displayed in the viewport.
         * There are multiple ways to display the image, but this method is the most flexible.
         * Other methods include:
         *  - Blitting the image to the swapchain image, with the UI drawn on top. However, this makes it harder 
         *    to fit the image within a specific area of the window.
         *  - Using the image as a texture in a quad and rendering it to the swapchain image. This is what ImGui 
         *    does, but we don't need to add a quad to the scene, as ImGui handles it for us.
        -*/
        // Using the dock "Viewport", this sets the window to cover the entire central viewport
        if(ImGui::Begin("Viewport"))
        {
            // !!! This is where the GBuffer image is displayed !!!
            ImGui::Image(uiDescriptorSet[0], ImGui::GetContentRegionAvail());

            // Adding overlay text on the upper left corner
            ImGui::SetCursorPos(ImVec2(0, 0));
            ImGui::Text("FPS: %.1f", ImGui::GetIO().Framerate);
        }
        ImGui::End();
        
        ImGui::Render();  // This is creating the data to draw the UI (not on GPU yet)
        
        // Update uniform buffers for all objects
        updateUniformBuffers();

        device.resetFences(*inFlightFences[currentFrame]);
        commandBuffers[currentFrame].reset();
        recordCommandBuffer(imageIndex);

        frameCount++;
        auto now = std::chrono::high_resolution_clock::now();
        float elapsed = std::chrono::duration<float>(now - lastTime).count();
        if (elapsed >= 1.0f) {
            fps = frameCount / elapsed;
            frameCount = 0;
            lastTime = now;
            std::cout << "FPS: " << fps << std::endl;
        }

        vk::PipelineStageFlags waitDestinationStageMask(vk::PipelineStageFlagBits::eColorAttachmentOutput);
        const vk::SubmitInfo submitInfo{
            .waitSemaphoreCount = 1,
            .pWaitSemaphores = &*presentCompleteSemaphores[currentFrame],
            .pWaitDstStageMask = &waitDestinationStageMask,
            .commandBufferCount = 1,
            .pCommandBuffers = &*commandBuffers[currentFrame],
            .signalSemaphoreCount = 1,
            .pSignalSemaphores = &*renderFinishedSemaphores[imageIndex]
        };
        queue.submit(submitInfo, *inFlightFences[currentFrame]);

        const vk::PresentInfoKHR presentInfoKHR{
            .waitSemaphoreCount = 1,
            .pWaitSemaphores = &*renderFinishedSemaphores[imageIndex],
            .swapchainCount = 1,
            .pSwapchains = &*swapChain,
            .pImageIndices = &imageIndex
        };
        VkResult rawResult = vkQueuePresentKHR(*queue, reinterpret_cast<const VkPresentInfoKHR*>(&presentInfoKHR));
        result = static_cast<vk::Result>(rawResult);
        //result = queue.presentKHR(presentInfoKHR); when resizing in hpp is fixed then use this https://github.com/KhronosGroup/Vulkan-Tutorial/issues/73
        if (result == vk::Result::eErrorOutOfDateKHR || result == vk::Result::eSuboptimalKHR || framebufferResized) {
            framebufferResized = false;
            recreateSwapChain();
        } else if (result != vk::Result::eSuccess) {
            throw std::runtime_error("failed to present swap chain image!");
        }
        currentFrame = (currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
    }

    [[nodiscard]] vk::raii::ShaderModule createShaderModule(const std::vector<char>& code) const {
        vk::ShaderModuleCreateInfo createInfo{ .codeSize = code.size(), .pCode = reinterpret_cast<const uint32_t*>(code.data()) };
        vk::raii::ShaderModule shaderModule{ device, createInfo };

        return shaderModule;
    }

    static uint32_t chooseSwapMinImageCount(vk::SurfaceCapabilitiesKHR const & surfaceCapabilities) {
        auto minImageCount = std::max( 3u, surfaceCapabilities.minImageCount );
        if ((0 < surfaceCapabilities.maxImageCount) && (surfaceCapabilities.maxImageCount < minImageCount)) {
            minImageCount = surfaceCapabilities.maxImageCount;
        }
        return minImageCount;
    }

    static vk::SurfaceFormatKHR chooseSwapSurfaceFormat(const std::vector<vk::SurfaceFormatKHR>& availableFormats) {
        assert(!availableFormats.empty());
        const auto formatIt = std::ranges::find_if(
            availableFormats,
            []( const auto & format ) { return format.format == vk::Format::eB8G8R8A8Srgb && format.colorSpace == vk::ColorSpaceKHR::eSrgbNonlinear; } );
        return formatIt != availableFormats.end() ? *formatIt : availableFormats[0];
    }

    static vk::PresentModeKHR chooseSwapPresentMode(const std::vector<vk::PresentModeKHR>& availablePresentModes, bool vsync) {
        if (vsync) 
            return vk::PresentModeKHR::eFifo; // guaranteed available

        if (std::ranges::any_of(availablePresentModes,
            [](auto mode){ return mode == vk::PresentModeKHR::eMailbox; }))
            return vk::PresentModeKHR::eMailbox;

        if (std::ranges::any_of(availablePresentModes,
            [](auto mode){ return mode == vk::PresentModeKHR::eImmediate; }))
            return vk::PresentModeKHR::eImmediate;

        return vk::PresentModeKHR::eFifo;
    }

    vk::Extent2D chooseSwapExtent(const vk::SurfaceCapabilitiesKHR& capabilities)
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

    std::vector<const char*> getRequiredExtensions()
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

    static VKAPI_ATTR vk::Bool32 VKAPI_CALL debugCallback(vk::DebugUtilsMessageSeverityFlagBitsEXT severity,
                                                          vk::DebugUtilsMessageTypeFlagsEXT type,
                                                          const vk::DebugUtilsMessengerCallbackDataEXT* pCallbackData,
                                                          void*)
    {
        std::cerr << "validation layer: type " << to_string(type) << " msg: " << pCallbackData->pMessage << std::endl;

        return vk::False;
    }

    static std::vector<char> readFile(const std::string& filename)
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
};

SDL_AppResult SDL_AppInit(void** appstate, int argc, char** argv)
{
    SDL_SetAppMetadata("Example Renderer Clear", "1.0", "com.example.renderer-clear");

    try
    {
        auto app = std::make_unique<HelloTriangleApplication>();
        app->init();

        *appstate = app.release();
    }
    catch (const std::exception& e)
    {
        std::cerr << e.what() << std::endl;
        return SDL_APP_FAILURE;
    }

    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppIterate(void* appstate)
{
    auto app = static_cast<HelloTriangleApplication*>(appstate);
    app->render();

    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppEvent(void* appstate, SDL_Event* event)
{
    auto app = static_cast<HelloTriangleApplication*>(appstate);

    ImGui_ImplSDL3_ProcessEvent(event);

    switch (event->type)
    {
    case SDL_EVENT_QUIT:
        {
            return SDL_APP_SUCCESS;
        }
    case SDL_EVENT_WINDOW_RESIZED:
        {
            app->setFramebufferResized(true);
            return SDL_APP_CONTINUE;
        }
    case SDL_EVENT_WINDOW_MINIMIZED:
        {
            app->setWindowMinimized(true);
            return SDL_APP_CONTINUE;
        }
    case SDL_EVENT_WINDOW_RESTORED:
        {
            app->setWindowMinimized(false);
            app->setFramebufferResized(true); // force swapchain recreation
            return SDL_APP_CONTINUE;
        }
    case SDL_EVENT_KEY_DOWN:
        {
            if (event->key.scancode == SDL_SCANCODE_ESCAPE)
            {
                return SDL_APP_SUCCESS;
            }
        }
    default:
        return SDL_APP_CONTINUE;
    }
}

void SDL_AppQuit(void* appstate, SDL_AppResult result)
{
    auto app = static_cast<HelloTriangleApplication*>(appstate);
    app->shutdown();
    delete app;
}
