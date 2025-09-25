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
#include <span>
#include <thread>
#include <unordered_map>

#ifdef __INTELLISENSE__
#include <vulkan/vulkan_raii.hpp>
#else
import vulkan_hpp;
#endif

#include <vulkan/vk_platform.h>

// Disable warnings in VMA
#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wnullability-completeness"
#pragma clang diagnostic ignored "-Wmissing-field-initializers"
#pragma clang diagnostic ignored "-Wunused-variable"
#pragma clang diagnostic ignored "-Wunused-function"
#endif
#pragma warning(push)
#pragma warning(disable : 4100)  // Unreferenced formal parameter
#pragma warning(disable : 4189)  // Local variable is initialized but not referenced
#pragma warning(disable : 4127)  // Conditional expression is constant
#pragma warning(disable : 4324)  // Structure was padded due to alignment specifier
#pragma warning(disable : 4505)  // Unreferenced function with internal linkage has been removed
#include "vk_mem_alloc.h"
#pragma warning(pop)
#ifdef __clang__
#pragma clang diagnostic pop
#endif


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

// Macro to either assert or throw based on the build type
#ifdef NDEBUG
#define ASSERT(condition, message)                                                                                     \
do                                                                                                                   \
{                                                                                                                    \
if(!(condition))                                                                                                   \
{                                                                                                                  \
throw std::runtime_error(message);                                                                               \
}                                                                                                                  \
} while(false)
#else
#define ASSERT(condition, message) assert((condition) && (message))
#endif


//--- Vulkan Helpers ------------------------------------------------------------------------------------------------------------
#ifdef NDEBUG
    #define VK_CHECK(vkFnc) vkFnc
#else
#define VK_CHECK(vkFnc)                                                                                                \
{                                                                                                                       \
    VkResult rawResult = (vkFnc);                                                                                       \
    vk::Result checkResult = static_cast<vk::Result>(rawResult);                                                                                       \
    if(checkResult != vk::Result::eSuccess)                                                \
    {                                                                                                                  \
        std::string errMsg = vk::to_string(checkResult);                                                               \
        /*LOGE("Vulkan error: %s", errMsg);    */                                                                            \
        ASSERT(checkResult == vk::Result::eSuccess, errMsg.c_str());                                                                       \
    }                                                                                                                  \
}
#endif

/*--
 * A buffer is a region of memory used to store data.
 * It is used to store vertex data, index data, uniform data, and other types of data.
 * There is a VkBuffer object that represents the buffer, and a VmaAllocation object that represents the memory allocation.
 * The address is used to access the buffer in the shader.
-*/
struct Buffer
{
    vk::Buffer buffer{}; // Vulkan Buffer
    VmaAllocation allocation{}; // Memory associated with the buffer
    vk::DeviceAddress address{}; // Address of the buffer in the shader
    vk::DeviceSize size{}; // Size of the buffer
};

/*--
 * An image is a region of memory used to store image data.
 * It is used to store texture data, framebuffer data, and other types of data.
-*/
struct Image
{
    vk::Image image{}; // Vulkan Image
    VmaAllocation allocation{}; // Memory associated with the image
};

/*-- 
 * The image resource is an image with an image view and a layout.
 * and other information like format and extent.
-*/
struct ImageResource : Image
{
    vk::ImageView view{}; // Image view
    std::vector<vk::ImageView> faceViews;
    vk::Extent2D extent{}; // Size of the image
    vk::ImageLayout layout{}; // Layout of the image (color attachment, shader read, ...)
};

/*- Not implemented here -*/
struct AccelerationStructure
{
    vk::AccelerationStructureKHR accel{};
    VmaAllocation allocation{};
    vk::DeviceAddress deviceAddress{};
    vk::DeviceSize size{};
    Buffer buffer; // Underlying buffer
};

//--- Resource Allocator ------------------------------------------------------------------------------------------------------------
/*--
 * Vulkan Memory Allocator (VMA) is a library that helps to manage memory in Vulkan.
 * This should be used to manage the memory of the resources instead of using the Vulkan API directly.
-*/
class ResourceAllocator
{
public:
    ResourceAllocator() = default;
    ~ResourceAllocator() { assert(m_allocator == nullptr && "Missing deinit()"); }
    operator VmaAllocator() const { return m_allocator; }

