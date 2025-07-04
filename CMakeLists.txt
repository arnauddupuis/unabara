cmake_minimum_required(VERSION 3.16)

# Set a default version in case VERSION file doesn't exist
set(UNABARA_VERSION "0.1")

# Try to read version from VERSION file
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION.md")
    file(READ "${CMAKE_CURRENT_SOURCE_DIR}/VERSION.md" UNABARA_VERSION)
    string(STRIP "${UNABARA_VERSION}" UNABARA_VERSION)
    message(STATUS "Building Unabara version: ${UNABARA_VERSION}")
else()
    message(STATUS "VERSION file not found, using default version: ${UNABARA_VERSION}")
endif()


project(Unabara LANGUAGES CXX)


set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Quick Qml Xml Multimedia DBus Concurrent Widgets)

# Configure a version header file
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/templates/version.h.in"
    "${CMAKE_CURRENT_BINARY_DIR}/include/version.h"
    @ONLY
)

set(PROJECT_SOURCES
    src/main.cpp
    src/core/dive_data.cpp
    src/core/log_parser.cpp
    src/core/config.cpp
    src/core/units.cpp
    src/ui/main_window.cpp
    src/ui/timeline.cpp
    src/generators/overlay_gen.cpp
    src/generators/overlay_image_provider.cpp
    src/export/image_export.cpp
    src/export/video_export.cpp
    resources.qrc
)

set(PROJECT_HEADERS
    include/core/dive_data.h
    include/core/log_parser.h
    include/core/config.h
    include/core/units.h
    include/ui/main_window.h
    include/ui/timeline.h
    include/generators/overlay_gen.h
    include/generators/overlay_image_provider.h
    include/export/image_export.h
    include/export/video_export.h
    "${CMAKE_CURRENT_BINARY_DIR}/include/version.h"
)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

qt_add_executable(unabara
    ${PROJECT_SOURCES}
    ${PROJECT_HEADERS}
)

if(APPLE)
    set_target_properties(unabara PROPERTIES MACOSX_BUNDLE TRUE)
endif()

target_include_directories(unabara PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_BINARY_DIR}/include
)

target_link_libraries(unabara PRIVATE
    Qt::Core
    Qt::Gui
    Qt::Quick
    Qt::Qml
    Qt::Xml
    Qt::Concurrent
    Qt::Widgets
    Qt::Multimedia
    Qt::DBus
)

install(TARGETS unabara
    BUNDLE DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
