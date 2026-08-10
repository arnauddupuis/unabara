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
    ${CMAKE_SOURCE_DIR}/src/core/cell_data.cpp
    ${CMAKE_SOURCE_DIR}/src/core/overlay_template.cpp
    ${CMAKE_SOURCE_DIR}/src/core/dive_data.cpp
    ${CMAKE_SOURCE_DIR}/src/core/log_parser.cpp
    ${CMAKE_SOURCE_DIR}/src/core/units.cpp
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
        TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data")
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
