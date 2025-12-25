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

    inline std::unordered_map<std::string, float> g_ProfileResults;

    class ScopeTimer
    {
    public:
        ScopeTimer(const std::string& name) : m_Name(name) { m_Timer.Reset(); }
        ~ScopeTimer() { g_ProfileResults[m_Name] = m_Timer.ElapsedMillis(); }

    private:
        std::string m_Name;
        Timer m_Timer;
    };
}
