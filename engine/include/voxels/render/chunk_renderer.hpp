#pragma once

/**
 * @file chunk_renderer.hpp
 * @brief GPU chunk mesh cache: job-scheduled meshing, budgeted uploads, and cull-and-draw.
 *
 * @details Owns the `chunkCoord -> GpuChunkMesh` cache described in ARCHITECTURE.md §6.2. Dirty
 *          chunks are meshed off the main thread via `JobSystem` (ADR-008); only GPU buffer
#include <chrono>
 *          creation/upload and drawing happen on the render thread. Reference
#include <limits>
 *          work_items/05_chunk_mesh_pipeline_and_world_rendering.md.
 */

#include <cstdint>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <glad/glad.h>
#include <glm/glm.hpp>

#include "voxels/core/job_system.hpp"
#include "voxels/render/camera.hpp"
#include "voxels/render/chunk_mesher.hpp"
#include "voxels/render/texture_atlas.hpp"
#include "voxels/world/block.hpp"
#include "voxels/world/geometry.hpp"
#include "voxels/world/world.hpp"

namespace voxels::graphics {

struct ChunkRenderMetrics {
    std::size_t loadedChunks = 0;
    std::size_t meshedChunks = 0;
    std::size_t visibleChunks = 0;
    std::size_t drawCalls = 0;
    std::size_t triangles = 0;
    std::size_t meshQueueDepth = 0;
    double lastUploadMilliseconds = 0.0;
    double lastEditMeshingMilliseconds = 0.0;
    double lastEditLatencyMilliseconds = 0.0;
    std::size_t completedEditMeshes = 0;
};

class ChunkRenderer {
public:
    ChunkRenderer(voxels::BlockRegistry& registry, voxels::TextureAtlas& atlas, voxels::JobSystem& jobSystem);
    ~ChunkRenderer();

    ChunkRenderer(const ChunkRenderer&) = delete;
    ChunkRenderer& operator=(const ChunkRenderer&) = delete;

    /// Caps GPU upload work per frame: at most `maxChunksPerFrame` uploads, and stops early once
    /// `maxMilliseconds` of upload time has been spent this call (ADR-008, §8 performance budget).
    void SetUploadBudget(std::uint32_t maxChunksPerFrame, double maxMilliseconds) noexcept;

    /// Limits queued/background mesh work so interactive edits can take an idle worker promptly.
    /// The default is unlimited for tools and tests; the desktop runtime reserves worker capacity.
    void SetBackgroundMeshQueueLimit(std::size_t maxJobs) noexcept;

    /// Marks a single chunk coordinate dirty (queued for re-mesh).
    void MarkChunkDirty(const voxels::ChunkCoordinate& coordinate);

    /// Marks the chunk owning a block edit dirty, plus any neighbour whose shared boundary the
    /// edit touched (a corner edit dirties at most 4 chunks total).
    void MarkBlockEdited(const voxels::ChunkCoordinate& coordinate, const voxels::Vec3I& localEditPos,
                         std::uint32_t chunkSize);

    /// Registers a chunk arrival or removal and invalidates only the affected chunk boundaries.
    void OnChunkArrived(const voxels::ChunkCoordinate& coordinate, const voxels::World& world);
    void OnChunkRemoved(const voxels::ChunkCoordinate& coordinate, const voxels::World& world);

    /// Enqueues bounded async mesh work for dirty resident chunks nearest to `cameraPosition`.
    void EnqueueDirtyMeshJobs(const voxels::World& world, const glm::vec3& cameraPosition);

    /// Drains completed mesh jobs and uploads them to the GPU, respecting the upload budget.
    void UploadCompletedMeshes();

    /// Frustum-culls resident meshes against `camera` and draws opaque front-to-back, then
    /// transparent back-to-front.
    void Render(const voxels::Camera& camera);

    /// Releases every GPU resource owned by this renderer.
    void Shutdown();

    [[nodiscard]] const ChunkRenderMetrics& GetMetrics() const noexcept { return m_metrics; }
    [[nodiscard]] std::size_t GetDirtyCount() const noexcept { return m_dirty.size(); }
    [[nodiscard]] bool IsDirty(const voxels::ChunkCoordinate& coordinate) const noexcept {
        return m_dirty.contains(coordinate);
    }
    [[nodiscard]] bool IsInFlight(const voxels::ChunkCoordinate& coordinate) const noexcept {
        return m_inFlight.contains(coordinate);
    }
    [[nodiscard]] std::size_t GetResidentMeshCount() const noexcept { return m_meshes.size(); }
    [[nodiscard]] bool HasMesh(const voxels::ChunkCoordinate& coordinate) const noexcept {
        return m_meshes.find(coordinate) != m_meshes.end();
    }

private:
    struct GpuChunkMesh {
        GLuint vao = 0;
        GLuint vbo = 0;
        GLuint ibo = 0;
        std::size_t vboCapacityBytes = 0;
        std::size_t iboCapacityBytes = 0;
        std::uint32_t opaqueIndexCount = 0;
        std::uint32_t transparentIndexCount = 0;
        voxels::BoundingBox aabb{};
        bool provisional = false;
    };

    struct PendingMeshResult {
        voxels::ChunkCoordinate coordinate;
        ChunkMeshData data;
        std::uint64_t revision = 0;
        bool editPriority = false;
        double meshingMilliseconds = 0.0;
    };

    void MarkChunkDirtyForEdit(const voxels::ChunkCoordinate& coordinate);

    bool EnsureProgram();
    void UploadMesh(const voxels::ChunkCoordinate& coordinate, ChunkMeshData&& data);
    static void ReleaseMesh(GpuChunkMesh& mesh);
    static void ConfigureVertexAttributes();

    voxels::BlockRegistry& m_registry;
    voxels::TextureAtlas& m_atlas;
    voxels::JobSystem& m_jobSystem;

    GLuint m_program = 0;
    GLint m_uniformViewProj = -1;
    GLint m_uniformChunkOrigin = -1;
    GLint m_uniformCameraPos = -1;
    GLint m_uniformTexture = -1;
    GLint m_uniformFoliageTint = -1;

    std::unordered_map<voxels::ChunkCoordinate, GpuChunkMesh, voxels::ChunkCoordinateHash> m_meshes;
    std::unordered_set<voxels::ChunkCoordinate, voxels::ChunkCoordinateHash> m_dirty;
    std::unordered_set<voxels::ChunkCoordinate, voxels::ChunkCoordinateHash> m_editPriority;
    std::unordered_set<voxels::ChunkCoordinate, voxels::ChunkCoordinateHash> m_backgroundInFlight;
    std::unordered_set<voxels::ChunkCoordinate, voxels::ChunkCoordinateHash> m_knownResidentChunks;
    std::unordered_set<voxels::ChunkCoordinate, voxels::ChunkCoordinateHash> m_inFlight;
    std::unordered_map<voxels::ChunkCoordinate, std::uint64_t, voxels::ChunkCoordinateHash> m_revisions;
    std::unordered_map<voxels::ChunkCoordinate, std::chrono::steady_clock::time_point,
                       voxels::ChunkCoordinateHash> m_editRequestedAt;

    std::mutex m_completedMutex;
    std::vector<PendingMeshResult> m_completed;
    std::vector<PendingMeshResult> m_deferredUploads;

    std::uint32_t m_chunkSize = 16;
    std::uint32_t m_uploadBudgetChunksPerFrame = 4;
    double m_uploadBudgetMilliseconds = 2.0;
    std::size_t m_backgroundMeshQueueLimit = std::numeric_limits<std::size_t>::max();

    ChunkRenderMetrics m_metrics;
};

} // namespace voxels::graphics
