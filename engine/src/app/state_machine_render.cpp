/**
 * @file state_machine_render.cpp
 * @brief Implements the real desktop states that render a visible scene in the shipping path.
 */

#include "voxels/app/state_machine.hpp"

#include <algorithm>
#include <cmath>
#include <chrono>
#include <future>
#include <random>
#include <utility>

#include <glad/glad.h>
#include <glm/gtc/matrix_transform.hpp>
#include <nlohmann/json.hpp>

#include "voxels/core/logger.hpp"
#include "voxels/core/paths.hpp"
#include "voxels/graphics/renderer.hpp"
#include "voxels/networking/client.hpp"
#include "voxels/networking/server.hpp"
#include "voxels/platform/platform.hpp"
#include "voxels/render/chunk_renderer.hpp"
#include "voxels/ui/imgui_ui_manager.hpp"
#include "voxels/world/spawn_calculator.hpp"

namespace voxels {

namespace {
voxels::graphics::IGraphicsRenderer* g_renderer = nullptr;
bool g_startupSplashShown = false;

constexpr float kPreviewRevealSeconds = 2.2f;
constexpr float kLogoFadeInSeconds = 0.8f;
constexpr float kLogoHoldSeconds = 10.0f;
constexpr float kLogoFadeOutSeconds = 0.9f;
constexpr float kPostLogoHoldSeconds = 1.0f;
constexpr float kMenuTitleFadeInSeconds = 2.0f;
constexpr int kMenuPreviewTerrainSections = 8;

struct CameraShot {
    enum class Style : std::uint8_t { Curve, PanX, PanZ };
    Style style = Style::Curve;
    glm::vec3 p0{};
    glm::vec3 p1{};
    glm::vec3 p2{};
    glm::vec3 p3{};
    float durationSeconds = 6.0f;
};

class MainMenuBackdropRuntime {
public:
    void Start(BlockRegistry* registry, TextureAtlas* atlas, graphics::IGraphicsRenderer* renderer,
               std::uint64_t seed, int renderDistanceChunks) {
        Stop();
        if (registry == nullptr || atlas == nullptr || renderer == nullptr) return;

        m_registry = registry;
        m_atlas = atlas;
        m_renderer = renderer;
        m_options = WorldOptions{};
        m_options.seed = static_cast<WorldSeed>(seed);
        m_options.renderDistanceChunks = std::clamp(renderDistanceChunks, 4, 8);
        m_options.simulationDistanceChunks = std::min(m_options.renderDistanceChunks, 6);
        m_options.alwaysSunny = true;

        m_sampler = WorldGenerator(m_options);
        m_world = std::make_unique<World>();
        if (!m_world->Initialize(m_options)) {
            Stop();
            return;
        }

        m_jobs = std::make_unique<JobSystem>();
        m_chunkRenderer = std::make_unique<graphics::ChunkRenderer>(*m_registry, *m_atlas, *m_jobs, m_renderer);
        m_skyRenderer = std::make_unique<graphics::SkyRenderer>();
        m_chunkRenderer->SetUploadBudget(3, 1.4);
        m_chunkRenderer->SetBackgroundMeshQueueLimit(2);

        const int radius = std::clamp(m_options.renderDistanceChunks, 3, 8);
        for (int ring = 0; ring <= radius; ++ring) {
            for (int z = -ring; z <= ring; ++z) {
                for (int x = -ring; x <= ring; ++x) {
                    if (std::max(std::abs(x), std::abs(z)) != ring) continue;
                    for (int y = 0; y < kMenuPreviewTerrainSections; ++y) {
                        const ChunkCoordinate coord{x, y, z};
                        m_pending.push_back({coord, m_jobs->EnqueueWithResult([options = m_options, coord] {
                                                return WorldGenerator(options).GenerateChunk(coord);
                                            })});
                    }
                }
            }
        }

        m_totalChunkJobs = m_pending.size();
        m_completedChunkJobs = 0;
        m_ready = false;
        m_shotElapsedSeconds = 0.0f;
        m_rng.seed(seed ^ 0x9e3779b97f4a7c15ULL);
        m_camera = Camera{};
        m_camera.position = glm::vec3{0.0f, 34.0f, 0.0f};
        m_camera.fovY = glm::radians(62.0f);
        m_camera.aspect = 16.0f / 9.0f;
        m_camera.nearPlane = 0.1f;
        m_camera.farPlane = 170.0f;
    }

    void Stop() noexcept {
        if (m_chunkRenderer != nullptr) m_chunkRenderer->Shutdown();
        if (m_skyRenderer != nullptr) m_skyRenderer->Shutdown();
        if (m_jobs != nullptr) m_jobs->Shutdown();
        m_chunkRenderer.reset();
        m_skyRenderer.reset();
        m_jobs.reset();
        m_world.reset();
        m_pending.clear();
        m_ready = false;
        m_totalChunkJobs = 0;
        m_completedChunkJobs = 0;
        m_registry = nullptr;
        m_atlas = nullptr;
        m_renderer = nullptr;
    }

    void Update(double deltaSeconds) {
        if (m_world == nullptr || m_chunkRenderer == nullptr) return;

        constexpr std::size_t kMaxIntegrationPerTick = 3;
        std::size_t integrated = 0;
        auto pending = m_pending.begin();
        while (pending != m_pending.end() && integrated < kMaxIntegrationPerTick) {
            if (pending->result.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
                ++pending;
                continue;
            }
            Chunk chunk = pending->result.get();
            chunk.ClearDirty();
            m_world->GetOrCreateChunk(pending->coordinate) = std::move(chunk);
            m_chunkRenderer->OnChunkArrived(pending->coordinate, *m_world);
            if (pending->coordinate.y == kMenuPreviewTerrainSections - 1) {
                const ChunkCoordinate cap{pending->coordinate.x, kMenuPreviewTerrainSections, pending->coordinate.z};
                m_world->GetOrCreateChunk(cap);
                m_chunkRenderer->OnChunkArrived(cap, *m_world);
            }
            ++m_completedChunkJobs;
            ++integrated;
            pending = m_pending.erase(pending);
        }

        if (!m_ready && m_pending.empty()) {
            m_ready = true;
            SelectNextShot();
        }

        UpdateCamera(static_cast<float>(std::max(0.0, deltaSeconds)));
        m_chunkRenderer->EnqueueDirtyMeshJobs(*m_world, m_camera.position);
        m_chunkRenderer->UploadCompletedMeshes();
    }

    void Render() {
        if (m_renderer == nullptr || m_chunkRenderer == nullptr || m_world == nullptr) return;
        m_renderer->SetCamera(m_camera);
        const graphics::CelestialLighting lighting =
            graphics::EvaluateCelestialLighting(graphics::kDayDurationSeconds * 0.30f, true);
        m_chunkRenderer->SetCelestialLighting(lighting);
        m_chunkRenderer->SetFogRange(graphics::FogRangeForRenderDistance(
            std::max(m_options.renderDistanceChunks, 8)));
        m_skyRenderer->Render(m_camera, lighting);
        m_chunkRenderer->Render(m_camera);
    }

    [[nodiscard]] bool IsReady() const noexcept { return m_ready; }
    [[nodiscard]] bool IsActive() const noexcept { return m_world != nullptr; }
    [[nodiscard]] float Progress() const noexcept {
        if (m_totalChunkJobs == 0) return 0.0f;
        return std::clamp(static_cast<float>(m_completedChunkJobs) / static_cast<float>(m_totalChunkJobs), 0.0f, 1.0f);
    }

private:
    struct PendingChunk {
        ChunkCoordinate coordinate{};
        std::future<Chunk> result;
    };

    [[nodiscard]] glm::vec3 BezierPoint(float t) const {
        const float u = 1.0f - t;
        return (u * u * u) * m_activeShot.p0 + (3.0f * u * u * t) * m_activeShot.p1 +
               (3.0f * u * t * t) * m_activeShot.p2 + (t * t * t) * m_activeShot.p3;
    }

    [[nodiscard]] glm::vec3 BezierTangent(float t) const {
        const float u = 1.0f - t;
        return 3.0f * u * u * (m_activeShot.p1 - m_activeShot.p0) +
               6.0f * u * t * (m_activeShot.p2 - m_activeShot.p1) +
               3.0f * t * t * (m_activeShot.p3 - m_activeShot.p2);
    }

    [[nodiscard]] int SurfaceYAt(float worldX, float worldZ) const {
        return m_sampler.SampleColumn(static_cast<int>(std::lround(worldX)), static_cast<int>(std::lround(worldZ))).surfaceY;
    }

