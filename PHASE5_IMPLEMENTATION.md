# Phase 5: Per-Cell Property Editing - Implementation Summary

## Overview
Phase 5 adds the ability to select individual cells and customize their font and color properties independently from the global defaults. This provides fine-grained control over overlay appearance.

## Features Implemented

### 1. Cell Selection Tracking
- **File**: `include/generators/overlay_gen.h`, `src/generators/overlay_gen.cpp`
- Added `selectedCellId` Q_PROPERTY to OverlayGenerator
- Tracks which cell (if any) is currently selected for editing
- Emits `selectedCellIdChanged()` signal when selection changes

### 2. Dual-Mode Property Editing
- **File**: `src/generators/overlay_gen.cpp`
- Modified `setFont()` and `setTextColor()` methods to support two modes:

#### No Selection (Global Mode)
- When `selectedCellId` is empty
- Changes apply to all cells that don't have custom properties
- Updates the global default (stored in Config)
- Only affects cells with inherited properties

#### Cell Selected (Per-Cell Mode)
- When a specific cell is selected
- Changes apply only to the selected cell
- Marks the property as "custom" on that cell
- Does not affect global defaults or other cells

### 3. Reset to Global Functionality
- **Methods**: `resetCellFont()`, `resetCellColor()`
- Clears per-cell customizations
- Reverts cell to global defaults
- Removes custom property flag

### 4. UI Enhancements

#### Editing Mode Indicator
- **File**: `src/ui/qml/OverlayEditor.qml` (lines 30-68)
- Visual banner at top of editor showing current mode:
  - Green background + "✓ Cell Selected: [cellId]" when cell is selected
  - Gray background + "⊞ Editing All Cells" when no selection
- Includes "Deselect" button to clear selection

#### Updated Text Settings GroupBox
- Title changes based on mode:
  - "Text Settings - Cell: [cellId]" when editing a specific cell
  - "Text Settings - All Cells" when editing globally

#### Reset Buttons
- **Location**: Next to Font and Color controls
- Shows "↺" reset button (always visible in 3-column grid layout)
- Button states:
  - **Enabled** (100% opacity): Cell is selected AND has custom font/color
  - **Disabled** (30% opacity): No selection or cell doesn't have custom property
- Tooltip changes based on state:
  - "Reset to global font/color" when enabled
  - "No custom font/color" when disabled
- Clicking resets the property to global default
- **Grid Layout**: Uses `enabled` instead of `visible` to maintain consistent 3-column layout

#### Visual Indicators on Cells
- **File**: `src/ui/qml/OverlayCell.qml` (lines 65-107)
- Small colored dots in top-right corner of cells:
  - **Cyan dot**: Cell has custom font
  - **Magenta dot**: Cell has custom color
- Tooltips appear on hover
- Helps identify which cells have customizations

### 5. Selection Integration and Synchronization
- **File**: `src/ui/qml/OverlayEditor.qml`, `src/ui/qml/InteractiveOverlayPreview.qml`
- **Bidirectional sync** between generator and preview:
  - Preview → Generator: `onCellSelected` sets `generator.selectedCellId = cellId`
  - Generator → Preview: `Connections` to `generator.onSelectedCellIdChanged()` updates preview
- **Critical**: Uses `Connections` signal handler instead of property binding
  - Property bindings break when preview directly assigns `selectedCellId` (on cell click)
  - Signal-based approach works in both directions without breaking
- Enables Deselect button to clear visual selection in preview

### 6. Reactive Property System
- **File**: `src/ui/qml/OverlayEditor.qml`
- **Reactive Properties**: `currentFont`, `currentColor`, `currentHasCustomFont`, `currentHasCustomColor`
- These update automatically when:
  - Selection changes (`onSelectedCellIdChanged`)
  - Cell data changes (`generator.onCellsChanged`)
  - Global properties change (when no selection)
- **Cell Property Access**: Reads actual values from InteractiveOverlayPreview's exposed `cellRepeater`
  - `getCellProperty()` iterates through Repeater items to find selected cell
  - Accesses cell's `cellFont`, `cellTextColor`, `hasCustomFont`, `hasCustomColor` properties
  - Returns selected cell's properties or global defaults if no selection
- **UI Controls**: Font selector, size spinner, color button all bind to reactive properties
- **Update Triggers**: Uses `onActivated` (ComboBox) and `onValueModified` (SpinBox) to avoid feedback loops

## User Workflow

### Editing All Cells (Global Mode)
1. Ensure no cell is selected (click background or "Deselect" button)
2. Change font/color in Text Settings
3. All cells without custom properties are updated
4. Global defaults are saved to Config

### Editing a Specific Cell
1. Click a cell in the preview (green outline appears)
2. Editing mode indicator shows "✓ Cell Selected: [cellId]"
3. Change font/color in Text Settings
4. Only the selected cell is affected
5. Small cyan/magenta dots appear on the cell

