# Editor

The editor project is a separate application that consumes the engine and provides tools for authoring and packing voxel assets, previews, and data creation workflows.

## Required structure

```text
editor/
├── CMakeLists.txt
├── README.md
├── src/
│   ├── main.cpp
│   ├── editor_app.cpp
│   ├── asset_browser.cpp
│   ├── model_preview.cpp
│   ├── texture_packer.cpp
│   └── project.cpp
├── include/
│   └── editor/
│       ├── editor_app.hpp
│       ├── asset_pipeline.hpp
│       ├── model_editor.hpp
│       └── preview_window.hpp
└── resources/
```

The editor is intentionally decoupled from the runtime app so the asset pipeline can evolve independently from the game loop.
