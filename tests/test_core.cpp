/**
 * @file test_core.cpp
 * @brief Unit tests for Work Item 01: logging, preferences, math utilities, and memory pooling.
 */

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <future>
#include <memory>
#include <vector>

#include "voxels/core/job_system.hpp"
#include "voxels/core/logger.hpp"
#include "voxels/core/math.hpp"
#include "voxels/core/memory.hpp"
#include "voxels/core/preferences.hpp"
#include "voxels/platform/platform.hpp"

namespace {

/// In-memory sink used to assert exactly which log records were emitted by the logger.
class CapturingLogSink final : public voxels::ILogSink {
public:
    void Write(voxels::LogLevel level, std::string_view formattedMessage) override {
        records.emplace_back(level, std::string(formattedMessage));
    }

    std::vector<std::pair<voxels::LogLevel, std::string>> records;
};

} // namespace

TEST_CASE("JobSystem.HighPriorityJobsRunBeforeQueuedNormalJobs", "[core][jobs]") {
    voxels::JobSystem jobSystem(1);
    std::promise<void> workerStarted;
    std::promise<void> releaseWorker;
    std::future<void> releaseFuture = releaseWorker.get_future();
    std::vector<int> executionOrder;

    jobSystem.Enqueue([&] {
        executionOrder.push_back(1);
        workerStarted.set_value();
        releaseFuture.wait();
    });
    workerStarted.get_future().wait();

    jobSystem.Enqueue([&] { executionOrder.push_back(2); });
    jobSystem.Enqueue([&] { executionOrder.push_back(3); }, voxels::JobPriority::High);
    releaseWorker.set_value();
    jobSystem.Shutdown();

    REQUIRE(executionOrder == std::vector<int>{1, 3, 2});
}

TEST_CASE("Logger.LevelFiltering", "[core][logger]") {
    voxels::Logger logger(voxels::LogLevel::Warn);
    auto sink = std::make_shared<CapturingLogSink>();
    logger.AddSink(sink);

    logger.Trace("trace message");
    logger.Debug("debug message");
    logger.Info("info message");
    logger.Warn("warn message");
    logger.Error("error message");
    logger.Fatal("fatal message");

    REQUIRE(sink->records.size() == 3);
    REQUIRE(sink->records[0].first == voxels::LogLevel::Warn);
    REQUIRE(sink->records[1].first == voxels::LogLevel::Error);
    REQUIRE(sink->records[2].first == voxels::LogLevel::Fatal);

    logger.SetLevel(voxels::LogLevel::Trace);
    REQUIRE(logger.GetLevel() == voxels::LogLevel::Trace);
    logger.Trace("now visible");
    REQUIRE(sink->records.size() == 4);
}

TEST_CASE("Preferences.SaveAndLoad", "[core][preferences]") {
    const std::filesystem::path configPath =
        std::filesystem::temp_directory_path() / "voxels_test_config_save_load.json";
    std::filesystem::remove(configPath);

    voxels::PreferencesManager manager(configPath, voxels::PlatformType::Windows);

    voxels::GamePreferences preferences;
    preferences.windowMode = voxels::WindowMode::Borderless;
    preferences.resolution = voxels::Resolution{1920, 1080, 144};
    preferences.renderDistance = 12;
    preferences.simulationDistance = 6;
    preferences.fieldOfView = 100.5f;
    preferences.antiAliasingSamples = 8;
    preferences.shadowQuality = voxels::ShadowQuality::High;
    preferences.masterVolume = 0.9f;
    preferences.musicVolume = 0.5f;
    preferences.sfxVolume = 0.6f;
    preferences.keyBindings = {{"moveForward", "W"}, {"moveBack", "S"}};

    manager.Save(preferences);
    REQUIRE(std::filesystem::exists(configPath));

    const voxels::GamePreferences loaded = manager.Load();
    REQUIRE(loaded == preferences);

    std::filesystem::remove(configPath);
}

