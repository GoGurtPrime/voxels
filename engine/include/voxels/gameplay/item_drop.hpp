#pragma once

/**
 * @file item_drop.hpp
 * @brief Ground-visible dropped item entities: simple gravity physics and proximity pickup.
 *
 * @details Item drops are spawned when a block breaks (see GameSession::Update) or when the
 *          player manually drops a stack from the hotbar/inventory. They fall under gravity,
 *          rest on the first solid block beneath them, and are collected into the local
 *          inventory once a player stands within pickup radius. Reference ARCHITECTURE.md §6.4;
 *          this is a GameSession-local simulation (see work item completion report for the
 *          multiplayer replication scope note).
 */

#include <cstdint>
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

/// Owns the set of currently active item drops for one GameSession/world and steps their
/// physics and lifetime. Not thread-safe; drive it from the main simulation thread only.
class ItemDropSimulation {
public:
    static constexpr float kPickupRadius = 1.4f;        ///< Blocks; matches the player's reach for casual pickup.
    static constexpr float kDespawnSeconds = 300.0f;     ///< Ground items vanish after 5 minutes.
    static constexpr float kGravity = 18.0f;             ///< Blocks/s^2, slightly gentler than player gravity.
    static constexpr float kTerminalVelocity = 30.0f;
    static constexpr float kGroundFriction = 6.0f;       ///< Exponential horizontal velocity decay once grounded.
    static constexpr float kDropRadius = 0.18f;          ///< Half-extent used for ground collision.

    /// Spawns one drop entity; `pickupDelay` defers CollectPickups eligibility (manual drops use
    /// a short delay so the dropping player doesn't instantly re-collect their own item).
    std::uint32_t Spawn(const ItemStack& stack, const Vec3& position, const Vec3& velocity, float pickupDelay = 0.0f);

    /// Advances physics and lifetime for every drop by `deltaSeconds`; removes despawned drops.
    void Update(const World& world, const BlockRegistry* registry, float deltaSeconds);

    /// Removes and returns every drop within pickup radius of `playerPosition` whose pickup
    /// delay has elapsed. Caller is responsible for granting the stacks to an inventory.
    [[nodiscard]] std::vector<ItemStack> CollectPickups(const Vec3& playerPosition);

    [[nodiscard]] const std::vector<ItemDrop>& Drops() const noexcept { return m_drops; }
    void Clear() noexcept { m_drops.clear(); }

private:
    std::vector<ItemDrop> m_drops;
    std::uint32_t m_nextId = 1;
};

} // namespace voxels::gameplay
