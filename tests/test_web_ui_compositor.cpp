/**
 * @file test_web_ui_compositor.cpp
 * @brief Tests bounded CEF paint handoff semantics.
 */

#include <catch2/catch_test_macros.hpp>

#include <array>

#include "voxels/ui/web_ui_compositor.hpp"

TEST_CASE("WebUiCompositor.ClipsDirtyRegionsAndDropsSupersededFrames", "[web-ui][WebUiCompositor]") {
    voxels::WebUiFrameQueue queue(64U);
    const std::array<std::uint8_t, 16> firstPixels{};
    const std::array<std::uint8_t, 16> secondPixels{1U};
    const std::array<voxels::WebUiDirtyRect, 2> dirtyRects{{{-1, -1, 3, 3}, {5, 5, 2, 2}}};

    REQUIRE(queue.Submit(2, 2, firstPixels, dirtyRects));
    REQUIRE(queue.RetainedPaintBytes() == firstPixels.size());
    REQUIRE(queue.Submit(2, 2, secondPixels, std::span<const voxels::WebUiDirtyRect>{}));
    REQUIRE(queue.DroppedFrameCount() == 1U);

    const auto frame = queue.ConsumeLatest();
    REQUIRE(frame.has_value());
    REQUIRE(frame->bgraPixels == std::vector<std::uint8_t>(secondPixels.begin(), secondPixels.end()));
    REQUIRE(frame->dirtyRects.size() == 1U);
    REQUIRE(frame->dirtyRects.front().width == 2);
    REQUIRE(frame->dirtyRects.front().height == 2);
    REQUIRE(queue.RetainedPaintBytes() == 0U);
}

TEST_CASE("WebUiCompositor.RejectsPaintBeyondTheConfiguredBudget", "[web-ui][WebUiCompositor]") {
    voxels::WebUiFrameQueue queue(15U);
    const std::array<std::uint8_t, 16> pixels{};
    REQUIRE_FALSE(queue.Submit(2, 2, pixels, {}));
    REQUIRE_FALSE(queue.ConsumeLatest().has_value());
}