    [[nodiscard]] float SurfaceYSmoothAt(float worldX, float worldZ) const {
        const int x0 = static_cast<int>(std::floor(worldX));
        const int z0 = static_cast<int>(std::floor(worldZ));
        const int x1 = x0 + 1;
        const int z1 = z0 + 1;
        const float tx = worldX - static_cast<float>(x0);
        const float tz = worldZ - static_cast<float>(z0);

        const float h00 = static_cast<float>(m_sampler.SampleColumn(x0, z0).surfaceY);
        const float h10 = static_cast<float>(m_sampler.SampleColumn(x1, z0).surfaceY);
        const float h01 = static_cast<float>(m_sampler.SampleColumn(x0, z1).surfaceY);
        const float h11 = static_cast<float>(m_sampler.SampleColumn(x1, z1).surfaceY);

        const float hx0 = h00 + (h10 - h00) * std::clamp(tx, 0.0f, 1.0f);
        const float hx1 = h01 + (h11 - h01) * std::clamp(tx, 0.0f, 1.0f);
        return hx0 + (hx1 - hx0) * std::clamp(tz, 0.0f, 1.0f);
    }

    [[nodiscard]] float RandomRange(float min, float max) {
        std::uniform_real_distribution<float> distribution(min, max);
        return distribution(m_rng);
    }

    [[nodiscard]] glm::vec2 RandomPointAround(float radiusMin, float radiusMax) {
        constexpr float kTwoPi = 6.28318530718f;
        const float theta = RandomRange(0.0f, kTwoPi);
        const float radius = RandomRange(radiusMin, radiusMax);
        return {std::cos(theta) * radius, std::sin(theta) * radius};
    }

    void SelectNextShot() {
        const float roll = RandomRange(0.0f, 1.0f);
        m_activeShot.style = roll < 0.5f ? CameraShot::Style::Curve : (roll < 0.75f ? CameraShot::Style::PanX : CameraShot::Style::PanZ);

        if (m_activeShot.style == CameraShot::Style::Curve) {
            const glm::vec2 start = RandomPointAround(30.0f, 58.0f);
            const glm::vec2 end = RandomPointAround(14.0f, 46.0f);
            const float startY = SurfaceYSmoothAt(start.x, start.y) + RandomRange(10.0f, 13.0f);
            const float endY = SurfaceYSmoothAt(end.x, end.y) + RandomRange(8.0f, 11.0f);

            m_activeShot.p0 = {start.x, startY, start.y};
            m_activeShot.p3 = {end.x, endY, end.y};

            const glm::vec3 span = m_activeShot.p3 - m_activeShot.p0;
            glm::vec3 side = glm::vec3{-span.z, 0.0f, span.x};
            if (glm::dot(side, side) < 0.0001f) side = glm::vec3{1.0f, 0.0f, 0.0f};
            side = glm::normalize(side);
            m_activeShot.p1 = m_activeShot.p0 + span * 0.33f + side * RandomRange(-14.0f, 14.0f);
            m_activeShot.p2 = m_activeShot.p0 + span * 0.66f - side * RandomRange(-10.0f, 10.0f);
            m_activeShot.p1.y = std::max(m_activeShot.p1.y, SurfaceYSmoothAt(m_activeShot.p1.x, m_activeShot.p1.z) + 9.0f);
            m_activeShot.p2.y = std::max(m_activeShot.p2.y, SurfaceYSmoothAt(m_activeShot.p2.x, m_activeShot.p2.z) + 8.0f);
            m_activeShot.durationSeconds = RandomRange(8.0f, 14.0f);
        } else if (m_activeShot.style == CameraShot::Style::PanX) {
            const float z = RandomRange(-46.0f, 46.0f);
            const float x0 = RandomRange(-58.0f, -20.0f);
            const float x1 = RandomRange(20.0f, 58.0f);
            const bool reverse = RandomRange(0.0f, 1.0f) > 0.5f;
            const glm::vec3 start = {reverse ? x1 : x0, 0.0f, z};
            const glm::vec3 end = {reverse ? x0 : x1, 0.0f, z};
            const float startY = SurfaceYSmoothAt(start.x, start.z) + RandomRange(8.0f, 11.0f);
            const float endY = SurfaceYSmoothAt(end.x, end.z) + RandomRange(8.0f, 11.0f);
            m_activeShot.p0 = {start.x, startY, start.z};
            m_activeShot.p3 = {end.x, endY, end.z};
            m_activeShot.p1 = m_activeShot.p0 + (m_activeShot.p3 - m_activeShot.p0) * 0.33f;
            m_activeShot.p2 = m_activeShot.p0 + (m_activeShot.p3 - m_activeShot.p0) * 0.66f;
            m_activeShot.durationSeconds = RandomRange(10.0f, 16.0f);
        } else {
            const float x = RandomRange(-46.0f, 46.0f);
            const float z0 = RandomRange(-58.0f, -20.0f);
            const float z1 = RandomRange(20.0f, 58.0f);
            const bool reverse = RandomRange(0.0f, 1.0f) > 0.5f;
            const glm::vec3 start = {x, 0.0f, reverse ? z1 : z0};
            const glm::vec3 end = {x, 0.0f, reverse ? z0 : z1};
            const float startY = SurfaceYSmoothAt(start.x, start.z) + RandomRange(8.0f, 11.0f);
            const float endY = SurfaceYSmoothAt(end.x, end.z) + RandomRange(8.0f, 11.0f);
            m_activeShot.p0 = {start.x, startY, start.z};
            m_activeShot.p3 = {end.x, endY, end.z};
            m_activeShot.p1 = m_activeShot.p0 + (m_activeShot.p3 - m_activeShot.p0) * 0.33f;
            m_activeShot.p2 = m_activeShot.p0 + (m_activeShot.p3 - m_activeShot.p0) * 0.66f;
            m_activeShot.durationSeconds = RandomRange(10.0f, 16.0f);
        }

        m_shotElapsedSeconds = 0.0f;
    }

    void UpdateCamera(float deltaSeconds) {
        if (!m_ready) {
            m_orbitTime += deltaSeconds;
            const float radius = 42.0f;
            const float angle = m_orbitTime * 0.09f;
            const glm::vec3 orbitTarget{std::sin(angle) * radius, 27.0f, std::cos(angle) * radius};
            if (!m_hasSmoothedPosition) {
                m_smoothedPosition = orbitTarget;
                m_hasSmoothedPosition = true;
            } else {
                const float alpha = 1.0f - std::exp(-deltaSeconds * 3.0f);
                m_smoothedPosition += (orbitTarget - m_smoothedPosition) * alpha;
            }
            m_camera.position = m_smoothedPosition;
            const glm::vec3 lookAt{0.0f, 18.0f, 0.0f};
            const glm::vec3 dir = glm::normalize(lookAt - m_camera.position);
            m_camera.yaw = std::atan2(-dir.x, -dir.z);
            m_camera.pitch = std::asin(std::clamp(dir.y, -1.0f, 1.0f));
            return;
        }

        m_shotElapsedSeconds += deltaSeconds;
        if (m_shotElapsedSeconds >= m_activeShot.durationSeconds) {
            SelectNextShot();
        }
        const float t = std::clamp(m_shotElapsedSeconds / std::max(0.001f, m_activeShot.durationSeconds), 0.0f, 1.0f);
        glm::vec3 position = BezierPoint(t);
        const float desiredClearance = 11.0f + (8.0f - 11.0f) * t;
        const float minimumY = SurfaceYSmoothAt(position.x, position.z) + desiredClearance;
        if (position.y < minimumY) position.y = minimumY;
        if (!m_hasSmoothedPosition) {
            m_smoothedPosition = position;
            m_hasSmoothedPosition = true;
        } else {
            const float alpha = 1.0f - std::exp(-deltaSeconds * 2.2f);
            m_smoothedPosition += (position - m_smoothedPosition) * alpha;
        }
        m_camera.position = m_smoothedPosition;

        glm::vec3 tangent = BezierTangent(t);
        if (glm::dot(tangent, tangent) < 0.0001f) tangent = glm::vec3{0.0f, -0.1f, -1.0f};
        const glm::vec3 dir = glm::normalize(tangent);
        m_camera.yaw = std::atan2(-dir.x, -dir.z);
        m_camera.pitch = std::asin(std::clamp(dir.y, -1.0f, 1.0f));
    }

