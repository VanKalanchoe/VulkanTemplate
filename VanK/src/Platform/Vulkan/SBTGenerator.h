#pragma once
#include <unordered_map>

#if defined(__INTELLISENSE__) || !defined(USE_CPP20_MODULES)
#define VULKAN_HPP_DEFAULT_DISPATCH_LOADER_DYNAMIC_STORAGE
#include <vulkan/vulkan_raii.hpp>
#else
#define VULKAN_HPP_DEFAULT_DISPATCH_LOADER_DYNAMIC_STORAGE
import vulkan_hpp;
#endif

namespace VanK
{
    /*------------------------------------------------------------------------------------
    #class VanK::SBTGenerator
    
    VanK::SBTGenerator is a generic SBT builder from the ray tracing pipeline
    
    The builder will iterate through the pipeline create info `VkRayTracingPipelineCreateInfoKHR`
    to find the number of raygen, miss, hit and callable shader groups were created. 
    The handles for those group will be retrieved from the pipeline and written in the right order in
    separated buffer.
    
    Convenient functions exist to retrieve all information to be used in TraceRayKHR.
    
    ## Usage
    - Setup the builder (`init()`)
    - After the pipeline creation, call `calculateSBTBufferSize()` which returns the size of the buffer to create
    - Create the buffer then call `populateSBTBuffer()` to fill the buffer with the handles and data
    - Use `getSBTRegions()` to get all the vk::StridedDeviceAddressRegionKHR needed by TraceRayKHR()
    
    See under usage_SBTGenerator()
    ------------------------------------------------------------------------------------*/
    
    class SBTGenerator
    {
    public:
        enum GroupType
        {
            eRaygen,
            eMiss,
            eHit,
            eCallable
        };

        // Return address regions of all groups.
        struct Regions
        {
            vk::StridedDeviceAddressRegionKHR raygen{};
            vk::StridedDeviceAddressRegionKHR miss{};
            vk::StridedDeviceAddressRegionKHR hit{};
            vk::StridedDeviceAddressRegionKHR callable{};
        };

        SBTGenerator() = default;
        // MOVE CONSTRUCTOR: Transfer ownership and nullify the source
        SBTGenerator(SBTGenerator&& other) noexcept 
        {
            *this = std::move(other);
        }
        // MOVE ASSIGNMENT: Clean up current state and take from source
        SBTGenerator& operator=(SBTGenerator&& other) noexcept 
        {
            if (this != &other) 
            {
                // If this object already had a device, deinit it first
                if (m_device != VK_NULL_HANDLE) deinit();

                m_shaderGroupIndices = std::move(other.m_shaderGroupIndices);
                m_bufferAddresses    = std::move(other.m_bufferAddresses);
                m_stride             = std::move(other.m_stride);
                m_data               = std::move(other.m_data);
                m_handleSize         = other.m_handleSize;
                m_handleAlignment    = other.m_handleAlignment;
                m_shaderGroupBaseAlignment = other.m_shaderGroupBaseAlignment;
                m_totalGroupCount    = other.m_totalGroupCount;
                m_dataSize           = other.m_dataSize;
                m_pipeline           = other.m_pipeline;
                m_device             = other.m_device;

                // NEUTRALIZE the source object so its destructor doesn't assert
                other.m_device = nullptr; 
                other.m_pipeline = nullptr;
            }
            return *this;
        }
        // Explicitly delete copy operations to prevent accidental pointer duplication
        SBTGenerator(const SBTGenerator&) = delete;
        SBTGenerator& operator=(const SBTGenerator&) = delete;
        ~SBTGenerator() { assert(m_device == nullptr); } // To ensure deinit() was called

        void init(vk::raii::Device& device, const vk::PhysicalDeviceRayTracingPipelinePropertiesKHR& rayProperties);
        void deinit();

        // Analyzes the ray tracing pipeline to determine the required SBT buffer size
        // Returns the size needed for the SBT buffer
        size_t calculateSBTBufferSize(vk::raii::Pipeline& rayPipeline,
                                      vk::RayTracingPipelineCreateInfoKHR rayPipelineInfo = {},
                                      std::span<const vk::RayTracingPipelineCreateInfoKHR> librariesInfo = {});

