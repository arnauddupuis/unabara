cmake_minimum_required(VERSION 3.16)

# Read version from VERSION file
file(STRINGS "VERSION" VERSION_LINE)
string(REGEX REPLACE "UNABARA_VERSION=(.+)" "\\1" UNABARA_VERSION "${VERSION_LINE}")

project(Unabara VERSION ${UNABARA_VERSION} LANGUAGES CXX)

# Add version as a define to be accessible in code
add_definitions(-DUNABARA_VERSION="${UNABARA_VERSION}")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Quick Qml Xml Multimedia DBus Concurrent Widgets)

set(PROJECT_SOURCES
    src/main.cpp
    src/core/dive_data.cpp
    src/core/log_parser.cpp
    src/core/config.cpp
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
    include/ui/main_window.h
    include/ui/timeline.h
    include/generators/overlay_gen.h
    include/generators/overlay_image_provider.h
    include/export/image_export.h
    include/export/video_export.h
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

# Configure a version header file
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/version.h.in
    ${CMAKE_CURRENT_BINARY_DIR}/include/version.h
)

# Add the binary dir to the include path to find the generated version.h
target_include_directories(unabara PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/include)