# PLAYER_UI_BRIDGE_API.md

## Purpose

This document is the source of truth for the browser-to-engine player UI bridge used by CEF routes.

If you change any bridge message kind, payload field, validation rule, route payload schema, or action mapping, update this file in the same commit.

## Ownership and authority

- Browser UI is presentation and input collection.
- Native C++ is authoritative for game state, save data, networking, and settings persistence.
- All bridge messages are asynchronous and versioned.

## Runtime path

1. React UI calls `window.voxelsBridgeSend(encodedEnvelope)`.
2. CEF render process forwards `voxels-action` process message to browser process.
3. `WebUIManager::BrowserClient::OnProcessMessageReceived` decodes and validates envelope.
4. Native maps envelope kind to `PlayerUIActionKind` and queues `PlayerUIAction`.
5. Main loop drains one action per frame and dispatches via `PlayerUIActionDispatcher`.
6. App states mutate authoritative engine state.
7. Native publishes `PlayerUIViewModel` snapshots back to the browser via `ui.model` envelope.
8. React route receives model and re-renders.

## Envelope contract

All bridge messages use this JSON envelope:

```json
{
  "version": 1,
  "kind": "string",
  "requestId": 123,
  "payload": "<JSON string>"
}
```

Rules:

- `version` must be `1`.
- `kind` must be non-empty.
- `requestId` must be greater than `0`.
- `payload` must be a string containing JSON for the message kind.
- Payload size limit is 64 KiB.

## Message kinds

### Browser -> native actions

Use kind prefix `ui.action.`:

- `ui.action.play`
- `ui.action.create-world`
- `ui.action.load-world`
- `ui.action.delete-world`
- `ui.action.confirm-delete`
- `ui.action.join`
- `ui.action.resume`
- `ui.action.settings`
- `ui.action.controls`
- `ui.action.toggle-world-visibility`
- `ui.action.return-to-main-menu`
- `ui.action.exit-to-desktop`
- `ui.action.back`
- `ui.action.quit`
- `ui.action.apply-settings`
- `ui.action.dismiss-controls`
- `ui.action.hotbar`
- `ui.action.open-chat`
- `ui.action.close-chat`
- `ui.action.send-chat`
- `ui.action.open-crafting`
- `ui.action.close-crafting`
- `ui.action.craft-recipe`
- `ui.action.drop-item`
- `ui.action.move-item`
- `ui.action.acknowledge-error`

Legacy (non-prefixed) kinds are still accepted for compatibility.

Action payload object:

```json
{
  "primary": "optional string",
  "secondary": "optional string or JSON object",
  "value": 0.0,
  "settings": { "optional": "object used by apply-settings" }
}
```

Mapping rules:

- If `secondary` is a string, C++ stores it in `PlayerUIAction.secondary`.
- If `secondary` is an object, C++ stores it as compact JSON text in `PlayerUIAction.secondary`.
- If `settings` exists and `secondary` is empty, C++ stores `settings` JSON in `PlayerUIAction.secondary`.
- `drop-item` sends the inventory slot index (0-35) as action `value` and drops the slot's
  entire stack as a ground item tossed in front of the player.
- `move-item` sends the source slot index as action `value` and the destination slot index as
  action `primary` (stringified integer); native swaps the two slots, or merges stacks up to
  the stack limit when both slots hold the same item. Used by the HUD's drag-and-drop inventory
  rearrangement.

### Native -> browser model updates

Kind:

- `ui.model`

Payload string contains a serialized `PlayerUIViewModel` object:

```json
{
  "route": 7,
  "revision": 2,
  "title": "SETTINGS",
  "message": "",
  "items": ["Apply", "Back"],
  "payload": "{\"fov\":90.0}",
  "progress": 0.0,
  "blocking": false
}
```

The inner `payload` field is route-specific JSON text.

### Startup splash route (`route = 11`)

`payload` JSON object:

```json
{
  "fade": 1.0
}
```

Notes:

- Published by the native main-menu startup flow before menu interactions are enabled.
- `fade` is clamped to `0..1` and drives logo/preface opacity in the browser route.
- While splash is active, UI remains blocking and no menu actions should be shown.

## Route payload schemas

### Main menu route (`route = 0`)

`payload` JSON object (optional):

```json
{
  "titleOpacity": 1.0,
  "showMenuButtons": true
}
```

Notes:

- `titleOpacity` is clamped to `0..1` and fades in the title block during startup intro.
- `showMenuButtons` keeps menu actions hidden until the startup title fade reaches full opacity.
- Non-startup transitions back to main menu publish full-opacity title and visible buttons immediately.

### World selection route (`route = 1`)

`payload` contains a `worlds` array. Each entry has `slot`, `name`, `seed`, `createdUtc`,
`lastPlayedAt`, `playTimeSeconds`, `mode`, and `public`. When `<save>/preview.png` exists,
native code also supplies `previewUrl` as a traversal-safe `voxels-ui://app/world-preview/...`
resource. The browser falls back to its styled empty preview if the field is absent or the image
cannot be decoded. Native save metadata is authoritative; the browser uses the legacy
`save:<slot>|<name>` items only as a compatibility fallback.

