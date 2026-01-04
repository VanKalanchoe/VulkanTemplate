#pragma once

#include <chrono>
#include <unordered_map>

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

    inline std::unordered_map<std::string, float> g_CPUProfileResults;
    inline std::unordered_map<std::string, VanKTimestampPass> g_GPUProfileResults;
    class ScopeTimer
    {
    public:
        ScopeTimer(const std::string& name) : m_Name(name) { m_Timer.Reset(); }
        ~ScopeTimer() { g_CPUProfileResults[m_Name] = m_Timer.ElapsedMillis(); }

    private:
        std::string m_Name;
        Timer m_Timer;
    };
    
    class GPUScopeTimer
    {
    public:
        GPUScopeTimer(const std::string& name, VanKCommandBuffer cmd, VanKTimestampPass& pass)
            : m_Name(name), m_Cmd(cmd), m_Pass(pass)
        {
            // Just start the timestamp on the existing pass
            RenderCommand::StartTimeStamp(m_Cmd, m_Pass);
        }

        ~GPUScopeTimer()
        {
            RenderCommand::StopTimeStamp(m_Cmd, m_Pass);
            g_GPUProfileResults[m_Name] = m_Pass;
        }

    private:
        VanKCommandBuffer m_Cmd;
        std::string m_Name;
        VanKTimestampPass& m_Pass; // reference to an existing pass, no allocation
    };
}
