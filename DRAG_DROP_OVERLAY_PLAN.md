# Drag & Drop Overlay Customization - Development Plan

## Overview
Implement a comprehensive drag-and-drop overlay editor that allows users to freely position and customize individual data cells on the overlay, creating fully customizable templates.

## Progress Summary

| Phase | Status | Completion Date | Description |
|-------|--------|----------------|-------------|
| Phase 1 | ✅ Complete | 2025-10-13 | Data Model & Architecture (CellData, OverlayTemplate, cell-based layout) |
| Phase 2 | ✅ Complete | 2025-10-14 | Interactive Preview (QML components, CellModel, selection system) |
| Phase 3 | ✅ Complete | 2025-11-15 | Drag & Drop Implementation (positioning, constraints, persistence) |
| Phase 4 | ✅ Complete | 2025-11-15 | Dynamic Cell Sizing (C++ implementation deferred to later) |
| Phase 4.5 | ✅ Complete | 2025-11-15 | Collision Detection (red border warnings for overlapping cells) |
| Phase 5 | ✅ Complete | 2025-11-21 | Per-Cell Property Editing (dual-mode, reset, visual indicators) |
| Phase 6 | ✅ Complete | 2025-11-21 | Template Save/Load System (file I/O, UI integration) |
| Phase 7 | ✅ Complete | 2026-03-05 | Template System Overhaul (persistence, cross-platform paths, font scaling) |
| Phase 7.5 | ✅ Complete | 2026-03-10 | Tank Cell Visibility & Display Option Sync (multi-tank bug fixes, template checkbox sync) |
| Phase 8+ | 📋 Future | - | Template Library Browser, Management Tools, Presets |

**Current Status**: Tank pressure cell visibility now correctly adapts to the dive's actual cylinder count. Display option checkboxes sync with template contents on load. New bundled templates added (OC_Rec_NoTanks_Titanium, OC_Rec_1Tank_Ocean, OC_Rec_1Tank_Titanium, OC_Tek_All_Data_4Tanks).

## Terminology
**Cell**: A data display element on the overlay. Each cell shows one type of telemetry data:
- Depth
- Temperature
- Dive Time
- No Decompression Limit (NDL) / Time To Surface (TTS)
- Tank Pressure (can be multiple cells for multi-tank dives)
- CCR Cell 1 PO2
- CCR Cell 2 PO2
- CCR Cell 3 PO2
- CCR Composite PO2

## Architecture Overview

### Current State
- Overlay generation happens in C++ (`OverlayGenerator`)
- Cells are positioned using automatic layout algorithm (horizontal sections)
- All cells share the same font, size, and color properties
- Preview is rendered as a static image via `OverlayImageProvider`
- No persistence of cell-specific positioning or styling

### Target State
- Each cell has independent position, size, font, and color properties
- Interactive QML-based editor with drag-and-drop capability
- Cell selection system with visual feedback
- Per-cell property editing vs global property editing
- Custom template save/load system
- Backward compatibility with existing templates

## Phase 1: Data Model & Architecture (Foundation)

### Status: ✅ COMPLETED (2025-10-13)

### Objective
Create the data structures and architecture to support per-cell positioning and styling.

### Tasks

#### 1.1 Create CellData Class (C++)
**File**: `include/core/cell_data.h` + `src/core/cell_data.cpp`

Create a new class to represent a single cell:
```cpp
class CellData {
    QString cellId;              // Unique identifier (e.g., "depth", "temperature", "tank_0")
    CellType cellType;           // Enum: Depth, Temperature, Time, NDL, TTS, Pressure, PO2Cell1, etc.
    QPointF position;            // Position relative to template (0.0-1.0 normalized coords)
    bool visible;                // Whether cell is shown
    QFont font;                  // Cell-specific font
    QColor textColor;            // Cell-specific text color
    QSizeF size;                 // Cell size (width/height, normalized or absolute?)

    // Serialization
    QJsonObject toJson() const;
    static CellData fromJson(const QJsonObject& obj);
};
```

**Properties needed**:
- Position (normalized coordinates 0.0-1.0 relative to template dimensions)
- Visibility flag
- Font properties (family, size, weight, style)
- Text color
- Cell type/identifier
- Size (auto-calculated from content or manual?)

**Considerations**:
- Should positions be normalized (0.0-1.0) or absolute pixels?
  - **DECISION ✓**: Normalized coordinates for resolution independence
- How to handle dynamic cells (multiple tank pressures)?
  - Store position for first tank, calculate offsets for additional tanks?
  - Or store individual positions for each tank?
  - **FUTURE ENHANCEMENT**: 3 auto-layout options (grid, columnar, linear)

#### 1.2 Create OverlayTemplate Class (C++)
**File**: `include/core/overlay_template.h` + `src/core/overlay_template.cpp`

Container for complete template configuration:
```cpp
class OverlayTemplate {
    QString templateName;
    QString backgroundImagePath;
    double backgroundOpacity;
    QVector<CellData> cells;
    QFont defaultFont;           // Fallback for cells without specific font
    QColor defaultTextColor;     // Fallback for cells without specific color

    // Serialization
    QJsonObject toJson() const;
    static OverlayTemplate fromJson(const QJsonObject& obj);
    bool saveToFile(const QString& path) const;
    static OverlayTemplate loadFromFile(const QString& path);
};
```

**Features**:
- JSON serialization/deserialization
- File save/load operations
- Validation (ensure all required cells are present)
- Migration support (convert old templates to new format)

#### 1.3 Update OverlayGenerator to Use Cell Data
**Files**: `include/generators/overlay_gen.h` + `src/generators/overlay_gen.cpp`

**Changes needed**:
1. Add `QVector<CellData> m_cells` member
2. Add methods to get/set individual cell properties:
   - `CellData* getCellData(const QString& cellId)`
   - `void setCellPosition(const QString& cellId, const QPointF& pos)`
   - `void setCellFont(const QString& cellId, const QFont& font)`
   - `void setCellColor(const QString& cellId, const QColor& color)`
   - `void setCellVisible(const QString& cellId, bool visible)`
3. Modify `generateOverlay()` to use cell positions instead of auto-layout
4. Keep auto-layout as fallback for missing position data

**Backward compatibility**:
- If cell positions are not set, use current auto-layout algorithm
- Provide migration method to convert current settings to cell-based layout

#### 1.4 Expose Cell Data to QML
**Files**: Update `src/generators/overlay_gen.h`

Add Q_PROPERTY and signals for:
- Selected cell ID
- Cell list (QML ListModel?)
- Individual cell properties

Consider creating a separate `CellModel` class that inherits `QAbstractListModel` for cleaner QML integration.

### Deliverables
- [x] `CellData` class implemented with JSON serialization
- [x] `OverlayTemplate` class implemented with file I/O
- [x] `OverlayGenerator` updated to support cell-based layout
- [x] Cell management methods (getCellData, setCellPosition, setCellFont, setCellColor, setCellVisible)
- [x] Template load/export methods (loadTemplate, exportTemplate)
- [x] Migration utility for existing templates (initializeDefaultCellLayout, migrateLegacySettings)
- [x] CMakeLists.txt updated with new files
- [x] Project builds successfully
- [ ] Cell data exposed to QML (deferred to Phase 2 - CellModel)
- [ ] Unit tests for serialization/deserialization (deferred)

### Implementation Notes (2025-10-13)
- **Files Created**:
  - `include/core/cell_data.h` + `src/core/cell_data.cpp` - Complete cell data structure
  - `include/core/overlay_template.h` + `src/core/overlay_template.cpp` - Template container with file I/O
- **Files Modified**:
  - `include/generators/overlay_gen.h` - Added cell-based layout support
  - `src/generators/overlay_gen.cpp` - Implemented all cell management methods
  - `CMakeLists.txt` - Added new source files
- **Build Status**: ✅ Compiles successfully
- **Backward Compatibility**: Current auto-layout still works, cell-based layout is opt-in via `m_useCellBasedLayout` flag
- **Next Session**: Begin Phase 2 - Interactive Preview with Selection

### Technical Decisions ✓ APPROVED
1. **Position coordinates**: Normalized (0.0-1.0) vs absolute pixels?
   - **DECISION ✓**: Normalized for resolution independence
2. **Cell sizing**: Auto-calculated from content vs manual sizing?
   - **DECISION ✓**: Auto-calculated with optional manual override
3. **Dynamic cells**: How to handle variable number of tanks/PO2 cells?
   - **DECISION ✓**: Store base position, calculate offsets algorithmically
4. **Default layout**: How to initialize cell positions for new templates?
   - **DECISION ✓**: Use current auto-layout algorithm as default generator

---

## Phase 2: Interactive Preview with Selection

### Status: ✅ COMPLETED (2025-10-14)

### Objective
Replace static image preview with interactive QML-based preview that supports cell selection.

### Tasks

#### 2.1 Create Interactive Overlay Preview Component (QML)
**File**: `src/ui/qml/InteractiveOverlayPreview.qml`

Replace the current static Image component with an interactive canvas:

```qml
Item {
    id: interactivePreview

    property var generator
    property string selectedCellId: ""

    signal cellSelected(string cellId)
    signal cellPositionChanged(string cellId, point newPosition)

    // Background image
    Image {
        id: backgroundImage
        source: generator ? generator.templatePath : ""
        // ...
    }

    // Repeater for cells
    Repeater {
        model: cellModel  // From generator
        delegate: OverlayCell {
            cellData: model.cellData
            selected: model.cellId === interactivePreview.selectedCellId
            onClicked: interactivePreview.cellSelected(model.cellId)
            onPositionChanged: interactivePreview.cellPositionChanged(model.cellId, newPos)
        }
    }

    // Click outside to deselect
    MouseArea {
        anchors.fill: parent
        z: -1  // Behind cells
        onClicked: interactivePreview.selectedCellId = ""
    }
}
```

**Features**:
- Render overlay background
- Render each cell as an interactive component
- Handle mouse clicks for selection
- Handle clicks outside cells to deselect

#### 2.2 Create OverlayCell Component (QML)
**File**: `src/ui/qml/OverlayCell.qml`

Individual cell component with drag capability:

```qml
Rectangle {
    id: cellRoot

    property var cellData
    property bool selected: false

    signal clicked()
    signal positionChanged(point newPosition)

    // Visual appearance
    border.color: selected ? "lime" : "transparent"
    border.width: selected ? 3 : 0
    color: "transparent"

    // Cell content (text rendering)
    Text {
        anchors.centerIn: parent
        text: cellData.displayText
        font: cellData.font
        color: cellData.textColor
    }

    // Selection
    MouseArea {
        anchors.fill: parent
        onClicked: cellRoot.clicked()
        // Will add drag behavior in Phase 3
    }
}
```

**Features**:
- Visual selection indicator (bright green border)
- Display cell content with proper styling
- Click handling for selection
- Preparation for drag behavior (Phase 3)

#### 2.3 Create CellModel for QML (C++)
**Files**: `include/ui/cell_model.h` + `src/ui/cell_model.cpp`

```cpp
class CellModel : public QAbstractListModel {
    Q_OBJECT

public:
    enum CellRoles {
        CellIdRole = Qt::UserRole + 1,
        CellTypeRole,
        PositionRole,
        VisibleRole,
        FontRole,
        TextColorRole,
        DisplayTextRole  // Preview text for the cell
    };

    // QAbstractListModel interface
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    // Cell management
    void updateFromGenerator(OverlayGenerator* generator, DiveData* dive, double timePoint);
    void updateCellPosition(const QString& cellId, const QPointF& position);

signals:
    void cellDataChanged(const QString& cellId);
};
```

**Features**:
- Expose cell data to QML Repeater
- Update model when generator changes
- Bidirectional sync with OverlayGenerator

#### 2.4 Integrate Interactive Preview into OverlayEditor
**File**: `src/ui/qml/OverlayEditor.qml`

Replace the current static preview with `InteractiveOverlayPreview`:
- Update layout
- Connect selection signals
- Update UI to show selected cell indicator

