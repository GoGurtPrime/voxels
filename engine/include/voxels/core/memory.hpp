#pragma once

/**
 * @file memory.hpp
 * @brief Fixed-size block pool allocator for high-frequency allocations.
 *
 * @details Provides `PoolAllocator<T>`, a fixed-capacity free-list allocator intended for
 *          hot-path allocations such as chunk and block objects, avoiding the overhead and
 *          fragmentation of general-purpose heap allocation. Not thread-safe by itself;
 *          callers needing concurrent access must synchronize externally.
 *
 *          Relation to the rest of the codebase: world/chunk streaming and block object
 *          management are expected to draw from a `PoolAllocator` sized to the platform's
 *          memory budget (e.g. a much smaller pool on Dreamcast than desktop targets).
 */

#include <cstddef>
#include <memory>
#include <type_traits>
#include <utility>
#include <vector>

namespace voxels {

template <typename T>
class PoolAllocator {
public:
    explicit PoolAllocator(std::size_t capacity)
        : m_capacity(capacity),
          m_storage(capacity > 0 ? std::make_unique<Slot[]>(capacity) : nullptr),
          m_freeList(capacity) {
        for (std::size_t i = 0; i < capacity; ++i) {
            m_freeList[i] = capacity - 1 - i;
        }
        m_freeTop = capacity;
    }

    ~PoolAllocator() = default;

    PoolAllocator(const PoolAllocator&) = delete;
    PoolAllocator& operator=(const PoolAllocator&) = delete;
    PoolAllocator(PoolAllocator&&) noexcept = default;
    PoolAllocator& operator=(PoolAllocator&&) noexcept = default;

    template <typename... Args>
    [[nodiscard]] T* Allocate(Args&&... args) {
        if (m_freeTop == 0) {
            return nullptr;
        }
        const std::size_t index = m_freeList[--m_freeTop];
        T* ptr = SlotAt(index);
        ::new (static_cast<void*>(ptr)) T(std::forward<Args>(args)...);
        return ptr;
    }

    void Deallocate(T* ptr) noexcept {
        if (ptr == nullptr) {
            return;
        }
        ptr->~T();
        m_freeList[m_freeTop++] = IndexOf(ptr);
    }

    [[nodiscard]] std::size_t Capacity() const noexcept { return m_capacity; }
    [[nodiscard]] std::size_t FreeCount() const noexcept { return m_freeTop; }
    [[nodiscard]] std::size_t UsedCount() const noexcept { return m_capacity - m_freeTop; }

private:
    using Slot = std::aligned_storage_t<sizeof(T), alignof(T)>;

    [[nodiscard]] T* SlotAt(std::size_t index) noexcept {
        return reinterpret_cast<T*>(&m_storage[index]);
    }

    [[nodiscard]] std::size_t IndexOf(T* ptr) const noexcept {
        return static_cast<std::size_t>(reinterpret_cast<Slot*>(ptr) - m_storage.get());
    }

    std::size_t m_capacity;
    std::unique_ptr<Slot[]> m_storage;
    std::vector<std::size_t> m_freeList;
    std::size_t m_freeTop = 0;
};

} // namespace voxels
