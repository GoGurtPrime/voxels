/**
 * @file test_web_ui_compositor.cpp
 * @brief Tests bounded CEF paint handoff semantics.
 */

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <array>
#include <vector>

#include "voxels/ui/web_ui_compositor.hpp"

TEST_CASE("WebUiCompositor.ResolvesLogicalViewAndPhysicalPaintScale", "[web-ui][WebUiCompositor]") {
    const voxels::WebUiSurfaceMetrics highDpi =
        voxels::ResolveWebUiSurfaceMetrics(1280, 720, 1920, 1080);
    REQUIRE(highDpi.viewWidth == 1280);
    REQUIRE(highDpi.viewHeight == 720);
    REQUIRE(highDpi.deviceScaleFactor == Catch::Approx(1.5f));

    const voxels::WebUiSurfaceMetrics transient =
        voxels::ResolveWebUiSurfaceMetrics(1280, 720, 1920, 720);
    REQUIRE(transient.viewWidth == 1280);
    REQUIRE(transient.viewHeight == 720);
    REQUIRE(transient.deviceScaleFactor == Catch::Approx(1.0f));
}

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

TEST_CASE("WebUiCompositor.DefaultBudgetAcceptsFullscreenSurface", "[web-ui][WebUiCompositor]") {
    constexpr int kWidth = 1920;
    constexpr int kHeight = 1080;
    const std::size_t byteCount = static_cast<std::size_t>(kWidth) * static_cast<std::size_t>(kHeight) * 4U;
    REQUIRE(voxels::WebUiFrameQueue::kDefaultMaximumPaintBytes >= byteCount);

    voxels::WebUiFrameQueue queue;
    std::vector<std::uint8_t> pixels(byteCount, 0U);
    REQUIRE(queue.Submit(kWidth, kHeight, pixels, {}));
    REQUIRE(queue.ConsumeLatest().has_value());
}