#### 2.5 Update Main Window Preview
**File**: `src/ui/qml/main.qml`

Decide whether main preview should also be interactive or remain static.
- **Recommendation**: Keep main preview static for performance, only editor has interactive preview

### Deliverables
- [x] `InteractiveOverlayPreview.qml` component created
- [x] `OverlayCell.qml` component created
- [x] `CellModel` class implemented
- [x] Selection system working (click to select, bright green border)
- [x] Click outside deselects all cells
- [x] Preview updates in real-time with overlay changes

### Technical Decisions ✓ APPROVED
1. **Rendering approach**: QML Text components vs rendering to QImage in C++?
   - **DECISION ✓**: QML for interactivity, C++ for final export
2. **Performance**: How to handle real-time updates efficiently?
   - **DECISION ✓**: Caching with lazy updates for performance
3. **Cell text content**: Generate sample text for preview or use live data?
   - **DECISION ✓**: Use live data from current dive at current time point

### Implementation Notes (2025-10-14)

**Files Created**:
1. `src/ui/qml/InteractiveOverlayPreview.qml`
   - Background image with proper aspect ratio calculation
   - Cell container positioned over actual image area (not letterbox)
   - Coordinate conversion from normalized (0.0-1.0) to pixel positions
   - Selection management with lime green border (width: 3)
   - Click outside to deselect functionality

2. `src/ui/qml/OverlayCell.qml`
   - Individual cell component with all properties from CellData
   - Visual selection indicator (lime border when selected)
   - Hover effects (subtle white border on hover)
   - Semi-transparent black background for text visibility
   - Click handling for selection
   - Stub for drag behavior (Phase 3)

3. `include/ui/cell_model.h` + `src/ui/cell_model.cpp`
   - QAbstractListModel for exposing cells to QML
   - 11 role types (CellId, CellType, Position, Visible, Font, TextColor, DisplayText, CalculatedSize, HasCustomFont, HasCustomColor, TankIndex)
   - Bidirectional sync with OverlayGenerator
   - Auto-updates on generator changes (cellsChanged, cellLayoutChanged signals)
   - Live data formatting using current dive and time point

**Files Modified**:
1. `src/main.cpp`
   - Added CellModel header include
   - Registered CellModel with QML (Unabara.UI 1.0)

2. `src/ui/qml/OverlayEditor.qml`
   - Replaced static Image preview with InteractiveOverlayPreview
   - Added timeline and dive properties
   - Added CellModel instance
   - Connected all update signals (generator, timeline, dive changes)
   - Cell selection logging (TODO: connect to cell editor panel)

3. `src/ui/qml/main.qml`
   - Passed timeline and dive references to OverlayEditor

4. `src/generators/overlay_gen.cpp`
   - Added initializeDefaultCellLayout() call in constructor
   - Ensures cells are always initialized on startup

5. `resources.qrc`
   - Added InteractiveOverlayPreview.qml
   - Added OverlayCell.qml

