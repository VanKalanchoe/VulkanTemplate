#pragma once

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

#include <SDL3/SDL_init.h>
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
        if (it != m_samplerMap.end())
        {
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

class Renderer
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
    void setWindowMinimized(bool minimized) { windowMinimized = minimized; }

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

    SDL_AppResult initWindow();

    SDL_AppResult initVulkan();

    SDL_AppResult initImGui();

    void mainLoop();

    void cleanupSwapChain();

    void cleanup();

    void recreateSwapChain();

    void createInstance();

    void setupDebugMessenger();

    void createSurface();

    void pickPhysicalDevice();

    void createLogicalDevice();

    void createSwapChain();

    //change this ? from chapter image view
    void createImageViews();

    void createDescriptorSetLayout();

    void createGraphicsPipeline();

    void createCommandPool();

    void createSceneResources();

    void createColorResources();

    void createDepthResources();

    vk::Format findSupportedFormat(const std::vector<vk::Format>& candidates, vk::ImageTiling tiling,
                                   vk::FormatFeatureFlags features) const;

    [[nodiscard]] vk::Format findDepthFormat() const;

    static bool hasStencilComponent(vk::Format format);

    void createTextureImage();

    void generateMipmaps(vk::raii::Image& image, vk::Format imageFormat, int32_t texWidth, int32_t texHeight,
                         uint32_t mipLevels);

    vk::SampleCountFlagBits getMaxUsableSampleCount();

    void createTextureImageView();

    void createTextureSampler();

    vk::raii::ImageView createImageView(vk::raii::Image& image, vk::Format format, vk::ImageAspectFlags aspectFlags,
                                        uint32_t mipLevels);

    void createImage(uint32_t width, uint32_t height, uint32_t mipLevels, vk::SampleCountFlagBits numSamples,
                     vk::Format format, vk::ImageTiling tiling,
                     vk::ImageUsageFlags usage, vk::MemoryPropertyFlags properties, vk::raii::Image& image,
                     vk::raii::DeviceMemory& imageMemory);

    void transitionImageLayout(const vk::raii::Image& image, vk::ImageLayout oldLayout, vk::ImageLayout newLayout,
                               uint32_t mipLevels);

    void copyBufferToImage(const vk::raii::Buffer& buffer, vk::raii::Image& image, uint32_t width, uint32_t height,
                           uint64_t offset = 0, uint32_t mipLevel = 0);

    void loadModel();

    //change this to vma because of max allocation  maxMemoryAllocationCount  physical device limit,
    void createVertexBuffer();

    void createIndexBuffer();

    // Initialize the game objects with different positions, rotations, and scales
    void setupGameObjects();

    // Create uniform buffers for each object
    void createUniformBuffers();

    void createDescriptorPool();

    void createDescriptorSets();

    void createBuffer(vk::DeviceSize size, vk::BufferUsageFlags usage, vk::MemoryPropertyFlags properties,
                      vk::raii::Buffer& buffer, vk::raii::DeviceMemory& bufferMemory);

    std::unique_ptr<vk::raii::CommandBuffer> beginSingleTimeCommands();

    void endSingleTimeCommands(const vk::raii::CommandBuffer& commandBuffer) const;

    //change this to use the above functions check images chapter or 1 before idk
    void copyBuffer(vk::raii::Buffer& srcBuffer, vk::raii::Buffer& dstBuffer, vk::DeviceSize size);

    uint32_t findMemoryType(uint32_t typeFilter, vk::MemoryPropertyFlags properties);

    void createCommandBuffers();

    void recordCommandBuffer(uint32_t imageIndex);

    void transition_image_layout(
        uint32_t imageIndex,
        vk::ImageLayout old_layout,
        vk::ImageLayout new_layout,
        vk::AccessFlags2 src_access_mask,
        vk::AccessFlags2 dst_access_mask,
        vk::PipelineStageFlags2 src_stage_mask,
        vk::PipelineStageFlags2 dst_stage_mask
    );

    void transition_image_layout_custom(
        vk::raii::Image& image,
        vk::ImageLayout old_layout,
        vk::ImageLayout new_layout,
        vk::AccessFlags2 src_access_mask,
        vk::AccessFlags2 dst_access_mask,
        vk::PipelineStageFlags2 src_stage_mask,
        vk::PipelineStageFlags2 dst_stage_mask,
        vk::ImageAspectFlags aspect_mask
    );

    void createSyncObjects();

    void updateUniformBuffers();

    void drawFrame();

    [[nodiscard]] vk::raii::ShaderModule createShaderModule(const std::vector<char>& code) const;

    static uint32_t chooseSwapMinImageCount(vk::SurfaceCapabilitiesKHR const& surfaceCapabilities);

    static vk::SurfaceFormatKHR chooseSwapSurfaceFormat(const std::vector<vk::SurfaceFormatKHR>& availableFormats);

    static vk::PresentModeKHR chooseSwapPresentMode(const std::vector<vk::PresentModeKHR>& availablePresentModes,
                                                    bool vsync);

    vk::Extent2D chooseSwapExtent(const vk::SurfaceCapabilitiesKHR& capabilities);

    std::vector<const char*> getRequiredExtensions();

    static VKAPI_ATTR vk::Bool32 VKAPI_CALL debugCallback(vk::DebugUtilsMessageSeverityFlagBitsEXT severity,
                                                          vk::DebugUtilsMessageTypeFlagsEXT type,
                                                          const vk::DebugUtilsMessengerCallbackDataEXT* pCallbackData,
                                                          void*);

    static std::vector<char> readFile(const std::string& filename);
};