### Pause route (`route = 6`)

```json
{
  "remote": false,
  "public": true,
  "hosting": true,
  "port": 27015
}
```

`toggle-world-visibility` sends the requested boolean as action `value`. Local save-and-exit
actions are deferred until `InGameState::Render` captures a player-perspective `preview.png`
after world entities and before HUD/web UI composition; normal persistence still completes in
`InGameState::OnExit`. Remote leave actions do not capture local previews and reset networking to
loopback before the state transition or process exit.

### Settings route (`route = 7`)

`payload` JSON object:

```json
{
  "windowMode": "Windowed",
  "resolutionWidth": 1280,
  "resolutionHeight": 720,
  "fov": 90.0,
  "renderDistance": 8,
  "simulationDistance": 4,
  "master": 1.0,
  "music": 0.7,
  "effects": 0.8,
  "sensitivity": 1.0,
  "invertY": false,
  "particles": true,
  "crosshairSize": 1.0,
  "crosshairHighContrast": false,
  "reducedMotion": false,
  "rendererBackend": 0,
  "activeRenderer": 1,
  "restartRequired": false
}
```

Notes:

- Web UI must hydrate controls from this payload when entering or revising settings.
- Native side clamps values before applying.
- Native side applies window mode and resolution immediately, then persists to settings file and republishes the current settings snapshot after apply.

### Gameplay HUD route (`route = 9`)

`payload` JSON object:

```json
{
  "hotbar": [
    { "slot": 0, "selected": true, "blockId": 21, "count": 1, "durability": 131, "name": "Stone Pickaxe" }
  ],
  "selectedSlot": 0,
  "inputMethod": "keyboard",
  "inventory": {
    "slots": [
      { "slot": 0, "hotbar": true, "selected": true, "blockId": 21, "count": 1, "durability": 131, "name": "Stone Pickaxe" },
      { "slot": 9, "hotbar": false, "selected": false, "blockId": 0, "count": 0, "name": "" }
    ]
  },
  "selectedSlot": 0,
  "heldItem": { "empty": false, "name": "Stone Pickaxe", "count": 1, "durability": 131 },
  "target": { "hit": true, "name": "Stone", "breakProgress": 0.35 },
  "status": { "health": 16, "heartColor": "#d94352", "remoteSession": false },
  "chat": { "open": false },
  "crafting": {
    "open": false,
    "recipes": [
      {
        "id": "recipe_planks",
        "name": "Planks",
        "icon": "planks",
        "category": "construction",
        "sort": "Planks",
        "ingredients": [{ "name": "Tree Trunk", "count": 1 }],
        "output": { "name": "Planks", "count": 4 }
      }
    ]
  },
  "notifications": [{ "id": 4, "text": "Player 2 joined.", "remaining": 7.6 }],
  "crosshair": { "size": 1.0, "highContrast": false, "reducedMotion": false }
}
```

Notes:

- Native publishes at up to 30 Hz plus immediate event-driven revisions for selection, break progress, and notifications.
- `inputMethod` is `"keyboard"` or `"gamepad"`, reflecting the InputManager's last-active device
  (any keyboard/mouse event, or a gamepad button/analog stick past a small deadzone). The HUD
  uses this to switch its control-hint bar between keyboard glyphs and controller button glyphs.
- `inventory.slots` is authoritative full inventory state (9 hotbar + 27 main slots) for centered inventory/crafting layouts.
- Browser HUD is presentation-only; native C++ remains authoritative for inventory changes, crafting, and gameplay state.

### Loading route (`route = 3`)

`payload` JSON object (optional):

```json
{
  "backdropOpacity": 1.0,
  "showStatus": true
}
```

Notes:

- `backdropOpacity` controls an opaque fullscreen overlay used by main-menu startup transitions.
- `showStatus` toggles loading text/progress visibility while the overlay can still fade.
- Regular world/join loading screens may omit this payload and use default route UI.

## JavaScript integration notes

- Preferred send function: `window.voxelsBridgeSend`.
- Compatibility send function: `window.voxelsAction`.
- Preferred receive hook: `window.__voxelsReceiveBridgeMessage` receiving an envelope object.
- Compatibility receive hook: `window.__voxelsReceiveModel` receiving plain model object.

## C++ integration points

- Envelope codec: `engine/src/ui/player_ui.cpp`
- CEF bridge: `engine/src/ui/web_ui_manager.cpp`
- Action dispatcher: `engine/src/app/player_ui_dispatcher.cpp`
- Settings route publishing: `engine/src/app/state_machine_screens.cpp`

## Extending for gameplay and new routes

When adding a gameplay-facing UI action:

1. Add a new `ui.action.<kind>` entry here.
2. Add native mapping in `ActionKindFromWire`.
3. Add route validation and state mutation in `PlayerUIActionDispatcher::Dispatch`.
4. Add route payload schema for the receiving route.
5. Add tests proving native side effects.

Do not add browser-only optimistic state for authoritative gameplay outcomes.