### Resetting Custom Properties
1. Select a cell with custom properties (has cyan/magenta dots)
2. Click the "↺" reset button next to the customized property
3. Cell reverts to global default
4. Indicator dot disappears

## Technical Implementation Details

### Property Inheritance System
- Cells store both the property value AND a flag indicating if it's custom
- `CellData::hasCustomFont()` and `hasCustomColor()` track inheritance
- When resetting, the flag is set to `false` and value copied from global

### Signal Flow
```
User clicks cell in preview
  ↓
InteractiveOverlayPreview.cellSelected(cellId)
  ↓
OverlayEditor sets generator.selectedCellId
  ↓
generator.selectedCellIdChanged() signal
  ↓
UI updates (mode indicator, reset button visibility)
  ↓
User changes font/color
  ↓
generator.setFont()/setTextColor() checks selectedCellId
  ↓
If selected: applies to cell only
If not selected: applies to all non-custom cells
  ↓
cellsChanged() signal
  ↓
CellModel updates
  ↓
Preview refreshes
```

### Grid Layout Adjustment
- Changed Text Settings GridLayout from 2 to 3 columns
- Added reset buttons in third column
- Added placeholder `Item` elements to maintain alignment

## Files Modified

### C++ Files
1. `include/generators/overlay_gen.h`
   - Added selectedCellId Q_PROPERTY
   - Added resetCellFont() and resetCellColor() methods

2. `src/generators/overlay_gen.cpp`
   - Implemented dual-mode setFont() and setTextColor()
   - Implemented resetCellFont() and resetCellColor()
   - Implemented setSelectedCellId()

### QML Files
1. `src/ui/qml/OverlayEditor.qml`
   - Added editing mode indicator banner with Deselect button
   - Implemented reactive property system (`currentFont`, `currentColor`, etc.)
   - Added `getCellProperty()` helper to access cell data from preview's repeater
   - Updated Text Settings GroupBox title (dynamic based on selection)
   - Added reset buttons for font and color (enabled/disabled states)
   - Connected selection signals (bidirectional sync with generator)
   - Used `Connections` to sync preview selection with generator changes
   - Changed GridLayout to 3 columns with proper alignment

2. `src/ui/qml/OverlayCell.qml`
   - Added visual indicators (cyan/magenta dots) for custom properties
   - Added tooltips for custom property indicators

3. `src/ui/qml/InteractiveOverlayPreview.qml`
   - Exposed `cellRepeater` via property alias for external access
   - Allows OverlayEditor to read actual cell property values

## Issues Encountered and Solutions

### Issue 1: Grid Layout Misalignment
**Problem**: Reset buttons used `visible: false` when disabled, causing GridLayout to collapse from 3 to 2 columns
**Solution**: Changed to `enabled: false` with `opacity: 0.3` to keep buttons visible but inactive

### Issue 2: Controls Don't Show Selected Cell Properties
**Problem**: Font/color controls showed global values even when cell was selected with custom properties
**Solution**:
- Created reactive properties (`currentFont`, `currentColor`) that update when selection changes
- Access actual cell data from InteractiveOverlayPreview's cellRepeater via exposed property alias
- Bind UI controls to reactive properties instead of global properties

### Issue 3: Deselect Button Doesn't Clear Green Outline
**Problem**: Clicking Deselect cleared generator's selectedCellId but preview kept showing green selection border
**Root Cause**: Property binding breaks when direct assignment occurs
  - Cell click: `interactivePreview.selectedCellId = "depth"` breaks the binding
  - Deselect: `generator.selectedCellId = ""` doesn't propagate to preview (binding broken)
**Solution**: Replaced property binding with `Connections` signal handler
  - Listens to `generator.onSelectedCellIdChanged()` signal
  - Explicitly updates preview's selectedCellId on every change
  - Signal handlers don't break like bindings do

## Testing Checklist

- [x] Select a cell and change its font - only that cell should change
- [x] Select a cell and change its color - only that cell should change
- [x] Font/color controls display selected cell's current values
- [x] Deselect and change font - all non-custom cells should change
- [x] Reset button enables when cell has custom property
- [x] Reset button is disabled when no selection or no custom property
- [x] Reset button clears customization and reverts to global
- [x] Visual indicators (cyan/magenta dots) appear correctly
- [x] Editing mode banner updates when selecting/deselecting
- [x] Deselect button clears both selection state and green outline
- [x] Clicking outside cells deselects properly
- [x] Multiple cells can have different custom fonts/colors
- [x] Global changes don't affect cells with custom properties

## Next Steps (Phase 6)

Phase 6 will implement Template Save/Load functionality:
- Export current overlay configuration (including per-cell customizations) to JSON
- Import saved templates
- Template browser/manager UI
- Preset templates included with application

Now that all editing capabilities are complete, we can implement the save/load system without worrying about missing features.
