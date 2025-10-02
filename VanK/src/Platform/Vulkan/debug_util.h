#pragma once
// This file defines the DebugUtil class, a singleton utility for managing Vulkan debug utilities.
// It provides functionality to set debug names for Vulkan objects and manage debug labels in command buffers.
//
// Usage:
// 1. Initialize the DebugUtil with a Vulkan device using debugUtilInitialize(VkDevice device).
// 2. Use DBG_VK_NAME(obj) macro to set debug names for Vulkan objects.
// 3. Use DBG_VK_SCOPE(cmdBuf) macro to create scoped debug labels in command buffers.
//
// Example:
// debugUtilInitialize(device);
// VkBuffer buffer = createBufer(...)
// DBG_VK_NAME(buffer);
//
// void someFunction(VkCommandBuffer cmdBuf)
// {
//   DBG_VK_SCOPE(cmdBuf);
//   // Command buffer operations
// }

#include <type_traits>  // std::is_same_v
#include <cstring>      // for strrchr
#include <string>

namespace utilsDebug
{
    class DebugUtil
    {
    public:
        static DebugUtil& getInstance()
        {
            static DebugUtil instance;
            return instance;
        }

        void init(vk::raii::Device& device) { m_device = &device; }

        bool isInitialized() const { return m_device != VK_NULL_HANDLE; }

        template <typename T>
        void setObjectName(T object, const std::string& name) const;

        class ScopedCmdLabel
        {
        public:
            ScopedCmdLabel(vk::raii::CommandBuffer& cmdBuf, const std::string& label)
                : m_cmdBuf(cmdBuf)
            {
                vk::DebugUtilsLabelEXT info
                {
                    vk::StructureType::eDebugUtilsLabelEXT,
                    nullptr,
                    label.c_str(),
                    vk::ArrayWrapper1D<float, 4>({1.0f, 1.0f, 1.0f, 1.0f})
                };

                /*m_cmdBuf.beginDebugUtilsLabelEXT(info);*/
            }

            ~ScopedCmdLabel()
            {
                /*m_cmdBuf.endDebugUtilsLabelEXT();*/
            }

        private:
            vk::raii::CommandBuffer& m_cmdBuf;
        };

    private:
        DebugUtil() = default;
        vk::raii::Device* m_device{nullptr};

        template <typename T>
        static constexpr vk::ObjectType getObjectType();
    };

    template <typename T>
    void DebugUtil::setObjectName(T object, const std::string& name) const
    {
        constexpr vk::ObjectType objectType = getObjectType<T>();

        if (objectType != vk::ObjectType::eUnknown && m_device != VK_NULL_HANDLE)
        {
            vk::DebugUtilsObjectNameInfoEXT info
            {
                vk::StructureType::eDebugUtilsObjectNameInfoEXT,
                nullptr,
                objectType,
                reinterpret_cast<uint64_t>(static_cast<typename T::CType>(object)),
                name.c_str()
            };
            /*m_device->setDebugUtilsObjectNameEXT(info);*/ // doesnt work right now 
        }
    }

    template <typename T>
    constexpr vk::ObjectType DebugUtil::getObjectType()
    {
        if constexpr (std::is_same_v<T, vk::Buffer>)
            return vk::ObjectType::eBuffer;
        else if constexpr (std::is_same_v<T, vk::BufferView>)
            return vk::ObjectType::eBufferView;
        else if constexpr (std::is_same_v<T, vk::CommandBuffer>)
            return vk::ObjectType::eCommandBuffer;
        else if constexpr (std::is_same_v<T, vk::CommandPool>)
            return vk::ObjectType::eCommandPool;
        else if constexpr (std::is_same_v<T, vk::DescriptorPool>)
            return vk::ObjectType::eDescriptorPool;
        else if constexpr (std::is_same_v<T, vk::DescriptorSet>)
            return vk::ObjectType::eDescriptorSet;
        else if constexpr (std::is_same_v<T, vk::DescriptorSetLayout>)
            return vk::ObjectType::eDescriptorSetLayout;
        else if constexpr (std::is_same_v<T, vk::Device>)
            return vk::ObjectType::eDevice;
        else if constexpr (std::is_same_v<T, vk::DeviceMemory>)
            return vk::ObjectType::eDeviceMemory;
        else if constexpr (std::is_same_v<T, vk::Fence>)
            return vk::ObjectType::eFence;
        else if constexpr (std::is_same_v<T, vk::Framebuffer>)
            return vk::ObjectType::eFramebuffer;
        else if constexpr (std::is_same_v<T, vk::Image>)
            return vk::ObjectType::eImage;
        else if constexpr (std::is_same_v<T, vk::ImageView>)
            return vk::ObjectType::eImageView;
        else if constexpr (std::is_same_v<T, vk::Instance>)
            return vk::ObjectType::eInstance;
        else if constexpr (std::is_same_v<T, vk::Pipeline>)
            return vk::ObjectType::ePipeline;
        else if constexpr (std::is_same_v<T, vk::PipelineCache>)
            return vk::ObjectType::ePipelineCache;
        else if constexpr (std::is_same_v<T, vk::PipelineLayout>)
            return vk::ObjectType::ePipelineLayout;
        else if constexpr (std::is_same_v<T, vk::QueryPool>)
            return vk::ObjectType::eQueryPool;
        else if constexpr (std::is_same_v<T, vk::RenderPass>)
            return vk::ObjectType::eRenderPass;
        else if constexpr (std::is_same_v<T, vk::Sampler>)
            return vk::ObjectType::eSampler;
        else if constexpr (std::is_same_v<T, vk::Semaphore>)
            return vk::ObjectType::eSemaphore;
        else if constexpr (std::is_same_v<T, vk::ShaderModule>)
            return vk::ObjectType::eShaderModule;
        else if constexpr (std::is_same_v<T, vk::SurfaceKHR>)
            return vk::ObjectType::eSurfaceKHR;
        else if constexpr (std::is_same_v<T, vk::SwapchainKHR>)
            return vk::ObjectType::eSwapchainKHR;
        else
            return vk::ObjectType::eUnknown;
    }
} // namespace utilsDebug

#define DBG_VK_SCOPE(_cmd) utilsDebug::DebugUtil::ScopedCmdLabel scopedCmdLabel(_cmd, __FUNCTION__)

#define DBG_VK_NAME(obj)                                                                                                       \
  if(utilsDebug::DebugUtil::getInstance().isInitialized())                                                                          \
  utilsDebug::DebugUtil::getInstance().setObjectName(                                                                               \
      obj, std::string(std::max(std::max(typeid(*this).name(), strrchr(typeid(*this).name(), ' ') + 1), typeid(*this).name())) \
               + "::" + #obj + " (" + std::string(" in ")                                                                      \
               + std::max({__FILE__, strrchr(__FILE__, '\\') ? strrchr(__FILE__, '\\') + 1 : __FILE__,                         \
                           strrchr(__FILE__, '/') ? strrchr(__FILE__, '/') + 1 : __FILE__})                                    \
               + ":" + std::to_string(__LINE__) + ")")


inline void debugUtilInitialize(vk::raii::Device& device)
{
    utilsDebug::DebugUtil::getInstance().init(device);
}
