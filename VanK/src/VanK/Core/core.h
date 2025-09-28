#pragma once

#include <iostream>
#include <sstream>
#include <functional>
#include <map>
#include <vector>
#include <string>
#include <optional>
#include <memory>

//---------Event def------
#define BIT(x) (1 << (x))
#define VanK_BIND_EVENT_FN(fn) [this](auto&&... args) -> decltype(auto) { return this->fn(std::forward<decltype(args)>(args)...); }
//------------------------

#if defined(_WIN32)
        #define VK_DEBUGBREAK() __debugbreak()
    #elif defined(__linux__)
        #include <signal.h>
        #define VK_DEBUGBREAK() raise(SIGTRAP)
#else
    #error "Platform doesn't support debugbreak yet!"
#endif

#ifdef VK_DEBUG
    #define VK_ENABLE_ASSERTS
#endif

#ifndef VK_DIST
    #define VK_ENABLE_VERIFY
#endif

#define VK_EXPAND_MACRO(x) x
#define VK_STRINGIFY_MACRO(x) #x

//---------Assert def-----
#ifdef VK_ENABLE_ASSERTS

    // Alteratively we could use the same "default" message for both "WITH_MSG" and "NO_MSG" and
    // provide support for custom formatting by concatenating the formatting string instead of having the format inside the default message
    #define VK_INTERNAL_ASSERT_IMPL(type, check, msg, ...) { if(!(check)) { VK##type##ERROR(msg, __VA_ARGS__); VK_DEBUGBREAK(); } }
    #define VK_INTERNAL_ASSERT_WITH_MSG(type, check, ...) VK_INTERNAL_ASSERT_IMPL(type, check, "Assertion failed: {0}", __VA_ARGS__)
    #define VK_INTERNAL_ASSERT_NO_MSG(type, check) VK_INTERNAL_ASSERT_IMPL(type, check, "Assertion '{0}' failed at {1}:{2}", VK_STRINGIFY_MACRO(check), std::filesystem::path(__FILE__).filename().string(), __LINE__)

    #define VK_INTERNAL_ASSERT_GET_MACRO_NAME(arg1, arg2, macro, ...) macro
    #define VK_INTERNAL_ASSERT_GET_MACRO(...) VK_EXPAND_MACRO( VK_INTERNAL_ASSERT_GET_MACRO_NAME(__VA_ARGS__, VK_INTERNAL_ASSERT_WITH_MSG, VK_INTERNAL_ASSERT_NO_MSG) )

    // Currently accepts at least the condition and one additional parameter (the message) being optional
    #define VK_ASSERT(...) VK_EXPAND_MACRO( VK_INTERNAL_ASSERT_GET_MACRO(__VA_ARGS__)(_, __VA_ARGS__) )
    #define VK_CORE_ASSERT(...) VK_EXPAND_MACRO( VK_INTERNAL_ASSERT_GET_MACRO(__VA_ARGS__)(_CORE_, __VA_ARGS__) )
#else
    #define VK_ASSERT(...)
    #define VK_CORE_ASSERT(...)
#endif

//------------------------

//---------Verify def-----
#ifdef VK_ENABLE_VERIFY

    // Alteratively we could use the same "default" message for both "WITH_MSG" and "NO_MSG" and
    // provide support for custom formatting by concatenating the formatting string instead of having the format inside the default message
    #define VK_INTERNAL_VERIFY_IMPL(type, check, msg, ...) { if(!(check)) { VK##type##ERROR(msg, __VA_ARGS__); VK_DEBUGBREAK(); } }
    #define VK_INTERNAL_VERIFY_WITH_MSG(type, check, ...) VK_INTERNAL_VERIFY_IMPL(type, check, "Verification failed: {0}", __VA_ARGS__)
    #define VK_INTERNAL_VERIFY_NO_MSG(type, check) VK_INTERNAL_VERIFY_IMPL(type, check, "Verification '{0}' failed at {1}:{2}", VK_STRINGIFY_MACRO(check), std::filesystem::path(__FILE__).filename().string(), __LINE__)

    #define VK_INTERNAL_VERIFY_GET_MACRO_NAME(arg1, arg2, macro, ...) macro
    #define VK_INTERNAL_VERIFY_GET_MACRO(...) VK_EXPAND_MACRO( VK_INTERNAL_VERIFY_GET_MACRO_NAME(__VA_ARGS__, VK_INTERNAL_VERIFY_WITH_MSG, VK_INTERNAL_VERIFY_NO_MSG) )

    // Currently accepts at least the condition and one additional parameter (the message) being optional
    #define VK_VERIFY(...) VK_EXPAND_MACRO( VK_INTERNAL_VERIFY_GET_MACRO(__VA_ARGS__)(_, __VA_ARGS__) )
    #define VK_CORE_VERIFY(...) VK_EXPAND_MACRO( VK_INTERNAL_VERIFY_GET_MACRO(__VA_ARGS__)(_CORE_, __VA_ARGS__) )
#else
    #define VK_VERIFY(...)
    #define VK_CORE_VERIFY(...)
#endif

//------------------------

//---------Pointer--------
namespace VanK
{
    template<typename T>
    using Scope = std::unique_ptr<T>;
    template<typename T, typename ... Args>
    constexpr Scope<T> CreateScope(Args&& ... args)
    {
        return std::make_unique<T>(std::forward<Args>(args)...);
    }
    
    template<typename T>
    using Ref = std::shared_ptr<T>;
    template<typename T, typename ... Args>
    constexpr Ref<T> CreateRef(Args&& ... args)
    {
        return std::make_shared<T>(std::forward<Args>(args)...);
    }
}
//------------------------