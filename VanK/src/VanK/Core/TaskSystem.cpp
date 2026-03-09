#include "TaskSystem.h"

namespace VanK {

TaskSystem& TaskSystem::Get() {
    static TaskSystem instance;
    return instance;
}

TaskSystem::TaskSystem() {
    // Constructor (optional initialization)
}

void TaskSystem::submit(std::function<void()> task) {
    m_taskflow.emplace(task);
}

void TaskSystem::wait() {
    m_executor.run(m_taskflow).wait();
    m_taskflow.clear();
}

} // namespace VanK