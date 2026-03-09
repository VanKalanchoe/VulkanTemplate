// TaskSystem.h
#pragma once
#include <taskflow.hpp>
#include <algorithm/for_each.hpp>
#include <functional>

namespace VanK
{
    class TaskSystem
    {
    public:
        static TaskSystem& Get();

        void submit(std::function<void()> task);
        void wait();

        template<typename F>
        void parallelFor(int begin, int end, F&& func)
        {
            m_taskflow.for_each_index(begin, end, 1, func);
        }

    private:
        TaskSystem();
    private:
        tf::Executor m_executor;
        tf::Taskflow m_taskflow;
    };
}