    // Initialization of VMA allocator.
    void init(VmaAllocatorCreateInfo allocatorInfo)
    {
        // #TODO : VK_EXT_memory_priority ? VMA_ALLOCATOR_CREATE_EXT_MEMORY_PRIORITY_BIT

        allocatorInfo.flags |= VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT;
        // allow querying for the GPU address of a buffer
        allocatorInfo.flags |= VMA_ALLOCATOR_CREATE_KHR_MAINTENANCE4_BIT;
        allocatorInfo.flags |= VMA_ALLOCATOR_CREATE_KHR_MAINTENANCE5_BIT;
        // allow using VkBufferUsageFlags2CreateInfoKHR

        m_device = allocatorInfo.device;
        // Because we use VMA_DYNAMIC_VULKAN_FUNCTIONS
        const VmaVulkanFunctions functions = {
            .vkGetInstanceProcAddr = vkGetInstanceProcAddr,
            .vkGetDeviceProcAddr = vkGetDeviceProcAddr,
        };
        allocatorInfo.pVulkanFunctions = &functions;
        vmaCreateAllocator(&allocatorInfo, &m_allocator);
    }

    // De-initialization of VMA allocator.
    void deinit()
    {
        vmaDestroyAllocator(m_allocator);
        *this = {};
    }

    /*-- Create a buffer -*/
    /* 
     * UBO: VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT
     *        + VMA_MEMORY_USAGE_CPU_TO_GPU
     * SSBO: VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
     *        + VMA_MEMORY_USAGE_CPU_TO_GPU // Use this if the CPU will frequently update the buffer
     *        + VMA_MEMORY_USAGE_GPU_ONLY // Use this if the CPU will rarely update the buffer
     *        + VMA_MEMORY_USAGE_GPU_TO_CPU  // Use this when you need to read back data from the SSBO to the CPU
     *      ----
     *        + VMA_ALLOCATION_CREATE_MAPPED_BIT // Automatically maps the buffer upon creation
     *        + VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT // If the CPU will sequentially write to the buffer's memory,
     */

    Buffer createBuffer(vk::DeviceSize size,
                        vk::BufferUsageFlags2KHR usage,
                        VmaMemoryUsage memoryUsage = VMA_MEMORY_USAGE_AUTO,
                        VmaAllocationCreateFlags flags = {}) const
    {
        // This can be used only with maintenance5
        const vk::BufferUsageFlags2CreateInfoKHR bufferUsageFlags2CreateInfo{
            .usage = usage | vk::BufferUsageFlagBits2::eShaderDeviceAddress,
        };

        const vk::BufferCreateInfo bufferInfo{
            .pNext = &bufferUsageFlags2CreateInfo,
            .size = size,
            .usage = {},
            .sharingMode = vk::SharingMode::eExclusive, // Only one queue family will access i
        };

        VmaAllocationCreateInfo allocInfo = {.flags = flags, .usage = memoryUsage};
        const vk::DeviceSize dedicatedMemoryMinSize = 64ULL * 1024; // 64 KB
        if (size > dedicatedMemoryMinSize)
        {
            allocInfo.flags |= VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;
            // Use dedicated memory for large buffers
        }

        // Create the buffer
        Buffer resultBuffer;
        VmaAllocationInfo allocInfoOut{};
        VkBufferCreateInfo cbufferInfo = static_cast<VkBufferCreateInfo>(bufferInfo);
        VkBuffer cbuffer = static_cast<VkBuffer>(resultBuffer.buffer);
        VK_CHECK(
            vmaCreateBuffer(m_allocator, &cbufferInfo, &allocInfo, &cbuffer, &resultBuffer.allocation
                , &
                allocInfoOut));

        resultBuffer.buffer = cbuffer;
        
        // Get the GPU address of the buffer
        const VkBufferDeviceAddressInfo  info =
        {
            .sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO,
            .buffer = cbuffer
        };
        /*resultBuffer.address = m_device.getBufferAddress(&info);*/
        resultBuffer.address = vkGetBufferDeviceAddress(m_device, &info);
        {
            // Find leaks
            static uint32_t counter = 0U;
            if (m_leakID == counter)
            {
#if defined(_MSVC_LANG)
                __debugbreak();
#endif
            }
            std::string allocID = std::string("allocID: ") + std::to_string(counter++);
            vmaSetAllocationName(m_allocator, resultBuffer.allocation, allocID.c_str());
        }

        resultBuffer.size = size;
        
        return resultBuffer;
    }

    //*-- Destroy a buffer -*/
    void destroyBuffer(Buffer buffer) const { vmaDestroyBuffer(m_allocator, buffer.buffer, buffer.allocation); }

    void copyBuffer(std::unique_ptr<vk::raii::CommandBuffer>& commandBuffer, Buffer& srcBuffer, Buffer& dstBuffer, vk::DeviceSize size)
    {
        commandBuffer->copyBuffer(srcBuffer.buffer, dstBuffer.buffer, vk::BufferCopy{.size = size});
    }

