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
        void setObjectName(T& object, const std::string& name);
        
        void beginCmdLabel
        (
            vk::raii::CommandBuffer& cmdBuf,
            const std::string& label,
            std::array<float, 4> color = {1.f, 1.f, 1.f, 1.f}
        )
        {
            if (!isInitialized())
                return;

            vk::DebugUtilsLabelEXT info
            {
                vk::StructureType::eDebugUtilsLabelEXT,
                nullptr,
                label.c_str(),
                colorFromLabel(label)
            };

            cmdBuf.beginDebugUtilsLabelEXT(info);
        }
        
        void insertCmdLabel
        (
            vk::raii::CommandBuffer& cmdBuf,
            const std::string& label,
            const std::array<float,4>& color = {1.f,1.f,1.f,1.f}
        )
        {
            if (!isInitialized()) return;

            vk::DebugUtilsLabelEXT info
            {
                vk::StructureType::eDebugUtilsLabelEXT,
                nullptr,
                label.c_str(),
                colorFromLabel(label)
            };
            cmdBuf.insertDebugUtilsLabelEXT(info);
        }

        void endCmdLabel(vk::raii::CommandBuffer& cmdBuf)
        {
            if (!isInitialized())
                return;

            cmdBuf.endDebugUtilsLabelEXT();
        }
        
        void beginQueueLabel
        (
            vk::raii::Queue queue,
            const std::string& label,
            const std::array<float,4>& color = {1.f,1.f,1.f,1.f}
        )
        {
            if (!isInitialized()) 
                return;

            vk::DebugUtilsLabelEXT info
            {
                vk::StructureType::eDebugUtilsLabelEXT,
                nullptr,
                label.c_str(),
                colorFromLabel(label)
            };
            queue.beginDebugUtilsLabelEXT(info);
        }

        void insertQueueLabel
        (
            vk::Queue queue,
            const std::string& label,
            const std::array<float,4>& color = {1.f,1.f,1.f,1.f}
        )
        {
            if (!isInitialized()) return;

            vk::DebugUtilsLabelEXT info
            {
                vk::StructureType::eDebugUtilsLabelEXT,
                nullptr,
                label.c_str(),
                colorFromLabel(label)
            };
            queue.insertDebugUtilsLabelEXT(info);
        }

        void endQueueLabel(vk::raii::Queue queue)
        {
            if (!isInitialized()) 
                return;
            
            queue.endDebugUtilsLabelEXT();
        }
    
    private:
        std::array<float,4> colorFromLabel(const std::string& label)
        {
            std::hash<std::string> hasher;
            size_t hash = hasher(label);

            // map hash to RGB [0.2,0.9] for visibility
            auto f = [&](size_t shift){ return 0.2f + 0.7f * ((hash >> shift) & 0xFF) / 255.0f; };
            return { f(0), f(8), f(16), 1.0f };
        }

    private:
        DebugUtil() = default;
        vk::raii::Device* m_device{nullptr};
    };

    template <typename T>
    void DebugUtil::setObjectName(T& object, const std::string& name)
    {
        if (!m_device) return;

        vk::DebugUtilsObjectNameInfoEXT info{};
        info.objectType   = T::objectType;
        info.objectHandle = reinterpret_cast<uint64_t>(static_cast<typename T::CType>(*object));
        info.pObjectName  = name.c_str();

        m_device->setDebugUtilsObjectNameEXT(info);
    }

    template <typename Caller>
    static std::string debugNameFallback(const char* varName, const std::string& customName = "")
    {
        if (!customName.empty())
            return customName;

        return std::string(typeid(Caller).name()) + "::" + varName;
    }
} // namespace utilsDebug

#define DBG_CMD_BEGIN(cmd, name) utilsDebug::DebugUtil::getInstance().beginCmdLabel(cmd, name)
#define DBG_CMD_INSERT(cmd, name) utilsDebug::DebugUtil::getInstance().insertCmdLabel(cmd, name)
#define DBG_CMD_END(cmd) utilsDebug::DebugUtil::getInstance().endCmdLabel(cmd)

#define DBG_QUEUE_BEGIN(queue, name) utilsDebug::DebugUtil::getInstance().beginQueueLabel(queue, name)
#define DBG_QUEUE_INSERT(queue, name) utilsDebug::DebugUtil::getInstance().insertQueueLabel(queue, name)
#define DBG_QUEUE_END(queue) utilsDebug::DebugUtil::getInstance().endQueueLabel(queue)

#define DBG_VK_NAME(obj, ...) \
    if (utilsDebug::DebugUtil::getInstance().isInitialized()) { \
        using CallerClass = std::remove_reference_t<decltype(*this)>; \
        std::string name = utilsDebug::debugNameFallback<CallerClass>(#obj, ##__VA_ARGS__); \
        utilsDebug::DebugUtil::getInstance().setObjectName(obj, name); \
    }

inline void debugUtilInitialize(vk::raii::Device& device)
{
    utilsDebug::DebugUtil::getInstance().init(device);
}
