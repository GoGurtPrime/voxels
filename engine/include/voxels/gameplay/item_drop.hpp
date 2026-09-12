#pragma once

/**
 * @file item_drop.hpp
 * @brief Server-owned dropped item collision, recovery, magnetism, and pickup.
 *
 * @details Item drops are spawned when a block breaks (see GameSession::Update) or when the
 *          player manually drops a stack from the hotbar/inventory. They use swept collision,
 *          recover deterministically from edited terrain, and magnetize toward eligible players.
 *          Inventory insertion and entity removal are one transaction. Reference
 *          ARCHITECTURE.md §6.4.
 */

#include <cstdint>
#include <span>
#include <vector>

#include "voxels/core/math.hpp"
#include "voxels/gameplay/inventory.hpp"
#include "voxels/world/block.hpp"
#include "voxels/world/world.hpp"

namespace voxels::gameplay {

/// One floating item entity resting or falling in the world.
struct ItemDrop {
    std::uint32_t id = 0;
    ItemStack stack{};
    Vec3 position{};
    Vec3 velocity{};
    float age = 0.0f;
    /// Seconds before this drop may be picked up; used to stop a manually dropped stack from
    /// being re-collected on the same frame it was thrown.
    float pickupDelay = 0.0f;
    bool grounded = false;
};

/// Measurements from the most recent fixed simulation update.
struct ItemDropSimulationMetrics {
    std::size_t activeDrops = 0;
    std::size_t collisionQueries = 0;
    std::size_t recoveredDrops = 0;
    std::size_t despawnedDrops = 0;
    double updateMicroseconds = 0.0;
};

/// Owns the set of currently active item drops for one GameSession/world and steps their
/// physics and lifetime. Not thread-safe; drive it from the main simulation thread only.
class ItemDropSimulation {
public:
    static constexpr float kPickupRadius = 1.1f;        ///< Inner radius where inventory insertion is attempted.
    static constexpr float kMagnetRadius = 2.25f;       ///< Horizontal radius where eligible drops begin steering.
    static constexpr float kMagnetRampDistance = 0.75f; ///< Distance over which magnetic pull ramps to full strength.
    static constexpr float kMagnetResponse = 18.0f;     ///< Horizontal velocity response rate, in 1/s.
    static constexpr float kMaximumMagnetSpeed = 5.0f;
    static constexpr float kObstacleClimbSpeed = 3.5f;  ///< Upward speed while horizontal pull is blocked.
    static constexpr float kRecoveryImpulse = 2.5f;     ///< Outward speed when an edit squeezes a drop free.
    static constexpr float kDespawnSeconds = 300.0f;     ///< Ground items vanish after 5 minutes.
    static constexpr float kGravity = 18.0f;             ///< Blocks/s^2, slightly gentler than player gravity.
    static constexpr float kTerminalVelocity = 30.0f;
    static constexpr float kGroundFriction = 6.0f;       ///< Exponential horizontal velocity decay once grounded.
    static constexpr float kDropRadius = 0.18f;          ///< Half-extent used for ground collision.
    static constexpr int kWorldMinimumY = 0;
    static constexpr int kWorldMaximumY = 256;
    static constexpr int kRecoverySearchRadius = 3;
    static constexpr int kRecoveryVerticalRange = 8;

    /// Spawns one drop entity; `pickupDelay` defers CollectPickups eligibility (manual drops use
    /// a short delay so the dropping player doesn't instantly re-collect their own item).
    std::uint32_t Spawn(const ItemStack& stack, const Vec3& position, const Vec3& velocity, float pickupDelay = 0.0f);

    /// Advances collision, recovery, magnetism, and lifetime. `magnetTarget` is null when no
    /// authoritative player is eligible to attract these drops.
    void Update(const World& world, const BlockRegistry* registry, float deltaSeconds,
                const Vec3* magnetTarget = nullptr);
    /// Multi-player form; each drop steers toward its nearest eligible player.
    void Update(const World& world, const BlockRegistry* registry, float deltaSeconds,
                std::span<const Vec3> magnetTargets);

    /// Revalidates only drops overlapping an edited block, immediately recovering or removing
    /// entities embedded by authoritative terrain changes.
    void ResolveAfterBlockEdit(const World& world, const BlockRegistry* registry,
                               const Vec3I& editedBlock);

    /// Inserts eligible nearby stacks and erases only the amount accepted by `inventory`.
    /// Returns accepted stack fragments; a full inventory leaves the original drop intact.
    [[nodiscard]] std::vector<ItemStack> CollectPickups(const Vec3& playerPosition, Inventory& inventory);

    [[nodiscard]] const std::vector<ItemDrop>& Drops() const noexcept { return m_drops; }
    [[nodiscard]] const ItemDropSimulationMetrics& GetMetrics() const noexcept { return m_metrics; }
    void Clear() noexcept { m_drops.clear(); m_nextId = 1; m_metrics = {}; }

private:
    std::vector<ItemDrop> m_drops;
    std::uint32_t m_nextId = 1;
    ItemDropSimulationMetrics m_metrics{};
};

} // namespace voxels::gameplay
