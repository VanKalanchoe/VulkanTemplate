#pragma once

#include "spdlog/spdlog.h"

#define GLM_ENABLE_EXPERIMENTAL
#include "glm/gtx/string_cast.hpp"

// This ignores all warnings raised inside External headers
#pragma warning(push, 0)
#include <spdlog/spdlog.h>
#include <spdlog/fmt/ostr.h>
#pragma warning(pop)

namespace VanK
{
    class Log
    {
    public:
        static void Init();

        inline static std::shared_ptr<spdlog::logger>& GetCoreLogger() { return s_CoreLogger; }
        inline static std::shared_ptr<spdlog::logger>& GetClientLogger() { return s_ClientLogger; }
    private:
        static std::shared_ptr<spdlog::logger> s_CoreLogger;
        static std::shared_ptr<spdlog::logger> s_ClientLogger;
    };
}

template<typename OStream, glm::length_t L, typename T, glm::qualifier Q>
inline OStream& operator<<(OStream& os, const glm::vec<L, T, Q>& vector)
{
    return os << glm::to_string(vector);
}

template<typename OStream, glm::length_t C, glm::length_t R, typename T, glm::qualifier Q>
inline OStream& operator<<(OStream& os, const glm::mat<C, R, T, Q>& matrix)
{
    return os << glm::to_string(matrix);
}

template<typename OStream, typename T, glm::qualifier Q>
inline OStream& operator<<(OStream& os, glm::qua<T, Q> quaternion)
{
    return os << glm::to_string(quaternion);
}


// Core log macros
#define VK_CORE_TRACE(...) ::VanK::Log::GetCoreLogger()->trace(__VA_ARGS__)
#define VK_CORE_INFO(...)  ::VanK::Log::GetCoreLogger()->info(__VA_ARGS__)
#define VK_CORE_WARN(...)  ::VanK::Log::GetCoreLogger()->warn(__VA_ARGS__)
#define VK_CORE_ERROR(...) ::VanK::Log::GetCoreLogger()->error(__VA_ARGS__)
#define VK_CORE_FATAL(...) ::VanK::Log::GetCoreLogger()->fatal(__VA_ARGS__)

// Client log macros
#define VK_TRACE(...)      ::VanK::Log::GetClientLogger()->trace(__VA_ARGS__)
#define VK_INFO(...)       ::VanK::Log::GetClientLogger()->info(__VA_ARGS__)
#define VK_WARN(...)       ::VanK::Log::GetClientLogger()->warn(__VA_ARGS__)
#define VK_ERROR(...)      ::VanK::Log::GetClientLogger()->error(__VA_ARGS__)
#define VK_FATAL(...)      ::VanK::Log::GetClientLogger()->fatal(__VA_ARGS__)