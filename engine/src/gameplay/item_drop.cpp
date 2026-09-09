/**
 * @file item_drop.cpp
 * @brief Gravity/ground-rest physics and proximity pickup for ground item entities.
 */

#include "voxels/gameplay/item_drop.hpp"

#include <algorithm>
#include <cmath>

#include "voxels/gameplay/physics.hpp"

namespace voxels::gameplay {

std::uint32_t ItemDropSimulation::Spawn(const ItemStack& stack, const Vec3& position, const Vec3& velocity,
                                        float pickupDelay) {
    if (stack.IsEmpty()) return 0;
    ItemDrop drop;
    drop.id = m_nextId++;
    drop.stack = stack;
    drop.position = position;
    drop.velocity = velocity;
    drop.pickupDelay = std::max(0.0f, pickupDelay);
    m_drops.push_back(drop);
    return drop.id;
}

void ItemDropSimulation::Update(const World& world, const BlockRegistry* registry, float deltaSeconds) {
    if (deltaSeconds <= 0.0f) return;
    for (auto& drop : m_drops) {
        drop.age += deltaSeconds;
        drop.pickupDelay = std::max(0.0f, drop.pickupDelay - deltaSeconds);

        drop.velocity.y = std::max(drop.velocity.y - ItemDropSimulation::kGravity * deltaSeconds,
                                   -ItemDropSimulation::kTerminalVelocity);
        Vec3 next{
            drop.position.x + drop.velocity.x * deltaSeconds,
            drop.position.y + drop.velocity.y * deltaSeconds,
            drop.position.z + drop.velocity.z * deltaSeconds,
        };

        const Vec3I belowCell{static_cast<int>(std::floor(next.x)), static_cast<int>(std::floor(next.y - kDropRadius)),
                              static_cast<int>(std::floor(next.z))};
        const bool supported = drop.velocity.y <= 0.0f && Physics::IsSolidBlock(world.GetBlock(belowCell), registry);
        if (supported) {
            next.y = static_cast<float>(belowCell.y + 1) + kDropRadius;
            drop.velocity.y = 0.0f;
            const float decay = std::exp(-kGroundFriction * deltaSeconds);
            drop.velocity.x *= decay;
            drop.velocity.z *= decay;
            drop.grounded = true;
        } else {
            drop.grounded = false;
        }

        drop.position = next;
    }

    std::erase_if(m_drops, [](const ItemDrop& drop) { return drop.age >= ItemDropSimulation::kDespawnSeconds; });
}

std::vector<ItemStack> ItemDropSimulation::CollectPickups(const Vec3& playerPosition) {
    std::vector<ItemStack> collected;
    std::erase_if(m_drops, [&](const ItemDrop& drop) {
        if (drop.pickupDelay > 0.0f) return false;
        const float dx = drop.position.x - playerPosition.x;
        const float dy = drop.position.y - playerPosition.y;
        const float dz = drop.position.z - playerPosition.z;
        const float distanceSquared = dx * dx + dy * dy + dz * dz;
        if (distanceSquared > kPickupRadius * kPickupRadius) return false;
        collected.push_back(drop.stack);
        return true;
    });
    return collected;
}

} // namespace voxels::gameplay
