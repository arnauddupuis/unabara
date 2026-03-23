# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

Unabara is a Qt6/C++ desktop application for creating telemetry overlays for scuba diving videos. It imports Subsurface dive logs (XML/SSRF format) and generates customizable overlays that can be composited with diving footage.

## Build Commands

### Standard Build Process
```bash
# From project root
mkdir build && cd build
cmake ..
cmake --build .

# Run application
./bin/unabara
```

### Platform-Specific Build
```bash
# macOS with Homebrew Qt6
cmake .. -DQt6_DIR=$(brew --prefix qt6)/lib/cmake/Qt6

# Windows with Qt6 installed
cmake .. -DCMAKE_PREFIX_PATH=C:\path\to\Qt\6.8.0\msvc2019_64
cmake --build . --config Release
```

## Architecture Overview

### Core Components

**Data Layer** (`src/core/`):
- `DiveData`: Central dive telemetry model with interpolation capabilities
- `LogParser`: Subsurface XML/SSRF file parser
- `Config`: Singleton settings manager using QSettings (persists template directory, active template path, etc.)

**UI Layer** (`src/ui/`):
- `MainWindow`: Main application controller (C++)
- `Timeline`: Interactive dive data timeline widget (C++)
- QML components: Modern Qt Quick interface (`src/ui/qml/`)

**Generation Layer** (`src/generators/`):
- `OverlayGenerator`: Core overlay rendering engine with template support
- `OverlayImageProvider`: Qt Quick image provider for real-time QML preview

**Export Layer** (`src/export/`):
- `ImageExporter`: PNG sequence export with multi-threading
- `VideoExporter`: FFmpeg-based direct video export with multiple codec support

### Key Integration Points

- QML ↔ C++ integration via Qt property system and image providers
- Timeline synchronization between dive data and video footage
- Template-based overlay system with configurable telemetry elements
- Multi-threaded export with progress tracking and cancellation

## Dependencies

### Required
- Qt 6.8.0+ (Core, Gui, Quick, Qml, Xml, Concurrent, Widgets, Multimedia, DBus)
- CMake 3.16+
- C++17 compiler

### Optional
- FFmpeg: Required for direct video export functionality

## Development Notes

### Data Model
- Dive telemetry stored as timestamped data points
- Multi-cylinder gas management with pressure tracking
- Interpolation algorithms provide smooth data between samples
- Decompression data includes NDL, ceiling, and TTS values

### Video Integration  
- Uses Qt Multimedia for video metadata extraction
- Timeline provides frame-accurate synchronization
- Supports video offset adjustment for dive/video alignment

### Export System
- Image sequences: High-quality PNG with transparency support
- Video export: Configurable codecs (H.264, ProRes, VP9, HEVC)
- Export ranges: Full dive, visible timeline range, or video duration range

### Test Data
- Sample dive logs available in `tests/data/` (SSRF format)
- Test video available in `tests/videos/`

### Template Management System

Templates (`.utp` files) define cell layout, positions, fonts, and background images for overlays.

**Template Lifecycle**:
- Bundled templates are embedded via Qt Resource System (`resources.qrc`)
- User templates are stored in a configurable template directory (persisted in `Config`)
- `Config::activeTemplatePath` tracks the last-used template across sessions
- At startup, the generator loads the active template (or the first bundled template on first run)

**Template ComboBox Sync** (`OverlayEditor.qml`):
- `Component.onCompleted` syncs the ComboBox to `activeTemplatePath` at startup
- After load/save: `refreshTemplateList()` → `indexOfTemplatePath()` → update model → set `currentIndex`
- Setting `currentIndex` programmatically does NOT fire `onActivated` (Qt behavior)

**Template Preservation**:
- `onDiveChanged` only calls `initializeDefaultCellLayout()` when `cellCount() === 0` (prevents wiping loaded templates on dive import)
- `onDiveChanged` also calls `adjustTankCellVisibility(dive)` to hide pressure cells whose `tankIndex >= dive->cylinderCount()`
- Cell visibility toggles (`onShow*Changed`) use `setCellVisible()` instead of `initializeDefaultCellLayout()` to avoid destructive resets
- Pressure toggle uses `setPressureCellsVisible(visible, dive)` which handles all pressure cell IDs (`"pressure"`, `"tank_0"`, `"tank_1"`, etc.) and creates default pressure cells if the template has none

**Display Option Sync on Template Load**:
- `loadTemplate()` resets all `m_show*` flags to `false` before scanning cells, so templates without certain cell types correctly uncheck the corresponding display options
- All individual `show*Changed` signals are emitted after loading to sync QML checkboxes

### Qt Resource Path Handling

`QImage` only understands the `:/` prefix, NOT `qrc:/`. The `setTemplatePath()` and `loadTemplate()` methods normalize `qrc:/` → `:/` automatically. When constructing resource paths in C++, always use `:/` prefix.

### Cross-Platform File Paths

Use `MainWindow::urlToLocalFile()` (exposed as `mainWindow` QML context property) for converting `QUrl` to local paths. Never use `QString::replace("file://", "")` — it breaks on Windows (`file:///C:/Users/...` → `/C:/Users/...`).

### Overlay Preview System

The application has two preview panels that render overlays differently:

**Main Preview (Left Panel) - C++ Rendering**:
- Renders to full template resolution (e.g., 1024×768)
- Uses `OverlayGenerator::generateOverlay()` → QPainter
- Font scaled with `getScaledFontSize(font, 1.8)` to match template resolution
- Result image scaled down to fit the panel

**Interactive Preview (Right Panel) - QML Rendering**:
- Renders at preview panel size directly
- Uses QML Text elements with cell fonts scaled by `pointSize * 1.8 * (cellContainer.width / templateWidth)`
- The `1.8` factor matches the C++ `getScaledFontSize()` scale factor so both panels show proportionally consistent text
- Font shown in Text Settings reads from `generator.font` (unscaled) — the `cellFont` scaling is purely visual
- Supports drag-and-drop cell positioning

**Important**: The two previews will look slightly different due to panel size differences. Both render correctly — the difference is in how scaling is applied. The exported overlay will match the template resolution exactly.

**Key files for overlay rendering**:
- `src/generators/overlay_gen.cpp` - C++ rendering (`renderCellBasedOverlay()`)
- `src/ui/qml/OverlayCell.qml` - QML cell rendering
- `src/ui/qml/InteractiveOverlayPreview.qml` - Interactive preview container
- `src/ui/qml/OverlayEditor.qml` - Editor panel with template management UI