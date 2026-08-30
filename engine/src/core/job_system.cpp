/**
 * @file job_system.cpp
 * @brief Implementation of the fixed worker pool and frame accumulator.
 */

#include "voxels/core/job_system.hpp"

#include <algorithm>
#include <cmath>

namespace voxels {

JobSystem::JobSystem(std::size_t workerCount) {
    const std::size_t count = workerCount == 0 ? std::max(1u, std::thread::hardware_concurrency() - 1u) : workerCount;
    m_workers.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        m_workers.emplace_back(&JobSystem::WorkerLoop, this);
    }
}

JobSystem::~JobSystem() {
    Shutdown();
}

void JobSystem::Enqueue(std::function<void()> job, JobPriority priority) {
    if (!m_running.load()) {
        return;
    }
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (priority == JobPriority::High) {
            m_highPriorityJobs.push(std::move(job));
        } else {
            m_normalJobs.push(std::move(job));
        }
    }
    m_cv.notify_one();
}

void JobSystem::DrainCompleted() {
    // Completed tasks are executed inline as soon as the queue is drained by the caller.
}

void JobSystem::Shutdown() {
    m_running.store(false);
    m_cv.notify_all();
    for (auto& worker : m_workers) {
        if (worker.joinable()) {
            worker.join();
        }
    }
    m_workers.clear();
}

void JobSystem::WorkerLoop() {
    // Loop on both queues being empty AND not-running so Shutdown() drains every queued job.
    while (true) {
        std::function<void()> job;
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_cv.wait(lock, [this] {
                return !m_highPriorityJobs.empty() || !m_normalJobs.empty() || !m_running.load();
            });
            if (m_highPriorityJobs.empty() && m_normalJobs.empty()) {
                if (!m_running.load()) {
                    return;
                }
                continue;
            }
            std::queue<std::function<void()>>& queue =
                m_highPriorityJobs.empty() ? m_normalJobs : m_highPriorityJobs;
            job = std::move(queue.front());
            queue.pop();
        }
        if (job) {
            job();
        }
    }
}

void FrameAccumulator::Accumulate(double deltaSeconds) noexcept {
    const double clamped = std::min(deltaSeconds, kMaxFrameDelta);
    m_accumulator += clamped;
}

int FrameAccumulator::Resolve(double fixedStepSeconds, int maxSteps) noexcept {
    if (fixedStepSeconds <= 0.0 || maxSteps <= 0) {
        return 0;
    }
    const int steps = std::min(static_cast<int>(m_accumulator / fixedStepSeconds), maxSteps);
    m_accumulator -= static_cast<double>(steps) * fixedStepSeconds;
    if (steps == maxSteps && m_accumulator >= fixedStepSeconds) {
        m_accumulator = std::fmod(m_accumulator, fixedStepSeconds);
    }
    return steps;
}

} // namespace voxels
