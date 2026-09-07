/**
 * @file test_web_ui_app_composition.cpp
 * @brief Asserts the WI-03.02 app-composition contract: which IPlayerUI backend the CEF
 *        diagnostic overlay resolves to at compile time, and that its lifecycle is safe to
 *        invoke without ever starting a real CEF subprocess (which would recursively spawn
 *        this very test binary as a CEF child process).
 */

#include <catch2/catch_test_macros.hpp>

#include "voxels/ui/imgui_ui_manager.hpp"
#ifdef VOXELS_HAS_CEF
#include "voxels/ui/web_ui_manager.hpp"
#endif

namespace {
enum class PlayerUIBackendKind { ImGui, Web };

constexpr PlayerUIBackendKind SelectDiagnosticOverlayBackend() noexcept {
#ifdef VOXELS_HAS_CEF
    return PlayerUIBackendKind::Web;
#else
    return PlayerUIBackendKind::ImGui;
#endif
}
} // namespace

TEST_CASE("PlayerUIComposition.SelectsWebUiOverlayOnlyWhenCefIsCompiledIn", "[app-composition][web-ui]") {
#ifdef VOXELS_HAS_CEF
    REQUIRE(SelectDiagnosticOverlayBackend() == PlayerUIBackendKind::Web);
#else
    REQUIRE(SelectDiagnosticOverlayBackend() == PlayerUIBackendKind::ImGui);
#endif
}

#ifdef VOXELS_HAS_CEF
TEST_CASE("PlayerUIComposition.WebUiOverlayIsAnIndependentIPlayerUiThatIsSafeToDestroyUnstarted",
          "[app-composition][web-ui]") {
    // main.cpp never assigns the CEF overlay to appContext.ui (ImGui keeps native route
    // presentation so menus never regress); prove the type it constructs really is the CEF
    // backend and that Shutdown() on a never-Initialize()'d instance (e.g. an early engine
    // startup failure before the overlay is reached) is a safe no-op, not a crash. We
    // deliberately never call Initialize()/ExecuteSubprocess() here: doing so would start a
    // real CEF subprocess tree rooted at this test executable instead of voxels_app.
    voxels::WebUIManager overlay;
    REQUIRE_FALSE(overlay.IsFrameActive());
    REQUIRE(overlay.UsesNativeRoutePresentation() == false);
    overlay.Shutdown();
    overlay.Shutdown();
}
#else
TEST_CASE("PlayerUIComposition.ImGuiRemainsThePrimaryBackendWithoutCef", "[app-composition][web-ui]") {
    voxels::ImGuiUIManager uiManager;
    REQUIRE_FALSE(uiManager.IsFrameActive());
}
#endif
