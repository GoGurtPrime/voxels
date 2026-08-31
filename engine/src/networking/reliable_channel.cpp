/**
 * @file reliable_channel.cpp
 * @brief Acknowledgement-window implementation for reliable UDP messages.
 *
 * @details Uses a 32-packet acknowledgement bitfield, which bounds connection state while
 *          tolerating reordered datagrams and resending only messages that remain unconfirmed.
 */

#include "voxels/networking/reliable_channel.hpp"

#include <algorithm>

namespace voxels::networking {

std::uint32_t ReliableChannel::Queue(std::vector<std::uint8_t> payload,
                                     std::chrono::steady_clock::time_point now) {
    const std::uint32_t sequence = m_nextSequence++;
    m_pending.emplace(sequence, PendingMessage{{sequence, std::move(payload), 0}, now});
    return sequence;
}

void ReliableChannel::Acknowledge(std::uint32_t acknowledgement, std::uint32_t acknowledgementBits) {
    std::erase_if(m_pending, [acknowledgement, acknowledgementBits](const auto& entry) {
        return IsAcknowledged(entry.first, acknowledgement, acknowledgementBits);
    });
}

bool ReliableChannel::Observe(std::uint32_t sequence) noexcept {
    if (sequence == 0) return false;
    if (m_latestReceived == 0) {
        m_latestReceived = sequence;
        m_receivedBits = 0;
        return true;
    }
    if (IsMoreRecent(sequence, m_latestReceived)) {
        const std::uint32_t distance = sequence - m_latestReceived;
        m_receivedBits = distance >= 32 ? 0U : (m_receivedBits << distance);
        m_receivedBits |= 1U << std::min(distance - 1U, 31U);
        m_latestReceived = sequence;
        return true;
    }
    const std::uint32_t distance = m_latestReceived - sequence;
    if (distance == 0 || distance > 32 || (m_receivedBits & (1U << (distance - 1U))) != 0U) return false;
    m_receivedBits |= 1U << (distance - 1U);
    return true;
}

std::vector<ReliableMessage> ReliableChannel::CollectDue(
    std::chrono::steady_clock::time_point now, std::chrono::milliseconds resendAfter) {
    std::vector<ReliableMessage> due;
    for (auto& [sequence, pending] : m_pending) {
        (void)sequence;
        if (pending.message.attempts == 0 || now - pending.lastSent >= resendAfter) {
            ++pending.message.attempts;
            pending.lastSent = now;
            due.push_back(pending.message);
        }
    }
    return due;
}

bool ReliableChannel::IsMoreRecent(std::uint32_t candidate, std::uint32_t reference) noexcept {
    return candidate != reference && static_cast<std::int32_t>(candidate - reference) > 0;
}

bool ReliableChannel::IsAcknowledged(std::uint32_t sequence, std::uint32_t acknowledgement,
                                     std::uint32_t acknowledgementBits) noexcept {
    if (sequence == acknowledgement) return true;
    const std::uint32_t distance = acknowledgement - sequence;
    return distance >= 1 && distance <= 32 && (acknowledgementBits & (1U << (distance - 1U))) != 0U;
}

} // namespace voxels::networking