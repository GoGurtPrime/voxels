# Player UI Design Guide

## Purpose

This guide defines the visual and interaction language for player-facing CEF UI. New menus and
changes to existing routes must follow it alongside the native bridge contract in
[PLAYER_UI_BRIDGE_API.md](PLAYER_UI_BRIDGE_API.md).

## Design Direction: Prismatic Liquid Glass

The interface should feel like polished mineral glass discovered in a bright voxel landscape:
translucent, substantial, colorful at its edges, and calm enough to keep the rendered world
legible behind it.

- Use softly frosted neutral glass for panels, with restrained cyan, green, amber, and coral
  reflections around the perimeter. Color belongs at edges and focus points rather than washing
  the entire surface in one hue.
- Keep panel corners at 8 px or less. Layer a sharp outer highlight, a low-contrast inner rim, and
  a soft shadow; do not nest decorative cards inside other cards.
- Use glass as a functional depth cue. Primary panels may be more opaque, while HUD and transient
  overlays should preserve more of the world behind them.
- Avoid decorative gradients that are detached from controls. Every reflected color should trace
  a panel edge, selected control, progress state, or directional light.
- Keep body text cool white and secondary text pale blue-gray. Amber is reserved for focus and
  primary actions; coral is reserved for destructive actions and errors.

## Typography

- Titles and primary button labels use a chunky geometric display face with generous counters and
  strong silhouettes at television viewing distance.
- Body copy and metadata use a friendly, highly legible sans serif with tabular numerals where
  values change.
- Fonts are bundled locally in the verified UI asset manifest. Player UI must never fetch fonts at
  runtime.
- Use uppercase sparingly for short labels. Keep letter spacing at zero and preserve enough line
  height for localization and 200% UI scale.

## Layout

- Main-menu identity and navigation are centered in the safe area. Do not tuck version text,
  navigation, or required status into viewport corners.
- The main-menu page background remains fully transparent so the native cinematic world is
  unobstructed. Do not add a full-screen color, atmosphere wash, or opaque backdrop to that route.
- Full-screen routes use one unframed stage with a single primary glass surface. A page section is
  not another card.
- Keep at least 32 px between the panel rim and interactive content at 1280x720; responsive rules
  may reduce this to 20 px on compact 4:3 output. Focus outlines must remain fully inside the
  scrollable content viewport.
- World selection uses a two-pane layout: a vertical save list on the left and a preview/details
  pane on the right. Collapse to one column only when the viewport cannot preserve 320 px for each
  pane.
- Support 4:3, 16:9, 16:10, and 21:9 without scaling font size from viewport width. Respect a
  5% console safe area and keep the next actionable control visible without precision scrolling.

## Controller-First Interaction

- Every route has one predictable initial focus target and a linear directional focus order.
  Selected, focused, disabled, pending, and destructive states must be visibly distinct without
  relying on color alone.
- Interactive targets are at least 52 px high, with 60 px preferred for primary menu actions.
- Lists use one focused row per item. Sliders and option pickers adjust with left/right; toggles
  activate with the primary action; Back always returns one layer without discarding unrelated
  state.
- On desktop world selection, only the save-list column scrolls; the selected preview, metadata,
  and action footer remain fixed. The save list hides scrollbar chrome, scrolls through pointer
  wheel or directional focus, and shows a bottom-center down indicator only while more rows remain;
  other route scrollbars use a narrow inset rail without arrow buttons.
- Do not require a pointer hover, freeform drag, or corner target for any essential action.
- Destructive actions open a separate confirmation surface. Delete controls are never permanently
  exposed beside ordinary play controls.
- Native keyboard input is supported for text fields. Console text entry will invoke a platform
  system keyboard through the platform-services seam; until that service exists, the web route may
  expose only a documented native invocation stub, never a fake text result.

## Controls

- Buttons use the same glass material as panels with a denser core, beveled edge reflection, and
  stable dimensions across hover, focus, press, and pending states.
- Text fields use an inset glass well and an internal focus ring so no outline can be clipped by a
  panel or scroll viewport. Show character count beside constrained inputs.
- World names accept letters, numbers, spaces, hyphens, and underscores up to 48 characters.
  Seeds accept ASCII letters and numbers up to 20 characters.
- Sliders pair a stable track with explicit decrement/increment controls so they remain usable on
  controllers. Always display the current value.
- Checkboxes and radios use visible custom indicators at least 24 px square and retain their native
  semantic input for accessibility.

## Motion and Accessibility

- Use short staged route entrances and a subtle moving edge reflection. Motion must reinforce
  hierarchy and never continuously move the content itself.
- Honor `prefers-reduced-motion` by disabling ambient reflection movement and route transforms.
- Maintain WCAG AA contrast for copy and focus indicators against the most transparent allowed
  panel state.
- Use semantic headings, forms, fieldsets, labels, progress elements, and live error regions.
- UI remains usable at 200% scale with no overlap, clipped text, or hidden primary action.

## Route Requirements

- Main menu: centered title and centered vertical action stack; no corner-aligned identity or
  version label.
- World select: save list, selected-world preview, available metadata, explicit Play/New/Delete,
  and a separate delete confirmation mode. With no saves, Play routes directly to creation.
- World creation: world name and seed counters, bounded input, large rule toggles, and controller-
  adjustable render distance.
- Pause: transparent enough to preserve game context; Resume is primary, followed by Settings and
  Controls. Local sessions expose visibility, host status, Save and Quit to Menu, and Save and Exit;
  remote sessions expose Leave Server and Leave and Exit.

## Review Checklist

- Test keyboard, pointer, and directional focus navigation.
- Test 1280x720 16:9, 1024x768 4:3, 1920x1200 16:10, and 2560x1080 21:9 layouts.
- Verify focus indicators and long labels do not clip at 100% and 200% scale.
- Confirm current manifest asset hashes are staged before desktop review.
- Confirm actions produce native side effects and returning between routes preserves the correct
  rendered world backdrop.