    BlockRegistry* m_registry = nullptr;
    TextureAtlas* m_atlas = nullptr;
    graphics::IGraphicsRenderer* m_renderer = nullptr;
    WorldOptions m_options{};
    WorldGenerator m_sampler{};
    std::unique_ptr<World> m_world;
    std::unique_ptr<JobSystem> m_jobs;
    std::unique_ptr<graphics::ChunkRenderer> m_chunkRenderer;
    std::unique_ptr<graphics::SkyRenderer> m_skyRenderer;
    std::vector<PendingChunk> m_pending;
    std::size_t m_totalChunkJobs = 0;
    std::size_t m_completedChunkJobs = 0;
    bool m_ready = false;
    std::mt19937_64 m_rng{};
    Camera m_camera{};
    CameraShot m_activeShot{};
    float m_shotElapsedSeconds = 0.0f;
    float m_orbitTime = 0.0f;
    glm::vec3 m_smoothedPosition{0.0f};
    bool m_hasSmoothedPosition = false;
};

MainMenuBackdropRuntime g_mainMenuBackdrop;

Logger& RenderStateLog() {
    static Logger logger;
    static const bool initialized = [] {
        logger.AddSink(std::make_shared<ConsoleLogSink>());
        return true;
    }();
    (void)initialized;
    return logger;
}

/// Chunk radius generated around the origin for the initial playable area. Chunk streaming
/// around a moving player is work item 06; this keeps the world bounded but large enough to
/// walk around once a player controller lands.
constexpr int kInitialGenerationRadiusChunks = 3;
constexpr float kAutosaveIntervalSeconds = 120.0f;

std::unique_ptr<World> SnapshotWorld(const World& source) {
    auto snapshot = std::make_unique<World>(source.GetChunkSize());
    for (const auto& [coordinate, chunk] : source.GetChunks()) snapshot->GetOrCreateChunk(coordinate) = *chunk;
    return snapshot;
}

std::string ScreenshotName() {
    const auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm localTime{};
#if defined(_WIN32)
    localtime_s(&localTime, &now);
#else
    localtime_r(&now, &localTime);
#endif
    char name[32]{};
    std::strftime(name, sizeof(name), "shot_%Y%m%d_%H%M%S.png", &localTime);
    return name;
}

std::string TrimmedMessage(std::string message, std::size_t limit = 96U) {
    auto first = message.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    auto last = message.find_last_not_of(" \t\r\n");
    message = message.substr(first, last - first + 1U);
    if (message.size() > limit) message.resize(limit);
    return message;
}

std::string DisplayNameForBlock(const BlockRegistry* registry, BlockId blockId) {
    if (registry == nullptr) return "Item";
    const BlockDefinition* definition = registry->GetDefinition(blockId);
    return definition == nullptr ? "Item" : definition->displayName;
}

std::string DisplayNameForItem(const BlockRegistry* registry, std::string_view itemName) {
    if (registry == nullptr) return std::string(itemName);
    const BlockDefinition* definition = registry->GetDefinition(std::string(itemName));
    return definition == nullptr ? std::string(itemName) : definition->displayName;
}

} // namespace

void SetGlobalRenderer(voxels::graphics::IGraphicsRenderer* renderer) noexcept {
    g_renderer = renderer;
}

void StartMainMenuBackdrop(BlockRegistry& registry, TextureAtlas& atlas,
                           voxels::graphics::IGraphicsRenderer& renderer,
                           std::uint64_t seed, int renderDistanceChunks) {
    g_mainMenuBackdrop.Start(&registry, &atlas, &renderer, seed, renderDistanceChunks);
}

void StopMainMenuBackdrop() noexcept { g_mainMenuBackdrop.Stop(); }

void UpdateMainMenuBackdrop(double deltaSeconds) { g_mainMenuBackdrop.Update(deltaSeconds); }

void RenderMainMenuBackdrop() { g_mainMenuBackdrop.Render(); }

bool IsMainMenuBackdropReady() noexcept { return g_mainMenuBackdrop.IsReady(); }

float GetMainMenuBackdropProgress() noexcept { return g_mainMenuBackdrop.Progress(); }

bool IsMainMenuBackdropActive() noexcept { return g_mainMenuBackdrop.IsActive(); }

bool ConsumeStartupSplashEligibility() noexcept {
    if (g_startupSplashShown) return false;
    g_startupSplashShown = true;
    return true;
}

void MainMenuState::PublishSplash(float fade) const {
    if (m_context == nullptr || m_context->ui == nullptr) return;
    m_context->ui->Publish({.route = PlayerUIRoute::Splash,
                            .revision = static_cast<std::uint64_t>(m_phaseElapsedSeconds * 1000.0f) + 1U,
                            .title = "VOXELS ENGINE",
                            .message = "(c) 2026 Fractal Dynamics, All rights reserved.",
                            .payload = std::string{"{\"fade\":"} + std::to_string(std::clamp(fade, 0.0f, 1.0f)) + "}",
                            .progress = std::clamp(fade, 0.0f, 1.0f),
                            .blocking = true});
}

void MainMenuState::PublishPreviewLoading(float progress, float backdropOpacity, bool showStatus) const {
    if (m_context == nullptr || m_context->ui == nullptr) return;
    m_context->ui->Publish({.route = PlayerUIRoute::Loading,
                            .revision = static_cast<std::uint64_t>(progress * 1000.0f) +
                                        static_cast<std::uint64_t>(backdropOpacity * 1000.0f) * 10U +
                                        (showStatus ? 1U : 0U) + 2000U,
                            .title = "PREPARING MAIN MENU",
                            .message = showStatus ? "Generating a scenic world flythrough..." : "",
                            .payload = std::string{"{\"backdropOpacity\":"} +
                                       std::to_string(std::clamp(backdropOpacity, 0.0f, 1.0f)) +
                                       ",\"showStatus\":" + (showStatus ? "true" : "false") + "}",
                            .progress = std::clamp(progress, 0.0f, 1.0f),
                            .blocking = true});
}

void MainMenuState::PublishReadyMenu(float titleOpacity, bool showMenuButtons) const {
    if (m_context == nullptr || m_context->ui == nullptr) return;
    m_context->ui->Publish({.route = PlayerUIRoute::MainMenu,
                            .revision = static_cast<std::uint64_t>(std::clamp(titleOpacity, 0.0f, 1.0f) * 1000.0f) * 10U +
                                        (showMenuButtons ? 1U : 0U) + 5000U,
                            .title = "VOXELS ENGINE",
                            .message = "A block-based world is waiting.",
                            .items = {"Play", "Join Game", "Settings", "Quit"},
                            .payload = std::string{"{\"titleOpacity\":"} +
                                       std::to_string(std::clamp(titleOpacity, 0.0f, 1.0f)) +
                                       ",\"showMenuButtons\":" + (showMenuButtons ? "true" : "false") + "}"});
}

void MainMenuState::Update(double deltaSeconds) {
    m_phaseElapsedSeconds += static_cast<float>(deltaSeconds);

    if (m_phase == IntroPhase::PreviewLoadingOpaque) {
        const float progress = GetMainMenuBackdropProgress();
        PublishPreviewLoading(progress, 1.0f, true);
        if (IsMainMenuBackdropReady() || !IsMainMenuBackdropActive()) {
            m_phase = IntroPhase::PreviewReveal;
            m_phaseElapsedSeconds = 0.0f;
        }
        return;
    }

    if (m_phase == IntroPhase::PreviewReveal) {
        const float overlay = 1.0f - std::clamp(m_phaseElapsedSeconds / kPreviewRevealSeconds, 0.0f, 1.0f);
        PublishPreviewLoading(1.0f, overlay, false);
        if (m_phaseElapsedSeconds >= kPreviewRevealSeconds) {
            m_phase = m_runLogoSequence ? IntroPhase::LogoFadeIn : IntroPhase::Ready;
            m_phaseElapsedSeconds = 0.0f;
        }
        return;
    }

    if (m_phase == IntroPhase::LogoFadeIn) {
        const float fade = std::clamp(m_phaseElapsedSeconds / kLogoFadeInSeconds, 0.0f, 1.0f);
        PublishSplash(fade);
        if (m_phaseElapsedSeconds >= kLogoFadeInSeconds) {
            m_phase = IntroPhase::LogoHold;
            m_phaseElapsedSeconds = 0.0f;
        }
        return;
    }

    if (m_phase == IntroPhase::LogoHold) {
        PublishSplash(1.0f);
        if (m_phaseElapsedSeconds >= kLogoHoldSeconds) {
            m_phase = IntroPhase::LogoFadeOut;
            m_phaseElapsedSeconds = 0.0f;
        }
        return;
    }

    if (m_phase == IntroPhase::LogoFadeOut) {
        const float fade = 1.0f - std::clamp(m_phaseElapsedSeconds / kLogoFadeOutSeconds, 0.0f, 1.0f);
        PublishSplash(fade);
        if (m_phaseElapsedSeconds >= kLogoFadeOutSeconds) {
            m_phase = IntroPhase::LogoPostFadeHold;
            m_phaseElapsedSeconds = 0.0f;
        }
        return;
    }

    if (m_phase == IntroPhase::LogoPostFadeHold) {
        // Hold one extra beat after the splash fully fades so music and title reveal line up.
        PublishPreviewLoading(1.0f, 0.0f, false);
        if (m_phaseElapsedSeconds >= kPostLogoHoldSeconds) {
            m_phase = IntroPhase::MenuTitleFadeIn;
            m_phaseElapsedSeconds = 0.0f;
        }
        return;
    }

    if (m_phase == IntroPhase::MenuTitleFadeIn) {
        const float titleOpacity = std::clamp(m_phaseElapsedSeconds / kMenuTitleFadeInSeconds, 0.0f, 1.0f);
        PublishReadyMenu(titleOpacity, false);
        if (m_phaseElapsedSeconds >= kMenuTitleFadeInSeconds) {
            m_phase = IntroPhase::Ready;
            m_phaseElapsedSeconds = 0.0f;
        }
        return;
    }

    if (!m_publishedReadyMenu) {
        PublishReadyMenu(1.0f, true);
        m_publishedReadyMenu = true;
    }
    if (!m_controlsCardQueued && m_context != nullptr && m_context->firstRun &&
        m_context->preferences != nullptr && !m_context->preferences->controlsCardSeen &&
        m_context->requestPushOverlay) {
        m_controlsCardQueued = true;
        m_context->requestPushOverlay(std::make_unique<ControlsCardState>(m_context));
    }
}

void InGameState::OnEnter() {
    StopMainMenuBackdrop();
    m_previewCaptureAction = PreviewCaptureAction::None;
    if (m_registry != nullptr) {
        try {
            m_craftingService.LoadFromFile(Paths::AssetsDir() / "data" / "recipes.json", *m_registry);
        } catch (const std::runtime_error& error) {
            RenderStateLog().Error(error.what());
            if (m_context != nullptr && m_context->requestTransition) {
                m_context->requestTransition(std::make_unique<ErrorState>(m_context, "Recipe catalogue invalid", error.what()));
            }
            return;
        }
    }
    if (m_jobSystem == nullptr) m_jobSystem = std::make_unique<JobSystem>();
    if (m_remoteSession) {
        if (m_preparedWorld != nullptr) m_session.AdoptWorld(std::move(m_preparedWorld));
        m_session.SetRemoteWorld(true);
        if (m_hasRemoteSpawn) m_session.SetPlayerSpawn(m_remoteSpawn);
    } else if (m_context != nullptr && m_context->networkServer != nullptr) {
        World& authoritativeWorld = m_context->networkServer->GetWorld();
        m_context->networkServer->SetBlockRegistry(m_registry);
        authoritativeWorld.Clear();
        authoritativeWorld.Initialize(m_options);
        if (m_preparedWorld != nullptr) {
            for (const auto& [coordinate, chunk] : m_preparedWorld->GetChunks()) {
                authoritativeWorld.GetOrCreateChunk(coordinate) = *chunk;
            }
            m_preparedWorld.reset();
        }
        m_session.SetWorld(&authoritativeWorld);
        m_session.SetAuthoritativeItemDrops(&m_context->networkServer->GetItemDrops());
    } else if (m_preparedWorld != nullptr) {
        m_session.AdoptWorld(std::move(m_preparedWorld));
    }
    m_session.SetWorldOptions(m_options);
    m_session.SetInputManager(m_inputManager);
    if (m_context != nullptr) m_session.SetNetworkClient(m_context->networkClient);
    m_session.SetBlockRegistry(m_registry);
    m_session.SetJobSystem(m_jobSystem.get());
    if (m_context != nullptr) m_session.SetPlatformServices(m_context->platformServices);
    if (m_context != nullptr && m_context->preferences != nullptr) m_session.SetPreferences(*m_context->preferences);
    if (m_context != nullptr) m_context->activeGame = this;
    PlayerState loadedPlayer{};
    const bool hasSavedPlayer = !m_remoteSession && m_context != nullptr && m_context->saveManager != nullptr &&
                                !m_activeSave.saveName.empty() &&
                                m_context->saveManager->LoadPlayerState(m_activeSave.saveName, m_activeSave.playerName, loadedPlayer);
    if (hasSavedPlayer) {
        m_session.RestorePlayerState(loadedPlayer);
    } else if (m_activeSave.spawnY > 0.0f) {
        m_session.SetPlayerSpawn(Vec3{m_activeSave.spawnX, m_activeSave.spawnY, m_activeSave.spawnZ});
    }
    m_session.Initialize();
    if (hasSavedPlayer && !IsSafePlayerSpawn(m_session.GetWorld(), m_session.GetPlayer().state.position) &&
        m_activeSave.spawnY > 0.0f) {
        m_session.SetPlayerSpawn(Vec3{m_activeSave.spawnX, m_activeSave.spawnY, m_activeSave.spawnZ});
    }
    if (!m_remoteSession && m_context != nullptr && m_context->networkServer != nullptr) {
        // Publish the hosted world so remote joiners receive its identity and spawn point.
        m_context->networkServer->SetWorldReady(m_options, m_session.GetPlayer().state.position, m_activeSave.worldTick);
        RenderStateLog().Info("Hosting world on UDP port " + std::to_string(m_context->networkServer->Port()));
    }
    if (m_remoteSession) {
        RenderStateLog().Info("Remote session started: playing on the host's world.");
    }

    m_chatOpen = false;
    m_craftingOpen = false;
    m_hudRevision = 0;
    m_hudPublishCooldownSeconds = 0.0f;
    m_lastPublishedBreakProgress = -1.0f;
    m_lastPublishedTargetHit = false;
    m_lastPublishedSelectedSlot = -1;
    m_hudNotifications.clear();
    m_knownRemotePlayers.clear();
    if (m_context != nullptr && m_context->ui != nullptr) {
        m_context->ui->SetInputPolicy(PlayerUIInputPolicy::Gameplay);
        PublishHudModel(true);
    } else if (m_platform != nullptr) {
        m_platform->SetRelativeMouseMode(true);
    }

    if (m_registry != nullptr && m_atlas != nullptr) {
        if (m_chunkRenderer == nullptr) {
            m_chunkRenderer = std::make_unique<graphics::ChunkRenderer>(
                *m_registry, *m_atlas, *m_jobSystem, m_context != nullptr ? m_context->renderer : nullptr);
            m_chunkRenderer->SetUploadBudget(2, 1.0);
            m_chunkRenderer->SetBackgroundMeshQueueLimit(1);
        }
        if (m_skyRenderer == nullptr) m_skyRenderer = std::make_unique<graphics::SkyRenderer>();
        if (m_underwaterOverlay == nullptr) m_underwaterOverlay = std::make_unique<graphics::UnderwaterOverlay>();
        if (m_itemDropRenderer == nullptr) {
            m_itemDropRenderer = std::make_unique<graphics::ItemDropRenderer>(*m_registry, *m_atlas);
        }
        if (m_hudRenderer == nullptr) m_hudRenderer = std::make_unique<graphics::GameplayHudRenderer>();
        if (m_remotePlayerRenderer == nullptr) m_remotePlayerRenderer = std::make_unique<graphics::RemotePlayerRenderer>();
    }

    if (m_inputManager != nullptr) {
        m_inputManager->BindAction("MoveForward", InputBinding{"MoveForward", static_cast<int>('w'), static_cast<int>('W'), InputDeviceType::Keyboard});
        m_inputManager->BindAction("MoveBackward", InputBinding{"MoveBackward", static_cast<int>('s'), static_cast<int>('S'), InputDeviceType::Keyboard});
        m_inputManager->BindAction("MoveLeft", InputBinding{"MoveLeft", static_cast<int>('a'), static_cast<int>('A'), InputDeviceType::Keyboard});
        m_inputManager->BindAction("MoveRight", InputBinding{"MoveRight", static_cast<int>('d'), static_cast<int>('D'), InputDeviceType::Keyboard});
        m_inputManager->BindAction("Jump", InputBinding{"Jump", static_cast<int>(' '), 0, InputDeviceType::Keyboard});
        m_inputManager->BindAction("Sprint", InputBinding{"Sprint", 1073742049, 0, InputDeviceType::Keyboard});
        m_inputManager->BindAction("Pause", InputBinding{"Pause", 27, 0, InputDeviceType::Keyboard});
        m_inputManager->BindAction("Chat", InputBinding{"Chat", static_cast<int>('t'), static_cast<int>('T'), InputDeviceType::Keyboard});
        m_inputManager->BindAction("Crafting", InputBinding{"Crafting", static_cast<int>('e'), static_cast<int>('E'), InputDeviceType::Keyboard});
        m_inputManager->BindAction("Screenshot", InputBinding{"Screenshot", 1073741883, 0, InputDeviceType::Keyboard});
        m_inputManager->BindAction("DestroyBlock", InputBinding{"DestroyBlock", 1, 0, InputDeviceType::Mouse});
        m_inputManager->BindAction("PlaceBlock", InputBinding{"PlaceBlock", 3, 0, InputDeviceType::Mouse});
        m_inputManager->BindAction("DropItem", InputBinding{"DropItem", static_cast<int>('g'), static_cast<int>('G'), InputDeviceType::Keyboard});
        for (int slot = 0; slot < 9; ++slot) {
            m_inputManager->BindAction("Hotbar" + std::to_string(slot + 1), InputBinding{"", static_cast<int>('1') + slot, 0, InputDeviceType::Keyboard});
        }
    }
    if (m_cameraOverride != nullptr) {
        *m_cameraOverride = m_session.GetCamera();
    }

    if (m_chunkRenderer) {
        auto& world = m_session.GetWorld();
        for (const auto& [coordinate, chunk] : world.GetChunks()) {
            (void)chunk;
            m_chunkRenderer->OnChunkArrived(coordinate, world);
        }
        const int chunkSize = static_cast<int>(world.GetChunkSize());
        for (const Vec3I& position : m_session.GetEditedBlocks()) {
            const LightingUpdate lighting = world.RebuildLightingAround(position, *m_registry);
            for (const ChunkCoordinate& dirty : lighting.dirtyChunks) m_chunkRenderer->MarkChunkDirty(dirty);
            const ChunkCoordinate coordinate{static_cast<int>(std::floor(static_cast<float>(position.x) / chunkSize)),
                                             static_cast<int>(std::floor(static_cast<float>(position.y) / chunkSize)),
                                             static_cast<int>(std::floor(static_cast<float>(position.z) / chunkSize))};
            const Vec3I local{((position.x % chunkSize) + chunkSize) % chunkSize, ((position.y % chunkSize) + chunkSize) % chunkSize,
                               ((position.z % chunkSize) + chunkSize) % chunkSize};
            m_chunkRenderer->MarkBlockEdited(coordinate, local, world.GetChunkSize());
        }
        m_session.ClearEditedBlocks();
        m_chunkRenderer->EnqueueDirtyMeshJobs(world, m_session.GetCamera().position);
        m_chunkRenderer->UploadCompletedMeshes();
    }
    if (!m_remoteSession && m_context != nullptr && m_context->saveManager != nullptr &&
        !m_activeSave.saveName.empty()) {
        std::error_code error;
        const std::filesystem::path previewPath =
            m_context->saveManager->GetWorldPreviewPath(m_activeSave.saveName);
        if (!previewPath.empty() && !std::filesystem::exists(previewPath, error)) {
            m_previewCaptureAction = PreviewCaptureAction::CaptureOnly;
        }
    }
    m_worldGenerated = true;
}

void InGameState::OnExit() {
    if (!m_remoteSession && m_context != nullptr && m_context->saveManager != nullptr && !m_activeSave.saveName.empty()) {
        m_activeSave.worldTick = GetWorldTick();
        m_activeSave.lastPlayedAt = "saved";
        m_activeSave.spawnX = m_session.GetPlayer().state.position.x;
        m_activeSave.spawnY = m_session.GetPlayer().state.position.y;
        m_activeSave.spawnZ = m_session.GetPlayer().state.position.z;
        m_context->saveManager->Save(m_activeSave);
        m_context->saveManager->SaveWorldState(m_activeSave.saveName, m_session.GetWorld());
        m_context->saveManager->SavePlayerState(m_activeSave.saveName, m_activeSave.playerName, m_session.GetPlayer().state);
        if (m_context->platformServices != nullptr) {
            m_context->platformServices->OnWorldSaved(m_context->saveManager->GetSaveDirectory(m_activeSave.saveName));
        }
    }
    if (!m_remoteSession && m_context != nullptr && m_context->networkServer != nullptr) {
        // End the hosted session: remote peers are told the world closed before it is dropped.
        m_context->networkServer->ClearWorld();
    }
    if (m_context != nullptr && m_context->activeGame == this) m_context->activeGame = nullptr;
    if (m_platform != nullptr) {
        m_platform->SetRelativeMouseMode(false);
    }
    if (m_autosaveFuture.valid()) m_autosaveFuture.wait();
    if (m_jobSystem) {
        m_jobSystem->Shutdown();
    }
    if (m_chunkRenderer) {
        m_chunkRenderer->Shutdown();
    }
    if (m_skyRenderer) m_skyRenderer->Shutdown();
    if (m_underwaterOverlay) m_underwaterOverlay->Shutdown();
    if (m_itemDropRenderer) m_itemDropRenderer->Shutdown();
    if (m_hudRenderer) m_hudRenderer->Shutdown();
    if (m_remotePlayerRenderer) m_remotePlayerRenderer->Shutdown();
    m_chunkRenderer.reset();
    m_skyRenderer.reset();
    m_underwaterOverlay.reset();
    m_itemDropRenderer.reset();
    m_hudRenderer.reset();
    m_remotePlayerRenderer.reset();
    m_jobSystem.reset();
    m_session.Shutdown();
    m_remoteSession = false;
    m_hasRemoteSpawn = false;
    m_chatOpen = false;
    m_craftingOpen = false;
    m_hudNotifications.clear();
    m_knownRemotePlayers.clear();
    m_previewCaptureAction = PreviewCaptureAction::None;
    m_worldGenerated = false;
}

void InGameState::OnResume() {
    m_chatOpen = false;
    m_craftingOpen = false;
    if (m_context != nullptr && m_context->ui != nullptr) {
        m_context->ui->SetInputPolicy(PlayerUIInputPolicy::Gameplay);
        PublishHudModel(true);
    } else if (m_platform != nullptr) {
        m_platform->SetRelativeMouseMode(true);
    }
    if (m_context != nullptr && m_context->input != nullptr) {
        m_context->input->ClearGameplayInput();
    }
    if (m_cameraOverride != nullptr) {
        *m_cameraOverride = m_session.GetCamera();
    }
}

bool InGameState::SetPublicVisibility(bool isPublic) {
    if (m_remoteSession || m_context == nullptr || m_context->saveManager == nullptr ||
        m_activeSave.saveName.empty()) {
        return false;
    }
    const bool previousVisibility = m_activeSave.publicVisibility;
    m_activeSave.publicVisibility = isPublic;
    if (!m_context->saveManager->Save(m_activeSave)) {
        m_activeSave.publicVisibility = previousVisibility;
        return false;
    }
    m_options.isPublic = isPublic;
    if (m_context->networkServer != nullptr) {
        m_context->networkServer->SetWorldReady(m_options, m_session.GetPlayer().state.position, GetWorldTick());
    }
    return true;
}

bool InGameState::SelectHotbarSlot(int slot) {
    m_session.GetPlayer().state.inventory.SetSelectedSlot(slot);
    PublishHudModel(true);
    return true;
}

void InGameState::UpdateHudInputPolicy() {
    if (m_context == nullptr || m_context->ui == nullptr) return;
    if (m_chatOpen) {
        m_context->ui->SetInputPolicy(PlayerUIInputPolicy::TextEntry);
    } else if (m_craftingOpen) {
        m_context->ui->SetInputPolicy(PlayerUIInputPolicy::Overlay);
    } else {
        m_context->ui->SetInputPolicy(PlayerUIInputPolicy::Gameplay);
    }
    if (m_context->input != nullptr) m_context->input->ClearGameplayInput();
}

void InGameState::SetChatOpen(bool open) {
    m_chatOpen = open;
    if (open) m_craftingOpen = false;
    UpdateHudInputPolicy();
    PublishHudModel(true);
}

void InGameState::SetCraftingOpen(bool open) {
    m_craftingOpen = open;
    if (open) m_chatOpen = false;
    UpdateHudInputPolicy();
    PublishHudModel(true);
}

void InGameState::PushHudNotification(std::string text, float lifetimeSeconds) {
    text = TrimmedMessage(std::move(text));
    if (text.empty()) return;
    if (m_hudNotifications.size() >= 24U) {
        m_hudNotifications.erase(m_hudNotifications.begin());
    }
    m_hudNotifications.push_back(
        {.id = m_nextHudNotificationId++, .text = std::move(text), .remainingSeconds = std::max(0.5f, lifetimeSeconds)});
}

bool InGameState::SubmitChatMessage(const std::string& message) {
    const std::string sanitized = TrimmedMessage(message, 120U);
    if (sanitized.empty()) return false;
    PushHudNotification("You: " + sanitized, 12.0f);
    PublishHudModel(true);
    return true;
}

bool InGameState::CraftRecipe(const std::string& recipeId) {
    if (m_registry == nullptr) return false;
    gameplay::Inventory& inventory = m_session.GetPlayer().state.inventory;
    const gameplay::RecipeDefinition* recipe = m_craftingService.FindRecipe(recipeId);
    if (recipe == nullptr) return false;
    const BlockDefinition* outputDefinition = m_registry->GetDefinition(recipe->outputBlockId);
    if (outputDefinition == nullptr) return false;
    if (!m_craftingService.Craft(inventory, recipeId)) {
        PushHudNotification("Missing ingredients or inventory space for " + outputDefinition->displayName + ".");
        PublishHudModel(true);
        return false;
    }
    PushHudNotification("Crafted " + std::to_string(recipe->outputCount) + "x " + outputDefinition->displayName + ".");
    PublishHudModel(true);
    return true;
}

bool InGameState::DropSlot(int slot) {
    if (slot < 0 || static_cast<std::size_t>(slot) >= gameplay::Inventory::kSlotCount) return false;
    const gameplay::ItemStack& stack = m_session.GetPlayer().state.inventory.GetSlot(static_cast<std::size_t>(slot));
    if (stack.IsEmpty()) return false;
    const std::string label = DisplayNameForBlock(m_registry, stack.blockId);
    const int dropped = m_session.DropInventorySlot(static_cast<std::size_t>(slot), stack.count);
    if (dropped <= 0) return false;
    PushHudNotification("Dropped " + std::to_string(dropped) + "x " + label + ".");
    PublishHudModel(true);
    return true;
}

bool InGameState::MoveInventoryItem(int from, int to) {
    if (from < 0 || to < 0 || from == to) return false;
    if (static_cast<std::size_t>(from) >= gameplay::Inventory::kSlotCount ||
        static_cast<std::size_t>(to) >= gameplay::Inventory::kSlotCount) {
        return false;
    }
    gameplay::Inventory& inventory = m_session.GetPlayer().state.inventory;
    gameplay::ItemStack& source = inventory.GetSlot(static_cast<std::size_t>(from));
    gameplay::ItemStack& destination = inventory.GetSlot(static_cast<std::size_t>(to));
    if (source.IsEmpty()) return false;

    if (!destination.IsEmpty() && destination.blockId == source.blockId) {
        const int capacity = gameplay::Inventory::kStackLimit - destination.count;
        const int moved = std::min(capacity, source.count);
        destination.count += moved;
        source.count -= moved;
        if (source.count <= 0) source = {};
    } else {
        std::swap(source, destination);
    }
    PublishHudModel(true);
    return true;
}

void InGameState::RequestSaveAndReturnToMenu() noexcept {
    if (!m_remoteSession) m_previewCaptureAction = PreviewCaptureAction::ReturnToMainMenu;
}

void InGameState::RequestSaveAndExitToDesktop() noexcept {
    if (!m_remoteSession) m_previewCaptureAction = PreviewCaptureAction::ExitToDesktop;
}

void InGameState::CompletePendingPreviewCapture() {
    if (m_previewCaptureAction == PreviewCaptureAction::None) return;
    if (m_previewCaptureAction == PreviewCaptureAction::CaptureOnly && m_chunkRenderer != nullptr &&
        m_chunkRenderer->GetMetrics().drawCalls == 0) {
        return;
    }

    const PreviewCaptureAction completedAction = std::exchange(m_previewCaptureAction, PreviewCaptureAction::None);
    bool captured = false;
    std::filesystem::path previewPath;
    if (!m_remoteSession && g_renderer != nullptr && m_context != nullptr &&
        m_context->saveManager != nullptr && !m_activeSave.saveName.empty()) {
        previewPath = m_context->saveManager->GetWorldPreviewPath(m_activeSave.saveName);
        captured = m_context->saveManager->SaveWorldPreview(
            m_activeSave.saveName,
            [](const std::filesystem::path& stagingPath) {
                return g_renderer != nullptr && g_renderer->CaptureScreenshot(stagingPath);
            });
    }
    if (captured) {
        RenderStateLog().Info("Saved world preview to " + previewPath.string());
    } else {
        RenderStateLog().Warn("World preview capture failed; continuing save and exit flow.");
    }

    if (m_context == nullptr) return;
    if (completedAction == PreviewCaptureAction::ReturnToMainMenu && m_context->requestTransition) {
        m_context->requestTransition(std::make_unique<MainMenuState>(m_context));
    } else if (completedAction == PreviewCaptureAction::ExitToDesktop && m_context->requestQuit) {
        m_context->requestQuit();
    }
}

void InGameState::GenerateInitialWorld() {
    if (m_worldGenerated) {
        return;
    }
    m_session.SetWorldOptions(m_options);
    m_session.Initialize();
    m_worldGenerated = true;
}

void InGameState::Update(double deltaSeconds) {
    m_elapsedSeconds += static_cast<float>(deltaSeconds);
    m_autosaveSeconds += static_cast<float>(deltaSeconds);
    m_hudPublishCooldownSeconds += static_cast<float>(deltaSeconds);
    RefreshHudNotifications(static_cast<float>(deltaSeconds));

    if (m_context != nullptr && m_context->input != nullptr) {
        if (m_context->input->IsActionActive("Chat")) {
            SetChatOpen(!m_chatOpen);
            m_context->input->ClearGameplayInput();
            return;
        }
        if (m_context->input->IsActionActive("Crafting")) {
            SetCraftingOpen(!m_craftingOpen);
            m_context->input->ClearGameplayInput();
            return;
        }
    }

    if (m_context != nullptr && m_context->input != nullptr && m_context->input->IsActionActive("Pause") &&
        m_context->requestPushOverlay) {
        if (m_chatOpen || m_craftingOpen) {
            m_chatOpen = false;
            m_craftingOpen = false;
            UpdateHudInputPolicy();
            PublishHudModel(true);
            return;
        }
        m_context->requestPushOverlay(std::make_unique<PauseMenuState>(m_context, m_activeSave, m_remoteSession));
        return;
    }

    if (m_remoteSession && m_context != nullptr && m_context->networkClient != nullptr) {
        networking::GameClient& client = *m_context->networkClient;
        if (client.WasDisconnectedByServer() || client.SecondsSinceLastServerPacket() > 10.0) {
            if (m_context->resetNetworkToLocal) m_context->resetNetworkToLocal();
            if (m_context->requestTransition) {
                m_context->requestTransition(std::make_unique<ErrorState>(
                    m_context, "Connection Lost",
                    client.WasDisconnectedByServer() ? "The host ended the session."
                                                     : "The host stopped responding."));
            }
            return;
        }
    }

    ApplyNetworkedBlockUpdates();
    ObserveRemotePlayerPresence();

    m_session.Update(static_cast<float>(deltaSeconds));
    for (const HealthEvent& event : m_session.GetHealthEvents()) {
        if (event.died) {
            PushHudNotification("You died and respawned at the world spawn.");
        } else if (event.source == DamageSource::Drowning) {
            PushHudNotification("You are drowning.", 2.0f);
        } else if (event.source == DamageSource::Fall) {
            PushHudNotification("Fall damage: " + std::to_string(event.amount / 2) +
                                (event.amount % 2 == 0 ? " hearts." : " and a half hearts."), 2.0f);
        }
    }
    m_session.ClearHealthEvents();
    if (m_context != nullptr && m_context->audio != nullptr && m_registry != nullptr) {
        const Camera& listener = m_session.GetCamera();
        const float cosPitch = std::cos(listener.pitch);
        const glm::vec3 listenerForward{-std::sin(listener.yaw) * cosPitch, std::sin(listener.pitch),
                                        -std::cos(listener.yaw) * cosPitch};
        m_context->audio->SetListener(listener.position, listenerForward);
        for (const GameplaySoundEvent& event : m_session.GetSoundEvents()) {
            const BlockDefinition* definition = m_registry->GetDefinition(event.blockId);
            if (definition == nullptr && event.type != GameplaySoundEventType::Jump && event.type != GameplaySoundEventType::Land && event.type != GameplaySoundEventType::Splash) continue;
            std::string soundId;
            switch (event.type) {
                case GameplaySoundEventType::Break: soundId = definition->sounds.breakSound; break;
                case GameplaySoundEventType::Place: soundId = definition->sounds.placeSound; break;
                case GameplaySoundEventType::Footstep: soundId = definition->sounds.stepSound; break;
                case GameplaySoundEventType::Jump: soundId = "sfx/jump"; break;
                case GameplaySoundEventType::Land: soundId = "sfx/land"; break;
                case GameplaySoundEventType::Splash: soundId = "sfx/splash"; break;
            }
            const auto found = m_context->soundBank.find(soundId);
            if (found != m_context->soundBank.end()) {
                m_context->audio->PlaySound(found->second, 1.0f, 1.0f,
                                            glm::vec3{event.position.x, event.position.y, event.position.z});
            }
        }
        m_session.ClearSoundEvents();
    }
    if (m_context != nullptr && m_context->input != nullptr) {
        const bool screenshotActive = m_context->input->IsActionActive("Screenshot");
        if (screenshotActive && !m_screenshotPressed && m_context->renderer != nullptr && m_context->saveManager != nullptr) {
            const auto path = m_context->saveManager->GetSaveDirectory(m_activeSave.saveName) / "screenshots" / ScreenshotName();
            const bool captured = m_context->renderer->CaptureScreenshot(path);
            if (m_context->ui != nullptr) m_context->ui->ShowToast(captured ? "Screenshot saved" : "Screenshot capture failed");
            PushHudNotification(captured ? "Screenshot saved." : "Screenshot capture failed.");
            PublishHudModel(true);
        }
        m_screenshotPressed = screenshotActive;
    }
    if (m_autosaveFuture.valid() && m_autosaveFuture.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        const bool saved = m_autosaveFuture.get();
        if (saved && m_context != nullptr && m_context->platformServices != nullptr && m_context->saveManager != nullptr) {
            m_context->platformServices->OnWorldSaved(m_context->saveManager->GetSaveDirectory(m_activeSave.saveName));
        }
        if (m_context != nullptr && m_context->ui != nullptr) m_context->ui->ShowToast(saved ? "World autosaved" : "World autosave failed");
        PushHudNotification(saved ? "World autosaved." : "World autosave failed.");
        PublishHudModel(true);
    }
    if (m_autosaveSeconds >= kAutosaveIntervalSeconds && !m_autosaveFuture.valid()) StartAutosave();
    if (m_cameraOverride != nullptr) {
        *m_cameraOverride = m_session.GetCamera();
    }

    const int selectedSlot = m_session.GetPlayer().state.inventory.GetSelectedSlot();
    const bool targetChanged = m_session.GetTarget().hit != m_lastPublishedTargetHit;
    const bool breakChanged = std::abs(m_session.GetBreakProgress() - m_lastPublishedBreakProgress) >= 0.05f;
    const bool slotChanged = selectedSlot != m_lastPublishedSelectedSlot;
    PublishHudModel(targetChanged || breakChanged || slotChanged);

    if (m_chunkRenderer) {
        auto& world = m_session.GetWorld();
        for (const ChunkCoordinate& coordinate : m_session.ConsumeRemovedChunks()) {
            m_chunkRenderer->OnChunkRemoved(coordinate, world);
        }
        for (const ChunkCoordinate& coordinate : m_session.ConsumeArrivedChunks()) {
            m_chunkRenderer->OnChunkArrived(coordinate, world);
        }
        if (m_remoteSession && m_context != nullptr && m_context->networkClient != nullptr) {
              for (const ChunkCoordinate& coordinate :
                  m_remoteChunkApplier.Apply(*m_context->networkClient, world, *m_registry)) {
                m_chunkRenderer->OnChunkArrived(coordinate, world);
            }
        }
        if (!m_remoteSession && m_context != nullptr && m_context->networkServer != nullptr) {
            for (const ChunkCoordinate& coordinate : m_context->networkServer->TakeNewlyGeneratedChunks()) {
                m_chunkRenderer->OnChunkArrived(coordinate, world);
            }
        }
        const int chunkSize = static_cast<int>(world.GetChunkSize());
        for (const Vec3I& position : m_session.GetEditedBlocks()) {
            const LightingUpdate lighting = world.RebuildLightingAround(position, *m_registry);
            for (const ChunkCoordinate& dirty : lighting.dirtyChunks) m_chunkRenderer->MarkChunkDirty(dirty);
            const ChunkCoordinate coordinate{static_cast<int>(std::floor(static_cast<float>(position.x) / chunkSize)),
                                             static_cast<int>(std::floor(static_cast<float>(position.y) / chunkSize)),
                                             static_cast<int>(std::floor(static_cast<float>(position.z) / chunkSize))};
            const Vec3I local{((position.x % chunkSize) + chunkSize) % chunkSize,
                               ((position.y % chunkSize) + chunkSize) % chunkSize,
                               ((position.z % chunkSize) + chunkSize) % chunkSize};
            m_chunkRenderer->MarkBlockEdited(coordinate, local, world.GetChunkSize());
        }
        m_session.ClearEditedBlocks();
        m_chunkRenderer->EnqueueDirtyMeshJobs(world, m_session.GetCamera().position);
        m_chunkRenderer->UploadCompletedMeshes();
    }
}

void InGameState::ApplyNetworkedBlockUpdates() {
    if (m_context == nullptr) return;
    World& world = m_session.GetWorld();
    const int chunkSize = static_cast<int>(world.GetChunkSize());
    const auto relightAndRemesh = [this, &world, chunkSize](const Vec3I& position) {
        m_session.ResolveItemDropsAfterBlockEdit(position);
        const LightingUpdate lighting = world.RebuildLightingAround(position, *m_registry);
        if (m_chunkRenderer == nullptr) return;
        for (const ChunkCoordinate& dirty : lighting.dirtyChunks) m_chunkRenderer->MarkChunkDirty(dirty);
        const ChunkCoordinate coordinate{static_cast<int>(std::floor(static_cast<float>(position.x) / chunkSize)),
                                         static_cast<int>(std::floor(static_cast<float>(position.y) / chunkSize)),
                                         static_cast<int>(std::floor(static_cast<float>(position.z) / chunkSize))};
        const Vec3I local{((position.x % chunkSize) + chunkSize) % chunkSize,
                          ((position.y % chunkSize) + chunkSize) % chunkSize,
                          ((position.z % chunkSize) + chunkSize) % chunkSize};
        m_chunkRenderer->MarkBlockEdited(coordinate, local, world.GetChunkSize());
    };
    if (m_context->networkClient != nullptr) {
        for (const networking::BlockModify& update : m_context->networkClient->TakeReceivedBlockUpdates()) {
            // Edits this client already applied predictively (and the host's shared world) match
            // the authoritative value and are skipped without a redundant remesh.
            if (world.GetBlock(update.position) == update.blockId) continue;
            if (!world.SetBlock(update.position, update.blockId)) continue;
            relightAndRemesh(update.position);
        }
    }
    if (!m_remoteSession && m_context->networkServer != nullptr) {
        // Remote peers' edits were already applied to the shared world by the server; the host
        // still needs to relight and remesh the touched chunks.
        for (const networking::BlockModify& edit : m_context->networkServer->TakeRemoteBlockEdits()) {
            relightAndRemesh(edit.position);
        }
    }
}

void InGameState::StartAutosave() {
    if (m_context == nullptr || m_context->saveManager == nullptr || m_jobSystem == nullptr || m_activeSave.saveName.empty()) return;
    m_autosaveSeconds = 0.0f;
    m_activeSave.worldTick = GetWorldTick();
    auto worldSnapshot = std::shared_ptr<World>(SnapshotWorld(m_session.GetWorld()).release());
    const GameSave saveSnapshot = m_activeSave;
    const PlayerState playerSnapshot = m_session.GetPlayer().state;
    SaveManager* const saveManager = m_context->saveManager;
    m_autosaveFuture = m_jobSystem->EnqueueWithResult([saveManager, saveSnapshot, playerSnapshot, worldSnapshot] {
        return saveManager->Save(saveSnapshot) && saveManager->SaveWorldState(saveSnapshot.saveName, *worldSnapshot) &&
               saveManager->SavePlayerState(saveSnapshot.saveName, saveSnapshot.playerName, playerSnapshot);
    });
}

void InGameState::RefreshHudNotifications(float deltaSeconds) {
    for (HudNotification& notification : m_hudNotifications) {
        notification.remainingSeconds -= std::max(0.0f, deltaSeconds);
    }
    const auto originalSize = m_hudNotifications.size();
    std::erase_if(m_hudNotifications, [](const HudNotification& notification) {
        return notification.remainingSeconds <= 0.0f;
    });
    if (m_hudNotifications.size() != originalSize) PublishHudModel(true);
}

void InGameState::ObserveRemotePlayerPresence() {
    if (m_context == nullptr || m_context->networkClient == nullptr) return;
    networking::GameClient& client = *m_context->networkClient;
    std::unordered_set<std::uint32_t> observed;
    const std::uint32_t localPlayerId = client.PlayerId();
    for (const auto& [entityId, state] : client.ReceivedEntityStates()) {
        (void)state;
        if (entityId == 0 || entityId == localPlayerId) continue;
        observed.insert(entityId);
        if (!m_knownRemotePlayers.contains(entityId)) {
            m_knownRemotePlayers.insert(entityId);
            PushHudNotification("Player " + std::to_string(entityId) + " joined.");
        }
    }
    for (const std::uint32_t departedId : client.TakeDepartedPlayers()) {
        m_knownRemotePlayers.erase(departedId);
        PushHudNotification("Player " + std::to_string(departedId) + " left.");
    }
    m_knownRemotePlayers = std::move(observed);
}

void InGameState::PublishHudModel(bool forcePublish) {
    if (m_context == nullptr || m_context->ui == nullptr) return;
    if (!forcePublish) {
        constexpr float kHudPublishIntervalSeconds = 1.0f / 30.0f;
        if (m_hudPublishCooldownSeconds < kHudPublishIntervalSeconds) return;
        m_hudPublishCooldownSeconds = 0.0f;
    } else {
        m_hudPublishCooldownSeconds = 0.0f;
    }

    const PlayerState& player = m_session.GetPlayer().state;
    const gameplay::Inventory& inventory = player.inventory;
    nlohmann::json hotbar = nlohmann::json::array();
    for (std::size_t slot = 0; slot < gameplay::Inventory::kHotbarSlots; ++slot) {
        const auto& stack = inventory.GetSlot(slot);
        hotbar.push_back({
            {"slot", static_cast<int>(slot)},
            {"selected", inventory.GetSelectedSlot() == static_cast<int>(slot)},
            {"blockId", stack.IsEmpty() ? 0 : static_cast<int>(stack.blockId)},
            {"count", stack.IsEmpty() ? 0 : stack.count},
            {"name", stack.IsEmpty() ? std::string{} : DisplayNameForBlock(m_registry, stack.blockId)}
        });
    }

    nlohmann::json inventorySlots = nlohmann::json::array();
    for (std::size_t slot = 0; slot < gameplay::Inventory::kSlotCount; ++slot) {
        const auto& stack = inventory.GetSlot(slot);
        inventorySlots.push_back({
            {"slot", static_cast<int>(slot)},
            {"hotbar", slot < gameplay::Inventory::kHotbarSlots},
            {"selected", inventory.GetSelectedSlot() == static_cast<int>(slot)},
            {"blockId", stack.IsEmpty() ? 0 : static_cast<int>(stack.blockId)},
            {"count", stack.IsEmpty() ? 0 : stack.count},
            {"name", stack.IsEmpty() ? std::string{} : DisplayNameForBlock(m_registry, stack.blockId)}
        });
    }

    nlohmann::json notifications = nlohmann::json::array();
    for (const HudNotification& notification : m_hudNotifications) {
        notifications.push_back({
            {"id", notification.id},
            {"text", notification.text},
            {"remaining", std::max(0.0f, notification.remainingSeconds)}
        });
    }

    nlohmann::json kRecipes = nlohmann::json::array();
    for (const gameplay::RecipeDefinition& recipe : m_craftingService.GetRecipes()) {
        nlohmann::json ingredients = nlohmann::json::array();
        for (const gameplay::RecipeIngredient& ingredient : recipe.ingredients) {
            ingredients.push_back({{"name", DisplayNameForItem(m_registry, ingredient.itemId)}, {"count", ingredient.count}});
        }
        const std::string outputName = DisplayNameForItem(m_registry, recipe.outputItemId);
        kRecipes.push_back({
            {"id", recipe.id},
            {"name", outputName},
            {"icon", recipe.icon},
            {"category", recipe.category},
            {"sort", outputName},
            {"ingredients", ingredients},
            {"output", {{"name", outputName}, {"count", recipe.outputCount}}}
        });
    }

    const RaycastHit& target = m_session.GetTarget();
    const gameplay::ItemStack& held = inventory.GetSelectedStack();
    const GamePreferences& preferences = m_session.GetPreferences();
    const BlockId targetBlockId = target.hit ? m_session.GetWorld().GetBlock(target.blockPosition)
                                             : static_cast<BlockId>(BlockType::Air);
    const char* inputMethod = "keyboard";
    if (m_inputManager != nullptr) {
        switch (m_inputManager->GetLastActiveDeviceType()) {
            case InputDeviceType::Gamepad: inputMethod = "gamepad"; break;
            case InputDeviceType::Mouse: inputMethod = "keyboard"; break;
            case InputDeviceType::Keyboard: inputMethod = "keyboard"; break;
            default: inputMethod = "keyboard"; break;
        }
    }
    const nlohmann::json payload{
        {"hotbar", std::move(hotbar)},
        {"selectedSlot", inventory.GetSelectedSlot()},
        {"inputMethod", inputMethod},
        {"heldItem", {
            {"empty", held.IsEmpty()},
            {"name", held.IsEmpty() ? std::string{} : DisplayNameForBlock(m_registry, held.blockId)},
            {"count", held.IsEmpty() ? 0 : held.count}
        }},
        {"target", {
            {"hit", target.hit},
            {"name", target.hit ? DisplayNameForBlock(m_registry, targetBlockId) : std::string{}},
            {"breakProgress", std::clamp(m_session.GetBreakProgress(), 0.0f, 1.0f)}
        }},
        {"status", {
            {"health", static_cast<int>(player.health)},
            {"heartColor", preferences.heartColor},
            {"remoteSession", m_remoteSession}
        }},
        {"chat", {{"open", m_chatOpen}}},
        {"crafting", {{"open", m_craftingOpen}, {"recipes", kRecipes}}},
        {"inventory", {{"slots", std::move(inventorySlots)}}},
        {"notifications", std::move(notifications)},
        {"crosshair", {
            {"size", std::clamp(preferences.crosshairSize, 0.5f, 2.0f)},
            {"highContrast", preferences.highContrastCrosshair},
            {"reducedMotion", preferences.reducedMotion}
        }}
    };

    m_context->ui->Publish({
        .route = PlayerUIRoute::Hud,
        .revision = ++m_hudRevision,
        .title = "HUD",
        .payload = payload.dump()
    });
    m_lastPublishedSelectedSlot = inventory.GetSelectedSlot();
    m_lastPublishedBreakProgress = m_session.GetBreakProgress();
    m_lastPublishedTargetHit = target.hit;
}

void InGameState::Render() {
    if (g_renderer == nullptr) {
        return;
    }
    const Camera camera = m_cameraOverride != nullptr ? *m_cameraOverride : m_session.GetCamera();
    g_renderer->SetCamera(camera);
    WorldTick worldTick = kInitialWorldTick;
    if (m_remoteSession && m_context != nullptr && m_context->networkClient != nullptr) {
        worldTick = m_context->networkClient->GetWorldTick();
    } else if (m_context != nullptr && m_context->networkServer != nullptr) {
        worldTick = m_context->networkServer->GetWorldTick();
    }
    const graphics::CelestialLighting celestial =
        graphics::EvaluateCelestialLighting(WorldDayTimeSeconds(worldTick), m_options.alwaysSunny);
    static_cast<void>(g_renderer->BeginFrame(
        {celestial.skyColor.r, celestial.skyColor.g, celestial.skyColor.b, 1.0f}));
    if (m_chunkRenderer) {
        m_chunkRenderer->SetCelestialLighting(celestial);
        m_chunkRenderer->SetFogRange(graphics::FogRangeForRenderDistance(m_options.renderDistanceChunks));
        if (m_skyRenderer) m_skyRenderer->Render(camera, celestial);
        m_chunkRenderer->RenderOpaque(camera);
    }
    if (m_remotePlayerRenderer && m_context != nullptr && m_context->networkClient != nullptr) {
        const networking::GameClient& client = *m_context->networkClient;
        std::vector<graphics::RemotePlayerVisual> visuals;
        for (const auto& [entityId, state] : client.ReceivedEntityStates()) {
            if (entityId == client.PlayerId()) continue;
            visuals.push_back({entityId,
                               glm::vec3{state.movement.position.x, state.movement.position.y,
                                         state.movement.position.z},
                               state.movement.rotation.x});
        }
        m_remotePlayerRenderer->Render(camera, visuals, 1.0f / 60.0f);
    }
    if (m_itemDropRenderer) {
        m_itemDropRenderer->SetCelestialLighting(celestial);
        m_itemDropRenderer->Render(camera, m_session.GetItemDrops());
    }
    if (m_chunkRenderer) m_chunkRenderer->RenderTransparent(camera);
    if (m_underwaterOverlay && m_session.IsEyeSubmerged()) {
        m_underwaterOverlay->Render(m_session.GetSubmersionFraction());
    }
    CompletePendingPreviewCapture();
    if (m_hudRenderer) {
        const bool drawNativeScreenHud =
            m_context == nullptr || m_context->ui == nullptr ||
            m_context->ui->UsesNativeRoutePresentation(PlayerUIRoute::Hud);
        m_hudRenderer->Render(camera, m_session.GetTarget(), m_session.GetBreakProgress(), m_session.GetPlayer().state.inventory,
                              m_session.GetSelectedItemLabel(), m_session.GetSelectedItemLabelAge(),
                              m_session.GetParticleBursts(), drawNativeScreenHud);
        m_session.ClearParticleBursts();
    }
}

WorldTick InGameState::GetWorldTick() const noexcept {
    if (m_remoteSession && m_context != nullptr && m_context->networkClient != nullptr) {
        return m_context->networkClient->GetWorldTick();
    }
    if (m_context != nullptr && m_context->networkServer != nullptr) {
        return m_context->networkServer->GetWorldTick();
    }
    return m_activeSave.worldTick;
}

} // namespace voxels
