# Work Item 14: Steam API & Platform SDK Integration

## 🎯 Objective & Overview
Integrate the Steamworks API SDK (and platform abstraction hooks for Xbox GDK / PlayStation / Dreamcast KallistiOS) to support achievements, Steam Rich Presence, Steam P2P networking relay fallback, and Steam Cloud save syncing. Implement a mock platform SDK interface so the game builds and runs cleanly when Steam SDK binaries are absent.

> **Priority note:** This item is intentionally scheduled *after* `13_minimum_viable_playable_build`. Platform SDK/achievement integration is a nice-to-have layered on top of a working game and must never be picked up before the MVP playthrough is real — do not let Steam/platform work substitute for or delay actual gameplay integration.

---

## 🔗 Dependencies & Context
* **Prerequisites:** `02_core_engine_foundation`, `09_app_lifecycle_ui_and_menus`, `10_networking_and_server_client`, `13_minimum_viable_playable_build`
* **Target Subsystems:** `engine/include/voxels/platform/steam/`, `engine/src/platform/steam/`

---

## 📋 Detailed Task Breakdown
1. **Platform Service Interface (`voxels/platform/platform_services.hpp`):**
   * Abstract interface `IPlatformServices`:
     * `bool initialize()`
     * `void update()`
     * `bool unlockAchievement(const std::string& achievementId)`
     * `void setRichPresence(const std::string& key, const std::string& value)`
     * `std::string getPlayerName()`
     * `uint64_t getPlayerID()`
     * `void shutdown()`

2. **Steamworks Implementation (`voxels/platform/steam/steam_services.hpp`):**
   * Wraps `SteamAPI_Init()`, `SteamAPI_RunCallbacks()`, `SteamUserStats()`, `SteamFriends()`.
   * Conditional compilation controlled via CMake option `VOXELS_ENABLE_STEAM` (default `OFF` unless Steam SDK headers/libraries are detected).

3. **Mock Platform Service Fallback (`voxels/platform/mock_services.hpp`):**
   * Headless/offline platform service implementation used when `VOXELS_ENABLE_STEAM=OFF` or when Steam is not running.
   * Logs unlocked achievements to local log file and simulates player profile ID `1234567890`.

4. **Steam App ID Configuration (`steam_appid.txt`):**
   * Automatically generate or copy `steam_appid.txt` containing the development App ID (`480` Spacewar default testing ID) next to the executable output path during CMake build.

---

## 🧪 Automated Testing Requirements
* **Test File:** `tests/test_platform_services.cpp`
* **Test Cases:**
  * `PlatformServices.MockInitialization`: Initialize `MockPlatformServices`, verify `getPlayerName()` returns non-empty local profile string.
  * `PlatformServices.AchievementUnlock`: Unlock achievement `"ACH_FIRST_BLOCK"`, verify achievement state recorded as `UNLOCKED` in mock service registry.
  * `PlatformServices.RichPresence`: Set rich presence `"status"` to `"In Main Menu"`, read back state from service layer.

---

## 👤 Human-in-the-Loop Actions Required
* **Steamworks SDK Installation (Human Step):**
  1. Download the Steamworks SDK from the official Valve Developer portal (`https://partner.steamgames.com/`).
  2. Extract the SDK zip archive.
  3. Copy the `sdk/public/steam/` header files into `engine/third_party/steam/include/steam/`.
  4. Copy the platform library files (`steam_api64.lib` / `libsteam_api.so` / `libsteam_api.dylib`) into `engine/third_party/steam/lib/<platform>/`.
  5. Configure CMake with `-DVOXELS_ENABLE_STEAM=ON`.
  6. (Procedural Fallback: If Steamworks SDK files are missing, CMake automatically sets `VOXELS_ENABLE_STEAM=OFF` and builds with `MockPlatformServices`).

---

## 🔄 Verification & Self-Healing Protocol
1. **Build:** Run `cmake --build build`
2. **Test:** Run `ctest --test-dir build --output-on-failure -R PlatformServicesTest`
3. **Self-Healing:** Ensure `SteamAPI_RunCallbacks()` is safely invoked on the main thread during frame updates without throwing access violations if Steam client is closed.
