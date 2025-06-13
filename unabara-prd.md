# Unabara: Dive Telemetry Overlay Tool

## Product Requirements Document (PRD)

### Project Overview

Unabara is a Qt6-based application written in C++ with a QtQuick UI that allows scuba divers to create customized telemetry overlays for their diving videos. The application extracts data from dive logs (primarily Subsurface XML format), visualizes it on an interactive timeline, and enables users to generate and export overlay images or videos that can be composited with diving footage.

### Current Features

#### 1. Dive Log Import
- Import Subsurface XML/SSRF dive log files
- Extract comprehensive dive telemetry data:
  - Depth profile
  - Temperature readings
  - No Decompression Limit (NDL)/Time To Surface (TTS)
  - Cylinder pressure tracking (supports multiple tanks)
  - Gas mix information (air, nitrox, trimix support)

#### 2. Interactive Dive Timeline
- Visualize depth profile on scrollable, zoomable timeline
- View detailed telemetry at specific time points
- Navigate throughout the dive chronology
- Supports mouse and keyboard controls for timeline manipulation
- Display real-time data values at cursor position

#### 3. Video Import and Synchronization
- Import video footage from dive
- Synchronize video timing with dive log data
- Visual alignment of video with depth profile
- Adjustable video offset through UI controls or direct timeline manipulation

#### 4. Customizable Overlay Generation
- Preview overlay in real-time as settings are adjusted
- Toggle display of specific telemetry data:
  - Depth
  - Temperature
  - NDL/Decompression information
  - Tank pressure(s)
  - Dive time
- Visual template selection (default, modern, classic, minimal)
- Font and color customization options

#### 5. Export Capabilities
- Image sequence export (PNG with transparency)
  - Configurable frame rate
  - Export full dive or selected time range
  - Export only video range option
  - Directory naming with dive information
- Video file export using FFmpeg (when available)
  - Multiple codec options (H.264, ProRes, VP9, HEVC)
  - Quality/bitrate adjustment
  - Resolution options including matching source video
  - Estimated file size calculation

#### 6. Multi-platform Support
- Linux (via Flatpak)
- Windows
- macOS (Universal binary with Intel/ARM support)

### Architecture and Technical Implementation

The application follows a modular architecture with clear separation of concerns:

1. **Core Data Module**
   - `DiveData` class for storage and manipulation of dive telemetry
   - `LogParser` for reading and interpreting dive log formats
   - `Config` for application settings management

2. **UI Module**
   - QtQuick-based user interface with responsive layouts
   - `MainWindow` as the primary application window controller
   - `TimelineView` for interactive dive data visualization

3. **Generation Module**
   - `OverlayGenerator` for creating customized data visualizations
   - `OverlayImageProvider` for QML-integrated image generation

4. **Export Module**
   - `ImageExporter` for frame-by-frame image output
   - `VideoExporter` for FFmpeg-based video file creation

### Future Development Opportunities

Based on the current implementation and typical user needs, these areas show promise for future development:

1. **Enhanced Dive Log Support**
   - Add support for more dive computer formats (Suunto, Oceanic, Mares, etc.)
   - Direct USB connection to dive computers for data import
   - Cloud sync with popular dive logging services

2. **Rebreather Support**
   - Implement specific data fields for Closed Circuit Rebreathers (CCR) and semi-Closed Circuit Rebreathers (sCCR):
     - Setpoint tracking and visualization
     - PO2 monitoring display
     - Diluent usage metrics
     - Scrubber timing information
     - Loop temperature data
     - Bailout cylinder information
   - Specialized overlay templates optimized for rebreather data
   - Warning indicators for critical rebreather parameters (PO2 spikes/drops)
   - Integration with popular rebreather log formats

3. **Advanced Video Processing**
   - Built-in video editing capabilities for trimming and selecting segments
   - Picture-in-picture options for showing both diver and telemetry
   - Multiple overlay layouts and positions (corner, side, top/bottom)
   - Motion tracking for dynamic overlay positioning

4. **Extended Telemetry Visualization**
   - 3D dive profile visualization
   - Interactive depth map with position tracking
   - Heart rate and breathing rate visualization (if available in dive log)
   - Environmental data integration (currents, visibility, marine life)

5. **Social and Sharing Features**
   - Direct upload to YouTube/Vimeo with appropriate metadata
   - Sharing dive profiles to social media platforms
   - Community templates and overlay styles
   - Collaborative editing for buddy dive footage

6. **Technical Improvements**
   - GPU acceleration for video processing
   - Improved interpolation algorithms for smoother data visualization
   - Real-time overlay preview during video playback
   - Batch processing for multiple dives/videos

7. **Mobile Companion App**
   - iOS/Android app for mobile editing
   - Remote control of desktop application
   - Field capture of additional dive metadata
   - Synchronization with mobile dive computers

8. **Educational Features**
   - Integration with marine species identification
   - Dive site information overlay
   - Training analysis tools (buoyancy, air consumption, etc.)
   - Comparison with theoretical dive plans

9. **Accessibility Improvements**
   - High-contrast UI options
   - Keyboard navigation enhancements
   - Screen reader compatibility
   - Localization for multiple languages

### Implementation Priorities

For the next development phase, these items are recommended based on potential user value and implementation complexity:

1. **Rebreather Data Support** - Adding support for CCR/sCCR dive logs, with specialized visualization of critical breathing loop parameters
2. **Additional Dive Log Format Support** - Expanding compatibility to reach more users
3. **Enhanced Template System** - More visual options and customization capabilities
4. **Improved Video Synchronization** - More precise alignment tools between video and dive data
5. **Performance Optimization** - Faster rendering and processing for larger dives/longer videos
6. **User Preset System** - Save and recall overlay configurations for consistent style

### Technical Challenges

Future development should address these technical considerations:

1. **Rebreather Data Complexity** - Handling the additional parameters and specialized visualization needs of CCR/sCCR
2. **Memory Management** - Large video files and dive logs require careful memory handling
3. **Cross-Platform Consistency** - Maintaining uniform experience across operating systems
4. **FFmpeg Integration** - Robust handling when FFmpeg is unavailable or fails
5. **Timestamp Synchronization** - Accurate alignment between dive logs and video footage
6. **UI Responsiveness** - Keeping interface fluid during intensive processing tasks

### Conclusion

Unabara has successfully implemented its core functionality, providing divers with a powerful tool to enhance their underwater videos with telemetry data. The planned expansion to include rebreather support will significantly increase its value to technical divers who use CCR and sCCR systems. The modular architecture and solid foundation make it well-positioned for expansion into more sophisticated features while maintaining ease of use. Future development should focus on broadening compatibility, enhancing visualization capabilities, and streamlining the user experience.
