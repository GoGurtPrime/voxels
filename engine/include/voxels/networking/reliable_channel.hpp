/**
 * @file reliable_channel.hpp
 * @brief Acknowledgement-window tracking for reliable UDP messages.
 *
 * @details Keeps reliable payloads pending until a remote acknowledgement confirms delivery.
 *          It is transport independent so GameClient and GameServer can use the same loss and
 *          duplicate handling policy described by DIAGRAMS.md section 12.
 */

#pragma once

#include <chrono>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace voxels::networking {

struct ReliableMessage {
    std::uint32_t sequence = 0;
    std::vector<std::uint8_t> payload;
    std::uint32_t attempts = 0;
};

class ReliableChannel {
public:
    [[nodiscard]] std::uint32_t Queue(std::vector<std::uint8_t> payload,
                                      std::chrono::steady_clock::time_point now);
    void Acknowledge(std::uint32_t acknowledgement, std::uint32_t acknowledgementBits);
    [[nodiscard]] bool Observe(std::uint32_t sequence) noexcept;
    [[nodiscard]] std::uint32_t LatestReceived() const noexcept { return m_latestReceived; }
    [[nodiscard]] std::uint32_t ReceivedAcknowledgementBits() const noexcept { return m_receivedBits; }
    [[nodiscard]] std::vector<ReliableMessage> CollectDue(
        std::chrono::steady_clock::time_point now, std::chrono::milliseconds resendAfter);
    [[nodiscard]] std::size_t PendingCount() const noexcept { return m_pending.size(); }

private:
    struct PendingMessage {
        ReliableMessage message;
        std::chrono::steady_clock::time_point lastSent;
    };

    [[nodiscard]] static bool IsMoreRecent(std::uint32_t candidate, std::uint32_t reference) noexcept;
    [[nodiscard]] static bool IsAcknowledged(std::uint32_t sequence, std::uint32_t acknowledgement,
                                              std::uint32_t acknowledgementBits) noexcept;

    std::uint32_t m_nextSequence = 1;
    std::uint32_t m_latestReceived = 0;
    std::uint32_t m_receivedBits = 0;
    std::unordered_map<std::uint32_t, PendingMessage> m_pending;
};

} // namespace voxels::networking