# Unit tests (enable with -DUNABARA_BUILD_TESTS=ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Test Concurrent Quick Multimedia)
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
    ${CMAKE_SOURCE_DIR}/include/core/video_clip.h
    ${CMAKE_SOURCE_DIR}/include/core/clip_list_model.h
    ${CMAKE_SOURCE_DIR}/include/generators/overlay_gen.h
    ${CMAKE_SOURCE_DIR}/include/export/video_export.h
    ${CMAKE_SOURCE_DIR}/include/core/ffmpeg_info.h
    ${CMAKE_SOURCE_DIR}/src/export/export_math.cpp
    ${CMAKE_SOURCE_DIR}/src/export/ffmpeg_args.cpp
    ${CMAKE_SOURCE_DIR}/src/export/frame_compositor.cpp
    # VideoExporter itself is under test since PR B2 (raw-pipe pipeline,
    # driven end-to-end in filtergraph_test when FFmpeg is installed). It
    # pulls FfmpegInfo for the canonical codec-id list.
    ${CMAKE_SOURCE_DIR}/src/export/video_export.cpp
    # ImageExporter (Core+Gui only): its save-failure cleanup contract is
    # driven in export_math_test.
    ${CMAKE_SOURCE_DIR}/include/export/image_export.h
    ${CMAKE_SOURCE_DIR}/src/export/image_export.cpp
    ${CMAKE_SOURCE_DIR}/src/core/ffmpeg_info.cpp
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
    ${CMAKE_SOURCE_DIR}/src/core/ffmpeg_probe.cpp
    ${CMAKE_SOURCE_DIR}/src/core/video_probe.cpp
    ${CMAKE_SOURCE_DIR}/src/core/video_clip.cpp
    ${CMAKE_SOURCE_DIR}/src/core/clip_list_model.cpp
    # VideoClip::thumbnailUrl() builds the image://clipthumb/ id (follow-up C).
    ${CMAKE_SOURCE_DIR}/src/core/clip_thumbnail.cpp
    ${CMAKE_SOURCE_DIR}/src/core/color_grade.cpp
    # UI-layer but Quick-free (Core+Gui only): the colour-grade controller
    # bridges VideoClip <-> ColorGrade for QML and is unit-tested directly.
    # Since C3 its depth-text getters read Config (unit system) — tests
    # that call them redirect QSettings first (clip_list_model_test
    # pattern); everything else on it stays Config-free.
    ${CMAKE_SOURCE_DIR}/include/ui/color_grade_controller.h
    ${CMAKE_SOURCE_DIR}/src/ui/color_grade_controller.cpp
    ${CMAKE_SOURCE_DIR}/src/core/cube_lut.cpp
    ${CMAKE_SOURCE_DIR}/src/core/color_grade_export.cpp
)
target_include_directories(unabara_testlib PUBLIC ${CMAKE_SOURCE_DIR})
# Multimedia: the colour-grade controller's eyedropper reads the player's
# QVideoSink (C4). Linking it here is cheaper and more honest than a
# string-based metaobject hack to keep the dependency out — the CI Qt
# install already ships Multimedia for the app target.
target_link_libraries(unabara_testlib PUBLIC Qt6::Core Qt6::Gui Qt6::Concurrent
    Qt6::Multimedia)

function(unabara_add_test NAME)
    add_executable(${NAME} ${NAME}.cpp)
    target_link_libraries(${NAME} PRIVATE unabara_testlib Qt6::Test)
    target_compile_definitions(${NAME} PRIVATE
        SYNTH_FIT_PATH="${SYNTH_FIT}"
        TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data"
        TEMPLATES_DIR="${CMAKE_SOURCE_DIR}/resources/templates"
        WHATSNEW_JSON_PATH="${CMAKE_SOURCE_DIR}/resources/whatsnew.json"
        COLOR_PRESETS_DIR="${CMAKE_SOURCE_DIR}/resources/color_presets")
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
unabara_add_test(circuit_mode_cell_test)
unabara_add_test(whatsnew_test)
unabara_add_test(export_math_test)
# Pure FFmpeg arg assembly for the raw-pipe export, plus an end-to-end
# VideoExporter run (ffprobe-verified frame count / pix_fmt) that QSKIPs
# when FFmpeg is not installed.
unabara_add_test(filtergraph_test)
# Composited export (PR B3): always-run unit tests for the composite
# filter-graph/arg strings, audio decisions and FrameCompositor geometry
# (QML placement parity), plus end-to-end frame-alignment runs against a
# synthesized burned-in-frame-number source — those QSKIP without FFmpeg.
unabara_add_test(composite_export_test)
unabara_add_test(video_probe_test)
unabara_add_test(clip_list_model_test)
# ClipStrip thumbnails (follow-up C): provider id <-> path/mtime round trip
# through QUrl, path+mtime cache keying, failure caching; the real-FFmpeg
# extraction case QSKIPs when FFmpeg is not installed.
unabara_add_test(clip_thumbnail_test)
unabara_add_test(color_grade_test)
# ColorGradeController: clip write-back round trip, uniforms vs
# GradeOps::resolve ground truth, clip-switch reload, no-clip safety.
unabara_add_test(color_grade_controller_test)
unabara_add_test(cube_lut_test)
unabara_add_test(color_export_test)
# Runs real FFmpeg against the baked static grade; QSKIPs when FFmpeg is
# not installed (FFmpegProbe discovery).
unabara_add_test(ffmpeg_parity_test)

# Timeline facade test. timeline.cpp is UI-layer (it pokes the Quick image
# providers through null-guarded globals), so it is compiled into this one
# test — with the provider symbols stubbed in the test file — instead of the
# core-only testlib. Qt6::Quick is needed for the QQuickImageProvider header;
# nothing Quick is instantiated.
add_executable(timeline_facade_test timeline_facade_test.cpp
    ${CMAKE_SOURCE_DIR}/include/ui/timeline.h
    ${CMAKE_SOURCE_DIR}/src/ui/timeline.cpp)
target_link_libraries(timeline_facade_test PRIVATE unabara_testlib Qt6::Test Qt6::Quick)
add_dependencies(timeline_facade_test unabara_test_fixtures)
add_test(NAME timeline_facade_test COMMAND timeline_facade_test)
set_tests_properties(timeline_facade_test PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

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
