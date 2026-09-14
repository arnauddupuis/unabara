# Unit tests (enable with -DUNABARA_BUILD_TESTS=ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Test)
find_package(Python3 REQUIRED COMPONENTS Interpreter)

# The synthetic FIT fixture is deterministic — regenerate it at build time so
# tests and generator can never drift apart
set(SYNTH_FIT "${CMAKE_CURRENT_BINARY_DIR}/synth_dive.fit")
add_custom_command(
    OUTPUT "${SYNTH_FIT}"
    COMMAND Python3::Interpreter "${CMAKE_CURRENT_SOURCE_DIR}/make_synth_fit.py" "${SYNTH_FIT}"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/make_synth_fit.py"
    COMMENT "Generating synthetic FIT fixture")
add_custom_target(unabara_test_fixtures DEPENDS "${SYNTH_FIT}")

# Core sources under test, built once and shared by all test executables.
# Q_OBJECT headers must be listed so AUTOMOC finds them (they live in
# include/, not next to their .cpp — same pattern as the main target).
add_library(unabara_testlib STATIC
    ${CMAKE_SOURCE_DIR}/include/core/dive_data.h
    ${CMAKE_SOURCE_DIR}/include/core/log_parser.h
    ${CMAKE_SOURCE_DIR}/include/core/units.h
    ${CMAKE_SOURCE_DIR}/include/core/whatsnew.h
    ${CMAKE_SOURCE_DIR}/include/core/config.h
    ${CMAKE_SOURCE_DIR}/include/generators/overlay_gen.h
    ${CMAKE_SOURCE_DIR}/src/export/export_math.cpp
    ${CMAKE_SOURCE_DIR}/src/core/cell_data.cpp
    ${CMAKE_SOURCE_DIR}/src/core/config.cpp
    # Core+Gui only (QPainter, QFontMetrics); pulled in for the cell geometry
    # tests. Tests that construct it must redirect QSettings first — see
    # overlay_geometry_test.cpp.
    ${CMAKE_SOURCE_DIR}/src/generators/overlay_gen.cpp
    ${CMAKE_SOURCE_DIR}/src/core/overlay_template.cpp
    ${CMAKE_SOURCE_DIR}/src/core/dive_data.cpp
    ${CMAKE_SOURCE_DIR}/src/core/log_parser.cpp
    ${CMAKE_SOURCE_DIR}/src/core/units.cpp
    ${CMAKE_SOURCE_DIR}/src/core/whatsnew.cpp
    ${CMAKE_SOURCE_DIR}/src/core/format_parsers/fit_decoder.cpp
    ${CMAKE_SOURCE_DIR}/src/core/format_parsers/fit_parser.cpp
    ${CMAKE_SOURCE_DIR}/src/core/format_parsers/subsurface_parser.cpp
    ${CMAKE_SOURCE_DIR}/src/core/format_parsers/uddf_parser.cpp
    ${CMAKE_SOURCE_DIR}/src/core/format_parsers/parse_utils.cpp
)
target_include_directories(unabara_testlib PUBLIC ${CMAKE_SOURCE_DIR})
target_link_libraries(unabara_testlib PUBLIC Qt6::Core Qt6::Gui)

function(unabara_add_test NAME)
    add_executable(${NAME} ${NAME}.cpp)
    target_link_libraries(${NAME} PRIVATE unabara_testlib Qt6::Test)
    target_compile_definitions(${NAME} PRIVATE
        SYNTH_FIT_PATH="${SYNTH_FIT}"
        TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data"
        TEMPLATES_DIR="${CMAKE_SOURCE_DIR}/resources/templates"
        WHATSNEW_JSON_PATH="${CMAKE_SOURCE_DIR}/resources/whatsnew.json")
    add_dependencies(${NAME} unabara_test_fixtures)
    add_test(NAME ${NAME} COMMAND ${NAME})
    # QFont/QColor tests need a QGuiApplication; keep it headless
    set_tests_properties(${NAME} PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endfunction()

unabara_add_test(fit_parser_test)
unabara_add_test(dive_data_test)
unabara_add_test(subsurface_parser_test)
unabara_add_test(uddf_parser_test)
unabara_add_test(core_utils_test)
unabara_add_test(cell_data_test)
unabara_add_test(overlay_template_test)
unabara_add_test(overlay_geometry_test)
unabara_add_test(whatsnew_test)
unabara_add_test(export_math_test)

# Golden-render regression test: every bundled template rendered through
# render_utp and SHA-256-compared against tests/golden/renders.sha256.
# Renders depend on the Qt/FreeType version, so the driver enforces only when
# the manifest has a section for the running Qt (CI pins one) and is
# advisory on other versions — see tests/golden/golden_renders.py.
add_test(NAME golden_renders
    COMMAND Python3::Interpreter "${CMAKE_CURRENT_SOURCE_DIR}/golden/golden_renders.py" check
        --render-utp "$<TARGET_FILE:render_utp>"
        --qrc "${CMAKE_SOURCE_DIR}/resources.qrc"
        --manifest "${CMAKE_CURRENT_SOURCE_DIR}/golden/renders.sha256"
        --work-dir "${CMAKE_CURRENT_BINARY_DIR}/golden_renders")
set_tests_properties(golden_renders PROPERTIES TIMEOUT 600)

# App-boot smoke test: launch the real binary offscreen with an isolated
# config, let it settle, SIGTERM it, and fail on QML errors in stderr —
# catches missing-property/broken-creation errors that only surface at
# runtime and are invisible to the core-only unit tests. Linux-only: config
# isolation relies on XDG_CONFIG_HOME.
if(UNIX AND NOT APPLE)
    add_test(NAME app_boot_smoke
        COMMAND Python3::Interpreter "${CMAKE_CURRENT_SOURCE_DIR}/boot_smoke.py"
            "$<TARGET_FILE:unabara>")
    set_tests_properties(app_boot_smoke PROPERTIES TIMEOUT 120)
endif()
