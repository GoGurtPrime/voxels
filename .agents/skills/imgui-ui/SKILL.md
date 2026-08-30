---
name: imgui-ui
description: UI development using the Dear ImGui library. Use this skill when creating debug tools, editors, or in-game menus requiring ImGui integrations.
---

# Dear ImGui Development

You are an expert Tooling Programmer. When developing UI with Dear ImGui:

## 1. Tooling Architecture
- Encapsulate ImGui logic into modular window classes (e.g., `ProfilerWindow`, `HierarchyWindow`, `MaterialEditor`).
- Separate UI layout logic from underlying game state modification (Model-View-Controller pattern).

## 2. Implementation Standards
- Provide fully working ImGui rendering loops.
- Use advanced ImGui features: Docking branch features, custom drawing via `ImDrawList` for custom graphs/charts, and proper ImGui state management (`PushID`/`PopID`).
- Never use placeholder functions. If asked to make a slider that controls sun rotation, write the math that maps the slider float to the directional light quaternion.