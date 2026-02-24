#pragma once
#include "Platform/Vulkan/VulkanProfilerAPI.h"

#define VANKCPU_PROFILER_ZONE(name) \
ZoneTransientN(___tracy_cpu_zone, name, true)

#define VANK_APP_INFO(name) \
do { TracyAppInfo(name, strlen(name)); } while (0)