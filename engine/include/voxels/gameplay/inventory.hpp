/**
 * @file inventory.hpp
 * @brief Fixed-size player inventory with hotbar selection and stack operations.
 *
 * @details Provides the serializable gameplay inventory used by block interaction and future
 *          persistence. The first nine slots form the hotbar described in ARCHITECTURE.md §6.5.
 */

#pragma once

#include <array>
#include <algorithm>
#include <cstddef>

#include "voxels/world/block.hpp"

namespace voxels::gameplay {

struct ItemStack {
    BlockId blockId = static_cast<BlockId>(BlockType::Air);
    int count = 0;

    [[nodiscard]] bool IsEmpty() const noexcept { return count <= 0 || blockId == static_cast<BlockId>(BlockType::Air); }
    [[nodiscard]] bool operator==(const ItemStack&) const noexcept = default;
};

class Inventory {
public:
    static constexpr std::size_t kHotbarSlots = 9;
    static constexpr std::size_t kMainSlots = 27;
    static constexpr std::size_t kSlotCount = kHotbarSlots + kMainSlots;
    static constexpr int kStackLimit = 64;

    [[nodiscard]] int AddItem(BlockId blockId, int count) noexcept {
        if (blockId == static_cast<BlockId>(BlockType::Air) || count <= 0) return count;
        for (auto& stack : m_slots) {
            if (stack.blockId == blockId && stack.count < kStackLimit) {
                const int added = std::min(count, kStackLimit - stack.count);
                stack.count += added;
                count -= added;
                if (count == 0) return 0;
            }
        }
        for (auto& stack : m_slots) {
            if (stack.IsEmpty()) {
                const int added = std::min(count, kStackLimit);
                stack = {blockId, added};
                count -= added;
                if (count == 0) return 0;
            }
        }
        return count;
    }

    [[nodiscard]] bool RemoveItem(std::size_t slot, int count) noexcept {
        if (slot >= kSlotCount || count <= 0 || m_slots[slot].count < count) return false;
        auto& stack = m_slots[slot];
        stack.count -= count;
        if (stack.count == 0) stack = {};
        return true;
    }

    [[nodiscard]] const ItemStack& GetSlot(std::size_t slot) const noexcept { return m_slots[std::min(slot, kSlotCount - 1)]; }
    [[nodiscard]] ItemStack& GetSlot(std::size_t slot) noexcept { return m_slots[std::min(slot, kSlotCount - 1)]; }
    [[nodiscard]] int GetSelectedSlot() const noexcept { return m_selectedSlot; }
    void SetSelectedSlot(int slot) noexcept { m_selectedSlot = std::clamp(slot, 0, static_cast<int>(kHotbarSlots) - 1); }
    void CycleSelectedSlot(int delta) noexcept {
        constexpr int slots = static_cast<int>(kHotbarSlots);
        m_selectedSlot = ((m_selectedSlot + delta) % slots + slots) % slots;
    }
    [[nodiscard]] const ItemStack& GetSelectedStack() const noexcept { return m_slots[static_cast<std::size_t>(m_selectedSlot)]; }
    [[nodiscard]] std::array<ItemStack, kSlotCount>& Slots() noexcept { return m_slots; }
    [[nodiscard]] const std::array<ItemStack, kSlotCount>& Slots() const noexcept { return m_slots; }
    [[nodiscard]] bool operator==(const Inventory&) const noexcept = default;

private:
    std::array<ItemStack, kSlotCount> m_slots{};
    int m_selectedSlot = 0;
};

} // namespace voxels::gameplay