        // Populates the SBT buffer with shader handles and data
        // The bufferAddress must be aligned to getBufferAlignment();
        // The buffer should be created with VK_BUFFER_USAGE_2_SHADER_BINDING_TABLE_BIT_KHR | VK_BUFFER_USAGE_2_SHADER_DEVICE_ADDRESS_BIT
        vk::Result populateSBTBuffer(vk::DeviceAddress bufferAddress, size_t bufferSize, void* bufferData);


        // After updateBuffer one can retrieve the regions
        // Return the address region of a group. indexOffset allow to offset the starting shader of the group.
        const vk::StridedDeviceAddressRegionKHR getSBTRegion(GroupType t, uint32_t indexOffset = 0) const;

        // Return the address regions of all groups. The offset allows to select which RayGen to use.
        const Regions getSBTRegions(uint32_t rayGenIndexOffset = 0) const;


        // Manually adds shader group indices from pipeline create info
        // Used when you want to specify indices directly instead of using calculateSBTBufferSize
        // The rayPipelineInfo parameter defines the pipeline, while librariesInfo describes potential input pipeline libraries
        void addIndices(vk::RayTracingPipelineCreateInfoKHR rayPipelineInfo,
                        const std::span<const vk::RayTracingPipelineCreateInfoKHR>& libraries = {});

        // Pushing back a GroupType and the handle pipeline index to use
        // i.e addIndex(eHit, 3) is pushing a Hit shader group using the 3rd entry in the pipeline
        void addIndex(GroupType t, uint32_t index) { m_shaderGroupIndices[t].push_back(index); }

        // Adding 'Shader Record' data to the group index.
        // i.e. addData(eHit, 0, myValue) is adding 'myValue' to the HIT group 0.
        template <typename T>
        void addData(GroupType t, uint32_t groupIndex, T& data)
        {
            addData(t, groupIndex, (uint8_t*)&data, sizeof(T));
        }

        void addData(GroupType t, uint32_t groupIndex, uint8_t* data, size_t dataSize)
        {
            m_data[t][groupIndex].assign(data, data + dataSize);
        }

        // Get buffer alignment
        uint32_t getBufferAlignment() const;

        // Resets internals, can start adding things freshly again
        void reset();

        // Reset state prior buffer updates only
        void resetBuffer();

    private:
        // Getters
        uint32_t getGroupIndexCount(GroupType t) const { return static_cast<uint32_t>(m_shaderGroupIndices[t].size()); }
        uint32_t getGroupStride(GroupType t) const { return m_stride[t]; }
        vk::DeviceAddress getGroupAddress(GroupType t) const;

        // returns the entire size of a group. Raygen Stride and Size must be equal, even if the buffer contains many of them.
        uint32_t getSize(GroupType t) const
        {
            return t == eRaygen ? getGroupStride(eRaygen) : getGroupStride(t) * getGroupIndexCount(t);
        }

        using shaderRecordMap = std::unordered_map<uint32_t, std::vector<uint8_t>>;

        std::array<std::vector<uint32_t>, 4> m_shaderGroupIndices; // For each group type, stores the pipeline indices of shader groups
        std::array<vk::DeviceAddress, 4> m_bufferAddresses{}; // The addresses of each group
        std::array<uint32_t, 4> m_stride{0, 0, 0, 0}; // Stride of each group
        std::array<shaderRecordMap, 4> m_data; // Local data to groups (Shader Record)

        uint32_t m_handleSize{0};
        uint32_t m_handleAlignment{0};
        uint32_t m_shaderGroupBaseAlignment{0};

        uint32_t m_totalGroupCount{0};
        size_t m_dataSize{0};
        vk::raii::Pipeline* m_pipeline{VK_NULL_HANDLE};

        vk::raii::Device* m_device{VK_NULL_HANDLE};
    };
} // namespace VanK