**Key Implementation Details**:
- **Coordinate System**: Cells use normalized positions (0.0-1.0) stored in CellData, converted to pixels in InteractiveOverlayPreview based on actual rendered image size
- **Aspect Ratio Handling**: cellContainer positioned only over actual image area, accounting for letterboxing
- **Live Data Display**: CellModel generates display text from current dive data at timeline's current time
- **Selection Visual**: Lime green (#00FF00) border with 3px width for high visibility
- **Performance**: Only updates when necessary (generator/timeline/dive changes)

**Testing Status**: Built successfully, ready for user testing with actual dive data

---

## Phase 3: Drag & Drop Implementation

### Status: ✅ COMPLETED (2025-11-15)

### Objective
Implement drag-and-drop functionality for repositioning cells on the overlay.

### Tasks

#### 3.1 Add Drag Behavior to OverlayCell
**File**: `src/ui/qml/OverlayCell.qml`

Enhance MouseArea to support dragging:

```qml
MouseArea {
    anchors.fill: parent
    drag.target: parent
    drag.axis: Drag.XAndYAxis

    onClicked: cellRoot.clicked()

    onReleased: {
        // Calculate normalized position relative to background
        var normalizedPos = calculateNormalizedPosition(cellRoot.x, cellRoot.y)
        cellRoot.positionChanged(normalizedPos)
    }
}
```

**Features**:
- Enable drag on cell
- Constrain drag to background bounds
- Calculate normalized position on drop
- Emit position change signal

#### 3.2 Update Cell Position in Generator
**File**: Connection in `InteractiveOverlayPreview.qml`

Connect position change signal to generator:

```qml
Connections {
    target: interactivePreview
    function onCellPositionChanged(cellId, newPosition) {
        generator.setCellPosition(cellId, newPosition)
    }
}
```

#### 3.3 Implement Position Constraints
Ensure cells stay within background boundaries:
- Clamp positions to [0.0, 1.0] range
- Visual feedback when approaching boundaries
- **FUTURE ENHANCEMENT**: Snap-to-grid option (keep in plan for later)

#### 3.4 Real-time Preview Update
Ensure overlay preview updates immediately as cells are dragged:
- Update CellModel
- Trigger preview refresh
- Maintain smooth performance

### Deliverables
- [x] Cells can be dragged within preview area
- [x] Cell positions are constrained to background bounds
- [x] Position changes persist in generator
- [x] Preview updates in real-time during drag
- [x] Drag performance is smooth (60 FPS)

### Technical Decisions ✓ APPROVED
1. **Coordinate system**: Where to convert between absolute pixels and normalized coordinates?
   - **DECISION ✓**: Store normalized, convert to pixels in QML
2. **Drag feedback**: Visual indicators during drag?
   - **DECISION ✓**: Display grid during drag + snap-to-grid functionality (show gridlines when dragging, snap positions to grid)
3. **Multi-cell drag**: Support dragging multiple selected cells?
   - **DECISION ✓**: Single-cell drag only, multi-cell is future enhancement

---

## Phase 4: Dynamic Cell Sizing

### Status: NOT STARTED

### Objective
Make cell QRect dynamically calculated based on content and font size, replacing static dimensions.

### Tasks

#### 4.1 Implement Dynamic Size Calculation in OverlayGenerator
**File**: `src/generators/overlay_gen.cpp`

Update drawing methods to calculate QRect dynamically:

```cpp
QRect OverlayGenerator::calculateCellRect(const QString& cellId,
                                          const QString& content,
                                          const QFont& font) {
    QFontMetrics fm(font);

    // Calculate text bounds
    QRect textRect = fm.boundingRect(content);

    // Add padding
    int padding = getScaledFontSize(0.5);
    textRect.adjust(-padding, -padding, padding, padding);

    // Get cell position
    CellData* cell = getCellData(cellId);
    if (!cell) return QRect();

    // Convert normalized position to absolute
    int x = cell->position.x() * templateWidth;
    int y = cell->position.y() * templateHeight;

    textRect.moveTo(x, y);
    return textRect;
}
```

**Changes needed**:
1. Replace all hardcoded QRect dimensions in draw methods
2. Use QFontMetrics to calculate text bounds
3. Add appropriate padding around text
4. Account for multi-line text (tank labels, etc.)
5. Handle special cases (decompression info, multi-value cells)

#### 4.2 Update All Drawing Methods
Update each draw* method in overlay_gen.cpp:
- `drawDepth()` - calculate based on depth value string
- `drawTemperature()` - calculate based on temp value string
- `drawTime()` - calculate based on time string
- `drawNDL()` - calculate based on NDL value
- `drawTTS()` - calculate based on TTS + ceiling values
- `drawPressure()` - calculate based on pressure + tank label
- `drawPO2Cell()` - calculate based on PO2 value
- `drawCompositePO2()` - calculate based on composite value

#### 4.3 Store Calculated Size in CellData
**Option A**: Calculate size on-demand every frame
**Option B**: Calculate once, store in CellData, recalculate on font change

**DECISION ✓**: Option B for performance
- Add `QSizeF calculatedSize` to CellData
- Recalculate when font changes or content type changes
- Use cached size for positioning/rendering
- **IMPORTANT**: Always recalculate all sizes when user triggers overlay export

#### 4.4 Handle Size in Interactive Preview
**File**: `src/ui/qml/OverlayCell.qml`

Update cell dimensions based on calculated size:

```qml
Rectangle {
    width: cellData.calculatedSize.width
    height: cellData.calculatedSize.height
    // ...
}
```

#### 4.5 Collision Detection
**DECISION ✓**: Visual warning system
- Detect when cell bounds intersect
- Visual warning when overlap occurs (red border around overlapping cells)
- Auto-adjustment/prevention is future enhancement

### Deliverables
- [ ] All cell QRects calculated dynamically using QFontMetrics
- [ ] Static dimensions removed from drawing methods
- [ ] Cells resize appropriately when font changes
- [ ] Cell sizes reflected in interactive preview
- [ ] Multi-line text handled correctly
- [ ] Performance remains acceptable

### Technical Decisions ✓ APPROVED
1. **Size calculation timing**: Every frame vs cached with invalidation?
   - **DECISION ✓**: Cached with invalidation
2. **Minimum/maximum sizes**: Should cells have size constraints?
   - **DECISION ✓**: Minimum size for readability (dynamic based on font size), no maximum
3. **Padding strategy**: Fixed pixels vs proportional to font size?
   - **DECISION ✓**: Proportional using getScaledFontSize()
4. **Overlap handling**: Detect, warn, prevent, or ignore?
   - **DECISION ✓**: Detect and warn with red border, prevention is future enhancement

---

## Phase 5: Per-Cell Property Editing

### Status: ✅ COMPLETED (2025-11-21)

### Objective
Allow editing individual cell properties (font, size, color) when cell is selected, while keeping global editing for all cells when none selected.

### Tasks

#### 5.1 Update OverlayEditor UI Logic
**File**: `src/ui/qml/OverlayEditor.qml`

Add logic to switch between global and per-cell editing modes:

```qml
property bool hasSelection: interactivePreview.selectedCellId !== ""
property var selectedCellData: hasSelection ?
    generator.getCellData(interactivePreview.selectedCellId) : null

GroupBox {
    title: hasSelection ?
        qsTr("Selected Cell: ") + interactivePreview.selectedCellId :
        qsTr("All Cells")

    // Font/color controls here
    // Bind to selectedCellData if hasSelection, otherwise to generator globals
}
```

**UI changes needed**:
1. Add indicator showing whether editing one cell or all cells
2. Update control bindings to use selected cell data when applicable
3. Show selected cell ID in UI
4. Add "Reset to Global" button for per-cell properties

#### 5.2 Implement Dual-Mode Property Setters in Generator
**File**: `src/generators/overlay_gen.cpp`

Add methods that respect selection:

```cpp
void OverlayGenerator::setFont(const QFont& font) {
    if (m_selectedCellId.isEmpty()) {
        // Apply to all cells
        m_defaultFont = font;
        for (auto& cell : m_cells) {
            cell.font = font;
        }
    } else {
        // Apply to selected cell only
        CellData* cell = getCellData(m_selectedCellId);
        if (cell) {
            cell->font = font;
        }
    }
    emit fontChanged();
}
```

**Methods to update**:
- `setFont()`
- `setTextColor()`
- Font size (via setFont with size change)

#### 5.3 Add Cell Selection Property to Generator
**File**: `include/generators/overlay_gen.h`

```cpp
Q_PROPERTY(QString selectedCellId READ selectedCellId WRITE setSelectedCellId NOTIFY selectedCellIdChanged)

QString selectedCellId() const { return m_selectedCellId; }
void setSelectedCellId(const QString& cellId);

signals:
    void selectedCellIdChanged();

private:
    QString m_selectedCellId;
```

Connect this to the interactive preview selection.

#### 5.4 Add Visual Indicators in OverlayEditor
Enhance UI to make editing mode clear:
1. Highlighted section title showing mode
2. Cell property display (show cell's current values)
3. "Apply to All" button to copy selected cell properties to all cells
4. "Reset Cell" button to revert cell to global defaults
5. Add small reset icon button next to each customized property ✓ (icon indicates different from global, clicking resets)

#### 5.5 Handle Property Inheritance
Define inheritance rules:
- New cells inherit from global defaults
- Cells with no custom properties use global values
- Cells with custom properties override globals
- Provide UI to clear custom properties (revert to global)

### Deliverables
- [x] OverlayEditor switches between global and per-cell editing modes
- [x] Font/size/color changes apply to selected cell when one is selected
- [x] Font/size/color changes apply to all cells when none selected
- [x] Background opacity always applies globally
- [x] UI clearly indicates current editing mode (banner with "✓ Cell Selected" or "⊞ Editing All Cells")
- [x] "Reset to Global" functionality working (↺ buttons next to font/color controls)
- [x] Property inheritance system implemented
- [x] Visual indicators on cells (cyan/magenta dots) showing custom properties
- [x] Deselect button clears selection including visual outline
- [x] Reactive property system updates UI controls with selected cell values

### Technical Decisions ✓ APPROVED
1. **Property storage**: Store full properties in each cell or only overrides?
   - **DECISION ✓**: Store full properties, track custom vs inherited
2. **UI indicators**: How to show which properties are customized vs inherited?
   - **DECISION ✓**: Small reset icon button next to each customized property
3. **Bulk operations**: Support selecting multiple cells for editing?
   - **DECISION ✓**: Not implemented initially, future enhancement for multi-cell editing

### Implementation Notes (2025-11-21)

**Files Modified**:

1. **include/generators/overlay_gen.h** + **src/generators/overlay_gen.cpp**
   - Added `selectedCellId` Q_PROPERTY with getter, setter, signal
   - Implemented dual-mode `setFont()` and `setTextColor()`:
     - No selection: Apply to all cells without custom properties + update global default
     - Cell selected: Apply only to selected cell + mark as custom
   - Added `resetCellFont()` and `resetCellColor()` methods
   - Clears custom flag and reverts to global defaults

2. **src/ui/qml/OverlayEditor.qml**
   - **Editing Mode Banner**: Visual indicator at top (green when cell selected, gray when editing all)
   - **Deselect Button**: Clears selection and updates visual state
   - **Reactive Properties**: `currentFont`, `currentColor`, `currentHasCustomFont`, `currentHasCustomColor`
   - **Cell Property Access**: `getCellProperty()` reads from InteractiveOverlayPreview's cellRepeater
   - **Update Mechanism**: Connections to `generator.onCellsChanged()`, `onSelectedCellIdChanged()`
   - **UI Controls**: Font selector, size spinner, color button bind to reactive properties
   - **Reset Buttons**: "↺" buttons next to font/color (enabled/disabled with opacity change)
   - **Grid Layout**: 3-column layout with placeholders for consistency

3. **src/ui/qml/OverlayCell.qml**
   - Added custom property indicators (cyan/magenta dots) in top-right corner
   - Tooltips on indicators ("Custom font", "Custom color")

4. **src/ui/qml/InteractiveOverlayPreview.qml**
   - Exposed `cellRepeater` via property alias for external access
   - Allows OverlayEditor to read actual cell property values

**Key Technical Solutions**:

1. **Reactive Property Updates**:
   - Created `updateCurrentProperties()` function called on selection/cell changes
   - Properties automatically reflect selected cell's values or global defaults
   - Font selector uses `onActivated`, size spinner uses `onValueModified` to avoid feedback loops

2. **Selection Synchronization**:
   - **Critical Issue**: Property bindings break when direct assignments occur
   - **Solution**: Replaced binding with `Connections` to `generator.onSelectedCellIdChanged()`
   - Signal handler explicitly updates preview's `selectedCellId` every time
   - Enables Deselect button to clear visual green outline

3. **Grid Layout Alignment**:
   - Reset buttons use `enabled: false` with `opacity: 0.3` instead of `visible: false`
   - Maintains consistent 3-column layout (Label | Control | Reset Button)
   - Placeholder `Item` for rows without reset buttons (size, opacity)

**Issues Resolved**:
- Grid layout misalignment when reset buttons hidden (fixed with `enabled` vs `visible`)
- Controls showing global values instead of selected cell values (fixed with reactive properties)
- Deselect button not clearing green outline (fixed with Connections signal handler)
- Reset buttons not enabling when cell customized (fixed with reactive `currentHasCustomFont/Color`)

**Testing Status**: All functionality tested and working correctly

See [PHASE5_IMPLEMENTATION.md](PHASE5_IMPLEMENTATION.md) for detailed documentation.

---

## Phase 6: Custom Template Save/Load System

### Status: ✅ COMPLETED (2025-11-21)

### Objective
Implement comprehensive save/load system for custom templates including all cell data and background settings.

### Tasks

#### 6.1 Define Template File Format
**File**: Document in `docs/template_format.md`

JSON format specification:

```json
{
  "version": "1.0",
  "templateName": "My Custom Template",
  "backgroundImage": "path/to/image.png",
  "backgroundOpacity": 1.0,
  "defaultFont": {
    "family": "Arial",
    "pointSize": 12,
    "weight": 50,
    "italic": false
  },
  "defaultTextColor": "#FFFFFF",
  "cells": [
    {
      "cellId": "depth",
      "cellType": "Depth",
      "position": {"x": 0.1, "y": 0.2},
      "visible": true,
      "font": {
        "family": "Arial",
        "pointSize": 14,
        "weight": 75,
        "italic": false
      },
      "textColor": "#00FF00",
      "customProperties": true
    },
    // ... more cells
  ]
}
```

**Considerations**:
- Background image: Store as absolute path, relative path, or embed?
  - **DECISION ✓**: Relative path for portability, with fallback
  - **IMPORTANT**: Ensure correct path interpolation when loading templates (if template and background in same directory)
- Version number for future format changes
- Validation schema
- Default values for missing fields

#### 6.2 Implement Save Functionality
**File**: `src/core/overlay_template.cpp`

```cpp
bool OverlayTemplate::saveToFile(const QString& filePath) const {
    QJsonObject json = toJson();

    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly)) {
        qWarning() << "Failed to open file for writing:" << filePath;
        return false;
    }

    QJsonDocument doc(json);
    file.write(doc.toJson(QJsonDocument::Indented));
    return true;
}
```

Also implement:
- Export current overlay state to OverlayTemplate
- Validation before saving
- Error handling

#### 6.3 Implement Load Functionality
**File**: `src/core/overlay_template.cpp`

```cpp
OverlayTemplate OverlayTemplate::loadFromFile(const QString& filePath) {
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning() << "Failed to open file for reading:" << filePath;
        return OverlayTemplate(); // Return default
    }

    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    return fromJson(doc.object());
}
```

Also implement:
- Validation of loaded data
- Migration from older versions
- Error recovery (use defaults for missing values)
- Background image path resolution

#### 6.4 Integrate Save/Load into UI
**File**: `src/ui/qml/OverlayEditor.qml`

Add UI controls:

```qml
RowLayout {
    Button {
        text: qsTr("Save Template")
        onClicked: saveTemplateDialog.open()
    }

    Button {
        text: qsTr("Load Template")
        onClicked: loadTemplateDialog.open()
    }

    Button {
        text: qsTr("Reset to Default")
        onClicked: generator.resetToDefaultLayout()
    }
}

FileDialog {
    id: saveTemplateDialog
    fileMode: FileDialog.SaveFile
    nameFilters: ["Unabara Template (*.utp)"]
    onAccepted: generator.saveTemplate(selectedFile)
}

FileDialog {
    id: loadTemplateDialog
    fileMode: FileDialog.OpenFile
    nameFilters: ["Unabara Template (*.utp)"]
    onAccepted: generator.loadTemplate(selectedFile)
}
```

#### 6.5 Template Management System
Create template library functionality:

**Storage location**: `~/.config/UnabaraProject/templates/` or `[AppData]/templates/`

**Features**:
1. List available templates
2. Template preview thumbnails
3. Delete templates
4. Rename templates
5. Duplicate templates
6. Share templates (export to file)
7. Import templates

**UI Component**: `src/ui/qml/TemplateLibrary.qml`
- Grid view of available templates
- Template preview
- Quick load
- Template management actions

#### 6.6 Backward Compatibility
Ensure old overlays still work:

1. Detect old-style templates (no cell position data)
2. Auto-generate cell positions using current auto-layout
3. Provide migration tool to convert old templates
4. Allow fallback to auto-layout if template loading fails

**Migration utility**:
```cpp
OverlayTemplate OverlayTemplate::migrateFromLegacy(const Config* config) {
    // Read old settings from Config
    // Generate default cell positions using auto-layout
    // Create new template with positioned cells
}
```

### Deliverables
- [x] Template file format documented (JSON with .utp extension)
- [x] JSON serialization/deserialization implemented (OverlayTemplate class)
- [x] Save template functionality working (saveTemplateToFile method)
- [x] Load template functionality working (loadTemplateFromFile method)
- [x] File dialogs integrated into UI (Save/Load Template buttons)
- [x] Reset layout functionality (restores default cell positions)
- [ ] Template library UI created (deferred to future enhancement)
- [ ] Template management features working (deferred to future enhancement)
- [ ] Backward compatibility maintained (existing code already handles this)
- [ ] Migration utility for old templates (not needed - templates already compatible)

### Technical Decisions ✓ APPROVED
1. **File format**: JSON, XML, binary, or custom?
   - **DECISION ✓**: JSON for readability and ease of editing
2. **File extension**: `.utp` (Unabara Template), `.json`, or other?
   - **DECISION ✓**: `.utp` for clarity
3. **Background image storage**: Embed, reference, or copy?
   - **DECISION ✓**: Reference with relative path, copy option for portability
4. **Template location**: User documents, app data, or user choice?
   - **DECISION ✓**: App data for templates, user choice for export
5. **Built-in templates**: Ship with default templates?
   - **DECISION ✓**: Yes, 2-3 starter templates (will build default templates later in development)

### Implementation Notes (2025-11-21)

**Core Components Already Existed from Phase 1:**
- ✅ `OverlayTemplate` class (Phase 1) with complete JSON serialization
- ✅ `toJson()` / `fromJson()` methods for template export/import
- ✅ `saveToFile()` / `loadFromFile()` with validation and error handling
- ✅ Background image path resolution (relative paths supported)
- ✅ Template validation with error reporting
- ✅ Version handling (v1.0 format)

**New C++ Methods Added:**

1. **include/generators/overlay_gen.h** + **src/generators/overlay_gen.cpp**
   - `bool saveTemplateToFile(const QString& filePath)` - Q_INVOKABLE method for QML
     - Calls `exportTemplate()` to get current overlay state
     - Extracts filename for automatic template naming
     - Calls `OverlayTemplate::saveToFile()`
     - Emits `templateSaved(filePath)` signal on success
   - `bool loadTemplateFromFile(const QString& filePath)` - Q_INVOKABLE method for QML
     - Calls `OverlayTemplate::loadFromFile()` with error message handling
     - Validates loaded template (checks for cells)
     - Calls `loadTemplate()` to apply to generator
     - Emits `templateLoaded(filePath)` signal on success
   - Added signals: `templateSaved(QString)` and `templateLoaded(QString)`
   - Added `#include <QFileInfo>` for filename extraction

**UI Integration:**

1. **src/ui/qml/OverlayEditor.qml**
   - **Template Management GroupBox** (lines 379-413):
     - "Save Template..." button → opens `saveTemplateDialog`
     - "Load Template..." button → opens `loadTemplateDialog`
     - "Reset Layout" button → calls `generator.initializeDefaultCellLayout(dive)`
   - **Save Template Dialog** (lines 749-775):
     - FileMode: SaveFile
     - Name filter: "Unabara Template (*.utp)"
     - Default suffix: "utp"
     - URL to local path conversion
     - Calls `generator.saveTemplateToFile(localPath)`
     - Console logging for success/failure
   - **Load Template Dialog** (lines 777-806):
     - FileMode: OpenFile
     - Name filter: "Unabara Template (*.utp)"
     - URL to local path conversion
     - Calls `generator.loadTemplateFromFile(localPath)`
     - Updates CellModel after successful load
     - Console logging for success/failure

**Template File Format (.utp):**
```json
{
  "version": "1.0",
  "templateName": "My Custom Template",
  "backgroundImage": "relative/path/to/image.png",
  "backgroundOpacity": 1.0,
  "defaultFont": {
    "family": "Arial",
    "pointSize": 12,
    "weight": 50,
    "italic": false,
    "bold": false
  },
  "defaultTextColor": "#FFFFFFFF",
  "cells": [
    {
      "cellId": "depth",
      "cellType": 0,
      "position": {"x": 0.1, "y": 0.2},
      "visible": true,
      "font": {...},
      "textColor": "#FF00FF00",
      "hasCustomFont": true,
      "hasCustomColor": true,
      "tankIndex": -1
    }
  ]
}
```

**Key Features:**
- Complete overlay state preservation (positions, fonts, colors, visibility)
- Per-cell custom properties saved and restored
- Relative background image paths for portability
- Automatic template naming from filename
- Validation before save and after load
- Error handling with console logging
- Seamless integration with existing drag-and-drop editor

**User Workflow:**

1. **Save Template:**
   - Customize overlay (drag cells, set fonts/colors)
   - Click "Save Template..."
   - Choose location and enter filename
   - Template saved as `.utp` JSON file

2. **Load Template:**
   - Click "Load Template..."
   - Select `.utp` file from file browser
   - Overlay updates instantly with saved configuration
   - All customizations restored (positions, fonts, colors)

3. **Reset Layout:**
   - Click "Reset Layout"
   - Cells return to default auto-calculated positions
   - Font/color settings remain unchanged

**Testing Status:** ✅ Tested and working correctly

### Future Enhancements (Optional)

The following features were identified as valuable additions for future development:

#### 7.1 Template Library Browser
**Priority**: Medium
**Description**: Built-in UI for managing saved templates

**Features**:
- Grid view of all saved templates in templates directory
- Template preview thumbnails (small overlay preview image)
- Quick-load functionality (double-click or single button)
- Template metadata display (name, date created, date modified)
- Search/filter templates by name
- Sort by name, date, or usage frequency

**Implementation**:
- New QML component: `src/ui/qml/TemplateLibrary.qml`
- Template storage location: `~/.config/UnabaraProject/templates/` (Linux/macOS) or `AppData/Local/UnabaraProject/templates/` (Windows)
- Auto-scan directory on startup
- Watch for file changes (add/remove templates externally)

#### 7.2 Template Management Actions
**Priority**: Medium
**Description**: In-app template organization tools

**Features**:
- **Rename Template**: Edit template name in library
- **Duplicate Template**: Create copy with new name
- **Delete Template**: Remove template with confirmation dialog
- **Export Template**: Copy template + background image to zip file for sharing
- **Import Template**: Unzip and install shared template package
- **Set as Default**: Automatically load template on app startup

**UI**:
- Context menu on template items (right-click)
- Toolbar buttons when template selected
- Keyboard shortcuts (F2 rename, Del delete, etc.)

#### 7.3 Template Metadata & Organization
**Priority**: Low
**Description**: Enhanced template information and categorization

**Features**:
- Template description field (multiline text)
- Author name
- Creation date (auto-generated)
- Last modified date (auto-updated)
- Tags/categories (e.g., "Recreational", "Technical", "CCR", "Wreck")
- Favorite/star system
- Usage statistics (times loaded, last used)

**Storage**: Extended JSON format with metadata section:
```json
{
  "version": "1.0",
  "metadata": {
    "author": "User Name",
    "description": "Template for deep wreck dives",
    "tags": ["Technical", "Wreck", "Deep"],
    "created": "2025-11-21T10:30:00Z",
    "modified": "2025-11-21T15:45:00Z",
    "favorite": true,
    "usageCount": 15
  },
  "templateName": "...",
  // ... rest of template
}
```

#### 7.4 Template Preview Thumbnails
**Priority**: Medium
**Description**: Visual previews of template layouts

**Features**:
- Auto-generate thumbnail when saving template
- 200x112 px preview image (16:9 aspect ratio)
- Shows cell positions with sample data
- Store as PNG alongside .utp file (e.g., `MyTemplate.utp.png`)
- Update thumbnail when template modified

**Implementation**:
- Use OverlayGenerator to render preview at save time
- Render to smaller QImage
- Save to same directory as template
- Display in template library grid view

#### 7.5 Built-in Template Presets
**Priority**: High (should be done before v1.0 release)
**Description**: Ship application with professionally designed templates

**Presets to Include**:
1. **Classic Compact** - Traditional horizontal layout, small text
2. **Modern Minimal** - Large text, spread across overlay area
3. **Technical Pro** - All data visible, optimized for technical diving
4. **Recreational Simple** - Just the essentials (depth, time, temp, NDL)
5. **CCR Focused** - Emphasizes PO2 cells and composite PO2

**Storage**: Bundle in application resources (`:/resources/templates/presets/`)
**UI**: Separate "Presets" tab in template library

#### 7.6 Template Sharing Platform
**Priority**: Low (community feature)
**Description**: Community repository for user-created templates

**Implementation Approaches**:

**Phase 1 - GitHub-Based Repository** (More Practical)
- Create `unabara-community-templates` GitHub repository
- Organized folder structure: `templates/recreational/`, `templates/technical/`, `templates/ccr/`, etc.
- Template submission via Pull Requests
- Community review process (maintainer approval)
- README with template showcase (screenshots, descriptions)
- Direct download links from GitHub
- **In-App Integration**:
  - "Browse Community Templates" button
  - Fetches template list from GitHub API
  - Preview template metadata (from JSON)
  - Download and install with one click
  - Link to submit your own template (opens browser to PR instructions)

**Advantages**:
- No server infrastructure needed
- Free hosting via GitHub
- Version control and history
- Community moderation through PR process
- Issue tracker for template bugs/requests
- Easy to fork and customize

**Disadvantages**:
- Not end-user friendly for submissions (requires GitHub knowledge)
- Slower update cycle (PR approval time)
- Limited search/discovery features
- No built-in rating system

**Phase 2 - Dedicated Web Platform** (Future, If Needed)
- Custom website with database
- User accounts and authentication
- Direct upload from application
- Rating, reviews, and comments
- Advanced search and filtering
- Usage statistics and trending templates
- Automated validation and preview generation

**Recommendation**: Start with GitHub-based approach. If community grows large enough and demand is high, migrate to dedicated platform later.

#### 7.7 Template Auto-Backup
**Priority**: Low
**Description**: Automatic backup of modified templates

**Features**:
- Save backup copy before overwriting template
- Keep last N backups (configurable, default 3)
- Timestamp-based backup naming (`MyTemplate.2025-11-21_143000.utp.bak`)
- Restore from backup functionality
- Auto-cleanup old backups

**Implementation**:
- Check if template file exists before save
- If exists, copy to `.bak` file with timestamp
- Limit backup count per template
- Provide "Restore from Backup" UI in template library

---

## Phase 7: Testing & Polish

### Status: NOT STARTED

### Objective
Comprehensive testing, bug fixes, and user experience improvements.

### Tasks

#### 7.1 Testing
1. **Unit Tests**:
   - CellData serialization/deserialization
   - OverlayTemplate save/load
   - Position coordinate conversions
   - Dynamic size calculations

2. **Integration Tests**:
   - End-to-end template creation and loading
   - Drag and drop functionality
   - Property editing (global vs per-cell)
   - Backward compatibility with old templates

3. **Manual Testing**:
   - Test with various screen resolutions
   - Test with different background image sizes
   - Test with extreme font sizes
   - Test with all cell types visible/hidden
   - Test with multi-tank dives
   - Test with CCR dives

#### 7.2 Performance Optimization
1. Profile interactive preview performance
2. Optimize cell rendering
3. Optimize real-time updates during drag
4. Cache calculated sizes appropriately
5. Minimize redundant preview regeneration

#### 7.3 User Experience Enhancements
1. **Keyboard shortcuts**:
   - Delete to hide selected cell
   - Ctrl+A to select all cells
   - Arrow keys to nudge cell position
   - Escape to deselect

2. **Visual feedback**:
   - Hover effects on cells
   - Drag cursor changes
   - Drop shadows for depth perception
   - Grid overlay (optional, toggle-able)
   - Alignment guides (snap to other cells)

3. **Undo/Redo**:
   - Track changes to cell positions
   - Track changes to cell properties
   - Undo/redo stack
   - UI buttons for undo/redo

4. **Help & Documentation**:
   - Tooltip explanations
   - Quick start tutorial
   - Example templates
   - User guide documentation

#### 7.4 Error Handling
1. Graceful handling of missing background images
2. Graceful handling of corrupt template files
3. Validation with user-friendly error messages
4. Recovery from invalid cell positions
5. Handling of cells positioned outside bounds

#### 7.5 Accessibility
1. Keyboard navigation support
2. Screen reader compatibility (if applicable)
3. High contrast mode consideration
4. Appropriate focus indicators

### Deliverables
- [ ] Comprehensive unit test suite
- [ ] Integration tests passing
- [ ] Manual test plan executed
- [ ] Performance meets targets (smooth 60 FPS dragging)
- [ ] Keyboard shortcuts implemented
- [ ] Visual feedback polished
- [ ] Undo/redo system working
- [ ] Error handling robust
- [ ] Documentation complete
- [ ] Known bugs list empty or triaged

---

## Implementation Order Summary

1. **Phase 1** (Foundation): Data structures and architecture - CRITICAL FIRST
2. **Phase 2** (Interactivity): Interactive preview with selection - ENABLES REMAINING PHASES
3. **Phase 3** (Positioning): Drag & drop implementation - CORE FEATURE
4. **Phase 4** (Sizing): Dynamic cell sizing - QUALITY IMPROVEMENT
5. **Phase 5** (Customization): Per-cell property editing - ADVANCED FEATURE
6. **Phase 6** (Persistence): Template save/load system - FINALIZATION
7. **Phase 7** (Quality): Testing and polish - ALWAYS LAST

## Dependencies Between Phases

```
Phase 1 (Foundation)
    ├─> Phase 2 (Interactivity)
    │       ├─> Phase 3 (Drag & Drop)
    │       └─> Phase 5 (Per-Cell Editing)
    ├─> Phase 4 (Dynamic Sizing)
    └─> Phase 6 (Save/Load)
            └─> Phase 7 (Testing & Polish)
```

- **Phase 1** must be completed first (foundation for everything)
- **Phase 2** must be completed before **Phase 3** and **Phase 5**
- **Phase 4** can be developed in parallel with **Phase 2/3** but should be integrated before **Phase 6**
- **Phase 6** requires **Phases 1-5** to be complete
- **Phase 7** is ongoing but intensifies after **Phase 6**

## Estimated Effort

- **Phase 1**: 2-3 sessions (complex architecture)
- **Phase 2**: 2-3 sessions (new QML components, model integration)
- **Phase 3**: 1-2 sessions (building on Phase 2)
- **Phase 4**: 1-2 sessions (refactoring existing code)
- **Phase 5**: 1-2 sessions (UI logic, conditional bindings)
- **Phase 6**: 2-3 sessions (file I/O, validation, UI integration)
- **Phase 7**: 2-3 sessions (testing, polish, documentation)

**Total**: ~13-20 sessions

## Risk Mitigation

### High Risk Areas

1. **Performance of Interactive Preview**
   - Risk: Real-time dragging might be sluggish
   - Mitigation: Profile early, optimize rendering, consider caching strategies

2. **Coordinate System Complexity**
   - Risk: Bugs in pixel <-> normalized coordinate conversion
   - Mitigation: Thorough unit testing, helper functions, clear documentation

3. **Backward Compatibility**
   - Risk: Breaking existing templates and user workflows
   - Mitigation: Migration utilities, fallback to auto-layout, extensive testing

4. **Dynamic Sizing Edge Cases**
   - Risk: Cell sizes might not calculate correctly for all content types
   - Mitigation: Test with extreme cases, fallback to minimum sizes

5. **Template File Format Evolution**
   - Risk: Future changes might break old templates
   - Mitigation: Version numbers, validation, migration support

### Medium Risk Areas

1. **QML/C++ Integration Complexity**
2. **File I/O Error Handling**
3. **Multi-tank and CCR Special Cases**

## Success Criteria

### Must Have (MVP)
- [ ] Users can drag and drop cells to reposition them
- [ ] Cell positions persist when saving/loading templates
- [ ] Cells can be selected with visual feedback
- [ ] Basic per-cell font and color customization works
- [ ] Backward compatibility with existing templates maintained
- [ ] No regressions in overlay export functionality

### Should Have
- [ ] Dynamic cell sizing based on font
- [ ] Template library with save/load UI
- [ ] Global vs per-cell editing mode works correctly
- [ ] Performance is smooth (60 FPS during drag)
- [ ] Comprehensive error handling

### Nice to Have
- [ ] Undo/redo functionality
- [ ] Keyboard shortcuts for cell manipulation
- [x] Alignment guides and snapping (Session 5: cyan for edges, gold for centers)
- [x] Grid overlay option (already implemented: shows during drag + toggle in UI)
- [ ] Template preview thumbnails

## Open Questions

1. Should cells have minimum/maximum size constraints?
2. Should we support cell rotation? (Probably not in Phase 1)
3. Should cells support text alignment options (left/center/right)?
4. Should we support cell grouping (multiple cells moving together)?
5. Should we support layers/z-ordering for overlapping cells?
6. Should template files embed background images or reference them?
7. Should we provide a "reset to auto-layout" option per-cell?
8. How should we handle very small background images (< 640px)?
9. Should cells have opacity settings separate from background opacity?
10. Should we support exporting templates to share with other users?

## Notes for Future Sessions

- This document should be read at the start of each session to restore context
- Update status and checkboxes as tasks are completed
- Add notes about implementation decisions and discoveries
- Update risk sections based on actual challenges encountered
- Keep this document in sync with actual implementation

---

## Summary of Approved Decisions

### Phase 1 Decisions ✓
- Normalized coordinates (0.0-1.0) for positions
- Auto-calculated cell sizing with optional manual override
- Store base position for dynamic cells (multi-tank, multi-PO2), calculate offsets
- Use current auto-layout as default generator
- **FUTURE**: Multi-tank auto-layout options: grid, columnar, linear

### Phase 2 Decisions ✓
- QML for interactive preview, C++ for final export
- Caching with lazy updates for performance
- Use live dive data for cell content in preview

### Phase 3 Decisions ✓
- Store normalized coordinates, convert to pixels in QML
- Display grid + snap-to-grid during drag
- Single-cell drag only (multi-cell is future enhancement)
- **FUTURE**: Snap-to-grid option

### Phase 4 Decisions ✓
- Cached size calculation with invalidation
- Recalculate all sizes on export trigger
- Minimum size constraints (dynamic based on font)
- Proportional padding using getScaledFontSize()
- Collision detection with red border warning
- **FUTURE**: Prevention system (block overlapping moves)

### Phase 5 Decisions ✓
- Store full properties in each cell, track custom vs inherited
- Reset icon button next to each customized property
- **FUTURE**: Multi-cell editing

### Phase 6 Decisions ✓
- JSON file format with .utp extension
- Relative path for background images with proper interpolation
- App data for template storage, user choice for export
- Ship 2-3 default templates (build later)

### Future Enhancements Identified
- Multi-tank auto-layout options: grid, columnar, linear
- Multi-cell selection and dragging
- Overlap prevention (currently detection only)
- Multi-cell property editing
- Undo/redo functionality
- Keyboard shortcuts for cell manipulation
- Template preview thumbnails

### Completed Enhancements (Beyond MVP)
- ✅ Snap-to-grid option (Phase 3)
- ✅ Grid overlay option (Phase 3)
- ✅ Visual alignment guides - cyan for edges, gold for centers (Session 5)

---

**Document Version**: 1.5
**Created**: 2025-10-13
**Last Updated**: 2026-02-13
**Status**: ✅ Phase 6 Complete + Main Preview Cell-Based Rendering Fixed

## Session History

### Session 1 (2025-10-13)
**Completed**: Phase 1 - Data Model & Architecture (Foundation)

**What was implemented**:
1. Created `CellData` class with complete JSON serialization support
2. Created `OverlayTemplate` class with file I/O and validation
3. Updated `OverlayGenerator` with full cell-based layout support:
   - Added `QVector<CellData> m_cells` storage
   - Implemented cell management methods (get, set position/font/color/visibility)
   - Implemented template load/export methods
   - Implemented migration utilities (initializeDefaultCellLayout, migrateLegacySettings)
   - Added signals: cellsChanged(), cellLayoutChanged()
4. Updated CMakeLists.txt with new source files
5. Fixed resources.qrc after accidental file deletion
6. Successfully built entire project

**Files created**:
- `include/core/cell_data.h`
- `src/core/cell_data.cpp`
- `include/core/overlay_template.h`
- `src/core/overlay_template.cpp`

**Files modified**:
- `include/generators/overlay_gen.h`
- `src/generators/overlay_gen.cpp`
- `CMakeLists.txt`
- `resources.qrc`

**Build status**: ✅ Compiles and links successfully

**Notes**:
- Accidental `rm -rf *` command deleted test data and uncommitted files (lesson learned: NEVER use this command)
- All Phase 1 code was successfully recreated from conversation context
- Backward compatibility maintained: auto-layout still works, cell-based layout is opt-in
- Cell exposure to QML deferred to Phase 2 (will use CellModel class)

**Next session**: Begin Phase 2 - Interactive Preview with Selection

---

### Session 2 (2025-10-18)
**Completed**: Phase 2 Polish - Multi-Tank Support & Layout Refinements

**Context**: Continued from previous session where Phase 2 basic implementation was complete. Today focused on achieving feature parity between main preview and interactive preview, fixing layout issues, and polishing the UI.

**Major Features Implemented**:
1. **Multi-Tank Support with Grid Layout**
   - Dynamic tank cell creation based on dive data (`dive->cylinderCount()`)
   - 2-column grid layout for 3+ tanks (e.g., 4 tanks → 2x2 grid)
   - Individual cells for each tank with unique IDs: "tank_0", "tank_1", "tank_2", "tank_3"
   - Tank-specific labels and gas mix information displayed
   - Automatic font scaling for multi-tank displays

2. **Section-Based Layout System**
   - Refactored `initializeDefaultCellLayout()` to match main overlay generator's section-based approach
   - Each cell allocated to sections based on content type
   - Section width calculated as `1.0 / numSections` (normalized coordinates)
   - Multi-tank grids span multiple sections appropriately
   - Dynamic section allocation based on visible cells

3. **Tank Labels with Gas Mix Info**
   - Enhanced `CellModel::formatValue()` to display gas mix for each tank
   - Format for multi-tank: "T1 (32%)", "T2 (21/35)", etc.
   - Format for single-tank: "TANK 1 (32%)" with full label
   - Trimix shows O2/He percentages: "(21/35)"
   - Nitrox shows O2 percentage: "(32%)"
   - Air (21% O2) displays no gas mix indicator

4. **Dynamic Cell Sizing**
   - Cell height auto-calculates based on text content: `cellText.implicitHeight + 8`
   - Cell width constrained to section width to prevent overlap
   - `calculatedSize` set for all cells in C++ (normalized coordinates)
   - Size conversion from normalized to pixels in QML
   - Handles multi-line cells correctly (NDL/TTS with DECO info)

**Bug Fixes Implemented**:

**Issue #1: Tank Grid Vertical Positioning**
- **Problem**: T3/T4 tanks not positioned below T1/T2 in grid
- **Root Cause**: Cell height hardcoded to 0.08 instead of calculated proportionally
- **Fix**: Changed to `cellHeight = 1.0 / rows` for proper grid spacing
- **File**: `src/generators/overlay_gen.cpp:399`

**Issue #2: PO2 Cells Vertical Position**
- **Problem**: CCR PO2 cells overlapping with first row
- **Root Cause**: PO2 Y position calculated relative to first row max, not normalized
- **Fix**: Set `po2YPos = 0.5` for consistent second-row placement
- **File**: `src/generators/overlay_gen.cpp:461`

**Issue #3: TTS Cell Vertical Alignment**
- **Problem**: TTS cell with DECO info (3 lines) expanding upward from center, misaligning with other cells
- **Root Cause**: Text using `anchors.centerIn: parent`, causing symmetric expansion
- **Fix**: Changed text anchoring to `anchors.top: parent.top` with `verticalAlignment: Text.AlignTop`
- **File**: `src/ui/qml/OverlayCell.qml:43-51`
- **Result**: Cells now expand downward only, maintaining top alignment

**Issue #4: Selection Border Size Mismatch**
- **Problem**: Green selection border smaller than actual cell content (visible when TTS shows 3 lines)
- **Root Cause**: Cell had fixed height of 40 pixels, text overflowed beyond border
- **Fix**: Made height dynamic: `height: cellText.implicitHeight + 8`
- **File**: `src/ui/qml/OverlayCell.qml:38`
- **Result**: Selection border now perfectly surrounds all cell content

**Issue #5: Horizontal Cell Overlap**
- **Problem**: TTS cell overlapping T1 tank cell when showing wide DECO text
- **Root Cause**: Cell width using implicit text width, ignoring section boundaries
- **Fix**: Set `calculatedSize` with section width for all cells, convert to pixels in QML
- **Files**:
  - `src/generators/overlay_gen.cpp` - Added `setCalculatedSize()` calls for all cells
  - `src/ui/qml/InteractiveOverlayPreview.qml:103-106` - Pixel conversion
- **Result**: Cells constrained to section width, no overlap

**Issue #6: Font/Color Update Regression**
- **Problem**: Interactive preview not updating when font/text color changed
- **Root Cause**: Removed `initializeDefaultCellLayout()` calls from C++ setters
- **Fix**: Added cell regeneration in QML `onFontChanged` and `onTextColorChanged` handlers
- **File**: `src/ui/qml/OverlayEditor.qml:91-102`

**Files Created**:
- None (all Phase 2 files already existed from previous session)

**Files Modified**:
1. `src/generators/overlay_gen.cpp`
   - Refactored `initializeDefaultCellLayout()` with section-based layout
   - Added `DiveData* dive` parameter for tank detection
   - Implemented multi-tank grid positioning (2x2 for 4 tanks)
   - Set `calculatedSize` for all cells (normalized coordinates)
   - Fixed cell height calculation: `1.0 / rows`
   - Fixed PO2 Y position: `0.5`

2. `src/ui/cell_model.cpp`
   - Enhanced `formatValue()` with tank-specific labels and gas mix
   - Added `tankIndex` parameter to `formatValue()`
   - Implemented dynamic NDL/TTS switching based on deco status
   - Format: "T1 (32%)" for multi-tank, "TANK 1 (32%)" for single-tank
   - Trimix format: "(O2/He)", Nitrox format: "(O2%)"

3. `src/ui/qml/OverlayCell.qml`
   - Changed text anchoring from center to top
   - Made height dynamic: `cellText.implicitHeight + 8`
   - Width constrained to `cellCalculatedSize.width`
   - Added `wrapMode: Text.NoWrap`

4. `src/ui/qml/InteractiveOverlayPreview.qml`
   - Added size conversion from normalized to pixels
   - `cellCalculatedSize: Qt.size(model.calculatedSize.width * cellContainer.width, ...)`

5. `src/ui/qml/OverlayEditor.qml`
   - Added cell regeneration on dive change
   - Added cell regeneration on font/color change
   - Connected all `onShow*Changed` signals to regenerate cells with dive data
   - Made overlay editor visible by default: `visible: true`
   - Removed maximum width constraint on overlay editor panel

6. `src/ui/qml/main.qml`
   - Removed `SplitView.maximumWidth` to allow free resizing of overlay editor

**Build Status**: ✅ Compiles and links successfully

**Testing Results**:
- ✅ Single-tank dives display correctly
- ✅ Multi-tank dives (4 tanks) display in 2x2 grid
- ✅ Tank labels show correct gas mix information
- ✅ CCR PO2 cells positioned correctly on second row
- ✅ NDL/TTS switching works with proper vertical alignment
- ✅ DECO info displays without misalignment
- ✅ Font and color changes update interactive preview
- ✅ Selection border matches cell content size
- ✅ No horizontal overlap between cells
- ✅ All cells constrained to section boundaries

**Key Implementation Details**:
- **Normalized Coordinates**: All positions and sizes stored as 0.0-1.0, converted to pixels in QML
- **Section-Based Layout**: Mimics main overlay generator's approach for perfect matching
- **Grid Calculations**: `cols = 2`, `rows = (tankCount + cols - 1) / cols`, `cellWidth = gridWidth / cols`
- **Height Calculations**: Tank cells: `1.0 / rows`, PO2 row: `0.5` (50% down)
- **Text Alignment**: Top-aligned with `anchors.top` to prevent upward expansion
- **Size Constraints**: Width from section, height from content (dynamic)

**Performance**: Interactive preview maintains smooth performance with all cells visible

**Next Steps**: Ready for Phase 3 - Drag & Drop Implementation

**Notes**:
- Achieved complete feature parity between main preview and interactive preview
- All layout algorithms now match exactly
- Dynamic sizing working correctly for all content types
- Selection visual feedback polished and accurate
- Multi-tank support fully functional with proper labeling

---

### Session 3 (2025-11-06)
**Completed**: Data Synchronization Fixes & Code Refactoring

**Context**: Session focused on fixing two critical bugs preventing data parity between main preview and interactive preview. The root causes were time precision mismatches and spaghetti code in pressure interpolation logic.

**Critical Bugs Fixed**:

**Bug #1: Time Precision Mismatch**
- **Problem**: Main preview and interactive preview showing different data values (1 second difference in dive time, 1 minute difference in TTS, significantly different tank pressures)
- **Root Cause**: Timeline `currentTime` had high precision (e.g., 123.456789123), but OverlayImageProvider logs showed 3 decimals (123.457). Different precision = different data point lookups = data mismatch
- **Analysis**: Time values converted between int/double inconsistently throughout codebase. Dive computer samples at 10s intervals, so sub-second precision was unnecessary noise causing bugs
- **Solution**: Standardized all time values to millisecond precision (3 decimals) at entry point
- **Files Modified**:
  - `include/ui/timeline.h:88-91` - Added `roundToMilliseconds()` static helper function
  - `src/ui/timeline.cpp:51` - Applied rounding in `setCurrentTime()`: `time = roundToMilliseconds(time)`
  - `src/ui/qml/OverlayEditor.qml:35-36` - Removed `.toFixed(3)` workaround since rounding now at source
- **Result**: ✅ Time values now consistent across all components. 1ms precision sufficient for video export (30fps = 33.3ms, 60fps = 16.7ms) while avoiding floating-point precision issues

**Bug #2: Missing Tank Pressure Interpolation (The Spaghetti Code)**
- **Problem**: Tank 3/4 showing frozen/incorrect pressure values (e.g., T4 showing 177 bar instead of 105 bar). Interactive preview never matched main preview for tanks with missing sample data
- **Root Cause**: Discovered **THREE LAYERS** of overlapping pressure interpolation logic:

  **Layer 1**: `DiveData::interpolateCylinderPressure()` - Interpolates based on cylinder start/end over active usage period (respects gas switches)

  **Layer 2**: `DiveData::dataAtTime()` - Attempts sample-to-sample interpolation with broken logic:
  ```cpp
  if (prevPressure > 0.0 && nextPressure > 0.0) {
      pressure = prevPressure + factor * (nextPressure - prevPressure);
  }
  ```
  This condition was **ALWAYS TRUE** because XML parser carries forward last known pressure (e.g., 176.919) for all samples. So `prev=176.919, next=176.919` (stale data) passed the check, resulting in `176.919 + factor * 0 = 176.919` (no change!)

  **Layer 3**: `OverlayGenerator::generateOverlay()` - Had band-aid logic to work around Layer 2:
  - Single tank: checked `if (pressure == startPressure)` to detect stale data (fragile!)
  - Multi-tank: **ALWAYS ignored** `dataPoint` pressure and called Layer 1 directly
  - This is why main preview worked but interactive preview didn't - main preview bypassed broken Layer 2!

- **Analysis**: User correctly identified this as "spaghetti code" with duplicated logic across 3 layers and no single source of truth. The fundamental design flaw: `dataAtTime()` tried to be smart about when to use cylinder interpolation but failed to detect stale carried-forward data.

- **Solution**: Complete refactoring to establish **single source of truth** in `DiveData::dataAtTime()`

  **New Logic** (simplified):
  ```cpp
  for (int i = 0; i < maxTanks; i++) {
      if (cylinder has valid start/end pressure) {
          if (cylinder is active at this time) {
              pressure = interpolateCylinderPressure(i, time);  // Use Layer 1
          } else {
              pressure = getLastInterpolatedPressure(i);  // Inactive: use cached
          }
      } else {
          // Fallback to sample-based interpolation
      }
  }
  ```

- **Files Modified**:
  - `src/core/dive_data.cpp:210-258` - Refactored `dataAtTime()` to be single source of truth:
    - Always calls `interpolateCylinderPressure()` for active cylinders with valid start/end pressure
    - Uses `getLastInterpolatedPressure()` for inactive cylinders (respects gas switches)
    - Falls back to sample-based interpolation only when cylinder data unavailable
    - Removed broken `prevPressure > 0.0 && nextPressure > 0.0` logic

  - `src/generators/overlay_gen.cpp:702-706` - Simplified single tank case:
    ```cpp
    // Before: 15 lines of re-interpolation logic
    // After: 2 lines
    double pressure = dataPoint.getPressure(0);
    drawPressure(painter, pressure, ...);
    ```

  - `src/generators/overlay_gen.cpp:753-759` - Simplified multi-tank case:
    ```cpp
    // Before: 30 lines checking active state, re-interpolating, caching
    // After: 2 lines
    double pressure = dataPoint.getPressure(i);
    drawPressure(painter, pressure, ...);
    ```

- **Code Removed**: ~40 lines of duplicated/redundant pressure interpolation logic from `generateOverlay()`

- **Result**: ✅ Both main preview and interactive preview now show **identical** pressure values. Single source of truth eliminates all data discrepancies.

**Architecture Improvements**:

**Before** (The Spaghetti):
```
Layer 1: interpolateCylinderPressure() - has gas switch logic
    ↓
Layer 2: dataAtTime() - tries to interpolate, fails with stale data
    ↓
Layer 3: generateOverlay() - re-interpolates, duplicates gas switch logic
```

**After** (Clean Architecture):
```
┌─────────────────────────────────────┐
│  DiveData::dataAtTime(timestamp)    │
│  SINGLE SOURCE OF TRUTH             │
│  ✓ Checks if cylinder is active     │
│  ✓ Calls interpolateCylinderPressure│
│  ✓ Handles inactive cylinders       │
│  ✓ Respects gas switches            │
└──────────────┬──────────────────────┘
               ↓
    ┌──────────┴──────────┐
    ↓                     ↓
OverlayGen           CellModel
(Main Preview)    (Interactive)
Just displays     Just displays
dataPoint         dataPoint
```

**Benefits**:
- ✅ Single decision point for all pressure interpolation
- ✅ No duplicated logic
- ✅ Proper gas switch handling (technical diving scenarios)
- ✅ Inactive cylinders show last known pressure (not interpolated while inactive)
- ✅ Active cylinders always show correct interpolated pressure
- ✅ Both previews get identical data automatically
- ✅ Much more maintainable code

**Technical Decisions Made**:

1. **Time Precision**: Standardized to milliseconds (3 decimals)
   - Rationale: Fine enough for video export (33.3ms @ 30fps), coarse enough to avoid floating-point precision bugs
   - Applied at entry point (`Timeline::setCurrentTime()`) for consistency

2. **Pressure Interpolation Architecture**: Single source of truth in `dataAtTime()`
   - Rationale: Eliminates spaghetti code, prevents data discrepancies, respects gas switches properly
   - Display layers (`generateOverlay()`, `CellModel`) now just trust the data

**Files Modified**:
1. `include/ui/timeline.h` - Added time rounding helper
2. `src/ui/timeline.cpp` - Applied rounding at entry point
3. `src/ui/qml/OverlayEditor.qml` - Removed workaround
4. `src/core/dive_data.cpp` - Complete refactor of tank pressure interpolation in `dataAtTime()`
5. `src/generators/overlay_gen.cpp` - Removed ~40 lines of redundant interpolation logic

**Build Status**: ✅ Compiles and links successfully

**Testing Results**:
- ✅ Time values synchronized between main and interactive preview (within 1ms)
- ✅ Tank pressure values identical in both previews
- ✅ Tank 3/4 now show correct interpolated pressures (e.g., 105 bar instead of frozen 177 bar)
- ✅ Gas switches properly respected (inactive tanks show last known pressure)
- ✅ CCR data synchronized
- ✅ All telemetry data (depth, temp, NDL/TTS) synchronized
- ✅ Multi-tank dives display correctly in both previews

**Performance**: No performance degradation. Refactoring actually improved efficiency by eliminating redundant calculations.

**Key Lessons**:
1. **Floating-point precision matters**: Sub-millisecond time precision was causing data lookup mismatches
2. **Beware of spaghetti code**: Three layers of interpolation logic with overlapping responsibilities created a maintenance nightmare
3. **Single source of truth**: Having one authoritative method for data calculation eliminates entire classes of bugs
4. **Trust user analysis**: User correctly identified the spaghetti code and proposed the right solution

**Next Steps**: Ready for Phase 3 - Drag & Drop Implementation

**Notes**:
- Both critical data synchronization bugs now fixed
- Codebase significantly cleaner and more maintainable
- Architecture now supports future features without fighting against itself
- Technical diving scenarios (gas switches) properly handled

---

### Session 4 (2025-11-15)
**Completed**: Phase 3 - Drag & Drop Implementation + Cell Sizing Fix

**Context**: Implemented drag-and-drop functionality for repositioning cells. Discovered and fixed critical bug with cell selection outline sizing.

**Phase 3 Implementation**:

1. **Drag Behavior Added to OverlayCell**
   - **File**: `src/ui/qml/OverlayCell.qml:66-103`
   - MouseArea with drag enabled: `drag.target: cellRoot`, `drag.axis: Drag.XAndYAxis`
   - Cursor changes: `Qt.OpenHandCursor` (hover) → `Qt.ClosedHandCursor` (dragging)
   - Position clamping to container bounds on release:
     ```qml
     var clampedX = Math.max(0, Math.min(cellRoot.x, containerWidth - cellRoot.width))
     var clampedY = Math.max(0, Math.min(cellRoot.y, containerHeight - cellRoot.height))
     ```
   - Normalized coordinate conversion (pixel → 0.0-1.0): `normalizedX = clampedX / containerWidth`
   - Emits `positionChanged(Qt.point(normalizedX, normalizedY))` on drop
   - Visual feedback: opacity 0.7 + cyan border during drag

2. **Position Update Connection**
   - **File**: `src/ui/qml/InteractiveOverlayPreview.qml:149-151`
   - Connected `onPositionChanged` signal to `generator.setCellPosition()`
   - Position changes immediately persisted to OverlayGenerator
   - CellModel automatically updates via `cellLayoutChanged` signal

3. **Visual States and Transitions**
   - **File**: `src/ui/qml/OverlayCell.qml:107-134`
   - Three visual states: default, hovered, dragging
   - Hovered: subtle white border (#40FFFFFF, width: 2)
   - Dragging: cyan border + reduced opacity (0.7)
   - Smooth transitions: 150ms PropertyAnimation for border/opacity changes

**Critical Bug Fixed: Cell Selection Outline Too Large**

**Problem**: Green selection border extending way beyond actual cell content (borders were ~82 pixels tall, should be ~40 pixels)

**Root Cause Analysis**:
- Initial hypothesis: `calculateCellSize()` using maximum values ("399.99 m" instead of "99.9 m") - WRONG
- Changed sample values to typical values - DID NOT FIX
- Real issue discovered: Cell Rectangle sized by `cellCalculatedSize` (pre-calculated in C++), but actual rendered text was much smaller
- The Text element and its background Rectangle were correctly sized, but the outer Rectangle (with green border) was using wrong dimensions

**Solution** (User's insight): Size cells based on actual rendered Text dimensions, not pre-calculated values
- **File**: `src/ui/qml/OverlayCell.qml:35-37`
- Changed from: `width: cellCalculatedSize.width`, `height: cellCalculatedSize.height`
- Changed to: `width: cellText.width + 8`, `height: cellText.height + 8`
- Text element positioned absolutely: `x: 4`, `y: 4` (instead of anchors)
- Cell now sizes itself automatically based on actual rendered content

**Key Insight**: Don't try to pre-calculate text dimensions in C++ with QFontMetrics when QML Text element already knows its own rendered size. Use the actual rendered dimensions!

**Files Modified**:
1. `src/ui/qml/OverlayCell.qml`
   - Added drag behavior with boundary clamping
   - Added visual states (hover, dragging)
   - Changed cell sizing to use actual text dimensions
   - Changed text positioning from anchors to absolute positioning

2. `src/ui/qml/InteractiveOverlayPreview.qml`
   - Connected cell position changes to generator

3. `src/generators/overlay_gen.cpp` (attempted fix, not the solution)
   - Changed sample values from maximum to typical (kept this change for consistency)

**Build Status**: ✅ Compiles and links successfully

**Testing Results**:
- ✅ Cells can be dragged smoothly within preview area
- ✅ Cell positions constrained to background bounds (no dragging outside)
- ✅ Position updates persist in generator immediately
- ✅ Preview updates in real-time during drag
- ✅ Green selection border now perfectly fits cell content
- ✅ Works with both default template (640×120) and custom templates (1080×1080)
- ✅ Drag performance smooth (60 FPS)
- ✅ Visual feedback clear (cyan border + reduced opacity during drag)
- ✅ Cursor changes appropriately (open hand → closed hand)

**Performance**: Excellent. Drag operations are smooth with no lag.

**Key Implementation Details**:
- **Coordinate Conversion**: Pixels → normalized (0.0-1.0) on drop, normalized → pixels on render
- **Boundary Clamping**: Applied before normalization to prevent cells from leaving visible area
- **Auto-sizing**: Cells use `Text.width/height` for perfect content fitting
- **Drag Visual**: Opacity 0.7 + cyan border for clear feedback
- **State Transitions**: 150ms smooth animations between states

**Lessons Learned**:
1. **QML knows best**: When QML elements already calculate their rendered size, don't try to pre-calculate in C++
2. **Trust the actual rendering**: Font metrics calculations never quite match actual rendered dimensions
3. **User insights are valuable**: User correctly identified that we should use the background Rectangle's size (which was already correct)
4. **Simple solutions**: Changed from complex pre-calculation to simple `width: cellText.width + 8`

**Next Steps**: Ready for Phase 4 - Dynamic Cell Sizing (may be partially complete due to auto-sizing implementation)

**Notes**:
- Phase 3 fully functional with all deliverables complete
- Cell sizing now properly auto-adapts to content
- Drag-and-drop UX polished and professional
- No remaining known issues with interactive preview

---

### Session 5 (2026-02-02)
**Completed**: Visual Alignment Guides Feature + Critical Bug Fixes

**Context**: User requested visual alignment guides to improve cell positioning UX. Also fixed critical app freeze bug discovered during testing.

**Critical Bug Fix: App Freeze When Loading Template**

**Problem**: Application would freeze completely when loading a template, requiring force quit.

**Root Cause**: Infinite loop in grid canvas `onPaint()` method when `gridSpacing` was 0 (default value before template fully loaded).
- **File**: `src/ui/qml/InteractiveOverlayPreview.qml:277-312`
- Loop: `for (var x = 0; x <= width; x += spacingX)` with `spacingX = 0` → infinite loop

**Solution**: Added guard to exit early if spacing is too small:
```qml
// Guard against infinite loop: skip if spacing is too small
if (spacingX < 1 || spacingY < 1 || width < 1 || height < 1) {
    return
}
```

**New Feature: Visual Alignment Guides**

**Problem**: Snap-to-grid alone didn't feel natural for cell alignment. Users needed visual feedback when cells align horizontally/vertically (like Figma, Sketch, etc.).

**Implementation**:

1. **Alignment Guide Properties**
   - **File**: `src/ui/qml/InteractiveOverlayPreview.qml:23-29`
   ```qml
   property var alignmentGuides: []  // Array of alignment guide objects
   property int alignmentThreshold: 5  // Pixels within which cells are considered aligned
   property int draggingCellCount: 0  // Counter for dragging cells
   property bool anyDragging: draggingCellCount > 0
   ```

2. **Alignment Detection Function**
   - **File**: `src/ui/qml/InteractiveOverlayPreview.qml:78-174`
   - `detectAlignments()` function checks 6 alignment types:
     - **Top edge**: Dragging cell's top aligns with other cell's top
     - **Bottom edge**: Dragging cell's bottom aligns with other cell's bottom
     - **Left edge**: Dragging cell's left aligns with other cell's left
     - **Right edge**: Dragging cell's right aligns with other cell's right
     - **Center-X**: Vertical centers align
     - **Center-Y**: Horizontal centers align
   - Threshold scaled based on preview zoom level for consistent behavior
   - Guides stored as objects: `{type, x/y, x1/y1, x2/y2}`

3. **Alignment Canvas**
   - **File**: `src/ui/qml/InteractiveOverlayPreview.qml:317-361`
   - Canvas element draws guide lines during drag
   - **Cyan lines** for edge alignment (top, bottom, left, right)
   - **Gold lines** for center alignment
   - Lines span between aligned cells
   - Visibility: `visible: anyDragging && alignmentGuides.length > 0`

4. **Counter-Based Drag Detection** (Critical Fix)
   - **Problem**: Initial `anyDragging` computed property with loop didn't work:
     ```qml
     // BROKEN: QML bindings can't track dependencies inside loops
     property bool anyDragging: {
         for (var i = 0; i < cellRepeater.count; i++) {
             if (cellRepeater.itemAt(i).dragging) return true
         }
         return false
     }
     ```
   - **Root Cause**: QML binding system cannot track `cellRepeater.itemAt(i).dragging` as a dependency. The property only evaluates once on initialization and never re-evaluates.
   - **Solution**: Counter-based approach with explicit increment/decrement:
     ```qml
     property int draggingCellCount: 0
     property bool anyDragging: draggingCellCount > 0
     ```
   - **Cell delegate** increments/decrements counter:
     ```qml
     onDraggingChanged: {
         if (dragging) {
             interactivePreview.draggingCellCount++
         } else {
             interactivePreview.draggingCellCount--
             interactivePreview.alignmentGuides = []
         }
     }
     ```

5. **Integration with Cell Drag**
   - **File**: `src/ui/qml/InteractiveOverlayPreview.qml:404-423`
   - `detectAlignments()` called from `onXChanged` and `onYChanged` handlers (via `Qt.callLater()`)
   - Guides cleared when drag ends (`onDraggingChanged` handler)

**Files Modified**:

1. `src/ui/qml/InteractiveOverlayPreview.qml`
   - Added alignment guide properties and counter-based drag detection
   - Added `detectAlignments()` function
   - Added `alignmentCanvas` Canvas element
   - Added guard against infinite loop in grid canvas
   - Updated cell delegate with counter increment/decrement
   - Connected alignment detection to cell position changes

**Build Status**: ✅ Compiles and links successfully

**Testing Results**:
- ✅ App no longer freezes when loading template
- ✅ Cyan alignment guides appear when dragging cell near another cell's edge
- ✅ Gold alignment guides appear when centers align
- ✅ Guides span between the two aligned cells
- ✅ Guides disappear immediately when drag ends
- ✅ Works correctly at different zoom levels (threshold scales with preview size)
- ✅ Multiple alignments can show simultaneously
- ✅ Performance remains smooth during drag

**Key Technical Insight: QML Binding Limitations**

**Critical Lesson**: QML computed properties with loops DO NOT automatically re-evaluate when individual items change.

```qml
// THIS DOES NOT WORK:
property bool anyDragging: {
    for (var i = 0; i < repeater.count; i++) {
        if (repeater.itemAt(i).dragging) return true  // Binding system can't track this!
    }
    return false
}

// THIS WORKS:
property int draggingCellCount: 0
property bool anyDragging: draggingCellCount > 0
// + increment/decrement counter in delegate's onDraggingChanged
```

**Why**: QML's binding system tracks direct property references (e.g., `someItem.property`), but cannot track dynamically accessed items via `itemAt(i)` inside loops. The computed property is evaluated once and cached, never re-evaluated when individual repeater items change.

**Pattern**: For tracking aggregate state across Repeater items, use a counter property that delegates explicitly modify.

**Performance**: No degradation. Counter approach is more efficient than loop-based polling.

**Notes**:
- Visual alignment guides significantly improve cell positioning UX
- Counter-based approach is the correct QML pattern for aggregate repeater state
- Documented QML limitation for future reference
- All Phase 7 "Nice to Have" items for alignment guides now complete

---

### Session 6 (2026-02-13)
**Completed**: Main Preview Cell-Based Rendering - Font Size & Text Alignment Fixes

**Context**: The main preview (left panel, C++ rendering) was not matching the interactive preview (right panel, QML rendering) when using template cell positions. Three issues were identified:
1. TTS not displayed when in deco mode ✅ Fixed in previous work
2. Cell geometry wrong (all cells same size) ✅ Fixed in previous work
3. Font settings ignored (font size and text alignment wrong) ← Fixed in this session

**Root Cause Analysis**:

| Aspect | QML (Interactive Preview) | C++ (Main Preview) - Before Fix |
|--------|---------------------------|----------------------------------|
| Canvas size | Scaled to fit preview panel (~400px) | Full template resolution (1024px) |
| Font rendering | Font used directly in scaled context | Font used directly on full canvas |
| Result | Normal-sized text | Tiny text (scaled down with image) |
| Text alignment | `Text.AlignHCenter` (centered) | `Qt::AlignLeft` (left-aligned) |

**Key Insight**: QML renders at preview size and displays at that size. C++ renders at full template resolution, then the image is scaled down for display. A 12pt font on a 1024px canvas, when scaled to ~400px, appears as ~5pt.

**Solution**: Scale font for template resolution and center text alignment.

**Changes Made to `renderCellBasedOverlay()` in `src/generators/overlay_gen.cpp`**:

1. **Font Scaling** (lines 914-918):
   ```cpp
   // Scale font for template resolution (match calculateCellSize behavior)
   QFont renderFont = effectiveFont;
   renderFont.setPixelSize(getScaledFontSize(effectiveFont, 1.8));
   ```
   Uses the same scaling factor as `calculateCellSize()` to ensure consistency.

2. **Text Alignment** (lines 923-924, 943):
   ```cpp
   // Changed from Qt::AlignLeft to Qt::AlignHCenter
   QRect textBounds = fm.boundingRect(QRect(0, 0, 1000, 1000),
                                      Qt::AlignHCenter | Qt::TextWordWrap, displayText);
   // ...
   painter.drawText(cellRect, Qt::AlignHCenter, displayText);
   ```

**Important Observation - Expected Differences**:

The main preview and interactive preview will still look slightly different due to **panel size differences**:
- The main preview panel and interactive preview panel have different dimensions
- Both render at full template resolution, then scale to fit their respective panels
- Since the scale factors differ, the rendered text will appear slightly different sizes
- This is **expected and correct behavior** - the actual overlay export will match the template resolution exactly

**Files Modified**:
1. `src/generators/overlay_gen.cpp` - Updated `renderCellBasedOverlay()` with font scaling and text centering

**Build Status**: ✅ Compiles and links successfully

**Testing Results**:
- ✅ Main preview text now centered (matches QML)
- ✅ Font size proportional to template resolution
- ✅ Both previews show similar rendering (accounting for scale differences)
- ✅ Cell positions match between previews
- ✅ All cell types render correctly (Depth, Temp, Time, NDL/TTS, Pressure, PO2)

**Key Takeaways**:
1. **C++ rendering at full resolution**: When C++ renders to full template size and the result is scaled for display, fonts must be scaled proportionally
2. **QML implicit scaling**: QML handles scaling automatically because the entire scene is scaled, including text
3. **`getScaledFontSize(font, 1.8)`**: The 1.8 scale factor matches what `calculateCellSize()` uses for value text
4. **Panel size affects appearance**: Different panel sizes = different scale factors = slightly different visual appearance (this is expected)

**Architecture Note**:

The rendering pipeline now follows this pattern:
```
┌─────────────────────────────────────────────────┐
│           generateCellDisplayText()              │
│   Single source of truth for text formatting    │
│   (matches CellModel::formatValue() in QML)     │
└─────────────────────┬───────────────────────────┘
                      │
    ┌─────────────────┴─────────────────┐
    ↓                                   ↓
┌───────────────────┐           ┌───────────────────┐
│  C++ Rendering    │           │  QML Rendering    │
│  (Main Preview)   │           │  (Interactive)    │
│                   │           │                   │
│  Scale font with  │           │  Uses font        │
│  getScaledFontSize│           │  directly         │
│  (1.8x for value) │           │  (scene scales)   │
│                   │           │                   │
│  AlignHCenter     │           │  AlignHCenter     │
└───────────────────┘           └───────────────────┘
```

**Notes**:
- Main preview now matches interactive preview as closely as possible
- Remaining visual differences are due to panel size differences (expected)
- Font scaling ensures exported overlays look correct at template resolution
- Text centering matches QML's `Text.AlignHCenter` behavior

---

### Session 7 (2026-03-05)
**Completed**: Template System Overhaul — Persistence, Cross-Platform Paths, Font Scaling

**Context**: Completing the template management system for 0.1 release. Multiple issues discovered and fixed across template loading, saving, persistence, cross-platform compatibility, and visual consistency.

#### 1. Template UI Restructure

Renamed `templateFileDialog` → `backgroundImageDialog` in `OverlayEditor.qml`. Added `FolderDialog` (`templateDirDialog`) for browsing template directories. Updated default template paths from `:/default_overlay.png` to `:/images/DC_Faces/unabara_round_ocean.png` across all files.

**Files**: `src/ui/qml/OverlayEditor.qml`, `src/generators/overlay_gen.cpp`, `src/core/config.cpp`, `src/core/overlay_template.cpp`

#### 2. QRC Path Normalization

**Problem**: `QImage` doesn't understand `qrc:/` URLs, only `:/` resource prefix. Templates embedded via `resources.qrc` failed to load.

**Fix**: Added normalization in `setTemplatePath()` and `loadTemplate()` to strip `qrc:` prefix:
```cpp
if (normalizedPath.startsWith("qrc:/")) {
    normalizedPath = normalizedPath.mid(3); // "qrc:/" -> ":/"
}
```

**Files**: `src/generators/overlay_gen.cpp`

#### 3. Windows File Path Fix

**Problem**: `selectedFile.toString().replace("file://", "")` on Windows yielded `/C:/Users/...` (invalid path with leading `/`).

**Fix**: Replaced all 4 fragile URL-to-path conversions in dialog handlers with `mainWindow.urlToLocalFile()` (using `QUrl::toLocalFile()` under the hood).

**Files**: `src/ui/qml/OverlayEditor.qml`

#### 4. Template ComboBox Synchronization

**Problem**: After load/save, the ComboBox didn't reflect the current template (showed empty name or wrong template).

**Root Cause**: `getAvailableTemplates()` internally called `refreshTemplateList()` which cleared lists, so `indexOfTemplatePath()` additions were lost.

**Fix**:
- Made `getAvailableTemplates()` only refresh if list is empty
- Added `indexOfTemplatePath()` method to find/append templates in the list
- Reordered QML calls: `refreshTemplateList()` → `indexOfTemplatePath()` → set model → set `currentIndex`

**Files**: `include/generators/overlay_gen.h`, `src/generators/overlay_gen.cpp`, `src/ui/qml/OverlayEditor.qml`

#### 5. Template Directory Persistence

**Problem**: Changing template directory wasn't saved — `setTemplateDirectory()` didn't call `saveConfig()`.

**Fix**: Added `saveConfig()` call to the setter.

**Files**: `src/core/config.cpp`

#### 6. Startup Template & ComboBox Sync

**Problem**: On first start, no template loaded. On subsequent starts, the background matched the last template but the ComboBox defaulted to index 0.

**Fix**:
- Added `activeTemplatePath` Q_PROPERTY to Config (persisted via QSettings)
- Generator loads active template at startup (or first bundled template on first run)
- ComboBox uses `Component.onCompleted` to sync `currentIndex` with `activeTemplatePath`
- Active template path saved on every load/save

**Files**: `include/core/config.h`, `src/core/config.cpp`, `src/generators/overlay_gen.cpp`, `src/ui/qml/OverlayEditor.qml`

#### 7. Template Preservation on Dive Import

**Problem**: Loading a dive called `initializeDefaultCellLayout()` unconditionally, destroying loaded templates (e.g., 4-cell template replaced by 5 default cells, font reverted to "Arial Size: 14").

**Fix**:
- Guarded `onDiveChanged` with `cellCount() === 0` check
- Replaced all 9 `onShow*Changed` handlers from `initializeDefaultCellLayout()` to `setCellVisible()` (non-destructive toggle)
- Added `Q_INVOKABLE int cellCount()` to OverlayGenerator

**Files**: `include/generators/overlay_gen.h`, `src/ui/qml/OverlayEditor.qml`

#### 8. Cell Font Scaling in Interactive Editor

**Problem**: Cells in the interactive editor (right panel) didn't scale fonts proportionally with the background image when resizing.

**Root Cause**: C++ renders with `getScaledFontSize(font, 1.8)` (= `pointSize * 1.33 * 1.8` pixels), but QML delegate only scaled by `cellContainer.width / templateWidth`, missing the `1.8x` factor.

**Fix**: Added the 1.8 C++ scale factor to the QML `cellFont` binding:
```qml
cellFont: {
    var scaleX = root.generator && root.generator.templateWidth > 0
        ? cellContainer.width / root.generator.templateWidth : 1.0
    var f = model.font
    var scaledSize = f.pointSize * 1.8 * scaleX
    return Qt.font({
        family: f.family,
        pointSize: Math.max(1, Math.round(scaledSize)),
        bold: f.bold, italic: f.italic
    })
}
```

The font in Text Settings still reads from `generator.font` (unscaled value) — the `cellFont` scaling is purely visual for the editor preview.

**Files**: `src/ui/qml/InteractiveOverlayPreview.qml`

#### Summary of All Files Modified

| File | Changes |
|------|---------|
| `src/ui/qml/OverlayEditor.qml` | Dialog renames, `urlToLocalFile()`, ComboBox sync, template preservation, `setCellVisible()` handlers |
| `src/ui/qml/InteractiveOverlayPreview.qml` | Cell font scaling with 1.8x factor |
| `src/generators/overlay_gen.cpp` | QRC normalization, default template path, `getAvailableTemplates()` guard, `indexOfTemplatePath()`, startup template loading |
| `include/generators/overlay_gen.h` | Added `indexOfTemplatePath()`, `cellCount()` |
| `src/core/config.cpp` | `activeTemplatePath` persistence, `saveConfig()` in `setTemplateDirectory()`, default path update |
| `include/core/config.h` | Added `activeTemplatePath` Q_PROPERTY |
| `src/core/overlay_template.cpp` | Default background and font updates |

**Key Takeaways**:
1. **Qt resource paths**: `QImage` uses `:/`, QML uses `qrc:/` — always normalize
2. **Cross-platform URLs**: Use `QUrl::toLocalFile()`, never manual string replacement
3. **QML ComboBox**: Programmatic `currentIndex` changes don't fire `onActivated`
4. **Destructive operations**: Guard `initializeDefaultCellLayout()` — prefer `setCellVisible()` for toggles
5. **Font scaling consistency**: Both previews must account for the same 1.8x scale factor used in C++ rendering

---

### Session 7 (2026-03-10)
**Completed**: Tank Cell Visibility Fixes, Display Option Sync, macOS Build Fix

**Bug 1 — Excess tank cells shown for single-tank dives**:
Templates with multiple pressure cells (e.g., 4-tank CCR template) showed all tank cells even when the imported dive had only 1 tank.

**Root Cause**: `onDiveChanged` preserved templates by skipping `initializeDefaultCellLayout()` when cells existed, but never adjusted tank cell visibility. The "Show Pressure" toggle only targeted the `"pressure"` cell ID, missing multi-tank IDs (`"tank_0"`, `"tank_1"`, etc.).

**Fix**: Added two new methods to `OverlayGenerator`:
- `adjustTankCellVisibility(DiveData* dive)` — hides pressure cells whose `tankIndex >= dive->cylinderCount()`
- `setPressureCellsVisible(bool visible, DiveData* dive)` — toggles all pressure-type cells, respects tank count, and creates default pressure cells if the template has none

Called `adjustTankCellVisibility()` from `onDiveChanged` in QML. Replaced `setCellVisible("pressure", ...)` with `setPressureCellsVisible(...)` in `onShowPressureChanged`.

**Bug 2 — Display option checkboxes not syncing on template load**:
Loading a template without pressure cells (e.g., `OC_Rec_NoTanks_Ocean`) left "Show Tank Pressure" checked.

**Root Cause**: `loadTemplate()` only updated `m_show*` flags from cells present in the template. Missing cell types left flags at their previous (stale) values.

**Fix**: Reset all `m_show*` flags to `false` before the cell scan loop. Emit all `show*Changed` signals after loading so QML checkboxes update.

**Bug 3 — Enabling pressure on no-pressure template did nothing**:
Checking "Show Tank Pressure" after loading a no-pressure template showed nothing because there were no pressure cells to toggle.

**Fix**: `setPressureCellsVisible()` now creates default pressure cell(s) when enabling pressure and none exist, based on the dive's actual tank count.

**Bug 4 — macOS DMG missing QtQuick/QtMultimedia QML plugins**:
The `macdeployqt` command was missing `-qmldir`, so QML plugins weren't bundled.

**Fix**: Added `-qmldir=${{github.workspace}}/src/ui/qml` to the `macdeployqt` command in `.github/workflows/build_releases.yml`.

**New bundled templates added to `resources.qrc`**:
- `OC_Rec_NoTanks_Titanium.utp`
- `OC_Rec_1Tank_Ocean.utp`
- `OC_Rec_1Tank_Titanium.utp`
- `OC_Tek_All_Data_4Tanks.utp`

#### Files Modified

| File | Changes |
|------|---------|
| `src/generators/overlay_gen.cpp` | Added `adjustTankCellVisibility()`, `setPressureCellsVisible()` with cell creation; reset `m_show*` flags in `loadTemplate()`; emit `show*Changed` signals |
| `include/generators/overlay_gen.h` | Declared `adjustTankCellVisibility()`, `setPressureCellsVisible()` |
| `src/ui/qml/OverlayEditor.qml` | Call `adjustTankCellVisibility()` in `onDiveChanged`; use `setPressureCellsVisible()` in `onShowPressureChanged` |
| `.github/workflows/build_releases.yml` | Added `-qmldir` to `macdeployqt` command |

---