    void copyBufferToImage(std::unique_ptr<vk::raii::CommandBuffer>& commandBuffer, const Buffer& buffer, vk::raii::Image& image, uint32_t width,
                                 uint32_t height,
                                 uint64_t offset = 0, uint32_t mipLevel = 0)
    {
        vk::BufferImageCopy region
        {
            .bufferOffset = offset, .bufferRowLength = 0, .bufferImageHeight = 0,
            .imageSubresource = {vk::ImageAspectFlagBits::eColor, mipLevel, 0, 1},
            .imageOffset = {0, 0, 0},
            .imageExtent = {width, height, 1}
        };
        commandBuffer->copyBufferToImage(buffer.buffer, image, vk::ImageLayout::eTransferDstOptimal, {region});
    }

    /*-- When leak are reported, set the ID of the leak here --*/
    void setLeakID(uint32_t id) { m_leakID = id; }

private:
    VmaAllocator m_allocator{};
    vk::Device m_device{};
    uint32_t m_leakID = ~0U;
};

/*--
 * Samplers are limited in Vulkan.
 * This class is used to create and store samplers, and to avoid creating the same sampler multiple times.
-*/
class SamplerPool
{
public:
    SamplerPool() = default;
    ~SamplerPool() = default;
    // Initialize the sampler pool with the device reference, then we can later acquire samplers
    void init(vk::raii::Device& device)
    {
        m_device = &device;
    }

    // Destroy internal resources and reset its initial state
    void deinit()
    {
        m_samplerMap.clear();
    }

    // Get or create VkSampler based on VkSamplerCreateInfo
    vk::raii::Sampler acquireSampler(const vk::SamplerCreateInfo& createInfo)
    {
        auto it = m_samplerMap.find(createInfo);
        if (it != m_samplerMap.end())
        {
            // If found, return existing sampler
            return std::move(it->second); // return existing
        }

        // Otherwise, create a new sampler
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

    // Stores unique samplers with their corresponding VkSamplerCreateInfo
    std::unordered_map<vk::SamplerCreateInfo, vk::raii::Sampler, SamplerCreateInfoHash, SamplerCreateInfoEqual>
    m_samplerMap;

    // Internal function to create a new VkSampler
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
    std::vector<Buffer> uniformBuffers;

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
    uint32_t getAPIVersion() const { return apiVersion; };
private:
    SDL_Window* window = nullptr;
    vk::raii::Context context;
    vk::raii::Instance instance = nullptr;
    uint32_t apiVersion = 0;
    vk::raii::DebugUtilsMessengerEXT debugMessenger = nullptr;
    vk::raii::SurfaceKHR surface = nullptr;
    vk::raii::PhysicalDevice physicalDevice = nullptr;
    vk::SampleCountFlagBits msaaSamples = vk::SampleCountFlagBits::e1;
    vk::raii::Device device = nullptr;
    uint32_t queueIndex = ~0;
    vk::raii::Queue queue = nullptr;
    ResourceAllocator allocator;
    vk::raii::SwapchainKHR swapChain = nullptr;
    std::vector<vk::Image> swapChainImages;
    vk::SurfaceFormatKHR swapChainSurfaceFormat;
    vk::Extent2D swapChainExtent;
    std::vector<vk::raii::ImageView> swapChainImageViews;

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
    Buffer vertexBuffer = {};
    Buffer indexBuffer = {};

    // Array of game objects to render
    std::array<GameObject, MAX_OBJECTS> gameObjects;

    vk::raii::DescriptorPool descriptorPool = nullptr;
    vk::raii::DescriptorPool uiDescriptorPool = nullptr; // imgui 
    std::vector<vk::raii::DescriptorSet> descriptorSets;
    std::vector<VkDescriptorSet> uiDescriptorSet{}; // imgui
    vk::raii::DescriptorSetLayout descriptorSetLayout = nullptr;
    vk::raii::DescriptorSetLayout commonDescriptorSetLayout = nullptr;

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
        vk::KHRCreateRenderpass2ExtensionName,
        vk::EXTDescriptorIndexingExtensionName
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

    /*void copyBufferToImage(const vk::raii::Buffer& buffer, vk::raii::Image& image, uint32_t width, uint32_t height,
                           uint64_t offset = 0, uint32_t mipLevel = 0);*/

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

    /*void createBuffer(vk::DeviceSize size, vk::BufferUsageFlags usage, vk::MemoryPropertyFlags properties,
                      vk::raii::Buffer& buffer, vk::raii::DeviceMemory& bufferMemory);*/

    std::unique_ptr<vk::raii::CommandBuffer> beginSingleTimeCommands();

    void endSingleTimeCommands(const vk::raii::CommandBuffer& commandBuffer) const;

    /*//change this to use the above functions check images chapter or 1 before idk
    void copyBuffer(vk::raii::Buffer& srcBuffer, vk::raii::Buffer& dstBuffer, vk::DeviceSize size);*/

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
