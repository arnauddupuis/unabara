# Unabara

<p align="center">
  <img src="resources/images/unabara-logo.svg" alt="Unabara Logo" width="200"/>
</p>

Unabara is a powerful tool for creating telemetry overlays for scuba diving videos. It extracts data from dive logs and generates customizable overlays that can be composited with your diving footage.

## Features

- **Import Dive Logs**: Import Subsurface (XML/SSRF) dive logs to extract comprehensive diving telemetry
- **Visual Timeline**: View and navigate your dive data on an interactive timeline
- **Video Import**: Import video footage and sync it with your dive data
- **Customizable Overlay**: Configure which telemetry data appears in your overlay (depth, temperature, NDL, tank pressure, dive time)
- **Template Selection**: Choose from built-in overlay templates or import your own designs
- **Export Options**:
  - Export as image sequence for video editing software
  - Export directly as video file (requires FFmpeg)

## Screenshots

*Screenshots coming soon*

## Requirements

- Qt 6.9.0 or newer
- C++17 compatible compiler
- FFmpeg (optional, required for direct video export)

## Building from Source

### Dependencies

- Qt 6.9.0 or newer (Core, Gui, Quick, Qml, Xml, Concurrent, Widgets)
- CMake 3.16 or newer
- C++17 compatible compiler (GCC 9+, Clang 10+, MSVC 2019+)

### Linux

```bash
# Install dependencies (Ubuntu/Debian)
sudo apt install build-essential cmake qt6-base-dev qt6-declarative-dev libqt6xml6-dev

# Install dependencies (Fedora)
sudo dnf install cmake gcc-c++ qt6-qtbase-devel qt6-qtdeclarative-devel qt6-qtbase-private-devel

# Install dependencies (Arch Linux)
sudo pacman -S cmake base-devel qt6-base qt6-declarative

# Clone the repository
git clone https://github.com/arnauddupuis/unabara.git
cd unabara

# Create build directory
mkdir build && cd build

# Configure and build
cmake ..
cmake --build .

# Run the application
./bin/unabara
```

### macOS

```bash
# Install dependencies with Homebrew
brew install qt6 cmake

# Clone the repository
git clone https://github.com/arnauddupuis/unabara.git
cd unabara

# Create build directory
mkdir build && cd build

# Configure and build
cmake .. -DQt6_DIR=$(brew --prefix qt6)/lib/cmake/Qt6
cmake --build .

# Run the application
./bin/unabara
```

### Windows

1. Install [Qt 6.9.0](https://www.qt.io/download) or newer
2. Install [CMake](https://cmake.org/download/)
3. Install [Visual Studio 2019](https://visualstudio.microsoft.com/downloads/) or newer with C++ desktop development workload

```powershell
# Clone the repository
git clone https://github.com/arnauddupuis/unabara.git
cd unabara

# Create build directory
mkdir build
cd build

# Configure and build
cmake .. -DCMAKE_PREFIX_PATH=C:\path\to\Qt\6.9.0\msvc2019_64
cmake --build . --config Release

# Run the application
.\bin\Release\unabara.exe
```

## Video Export

For direct video export functionality, FFmpeg needs to be installed on your system:

- **Linux**: 
   - _Ubuntu/Debian_: `sudo apt install ffmpeg`
   - _Fedora_: `sudo apt install ffmpeg`
   - _Arch Linux_: `sudo pacman -S ffmpeg`
- **macOS**: `brew install ffmpeg`
- **Windows**: Download from [FFmpeg website](https://ffmpeg.org/download.html)

## Usage

1. Launch Unabara
2. Import a dive log file (Subsurface XML/SSRF format)
3. Optionally import your dive video footage
4. Adjust the positioning and video sync timing using the timeline
5. Configure the overlay display options in settings
6. Export as image sequence or video file

## License

Unabara is licensed under the GNU General Public License v2.0.

## Name Origin

"Unabara" (海原) is a Japanese word meaning "the ocean" or "the great expanse of the sea."