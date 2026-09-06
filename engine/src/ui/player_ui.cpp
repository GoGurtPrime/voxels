/**
 * @file player_ui.cpp
 * @brief Validated player UI protocol envelope and headless UI double implementation.
 */

#include "voxels/ui/player_ui.hpp"

#include <algorithm>

#include <nlohmann/json.hpp>

namespace voxels {
namespace {
constexpr std::size_t kMaximumPayloadBytes = 64U * 1024U;

bool IsValidEnvelope(const PlayerUIProtocolMessage& message) {
    return message.version == PlayerUIProtocolMessage::kVersion && !message.kind.empty() &&
           message.requestId != 0 && message.payload.size() <= kMaximumPayloadBytes;
}
} // namespace

std::optional<std::string> EncodePlayerUIProtocolMessage(const PlayerUIProtocolMessage& message) {
    if (!IsValidEnvelope(message)) return std::nullopt;
    return nlohmann::json{{"version", message.version}, {"kind", message.kind},
                          {"requestId", message.requestId}, {"payload", message.payload}}.dump();
}

std::optional<PlayerUIProtocolMessage> DecodePlayerUIProtocolMessage(std::string_view encoded) {
    if (encoded.size() > kMaximumPayloadBytes + 256U) return std::nullopt;
    try {
        const nlohmann::json value = nlohmann::json::parse(encoded);
        if (!value.is_object() || value.size() != 4 || !value.contains("version") ||
            !value.contains("kind") || !value.contains("requestId") || !value.contains("payload")) return std::nullopt;
        PlayerUIProtocolMessage message{.version = value.at("version").get<std::uint32_t>(),
                                        .kind = value.at("kind").get<std::string>(),
                                        .requestId = value.at("requestId").get<std::uint32_t>(),
                                        .payload = value.at("payload").get<std::string>()};
        return IsValidEnvelope(message) ? std::optional<PlayerUIProtocolMessage>{std::move(message)} : std::nullopt;
    } catch (const nlohmann::json::exception&) {
        return std::nullopt;
    }
}

bool NullPlayerUI::Initialize(IPlatform* platform, graphics::IGraphicsRenderer* renderer) {
    return m_base.Initialize(platform, renderer);
}

void NullPlayerUI::Shutdown() { m_base.Shutdown(); }
void NullPlayerUI::BeginFrame() { m_base.BeginFrame(); }
void NullPlayerUI::EndFrame() { m_base.EndFrame(); }
float NullPlayerUI::GetUIScale() const noexcept { return m_base.GetUIScale(); }
bool NullPlayerUI::IsFrameActive() const noexcept { return m_base.IsFrameActive(); }
void NullPlayerUI::OnPlatformEvent(const PlatformEvent& event) { m_base.OnPlatformEvent(event); }

void NullPlayerUI::SetInputPolicy(PlayerUIInputPolicy policy) {
    if (m_policy == policy) return;
    m_policy = policy;
    m_discardNextMouseDelta = policy == PlayerUIInputPolicy::Gameplay;
}

bool NullPlayerUI::ConsumeTransitionMouseDelta() noexcept {
    const bool discard = m_discardNextMouseDelta;
    m_discardNextMouseDelta = false;
    return discard;
}

void NullPlayerUI::Publish(PlayerUIViewModel model) { m_lastModel = std::move(model); }

std::optional<PlayerUIAction> NullPlayerUI::ConsumeAction() {
    if (m_actions.empty()) return std::nullopt;
    PlayerUIAction action = std::move(m_actions.front());
    m_actions.erase(m_actions.begin());
    return action;
}

void NullPlayerUI::ShowToast(std::string, float) {}
void NullPlayerUI::SubmitAction(PlayerUIAction action) { m_actions.push_back(std::move(action)); }

} // namespace voxels