TEST_CASE("Preferences.PlatformConstraints", "[core][preferences]") {
    voxels::GamePreferences preferences;
    preferences.windowMode = voxels::WindowMode::Windowed;
    preferences.resolution = voxels::Resolution{1920, 1080, 60};

    const voxels::GamePreferences constrained =
        voxels::PreferencesManager::ApplyPlatformConstraints(preferences, voxels::PlatformType::Dreamcast);

    REQUIRE(constrained.windowMode == voxels::WindowMode::Fullscreen);
    REQUIRE(constrained.resolution.width == 640);
    REQUIRE(constrained.resolution.height == 480);

    const voxels::GamePreferences unconstrained =
        voxels::PreferencesManager::ApplyPlatformConstraints(preferences, voxels::PlatformType::Windows);
    REQUIRE(unconstrained.windowMode == voxels::WindowMode::Windowed);
    REQUIRE(unconstrained.resolution.width == 1920);

    const std::filesystem::path configPath =
        std::filesystem::temp_directory_path() / "voxels_test_config_dreamcast.json";
    std::filesystem::remove(configPath);

    voxels::PreferencesManager dreamcastManager(configPath, voxels::PlatformType::Dreamcast);
    dreamcastManager.Save(preferences);
    const voxels::GamePreferences loaded = dreamcastManager.Load();
    REQUIRE(loaded.windowMode == voxels::WindowMode::Fullscreen);
    REQUIRE(loaded.resolution.width == 640);
    REQUIRE(loaded.resolution.height == 480);

    std::filesystem::remove(configPath);
}

TEST_CASE("Math.ChunkCoordinateTransform", "[core][math]") {
    SECTION("Positive world position") {
        const voxels::Vec3 worldPos{20.0f, 5.0f, 33.0f};
        const voxels::ChunkCoordinate chunk = voxels::WorldPosToChunkPos(worldPos);
        const voxels::Vec3I local = voxels::WorldPosToLocalBlockPos(worldPos);

        REQUIRE(chunk.x == 1);
        REQUIRE(chunk.y == 0);
        REQUIRE(chunk.z == 2);
        REQUIRE(local.x == 4);
        REQUIRE(local.y == 5);
        REQUIRE(local.z == 1);
    }

    SECTION("Negative world position") {
        const voxels::Vec3 worldPos{-1.0f, -17.0f, -33.0f};
        const voxels::ChunkCoordinate chunk = voxels::WorldPosToChunkPos(worldPos);
        const voxels::Vec3I local = voxels::WorldPosToLocalBlockPos(worldPos);

        REQUIRE(chunk.x == -1);
        REQUIRE(chunk.y == -2);
        REQUIRE(chunk.z == -3);
        REQUIRE(local.x == 15);
        REQUIRE(local.y == 15);
        REQUIRE(local.z == 15);

        REQUIRE(local.x >= 0);
        REQUIRE(local.x < 16);
        REQUIRE(local.y >= 0);
        REQUIRE(local.y < 16);
        REQUIRE(local.z >= 0);
        REQUIRE(local.z < 16);
    }

    SECTION("AABB intersection") {
        const voxels::BoundingBox a{voxels::Vec3I{0, 0, 0}, voxels::Vec3I{16, 16, 16}};
        const voxels::BoundingBox overlapping{voxels::Vec3I{8, 8, 8}, voxels::Vec3I{24, 24, 24}};
        const voxels::BoundingBox disjoint{voxels::Vec3I{32, 32, 32}, voxels::Vec3I{48, 48, 48}};

        REQUIRE(voxels::IntersectsAABB(a, overlapping));
        REQUIRE_FALSE(voxels::IntersectsAABB(a, disjoint));
    }
}

TEST_CASE("Memory.PoolAllocator", "[core][memory]") {
    voxels::PoolAllocator<int> pool(4);
    REQUIRE(pool.Capacity() == 4);
    REQUIRE(pool.FreeCount() == 4);
    REQUIRE(pool.UsedCount() == 0);

    int* a = pool.Allocate(1);
    int* b = pool.Allocate(2);
    int* c = pool.Allocate(3);
    int* d = pool.Allocate(4);

    REQUIRE(a != nullptr);
    REQUIRE(b != nullptr);
    REQUIRE(c != nullptr);
    REQUIRE(d != nullptr);
    REQUIRE(*a == 1);
    REQUIRE(*b == 2);
    REQUIRE(*c == 3);
    REQUIRE(*d == 4);
    REQUIRE(pool.UsedCount() == 4);
    REQUIRE(pool.FreeCount() == 0);

    // Pool is exhausted; allocation must fail gracefully rather than overrun storage.
    REQUIRE(pool.Allocate(5) == nullptr);

    pool.Deallocate(b);
    REQUIRE(pool.UsedCount() == 3);
    REQUIRE(pool.FreeCount() == 1);

    int* e = pool.Allocate(99);
    REQUIRE(e != nullptr);
    REQUIRE(*e == 99);
    REQUIRE(pool.UsedCount() == 4);

    pool.Deallocate(a);
    pool.Deallocate(c);
    pool.Deallocate(d);
    pool.Deallocate(e);
    REQUIRE(pool.UsedCount() == 0);
    REQUIRE(pool.FreeCount() == 4);
}
