/**
 * @file engine.hpp
 * @brief Primary engine orchestrator and runtime lifecycle manager.
 * 
 * @details Declares the core Engine class responsible for bootstrapping,
 *          subsystem coordination (platform, rendering, input, world simulation, networking),
 *          and lifecycle management across all supported platforms.
 */

#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

namespace voxels {

class IPlatform;

enum class EngineStatus {
    Uninitialized,
    Initialized,
    Running,
    Stopped,
    Error
};

class Engine {
public:
    Engine();
    ~Engine();

    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;
    Engine(Engine&&) noexcept;
    Engine& operator=(Engine&&) noexcept;

    bool initialize();
    void shutdown();
    [[nodiscard]] bool isInitialized() const noexcept;
    [[nodiscard]] EngineStatus getStatus() const noexcept;
    [[nodiscard]] std::string getVersion() const;
    [[nodiscard]] IPlatform* getPlatform() const noexcept;

    static constexpr std::string_view kVersionString = "0.1.0";
    static constexpr std::uint32_t kVersionMajor = 0;
    static constexpr std::uint32_t kVersionMinor = 1;
    static constexpr std::uint32_t kVersionPatch = 0;

private:
    EngineStatus m_status{EngineStatus::Uninitialized};
    std::unique_ptr<IPlatform> m_platform;
};

int EngineMain();

} // namespace voxels
