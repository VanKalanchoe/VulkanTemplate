#pragma once

#include <chrono>
#include <unordered_map>
#include "VanK/Renderer/RenderCommand.h"

namespace VanK
{
    class Timer
    {
    public:
        Timer()
        {
            Reset();
        }

        void Reset()
        {
            m_Start = std::chrono::high_resolution_clock::now();
        }

        float Elapsed()
        {
            return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::high_resolution_clock::now() - m_Start).count() * 0.001f * 0.001f * 0.001f;
        }

        float ElapsedMillis()
        {
            return Elapsed() * 1000.0f;
        }

    private:
        std::chrono::time_point<std::chrono::high_resolution_clock> m_Start;
    };
    
    struct ProfileResult
    {
        float cpuMs = 0.0f;
        float gpuMs = 0.0f;
    };

    inline std::unordered_map<std::string, ProfileResult> g_ProfileResults;

    class ScopeTimer
    {
    public:
        ScopeTimer(const std::string& name, bool enableGpuTimeStamp = false) : m_Name(name), m_EnableGpu(enableGpuTimeStamp)
        {
            m_Timer.Reset();
            if (m_EnableGpu)
            {
               RenderCommand::setEnableTimeStamp(true);
            }
        }
        
        ~ScopeTimer()
        {
            float cpuTimeMs = m_Timer.ElapsedMillis();
            g_ProfileResults[m_Name].cpuMs = cpuTimeMs;
            
            if (m_EnableGpu)
            {
                RenderCommand::setEnableTimeStamp(false);
                auto gpuTimes = RenderCommand::getTimeStampPass();
                // gpuTimes could be a struct { begin, end } in nanoseconds
                g_ProfileResults[m_Name].gpuMs = (gpuTimes.end - gpuTimes.begin) * RenderCommand::getTimeStampPeriod() * 1e-6f; // ms
            }
        }

    private:
        std::string m_Name;
        Timer m_Timer;
        bool m_EnableGpu = false;
    };
}
