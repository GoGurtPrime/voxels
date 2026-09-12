/**
 * @file item_drop.cpp
 * @brief Gravity/ground-rest physics and proximity pickup for ground item entities.
 */

#include "voxels/gameplay/item_drop.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <memory>
#include <string>

#include "voxels/core/logger.hpp"
#include "voxels/gameplay/physics.hpp"

namespace voxels::gameplay {
namespace {

constexpr float kCollisionEpsilon = 0.0001f;

Logger& ItemDropLog() {
    static Logger logger;
    static const bool configured = [] {
        logger.AddSink(std::make_shared<ConsoleLogSink>());
        return true;
    }();
    (void)configured;
    return logger;
}

float AxisValue(const Vec3& value, int axis) noexcept {
    if (axis == 0) return value.x;
    if (axis == 1) return value.y;
    return value.z;
}

void SetAxisValue(Vec3& value, int axis, float component) noexcept {
    if (axis == 0) value.x = component;
    else if (axis == 1) value.y = component;
    else value.z = component;
}

bool SliceContainsSolid(const World& world, const BlockRegistry* registry, const Vec3& position,
                        int axis, int slice, std::size_t& collisionQueries) {
    const int firstAxis = axis == 0 ? 1 : 0;
    const int secondAxis = axis == 2 ? 1 : 2;
    const int firstMin = static_cast<int>(std::floor(AxisValue(position, firstAxis) - ItemDropSimulation::kDropRadius + kCollisionEpsilon));
    const int firstMax = static_cast<int>(std::floor(AxisValue(position, firstAxis) + ItemDropSimulation::kDropRadius - kCollisionEpsilon));
    const int secondMin = static_cast<int>(std::floor(AxisValue(position, secondAxis) - ItemDropSimulation::kDropRadius + kCollisionEpsilon));
    const int secondMax = static_cast<int>(std::floor(AxisValue(position, secondAxis) + ItemDropSimulation::kDropRadius - kCollisionEpsilon));
    for (int first = firstMin; first <= firstMax; ++first) {
        for (int second = secondMin; second <= secondMax; ++second) {
            Vec3I cell{};
            if (axis == 0) cell = {slice, first, second};
            else if (axis == 1) cell = {first, slice, second};
            else cell = {first, second, slice};
            ++collisionQueries;
            if (Physics::IsSolidBlock(world.GetBlock(cell), registry)) return true;
        }
    }
    return false;
}

bool SweepAxis(const World& world, const BlockRegistry* registry, Vec3& position,
               Vec3& velocity, int axis, float distance, std::size_t& collisionQueries) {
    if (distance == 0.0f) return false;

    const float center = AxisValue(position, axis);
    if (distance > 0.0f) {
        const float leadingFace = center + ItemDropSimulation::kDropRadius;
        const int finalSlice = static_cast<int>(std::floor(leadingFace + distance));
        for (int slice = static_cast<int>(std::floor(leadingFace)) + 1; slice <= finalSlice; ++slice) {
            if (!SliceContainsSolid(world, registry, position, axis, slice, collisionQueries)) continue;
            SetAxisValue(position, axis, static_cast<float>(slice) - ItemDropSimulation::kDropRadius - kCollisionEpsilon);
            SetAxisValue(velocity, axis, 0.0f);
            return true;
        }
    } else {
        const float leadingFace = center - ItemDropSimulation::kDropRadius;
        const int finalSlice = static_cast<int>(std::floor(leadingFace + distance));
        for (int slice = static_cast<int>(std::ceil(leadingFace)) - 1; slice >= finalSlice; --slice) {
            if (!SliceContainsSolid(world, registry, position, axis, slice, collisionQueries)) continue;
            SetAxisValue(position, axis, static_cast<float>(slice + 1) + ItemDropSimulation::kDropRadius + kCollisionEpsilon);
            SetAxisValue(velocity, axis, 0.0f);
            return true;
        }
    }

    SetAxisValue(position, axis, center + distance);
    return false;
}

bool IsClear(const World& world, const BlockRegistry* registry, const Vec3& position,
             std::size_t& collisionQueries) {
    const int minX = static_cast<int>(std::floor(position.x - ItemDropSimulation::kDropRadius + kCollisionEpsilon));
    const int maxX = static_cast<int>(std::floor(position.x + ItemDropSimulation::kDropRadius - kCollisionEpsilon));
    const int minY = static_cast<int>(std::floor(position.y - ItemDropSimulation::kDropRadius + kCollisionEpsilon));
    const int maxY = static_cast<int>(std::floor(position.y + ItemDropSimulation::kDropRadius - kCollisionEpsilon));
    const int minZ = static_cast<int>(std::floor(position.z - ItemDropSimulation::kDropRadius + kCollisionEpsilon));
    const int maxZ = static_cast<int>(std::floor(position.z + ItemDropSimulation::kDropRadius - kCollisionEpsilon));
    for (int y = minY; y <= maxY; ++y) {
        for (int z = minZ; z <= maxZ; ++z) {
            for (int x = minX; x <= maxX; ++x) {
                ++collisionQueries;
                if (Physics::IsSolidBlock(world.GetBlock({x, y, z}), registry)) return false;
            }
        }
    }
    return true;
}

bool IsSupported(const World& world, const BlockRegistry* registry, const Vec3& position,
                 std::size_t& collisionQueries) {
    const Vec3I support{static_cast<int>(std::floor(position.x)),
                        static_cast<int>(std::floor(position.y - ItemDropSimulation::kDropRadius - kCollisionEpsilon)),
                        static_cast<int>(std::floor(position.z))};
    ++collisionQueries;
    return Physics::IsSolidBlock(world.GetBlock(support), registry);
}

bool RecoverDrop(const World& world, const BlockRegistry* registry, ItemDrop& drop,
                 std::size_t& collisionQueries) {
    const int originX = static_cast<int>(std::floor(drop.position.x));
    const int originZ = static_cast<int>(std::floor(drop.position.z));
    const int originAirY = std::clamp(static_cast<int>(std::floor(drop.position.y - ItemDropSimulation::kDropRadius)),
                                      ItemDropSimulation::kWorldMinimumY + 1,
                                      ItemDropSimulation::kWorldMaximumY - 1);
    for (int radius = 0; radius <= ItemDropSimulation::kRecoverySearchRadius; ++radius) {
        for (int zOffset = -radius; zOffset <= radius; ++zOffset) {
            for (int xOffset = -radius; xOffset <= radius; ++xOffset) {
                if (std::max(std::abs(xOffset), std::abs(zOffset)) != radius) continue;
                for (int verticalStep = 0; verticalStep <= ItemDropSimulation::kRecoveryVerticalRange; ++verticalStep) {
                    const int candidateY = originAirY + verticalStep;
                    if (candidateY >= ItemDropSimulation::kWorldMaximumY) break;
                    const int cellX = originX + xOffset;
                    const int cellZ = originZ + zOffset;
                    const float candidateX = xOffset == 0
                        ? std::clamp(drop.position.x, static_cast<float>(cellX) + ItemDropSimulation::kDropRadius + kCollisionEpsilon,
                                     static_cast<float>(cellX + 1) - ItemDropSimulation::kDropRadius - kCollisionEpsilon)
                        : static_cast<float>(cellX) + 0.5f;
                    const float candidateZ = zOffset == 0
                        ? std::clamp(drop.position.z, static_cast<float>(cellZ) + ItemDropSimulation::kDropRadius + kCollisionEpsilon,
                                     static_cast<float>(cellZ + 1) - ItemDropSimulation::kDropRadius - kCollisionEpsilon)
                        : static_cast<float>(cellZ) + 0.5f;
                    const Vec3 candidate{candidateX,
                                         static_cast<float>(candidateY) + ItemDropSimulation::kDropRadius + kCollisionEpsilon,
                                         candidateZ};
                    if (IsClear(world, registry, candidate, collisionQueries) &&
                        IsSupported(world, registry, candidate, collisionQueries)) {
                        drop.position = candidate;
                        drop.velocity = {};
                        drop.grounded = true;
                        return true;
                    }
                }
            }
        }
    }
    return false;
}

bool SqueezeFromEditedBlock(const World& world, const BlockRegistry* registry, ItemDrop& drop,
                            const Vec3I& editedBlock, std::size_t& collisionQueries) {
    struct ExitCandidate {
        float distance = 0.0f;
        Vec3 position{};
        Vec3 direction{};
        Vec3I neighboringCell{};
    };

    const float minX = static_cast<float>(editedBlock.x);
    const float minY = static_cast<float>(editedBlock.y);
    const float minZ = static_cast<float>(editedBlock.z);
    const float maxX = minX + 1.0f;
    const float maxY = minY + 1.0f;
    const float maxZ = minZ + 1.0f;
    std::array<ExitCandidate, 6> candidates{{
        {drop.position.x - minX, {minX - ItemDropSimulation::kDropRadius - kCollisionEpsilon,
                                  drop.position.y, drop.position.z}, {-1.0f, 0.0f, 0.0f},
                                  {editedBlock.x - 1, editedBlock.y, editedBlock.z}},
        {maxX - drop.position.x, {maxX + ItemDropSimulation::kDropRadius + kCollisionEpsilon,
                                  drop.position.y, drop.position.z}, {1.0f, 0.0f, 0.0f},
                                  {editedBlock.x + 1, editedBlock.y, editedBlock.z}},
        {drop.position.y - minY, {drop.position.x,
                                  minY - ItemDropSimulation::kDropRadius - kCollisionEpsilon,
                                  drop.position.z}, {0.0f, -1.0f, 0.0f},
                                  {editedBlock.x, editedBlock.y - 1, editedBlock.z}},
        {maxY - drop.position.y, {drop.position.x,
                                  maxY + ItemDropSimulation::kDropRadius + kCollisionEpsilon,
                                  drop.position.z}, {0.0f, 1.0f, 0.0f},
                                  {editedBlock.x, editedBlock.y + 1, editedBlock.z}},
        {drop.position.z - minZ, {drop.position.x, drop.position.y,
                                  minZ - ItemDropSimulation::kDropRadius - kCollisionEpsilon},
                                  {0.0f, 0.0f, -1.0f},
                                  {editedBlock.x, editedBlock.y, editedBlock.z - 1}},
        {maxZ - drop.position.z, {drop.position.x, drop.position.y,
                                  maxZ + ItemDropSimulation::kDropRadius + kCollisionEpsilon},
                                  {0.0f, 0.0f, 1.0f},
                                  {editedBlock.x, editedBlock.y, editedBlock.z + 1}},
    }};
    std::stable_sort(candidates.begin(), candidates.end(), [](const ExitCandidate& left,
                                                               const ExitCandidate& right) {
        return left.distance < right.distance;
    });
    for (const ExitCandidate& candidate : candidates) {
        ++collisionQueries;
        if (candidate.position.y - ItemDropSimulation::kDropRadius <
                static_cast<float>(ItemDropSimulation::kWorldMinimumY) ||
            candidate.position.y + ItemDropSimulation::kDropRadius >=
                static_cast<float>(ItemDropSimulation::kWorldMaximumY) ||
            Physics::IsSolidBlock(world.GetBlock(candidate.neighboringCell), registry) ||
            !IsClear(world, registry, candidate.position, collisionQueries)) {
            continue;
        }
        drop.position = candidate.position;
        drop.velocity = {candidate.direction.x * ItemDropSimulation::kRecoveryImpulse,
                         candidate.direction.y * ItemDropSimulation::kRecoveryImpulse,
                         candidate.direction.z * ItemDropSimulation::kRecoveryImpulse};
        drop.grounded = false;
        return true;
    }
    return false;
}

bool OutsideWorld(const ItemDrop& drop) noexcept {
    return drop.position.y - ItemDropSimulation::kDropRadius < static_cast<float>(ItemDropSimulation::kWorldMinimumY) ||
           drop.position.y + ItemDropSimulation::kDropRadius >= static_cast<float>(ItemDropSimulation::kWorldMaximumY);
}

bool ApplyMagnet(ItemDrop& drop, const Vec3& target, float deltaSeconds) {
    if (drop.pickupDelay > 0.0f) return false;
    const float offsetX = target.x - drop.position.x;
    const float offsetZ = target.z - drop.position.z;
    if (std::abs(offsetX) > ItemDropSimulation::kMagnetRadius ||
        std::abs(offsetZ) > ItemDropSimulation::kMagnetRadius) return false;
    const float distanceSquared = offsetX * offsetX + offsetZ * offsetZ;
    if (distanceSquared >= ItemDropSimulation::kMagnetRadius * ItemDropSimulation::kMagnetRadius) return false;

    const float distance = std::sqrt(distanceSquared);
    float desiredVelocityX = 0.0f;
    float desiredVelocityZ = 0.0f;
    if (distance > 0.000001f) {
        const float ramp = std::clamp((ItemDropSimulation::kMagnetRadius - distance) /
                                      ItemDropSimulation::kMagnetRampDistance, 0.0f, 1.0f);
        const float strength = ramp * ramp * (3.0f - 2.0f * ramp);
        const float arrival = std::min(1.0f, distance / ItemDropSimulation::kPickupRadius);
        const float desiredSpeed = ItemDropSimulation::kMaximumMagnetSpeed * strength * arrival;
        desiredVelocityX = offsetX / distance * desiredSpeed;
        desiredVelocityZ = offsetZ / distance * desiredSpeed;
    }
    const float response = 1.0f - std::exp(-ItemDropSimulation::kMagnetResponse * deltaSeconds);
    drop.velocity.x += (desiredVelocityX - drop.velocity.x) * response;
    drop.velocity.z += (desiredVelocityZ - drop.velocity.z) * response;
    return true;
}

} // namespace

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

void ItemDropSimulation::Update(const World& world, const BlockRegistry* registry, float deltaSeconds,
                                const Vec3* magnetTarget) {
    Update(world, registry, deltaSeconds,
           magnetTarget == nullptr ? std::span<const Vec3>{} : std::span<const Vec3>{magnetTarget, 1});
}

void ItemDropSimulation::Update(const World& world, const BlockRegistry* registry, float deltaSeconds,
                                std::span<const Vec3> magnetTargets) {
    if (deltaSeconds <= 0.0f) return;
    const auto updateStart = std::chrono::steady_clock::now();
    m_metrics.collisionQueries = 0;
    m_metrics.updateMicroseconds = 0.0;
    for (auto& drop : m_drops) {
        drop.age += deltaSeconds;
        drop.pickupDelay = std::max(0.0f, drop.pickupDelay - deltaSeconds);

        if (OutsideWorld(drop)) {
            if (!RecoverDrop(world, registry, drop, m_metrics.collisionQueries)) {
                ItemDropLog().Warn("Despawning unreachable item drop " + std::to_string(drop.id) +
                                   " after bounded recovery search");
                drop.age = kDespawnSeconds;
                ++m_metrics.despawnedDrops;
                continue;
            }
            ++m_metrics.recoveredDrops;
        }

        const Vec3* nearestTarget = nullptr;
        float nearestDistanceSquared = kMagnetRadius * kMagnetRadius;
        for (const Vec3& target : magnetTargets) {
            const float dx = target.x - drop.position.x;
            const float dz = target.z - drop.position.z;
            const float targetFeetY = target.y - Physics::kPlayerHalfHeight + kDropRadius;
            if (std::abs(dx) > kMagnetRadius || std::abs(targetFeetY - drop.position.y) > kMagnetRadius ||
                std::abs(dz) > kMagnetRadius) continue;
            const float distanceSquared = dx * dx + dz * dz;
            if (distanceSquared < nearestDistanceSquared) {
                nearestDistanceSquared = distanceSquared;
                nearestTarget = &target;
            }
        }
        const bool magnetized = nearestTarget != nullptr && ApplyMagnet(drop, *nearestTarget, deltaSeconds);

        const bool movedHorizontally = drop.velocity.x != 0.0f || drop.velocity.z != 0.0f;
        const bool hitX = SweepAxis(world, registry, drop.position, drop.velocity, 0,
                                    drop.velocity.x * deltaSeconds, m_metrics.collisionQueries);
        const bool hitZ = SweepAxis(world, registry, drop.position, drop.velocity, 2,
                                    drop.velocity.z * deltaSeconds, m_metrics.collisionQueries);
        const bool climbingObstacle = magnetized && (hitX || hitZ);
        const float targetFeetY = nearestTarget == nullptr
            ? drop.position.y
            : nearestTarget->y - Physics::kPlayerHalfHeight + kDropRadius;
        const bool liftingFromBelow = magnetized && nearestDistanceSquared <= kPickupRadius * kPickupRadius &&
                                      drop.position.y + kCollisionEpsilon < targetFeetY;
        if (climbingObstacle || liftingFromBelow) drop.grounded = false;
        if (drop.grounded && movedHorizontally &&
            !IsSupported(world, registry, drop.position, m_metrics.collisionQueries)) {
            drop.grounded = false;
        }
        if (!drop.grounded) {
            if (climbingObstacle) {
                drop.velocity.y = std::max(drop.velocity.y, kObstacleClimbSpeed);
            } else if (liftingFromBelow) {
                drop.velocity.y = std::min(kObstacleClimbSpeed,
                                           (targetFeetY - drop.position.y) / deltaSeconds);
            } else {
                drop.velocity.y = std::max(drop.velocity.y - ItemDropSimulation::kGravity * deltaSeconds,
                                           -ItemDropSimulation::kTerminalVelocity);
            }
            const bool descending = drop.velocity.y <= 0.0f;
            const bool hitVertical = SweepAxis(world, registry, drop.position, drop.velocity, 1,
                                               drop.velocity.y * deltaSeconds, m_metrics.collisionQueries);
            drop.grounded = descending && hitVertical;
        }
        if (drop.grounded) {
            const float decay = std::exp(-kGroundFriction * deltaSeconds);
            drop.velocity.x *= decay;
            drop.velocity.z *= decay;
        }
    }

    std::erase_if(m_drops, [](const ItemDrop& drop) { return drop.age >= ItemDropSimulation::kDespawnSeconds; });
    m_metrics.activeDrops = m_drops.size();
    m_metrics.updateMicroseconds = std::chrono::duration<double, std::micro>(
        std::chrono::steady_clock::now() - updateStart).count();
}

void ItemDropSimulation::ResolveAfterBlockEdit(const World& world, const BlockRegistry* registry,
                                               const Vec3I& editedBlock) {
    const float blockMinX = static_cast<float>(editedBlock.x);
    const float blockMinY = static_cast<float>(editedBlock.y);
    const float blockMinZ = static_cast<float>(editedBlock.z);
    for (ItemDrop& drop : m_drops) {
        const bool supportedByEditedBlock = drop.grounded &&
            drop.position.x + kDropRadius > blockMinX && drop.position.x - kDropRadius < blockMinX + 1.0f &&
            std::abs((drop.position.y - kDropRadius) - (blockMinY + 1.0f)) <= kCollisionEpsilon * 2.0f &&
            drop.position.z + kDropRadius > blockMinZ && drop.position.z - kDropRadius < blockMinZ + 1.0f;
        if (supportedByEditedBlock && !Physics::IsSolidBlock(world.GetBlock(editedBlock), registry)) {
            drop.grounded = false;
        }
        const bool overlaps = drop.position.x + kDropRadius > blockMinX &&
                              drop.position.x - kDropRadius < blockMinX + 1.0f &&
                              drop.position.y + kDropRadius > blockMinY &&
                              drop.position.y - kDropRadius < blockMinY + 1.0f &&
                              drop.position.z + kDropRadius > blockMinZ &&
                              drop.position.z - kDropRadius < blockMinZ + 1.0f;
        if (!overlaps || IsClear(world, registry, drop.position, m_metrics.collisionQueries)) continue;
        if (SqueezeFromEditedBlock(world, registry, drop, editedBlock, m_metrics.collisionQueries) ||
            RecoverDrop(world, registry, drop, m_metrics.collisionQueries)) {
            ++m_metrics.recoveredDrops;
        } else {
            ItemDropLog().Warn("Despawning unreachable item drop " + std::to_string(drop.id) +
                               " after authoritative block edit");
            drop.age = kDespawnSeconds;
            ++m_metrics.despawnedDrops;
        }
    }
    std::erase_if(m_drops, [](const ItemDrop& drop) { return drop.age >= ItemDropSimulation::kDespawnSeconds; });
    m_metrics.activeDrops = m_drops.size();
}

std::vector<ItemStack> ItemDropSimulation::CollectPickups(const Vec3& playerPosition, Inventory& inventory) {
    std::vector<ItemStack> collected;
    std::erase_if(m_drops, [&](ItemDrop& drop) {
        if (drop.pickupDelay > 0.0f) return false;
        const float dx = drop.position.x - playerPosition.x;
        const float dy = drop.position.y - playerPosition.y;
        const float dz = drop.position.z - playerPosition.z;
        if (std::abs(dx) > kPickupRadius || std::abs(dy) > kPickupRadius || std::abs(dz) > kPickupRadius) return false;
        const float distanceSquared = dx * dx + dy * dy + dz * dz;
        if (distanceSquared > kPickupRadius * kPickupRadius) return false;
        const int previousCount = drop.stack.count;
        drop.stack.count = inventory.AddItem(drop.stack.blockId, drop.stack.count);
        const int acceptedCount = previousCount - drop.stack.count;
        if (acceptedCount > 0) collected.push_back({drop.stack.blockId, acceptedCount});
        return drop.stack.IsEmpty();
    });
    return collected;
}

} // namespace voxels::gameplay
