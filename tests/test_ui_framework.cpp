/**
 * @file test_ui_framework.cpp
 * @brief Behavioral tests for UI scale, input arbitration, and theme tokens.
 */

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <imgui.h>

#include "voxels/ui/imgui_ui_manager.hpp"

TEST_CASE("UIScale.ComputesExpectedFactorsAcrossResolutions", "[ui]") {
    const auto scaleFor = [](int width, int height) {
        return voxels::ComputeUIScale({width, height, 1.0f});
    };
    REQUIRE(scaleFor(640, 480) == Catch::Approx(1.0f));
    REQUIRE(scaleFor(1280, 720) == Catch::Approx(1.5f));
    REQUIRE(scaleFor(1920, 1080) == Catch::Approx(2.25f));
    REQUIRE(scaleFor(3840, 2160) == Catch::Approx(4.5f));
}

TEST_CASE("InputRouting.ContextSwitchCapturesAndDiscardsFirstDelta", "[ui]") {
    voxels::ImGuiUIManager manager;
    manager.SetInputContext(voxels::InputContext::Menu);
    REQUIRE(manager.WantsMouseCapture());
    REQUIRE(manager.WantsKeyboardCapture());

    manager.SetInputContext(voxels::InputContext::Gameplay);
    REQUIRE(manager.ConsumeFirstMouseDelta());
    REQUIRE_FALSE(manager.ConsumeFirstMouseDelta());
}

TEST_CASE("Theme.AppliesConsistentTokenSetAndIsIdempotent", "[ui]") {
    ImGui::CreateContext();
    voxels::ApplyVoxelsTheme();
    const ImVec4 firstButton = ImGui::GetStyle().Colors[ImGuiCol_Button];
    const float firstPadding = ImGui::GetStyle().FramePadding.x;
    voxels::ApplyVoxelsTheme();

    REQUIRE(ImGui::GetStyle().Colors[ImGuiCol_Button].x == Catch::Approx(firstButton.x));
    REQUIRE(ImGui::GetStyle().Colors[ImGuiCol_Button].y == Catch::Approx(firstButton.y));
    REQUIRE(ImGui::GetStyle().FramePadding.x == Catch::Approx(firstPadding));
    ImGui::DestroyContext();
}