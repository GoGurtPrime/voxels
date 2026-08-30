#pragma once

/**
 * @file job_system.hpp
 * @brief Worker pool used for async generation, meshing, and I/O work.
 *
 * @details The runtime requires a fixed-size worker pool for chunk generation and asset work
 *          without blocking the main render loop. Jobs are queued from any thread and drained
 *          back on the main thread through `DrainCompleted` once their results are ready.
 */

#include <atomic>
#include <condition_variable>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>
#include <utility>
#include <vector>

namespace voxels {

class JobSystem {
public:
    explicit JobSystem(std::size_t workerCount = 0);
    ~JobSystem();

    JobSystem(const JobSystem&) = delete;
    JobSystem& operator=(const JobSystem&) = delete;
    JobSystem(JobSystem&&) = delete;
    JobSystem& operator=(JobSystem&&) = delete;

    void Enqueue(std::function<void()> job);
    template <typename F>
    auto EnqueueWithResult(F&& function) -> std::future<decltype(function())> {
        using ResultType = decltype(function());
        auto task = std::make_shared<std::packaged_task<ResultType()>>(std::forward<F>(function));
        std::future<ResultType> future = task->get_future();
        Enqueue([task]() { (*task)(); });
        return future;
    }
    void DrainCompleted();
    void Shutdown();

    [[nodiscard]] std::size_t WorkerCount() const noexcept { return m_workers.size(); }

private:
    void WorkerLoop();

    std::mutex m_mutex;
    std::condition_variable m_cv;
    std::queue<std::function<void()>> m_jobs;
    std::vector<std::thread> m_workers;
    std::atomic<bool> m_running{true};
};

class FrameAccumulator {
public:
    void Accumulate(double deltaSeconds) noexcept;
    int Resolve(double fixedStepSeconds) noexcept;
    [[nodiscard]] double GetRemainder() const noexcept { return m_accumulator; }

private:
    static constexpr double kMaxFrameDelta = 0.25;
    double m_accumulator = 0.0;
};

} // namespace voxels
