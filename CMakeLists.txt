cmake_minimum_required(VERSION 3.25)

project(ProjectSEAM VERSION 0.13.1 LANGUAGES CXX)

# Release identity is a single generated contract shared by native binaries,
# plug-in descriptors, package metadata, and evidence producers.  Release
# automation supplies source/build values explicitly; deterministic defaults
# keep local development reproducible without pretending to be a release.
set(SEAM_BUILD_ID "${PROJECT_VERSION}-local" CACHE STRING
    "Project SEAM candidate/build identity")
set(SEAM_SOURCE_COMMIT "0000000000000000000000000000000000000000" CACHE STRING
    "40-character source commit for the generated release identity")
set(SEAM_BUILD_EPOCH "0" CACHE STRING
    "UTC build epoch for the generated release identity")
string(LENGTH "${SEAM_SOURCE_COMMIT}" SEAM_SOURCE_COMMIT_LENGTH)
if(NOT SEAM_SOURCE_COMMIT_LENGTH EQUAL 40 OR
   NOT SEAM_SOURCE_COMMIT MATCHES "^[0-9a-fA-F]+$")
  message(FATAL_ERROR "SEAM_SOURCE_COMMIT must be a 40-character hexadecimal commit")
endif()
if(NOT SEAM_BUILD_EPOCH MATCHES "^[0-9]+$")
  message(FATAL_ERROR "SEAM_BUILD_EPOCH must be a non-negative integer")
endif()

option(SEAM_BUILD_TESTS "Build Project SEAM tests" ON)
option(SEAM_BUILD_BENCHMARKS "Build Project SEAM benchmarks" ON)
option(SEAM_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" ON)
option(SEAM_ENABLE_SANITIZERS "Enable AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
option(SEAM_ENABLE_THREAD_SANITIZER "Enable ThreadSanitizer" OFF)
option(SEAM_ENABLE_IPLUG2_SKIA "Enable the optional iPlug2 + Skia shell adapter" OFF)
option(SEAM_BUILD_NATIVE_DESKTOP "Build the first-party native desktop editor" ON)
option(SEAM_BUILD_CLAP_PLUGIN "Build the Project SEAM CLAP render-player plug-in" ON)
option(SEAM_BUILD_CLAP_EDITOR_PLUGIN "Build the Project SEAM embedded-editor CLAP plug-in" ON)
option(SEAM_RUN_NATIVE_GUI_TESTS
  "Run target-host native editor and Voicebank Studio smoke tests" OFF)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(SeamCompilerOptions)

find_package(Threads REQUIRED)
find_package(OpenSSL 3.0 REQUIRED COMPONENTS Crypto)
find_package(Python3 COMPONENTS Interpreter)

set(SEAM_RENDER_ABI_ID "seam-render-abi-4.1-r1")
set(SEAM_PHONEMIZER_REVISION 1)
set(SEAM_UNIT_SELECTOR_REVISION 1)
set(SEAM_TIMING_SOLVER_REVISION 1)
set(SEAM_RAW_RENDERER_REVISION 2)
set(SEAM_PSOLA_RENDERER_REVISION 2)
set(SEAM_SPECTRAL_RENDERER_REVISION 2)
set(SEAM_STRETCH_RENDERER_REVISION 2)
set(SEAM_SEAM_COMPOSER_REVISION 2)
set(SEAM_PCM_CACHE_FORMAT_REVISION 3)
configure_file(
  cmake/SeamBuildVersion.hpp.in
  ${CMAKE_CURRENT_BINARY_DIR}/generated/seam/build/version.hpp
  @ONLY)

add_library(seam_core
  libs/seam-core/src/logger.cpp
  libs/seam-core/src/log_event.cpp
  libs/seam-core/src/sha256.cpp
  libs/seam-core/src/file_io.cpp
)
target_include_directories(seam_core PUBLIC
  libs/seam-core/include
  ${CMAKE_CURRENT_BINARY_DIR}/generated)
seam_apply_compiler_options(seam_core)

add_library(seam_clap
  libs/seam-clap/src/session.cpp
  libs/seam-clap/src/state_codec.cpp
)
target_include_directories(seam_clap PUBLIC libs/seam-clap/include)
target_link_libraries(seam_clap PUBLIC seam_core)
seam_apply_compiler_options(seam_clap)

add_library(seam_text
  libs/seam-text/src/unicode.cpp
  libs/seam-text/src/text_engine.cpp
  libs/seam-text/src/stb_truetype_impl.cpp
)
target_include_directories(seam_text PUBLIC libs/seam-text/include)
target_include_directories(seam_text PRIVATE third_party/stb)
target_link_libraries(seam_text PUBLIC seam_core)
seam_apply_compiler_options(seam_text)
if(MSVC)
  set_source_files_properties(libs/seam-text/src/stb_truetype_impl.cpp
    PROPERTIES COMPILE_OPTIONS "/w")
else()
  set_source_files_properties(libs/seam-text/src/stb_truetype_impl.cpp
    PROPERTIES COMPILE_OPTIONS "-w")
endif()

add_library(seam_domain
  libs/seam-domain/src/tempo_map.cpp
  libs/seam-domain/src/meter_map.cpp
  libs/seam-domain/src/note.cpp
  libs/seam-domain/src/phoneme.cpp
  libs/seam-domain/src/render_controls.cpp
  libs/seam-domain/src/routing.cpp
  libs/seam-domain/src/project.cpp
)
target_include_directories(seam_domain PUBLIC libs/seam-domain/include)
target_link_libraries(seam_domain PUBLIC seam_core)
seam_apply_compiler_options(seam_domain)


add_library(seam_phonemizer
  libs/seam-phonemizer/src/phonemizer.cpp
  libs/seam-phonemizer/src/japanese_phonemizer.cpp
)
target_include_directories(seam_phonemizer PUBLIC libs/seam-phonemizer/include)
target_link_libraries(seam_phonemizer PUBLIC seam_domain seam_core)
seam_apply_compiler_options(seam_phonemizer)

add_library(seam_application
  libs/seam-application/src/command.cpp
  libs/seam-application/src/note_commands.cpp
  libs/seam-application/src/lyric_commands.cpp
  libs/seam-application/src/render_commands.cpp
  libs/seam-application/src/editor_session.cpp
  libs/seam-application/src/project_factory.cpp
)
target_include_directories(seam_application PUBLIC libs/seam-application/include)
target_link_libraries(seam_application PUBLIC seam_domain seam_core)
seam_apply_compiler_options(seam_application)

add_library(seam_authoring_runtime
  libs/seam-authoring-runtime/src/project_document.cpp
  libs/seam-authoring-runtime/src/project_lifecycle.cpp
  libs/seam-authoring-runtime/src/autosave_service.cpp
  libs/seam-authoring-runtime/src/recent_projects.cpp
  libs/seam-authoring-runtime/src/voicebank_session.cpp
  libs/seam-authoring-runtime/src/voicebank_browser.cpp
  libs/seam-authoring-runtime/src/voicebank_installer_service.cpp
  libs/seam-authoring-runtime/src/render_coordinator.cpp
  libs/seam-authoring-runtime/src/technical_edit_controller.cpp
  libs/seam-authoring-runtime/src/transport_controller.cpp
  libs/seam-authoring-runtime/src/authoring_runtime.cpp
  libs/seam-authoring-runtime/src/diagnostic.cpp
  libs/seam-authoring-runtime/src/bank_reference_registry.cpp
)
target_include_directories(seam_authoring_runtime PUBLIC
  libs/seam-authoring-runtime/include)
target_link_libraries(seam_authoring_runtime PUBLIC
  seam_rendering seam_distribution seam_voicebank seam_formats seam_application seam_domain seam_core)
seam_apply_compiler_options(seam_authoring_runtime)


add_library(seam_editor_ui
  libs/seam-editor-ui/src/timeline_transform.cpp
  libs/seam-editor-ui/src/note_spatial_index.cpp
  libs/seam-editor-ui/src/piano_roll_model.cpp
  libs/seam-editor-ui/src/phoneme_lane_model.cpp
  libs/seam-editor-ui/src/text_composition_model.cpp
  libs/seam-editor-ui/src/svg_renderer.cpp
)
target_include_directories(seam_editor_ui PUBLIC libs/seam-editor-ui/include)
target_link_libraries(seam_editor_ui PUBLIC seam_application seam_phonemizer seam_domain seam_core)
seam_apply_compiler_options(seam_editor_ui)

add_library(seam_formats
  libs/seam-formats/src/json_value.cpp
  libs/seam-formats/src/project_json.cpp
)
target_include_directories(seam_formats PUBLIC libs/seam-formats/include)
target_link_libraries(seam_formats PUBLIC seam_domain seam_core)
seam_apply_compiler_options(seam_formats)



add_library(seam_character
  libs/seam-character/src/character.cpp
)
target_include_directories(seam_character PUBLIC libs/seam-character/include)
target_link_libraries(seam_character PUBLIC seam_formats seam_core)
seam_apply_compiler_options(seam_character)

add_library(seam_voicebank
  libs/seam-voicebank/src/voicebank.cpp
  libs/seam-voicebank/src/asset_path.cpp
  libs/seam-voicebank/src/wav.cpp
  libs/seam-voicebank/src/waveform.cpp
  libs/seam-voicebank/src/spectrogram.cpp
  libs/seam-voicebank/src/pitch.cpp
  libs/seam-voicebank/src/pitch_marks.cpp
  libs/seam-voicebank/src/marker_editor.cpp
  libs/seam-voicebank/src/validator.cpp
  libs/seam-voicebank/src/manifest_json.cpp
  libs/seam-voicebank/src/content_identity.cpp
  libs/seam-voicebank/src/catalog.cpp
  libs/seam-voicebank/src/coverage.cpp
)
target_include_directories(seam_voicebank PUBLIC libs/seam-voicebank/include)
target_link_libraries(seam_voicebank PUBLIC seam_formats seam_domain seam_core)
seam_apply_compiler_options(seam_voicebank)

add_library(seam_distribution
  libs/seam-distribution/src/signing.cpp
  libs/seam-distribution/src/seambank.cpp
  libs/seam-distribution/src/installer.cpp
)
target_include_directories(seam_distribution PUBLIC
  libs/seam-distribution/include)
target_link_libraries(seam_distribution PUBLIC
  seam_voicebank seam_formats seam_domain seam_core OpenSSL::Crypto)
seam_apply_compiler_options(seam_distribution)


add_library(seam_synthesis
  libs/seam-synthesis/src/unit_selection.cpp
  libs/seam-synthesis/src/timing_solver.cpp
  libs/seam-synthesis/src/raw_renderer.cpp
  libs/seam-synthesis/src/pitch_curve.cpp
  libs/seam-synthesis/src/classic_psola.cpp
  libs/seam-synthesis/src/spectral_classic.cpp
  libs/seam-synthesis/src/stretch_renderer.cpp
  libs/seam-synthesis/src/renderer_dispatcher.cpp
  libs/seam-synthesis/src/seam_composer.cpp
  libs/seam-synthesis/src/phrase_renderer.cpp
)
target_include_directories(seam_synthesis PUBLIC libs/seam-synthesis/include)
target_link_libraries(seam_synthesis PUBLIC
  seam_voicebank seam_phonemizer seam_formats seam_domain seam_core)
seam_apply_compiler_options(seam_synthesis)


add_library(seam_rendering
  libs/seam-rendering/src/phrase_segmenter.cpp
  libs/seam-rendering/src/render_snapshot.cpp
  libs/seam-rendering/src/render_pipeline.cpp
  libs/seam-rendering/src/region_renderer.cpp
  libs/seam-rendering/src/project_renderer.cpp
  libs/seam-rendering/src/pcm_cache.cpp
  libs/seam-rendering/src/audio_ring_buffer.cpp
  libs/seam-rendering/src/interleaved_audio_ring_buffer.cpp
  libs/seam-rendering/src/multichannel_routing.cpp
  libs/seam-rendering/src/multichannel_playback.cpp
  libs/seam-rendering/src/playback_engine.cpp
  libs/seam-rendering/src/playback_feeder_service.cpp
  libs/seam-rendering/src/stale_audio_store.cpp
  libs/seam-rendering/src/render_scheduler.cpp
)
target_include_directories(seam_rendering PUBLIC libs/seam-rendering/include)
target_link_libraries(seam_rendering PUBLIC
  seam_synthesis seam_voicebank seam_phonemizer seam_formats seam_domain seam_core
  Threads::Threads)
seam_apply_compiler_options(seam_rendering)


add_library(seam_editor_inspection
  libs/seam-editor-inspection/src/unit_lane_model.cpp
  libs/seam-editor-inspection/src/sample_microscope_model.cpp
)
target_include_directories(seam_editor_inspection PUBLIC
  libs/seam-editor-inspection/include)
target_link_libraries(seam_editor_inspection PUBLIC
  seam_editor_ui seam_synthesis seam_voicebank seam_phonemizer seam_domain seam_core)
seam_apply_compiler_options(seam_editor_inspection)

add_library(seam_platform
  libs/seam-platform/src/audio_callback.cpp
  libs/seam-platform/src/platform_capabilities.cpp
  libs/seam-platform/src/threaded_audio_device.cpp
  libs/seam-platform/src/pulse_audio_device.cpp
  libs/seam-platform/src/system_audio_device_unavailable.cpp
  libs/seam-platform/src/threaded_audio_input_device.cpp
  libs/seam-platform/src/pulse_audio_input_device.cpp
  libs/seam-platform/src/system_audio_input_device_unavailable.cpp
  libs/seam-platform/src/recording_session.cpp
  libs/seam-platform/src/application_paths.cpp
)
target_include_directories(seam_platform PUBLIC libs/seam-platform/include)
target_link_libraries(seam_platform PUBLIC seam_voicebank seam_core Threads::Threads ${CMAKE_DL_LIBS})
if(WIN32)
  target_sources(seam_platform PRIVATE
    libs/seam-platform/src/wasapi_audio_device.cpp
    libs/seam-platform/src/wasapi_audio_input_device.cpp
    libs/seam-platform/src/file_dialog_win32.cpp
    libs/seam-platform/src/application_menu_win32.cpp)
  target_compile_definitions(seam_platform PRIVATE
    SEAM_AUDIO_WASAPI=1 WIN32_LEAN_AND_MEAN NOMINMAX _WIN32_WINNT=0x0A00)
  target_link_libraries(seam_platform PRIVATE ole32 uuid avrt shell32)
elseif(APPLE)
  enable_language(OBJCXX)
  target_sources(seam_platform PRIVATE
    libs/seam-platform/src/coreaudio_audio_device.mm
    libs/seam-platform/src/coreaudio_audio_input_device.mm
    libs/seam-platform/src/file_dialog_appkit.mm
    libs/seam-platform/src/application_menu_appkit.mm)
  set_source_files_properties(
    libs/seam-platform/src/file_dialog_appkit.mm
    libs/seam-platform/src/application_menu_appkit.mm
    PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
  target_compile_definitions(seam_platform PRIVATE SEAM_AUDIO_COREAUDIO=1)
  target_link_libraries(seam_platform PRIVATE
    "-framework AudioToolbox" "-framework CoreAudio" "-framework CoreFoundation"
    "-framework Cocoa" "-framework UniformTypeIdentifiers")
elseif(UNIX)
  target_sources(seam_platform PRIVATE
    libs/seam-platform/src/file_dialog_unavailable.cpp
    libs/seam-platform/src/application_menu_unavailable.cpp)
  target_compile_definitions(seam_platform PRIVATE SEAM_AUDIO_PULSE=1)
endif()
seam_apply_compiler_options(seam_platform)


if(SEAM_BUILD_NATIVE_DESKTOP)
  set(SEAM_NATIVE_UI_SOURCES
    libs/seam-native-ui/src/pixel_surface.cpp
    libs/seam-native-ui/src/editor_scene.cpp
    libs/seam-native-ui/src/editor_controller.cpp
    libs/seam-native-ui/src/character_presentation.cpp
    libs/seam-native-ui/src/voicebank_studio.cpp
    libs/seam-native-ui/src/native_window_unavailable.cpp)

  if(WIN32)
    list(APPEND SEAM_NATIVE_UI_SOURCES
      libs/seam-native-ui/src/native_window_win32.cpp)
  elseif(APPLE)
    enable_language(OBJCXX)
    list(APPEND SEAM_NATIVE_UI_SOURCES
      libs/seam-native-ui/src/native_window_appkit.mm)
    set_source_files_properties(
      libs/seam-native-ui/src/native_window_appkit.mm
      PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
  elseif(UNIX)
    find_package(X11)
    if(X11_FOUND)
      list(APPEND SEAM_NATIVE_UI_SOURCES
        libs/seam-native-ui/src/native_window_x11.cpp)
    endif()
  endif()

  add_library(seam_native_ui ${SEAM_NATIVE_UI_SOURCES})
  target_include_directories(seam_native_ui PUBLIC
    libs/seam-native-ui/include)
  target_link_libraries(seam_native_ui PUBLIC
    seam_character seam_editor_inspection seam_voicebank seam_editor_ui seam_phonemizer seam_application seam_domain seam_text seam_core)
  if(WIN32)
    target_compile_definitions(seam_native_ui PRIVATE
      SEAM_NATIVE_WIN32=1 WIN32_LEAN_AND_MEAN NOMINMAX _WIN32_WINNT=0x0A00)
    target_link_libraries(seam_native_ui PRIVATE user32 gdi32 imm32 ole32 uuid)
  elseif(APPLE)
    target_compile_definitions(seam_native_ui PRIVATE SEAM_NATIVE_APPKIT=1)
    target_link_libraries(seam_native_ui PRIVATE
      "-framework AppKit" "-framework CoreGraphics" "-framework CoreFoundation" "-framework Foundation")
  elseif(UNIX AND X11_FOUND)
    target_compile_definitions(seam_native_ui PRIVATE SEAM_NATIVE_X11=1)
    target_include_directories(seam_native_ui PRIVATE ${X11_INCLUDE_DIR})
    target_link_libraries(seam_native_ui PRIVATE ${X11_LIBRARIES})
  endif()
  seam_apply_compiler_options(seam_native_ui)
endif()

add_library(seam_playback_platform
  libs/seam-platform/src/ring_buffer_processor.cpp
  libs/seam-platform/src/multichannel_ring_buffer_processor.cpp
)
target_include_directories(seam_playback_platform PUBLIC libs/seam-platform/include)
target_link_libraries(seam_playback_platform PUBLIC
  seam_platform seam_rendering seam_core)
seam_apply_compiler_options(seam_playback_platform)

add_executable(seam_phase1_demo apps/seam-phase1-demo/main.cpp)
target_link_libraries(seam_phase1_demo PRIVATE
  seam_editor_ui seam_formats seam_platform seam_application seam_domain seam_core)
seam_apply_compiler_options(seam_phase1_demo)

add_executable(seam_phase2_demo apps/seam-phase2-demo/main.cpp)
target_link_libraries(seam_phase2_demo PRIVATE
  seam_synthesis seam_voicebank seam_editor_ui seam_phonemizer seam_formats
  seam_application seam_domain seam_core)
seam_apply_compiler_options(seam_phase2_demo)


add_executable(seam_phase3_demo apps/seam-phase3-demo/main.cpp)
target_link_libraries(seam_phase3_demo PRIVATE
  seam_rendering seam_synthesis seam_voicebank seam_editor_ui seam_phonemizer
  seam_formats seam_application seam_domain seam_core)
seam_apply_compiler_options(seam_phase3_demo)

add_executable(seam_phase4_demo apps/seam-phase4-demo/main.cpp)
target_link_libraries(seam_phase4_demo PRIVATE
  seam_editor_inspection seam_playback_platform seam_platform seam_rendering seam_synthesis seam_voicebank
  seam_editor_ui seam_phonemizer seam_formats seam_application seam_domain seam_core)
seam_apply_compiler_options(seam_phase4_demo)

if(SEAM_BUILD_NATIVE_DESKTOP)
  add_library(seam_standalone_authoring STATIC
    libs/seam-standalone/src/production_configuration.cpp
    libs/seam-standalone/src/authoring_session.cpp
    libs/seam-standalone/src/application_controller.cpp
    libs/seam-standalone/src/native_editor_app.cpp)
  target_include_directories(seam_standalone_authoring PUBLIC
    libs/seam-standalone/include)
  target_link_libraries(seam_standalone_authoring PUBLIC
    seam_authoring_runtime seam_native_ui seam_playback_platform seam_platform
    seam_application seam_domain seam_voicebank seam_core)
  seam_apply_compiler_options(seam_standalone_authoring)

  add_executable(seam_phase5_demo apps/seam-phase5-demo/main.cpp)
  target_link_libraries(seam_phase5_demo PRIVATE
    seam_native_ui seam_playback_platform seam_platform seam_rendering
    seam_voicebank seam_editor_ui seam_application seam_domain seam_core)
  seam_apply_compiler_options(seam_phase5_demo)

  add_executable(seam_editor_native apps/seam-editor-native/main.cpp)
  target_link_libraries(seam_editor_native PRIVATE
    seam_standalone_authoring seam_native_ui seam_playback_platform seam_platform
    seam_rendering seam_editor_ui seam_application seam_domain seam_core)
  seam_apply_compiler_options(seam_editor_native)

  add_executable(seam_voicebank_studio_native apps/seam-voicebank-studio-native/main.cpp)
  target_link_libraries(seam_voicebank_studio_native PRIVATE
    seam_native_ui seam_editor_inspection seam_platform seam_voicebank
    seam_formats seam_domain seam_core)
  seam_apply_compiler_options(seam_voicebank_studio_native)
endif()


add_executable(seam_phase6_demo apps/seam-phase6-demo/main.cpp)
target_link_libraries(seam_phase6_demo PRIVATE
  seam_playback_platform seam_platform seam_rendering seam_formats
  seam_voicebank seam_domain seam_core)
seam_apply_compiler_options(seam_phase6_demo)

add_executable(seam_bank_tool apps/seam-bank-tool/main.cpp)
target_link_libraries(seam_bank_tool PRIVATE seam_distribution seam_voicebank seam_core)
seam_apply_compiler_options(seam_bank_tool)

add_executable(seam_phase7_demo apps/seam-phase7-demo/main.cpp)
target_link_libraries(seam_phase7_demo PRIVATE seam_distribution seam_voicebank seam_formats seam_core)
seam_apply_compiler_options(seam_phase7_demo)

add_executable(seam_phase8_demo apps/seam-phase8-demo/main.cpp)
target_link_libraries(seam_phase8_demo PRIVATE seam_platform seam_core)
seam_apply_compiler_options(seam_phase8_demo)

if(SEAM_BUILD_NATIVE_DESKTOP)
  add_executable(seam_phase9_demo apps/seam-phase9-demo/main.cpp)
  target_link_libraries(seam_phase9_demo PRIVATE
    seam_native_ui seam_text seam_formats seam_core)
  seam_apply_compiler_options(seam_phase9_demo)
endif()

if(SEAM_BUILD_CLAP_PLUGIN)
  add_library(seam_clap_plugin MODULE libs/seam-clap/src/plugin_entry.cpp)
  target_include_directories(seam_clap_plugin SYSTEM PRIVATE third_party/clap/include)
  target_link_libraries(seam_clap_plugin PRIVATE seam_clap seam_core)
  set_target_properties(seam_clap_plugin PROPERTIES
    PREFIX ""
    OUTPUT_NAME "ProjectSEAM"
    SUFFIX ".clap")
  seam_apply_compiler_options(seam_clap_plugin)

  add_executable(seam_clap_host apps/seam-clap-host/main.cpp)
  target_include_directories(seam_clap_host SYSTEM PRIVATE third_party/clap/include)
  target_link_libraries(seam_clap_host PRIVATE seam_clap seam_core ${CMAKE_DL_LIBS})
  seam_apply_compiler_options(seam_clap_host)
endif()


if(SEAM_BUILD_CLAP_EDITOR_PLUGIN)
  if(NOT SEAM_BUILD_NATIVE_DESKTOP)
    message(FATAL_ERROR "The Phase 11 CLAP editor requires SEAM_BUILD_NATIVE_DESKTOP=ON")
  endif()

  set(SEAM_CLAP_EDITOR_VIEW_SOURCE)
  if(WIN32)
    set(SEAM_CLAP_EDITOR_VIEW_SOURCE
      libs/seam-clap-editor/src/embedded_view_win32.cpp)
  elseif(APPLE)
    enable_language(OBJCXX)
    set(SEAM_CLAP_EDITOR_VIEW_SOURCE
      libs/seam-clap-editor/src/embedded_view_appkit.mm)
    set_source_files_properties(
      libs/seam-clap-editor/src/embedded_view_appkit.mm
      PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
  elseif(UNIX AND X11_FOUND)
    set(SEAM_CLAP_EDITOR_VIEW_SOURCE
      libs/seam-clap-editor/src/embedded_view_x11.cpp)
  else()
    set(SEAM_CLAP_EDITOR_VIEW_SOURCE
      libs/seam-clap-editor/src/embedded_view_unavailable.cpp)
  endif()

  add_library(seam_clap_editor
    libs/seam-clap-editor/src/editor_runtime_adapter.cpp
  libs/seam-clap-editor/src/editor_runtime_paint.cpp
  libs/seam-clap-editor/src/editor_runtime_input.cpp
  libs/seam-clap-editor/src/editor_runtime_project.cpp
  libs/seam-clap-editor/src/editor_runtime_preview.cpp
  libs/seam-clap-editor/src/editor_runtime_state.cpp
    libs/seam-clap-editor/src/host_timeline.cpp
    ${SEAM_CLAP_EDITOR_VIEW_SOURCE})
  target_include_directories(seam_clap_editor PUBLIC
    libs/seam-clap-editor/include)
  target_include_directories(seam_clap_editor SYSTEM PRIVATE
    third_party/clap/include
    libs/seam-clap-editor/generated)
  target_link_libraries(seam_clap_editor PUBLIC
    seam_native_ui seam_editor_inspection seam_rendering seam_synthesis
    seam_authoring_runtime seam_voicebank seam_editor_ui seam_phonemizer seam_formats
    seam_application seam_domain seam_text seam_core Threads::Threads)
  if(WIN32)
    target_compile_definitions(seam_clap_editor PRIVATE
      SEAM_CLAP_EDITOR_WIN32=1 WIN32_LEAN_AND_MEAN NOMINMAX _WIN32_WINNT=0x0A00)
    target_link_libraries(seam_clap_editor PRIVATE user32 gdi32 imm32 ole32 uuid)
  elseif(APPLE)
    target_compile_definitions(seam_clap_editor PRIVATE SEAM_CLAP_EDITOR_COCOA=1)
    target_link_libraries(seam_clap_editor PRIVATE
      "-framework AppKit" "-framework CoreGraphics" "-framework Foundation")
  elseif(UNIX AND X11_FOUND)
    target_compile_definitions(seam_clap_editor PRIVATE SEAM_CLAP_EDITOR_X11=1)
    target_include_directories(seam_clap_editor PRIVATE ${X11_INCLUDE_DIR})
    target_link_libraries(seam_clap_editor PRIVATE ${X11_LIBRARIES})
  endif()
  seam_apply_compiler_options(seam_clap_editor)

  add_library(seam_clap_editor_plugin MODULE
    libs/seam-clap-editor/src/plugin_entry.cpp)
  target_include_directories(seam_clap_editor_plugin SYSTEM PRIVATE
    third_party/clap/include)
  target_compile_definitions(seam_clap_editor_plugin PRIVATE
    SEAM_SOURCE_CHARACTER_PACKAGE="${CMAKE_CURRENT_SOURCE_DIR}/assets/character-01")
  target_link_libraries(seam_clap_editor_plugin PRIVATE
    seam_clap_editor seam_core)
  set_target_properties(seam_clap_editor_plugin PROPERTIES
    PREFIX ""
    OUTPUT_NAME "ProjectSEAMEditor"
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN YES)
  if(APPLE)
    set_target_properties(seam_clap_editor_plugin PROPERTIES
      BUNDLE TRUE
      BUNDLE_EXTENSION "clap"
      MACOSX_BUNDLE_INFO_PLIST
        "${CMAKE_CURRENT_SOURCE_DIR}/packaging/macos/ProjectSEAMEditor-Info.plist.in")
  else()
    set_target_properties(seam_clap_editor_plugin PROPERTIES SUFFIX ".clap")
  endif()
  if(WIN32)
    target_compile_definitions(seam_clap_editor_plugin PRIVATE
      SEAM_CLAP_EDITOR_WIN32=1 WIN32_LEAN_AND_MEAN NOMINMAX _WIN32_WINNT=0x0A00)
  elseif(APPLE)
    target_compile_definitions(seam_clap_editor_plugin PRIVATE
      SEAM_CLAP_EDITOR_COCOA=1)
  elseif(UNIX AND X11_FOUND)
    target_compile_definitions(seam_clap_editor_plugin PRIVATE
      SEAM_CLAP_EDITOR_X11=1)
    target_link_options(seam_clap_editor_plugin PRIVATE
      "LINKER:--exclude-libs,ALL")
  endif()
  seam_apply_compiler_options(seam_clap_editor_plugin)

  set(SEAM_PRODUCTION_DEMO_BANK_SOURCE
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/demo-human-voicebank-public-domain/production-bank")
  set(SEAM_PRODUCTION_DEMO_BANK_ID "demo.public-domain.human.production")
  set(SEAM_PRODUCTION_DEMO_BANK_VERSION "0.12.0")
  if(APPLE)
    set(SEAM_EDITOR_RESOURCE_ROOT
      "$<TARGET_BUNDLE_DIR:seam_clap_editor_plugin>/Contents/Resources")
  else()
    set(SEAM_EDITOR_RESOURCE_ROOT
      "$<TARGET_FILE_DIR:seam_clap_editor_plugin>/ProjectSEAMEditor.resources")
  endif()
  add_custom_command(TARGET seam_clap_editor_plugin POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory
      "${SEAM_EDITOR_RESOURCE_ROOT}/voicebanks/${SEAM_PRODUCTION_DEMO_BANK_ID}/${SEAM_PRODUCTION_DEMO_BANK_VERSION}"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
      "${SEAM_PRODUCTION_DEMO_BANK_SOURCE}"
      "${SEAM_EDITOR_RESOURCE_ROOT}/voicebanks/${SEAM_PRODUCTION_DEMO_BANK_ID}/${SEAM_PRODUCTION_DEMO_BANK_VERSION}"
    VERBATIM)

  install(TARGETS seam_clap_editor_plugin
    RUNTIME DESTINATION "CLAP"
    LIBRARY DESTINATION "CLAP"
    BUNDLE DESTINATION "CLAP")
  if(APPLE)
    install(DIRECTORY "${SEAM_PRODUCTION_DEMO_BANK_SOURCE}/"
      DESTINATION
        "CLAP/ProjectSEAMEditor.clap/Contents/Resources/voicebanks/${SEAM_PRODUCTION_DEMO_BANK_ID}/${SEAM_PRODUCTION_DEMO_BANK_VERSION}")
  else()
    install(DIRECTORY assets/character-01
      DESTINATION "CLAP/ProjectSEAMEditor.resources")
    install(DIRECTORY "${SEAM_PRODUCTION_DEMO_BANK_SOURCE}/"
      DESTINATION
        "CLAP/ProjectSEAMEditor.resources/voicebanks/${SEAM_PRODUCTION_DEMO_BANK_ID}/${SEAM_PRODUCTION_DEMO_BANK_VERSION}")
  endif()

  add_executable(seam_clap_editor_host
    apps/seam-clap-editor-host/main.cpp)
  target_include_directories(seam_clap_editor_host SYSTEM PRIVATE
    third_party/clap/include)
  target_link_libraries(seam_clap_editor_host PRIVATE
    seam_voicebank seam_core ${CMAKE_DL_LIBS})
  if(UNIX AND X11_FOUND)
    target_include_directories(seam_clap_editor_host PRIVATE ${X11_INCLUDE_DIR})
    target_link_libraries(seam_clap_editor_host PRIVATE ${X11_LIBRARIES})
  endif()
  seam_apply_compiler_options(seam_clap_editor_host)
endif()

add_executable(seam_phase10_demo apps/seam-phase10-demo/main.cpp)
target_link_libraries(seam_phase10_demo PRIVATE seam_clap seam_voicebank seam_core)
seam_apply_compiler_options(seam_phase10_demo)

add_executable(seam_clap_state_tool apps/seam-clap-state-tool/main.cpp)
target_link_libraries(seam_clap_state_tool PRIVATE seam_clap seam_voicebank seam_core)
seam_apply_compiler_options(seam_clap_state_tool)

add_executable(seam_voicebank_cli apps/seam-voicebank-cli/main.cpp)
target_link_libraries(seam_voicebank_cli PRIVATE
  seam_voicebank seam_formats seam_domain seam_core)
seam_apply_compiler_options(seam_voicebank_cli)


if(SEAM_BUILD_CLAP_EDITOR_PLUGIN)
  add_executable(seam_phase12a_demo apps/seam-phase12a-demo/main.cpp)
  target_compile_definitions(seam_phase12a_demo PRIVATE
    SEAM_SOURCE_PRODUCTION_VOICEBANK="${CMAKE_CURRENT_SOURCE_DIR}/assets/demo-human-voicebank-public-domain/production-bank")
  target_link_libraries(seam_phase12a_demo PRIVATE
    seam_clap_editor seam_rendering seam_voicebank seam_formats seam_domain seam_core)
  seam_apply_compiler_options(seam_phase12a_demo)

  add_executable(seam_phase12b_demo apps/seam-phase12b-demo/main.cpp)
  target_compile_definitions(seam_phase12b_demo PRIVATE
    SEAM_SOURCE_PRODUCTION_VOICEBANK="${CMAKE_CURRENT_SOURCE_DIR}/assets/demo-human-voicebank-public-domain/production-bank")
  target_link_libraries(seam_phase12b_demo PRIVATE
    seam_clap_editor seam_rendering seam_voicebank seam_formats
    seam_application seam_domain seam_core)
  seam_apply_compiler_options(seam_phase12b_demo)
endif()


if(SEAM_BUILD_TESTS AND SEAM_BUILD_CLAP_EDITOR_PLUGIN)
  add_executable(seam_phase11_tests tests/test_phase11_clap_editor.cpp)
  target_include_directories(seam_phase11_tests PRIVATE tests)
  target_compile_definitions(seam_phase11_tests PRIVATE
    SEAM_SOURCE_PRODUCTION_VOICEBANK="${CMAKE_CURRENT_SOURCE_DIR}/assets/demo-human-voicebank-public-domain/production-bank")
  target_link_libraries(seam_phase11_tests PRIVATE
    seam_clap_editor seam_formats seam_application seam_domain seam_core)
  seam_apply_compiler_options(seam_phase11_tests)
  add_test(NAME seam_phase11_tests COMMAND seam_phase11_tests)


  add_executable(seam_phase12a_tests tests/test_phase12a.cpp)
  target_include_directories(seam_phase12a_tests PRIVATE tests)
  target_compile_definitions(seam_phase12a_tests PRIVATE
    SEAM_SOURCE_PRODUCTION_VOICEBANK="${CMAKE_CURRENT_SOURCE_DIR}/assets/demo-human-voicebank-public-domain/production-bank")
  target_link_libraries(seam_phase12a_tests PRIVATE
    seam_clap_editor seam_rendering seam_voicebank seam_formats
    seam_application seam_domain seam_core)
  seam_apply_compiler_options(seam_phase12a_tests)
  add_test(NAME seam_phase12a_tests COMMAND seam_phase12a_tests)
  add_test(NAME seam_phase12a_demo_smoke
    COMMAND seam_phase12a_demo --output ${CMAKE_BINARY_DIR}/phase12a-smoke)
  set_tests_properties(seam_phase12a_demo_smoke PROPERTIES TIMEOUT 60)

  add_executable(seam_phase12b_tests tests/test_phase12b.cpp)
  target_include_directories(seam_phase12b_tests PRIVATE tests)
  target_compile_definitions(seam_phase12b_tests PRIVATE
    SEAM_SOURCE_PRODUCTION_VOICEBANK="${CMAKE_CURRENT_SOURCE_DIR}/assets/demo-human-voicebank-public-domain/production-bank")
  target_link_libraries(seam_phase12b_tests PRIVATE
    seam_clap_editor seam_rendering seam_voicebank seam_formats
    seam_phonemizer seam_application seam_domain seam_core)
  seam_apply_compiler_options(seam_phase12b_tests)
  add_test(NAME seam_phase12b_tests COMMAND seam_phase12b_tests)
  add_test(NAME seam_phase12b_demo_smoke
    COMMAND seam_phase12b_demo --output ${CMAKE_BINARY_DIR}/phase12b-smoke)
  set_tests_properties(seam_phase12b_tests seam_phase12b_demo_smoke
    PROPERTIES TIMEOUT 120)

  if(UNIX AND NOT APPLE AND X11_FOUND)
    find_program(SEAM_PHASE11_XVFB_RUN_EXECUTABLE xvfb-run)
    if(SEAM_PHASE11_XVFB_RUN_EXECUTABLE)
      add_test(NAME seam_phase11_clap_editor_host_smoke
        COMMAND ${SEAM_PHASE11_XVFB_RUN_EXECUTABLE} -a
          $<TARGET_FILE:seam_clap_editor_host>
          --plugin $<TARGET_FILE:seam_clap_editor_plugin>
          --screenshot ${CMAKE_BINARY_DIR}/phase11-clap-editor.ppm
          --summary ${CMAKE_BINARY_DIR}/phase11-clap-editor-summary.json
          --audio ${CMAKE_BINARY_DIR}/phase11-clap-editor-live.wav)
      set_tests_properties(seam_phase11_clap_editor_host_smoke PROPERTIES
        TIMEOUT 45)
    endif()
  endif()
  if(Python3_Interpreter_FOUND)
    add_test(NAME seam_phase11_source_and_voice_contract
      COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/scripts/verify_phase11_contracts.py
        --root ${CMAKE_CURRENT_SOURCE_DIR})
    set_tests_properties(seam_phase11_source_and_voice_contract PROPERTIES
      TIMEOUT 30)
    add_test(NAME seam_phase12a_production_contract
      COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/scripts/verify_phase12a_contracts.py
        --root ${CMAKE_CURRENT_SOURCE_DIR})
    set_tests_properties(seam_phase12a_production_contract PROPERTIES
      TIMEOUT 30)
    add_test(NAME seam_phase12b_contract
      COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/scripts/verify_phase12b_contracts.py
        --root ${CMAKE_CURRENT_SOURCE_DIR})
    set_tests_properties(seam_phase12b_contract PROPERTIES TIMEOUT 30)
  endif()
endif()


if(SEAM_ENABLE_IPLUG2_SKIA)
  message(FATAL_ERROR
    "The iPlug2 + Skia adapter is intentionally dependency-gated until its exact source closure is approved. "
    "Set IPLUG2_ROOT and SKIA_ROOT only after the exact source revisions pass "
    "the repository license audit. See docs/architecture/IPLUG2_SKIA_ADAPTER.md.")
endif()

if(SEAM_BUILD_TESTS)
  enable_testing()
  set(SEAM_TEST_SOURCES
    tests/test_main.cpp
    tests/test_time.cpp
    tests/test_commands.cpp
    tests/test_serialization.cpp
    tests/test_ui.cpp
    tests/test_platform.cpp
    tests/test_phonemizer.cpp
    tests/test_voicebank.cpp
    tests/test_synthesis.cpp
    tests/test_rendering.cpp
    tests/test_inspection.cpp
    tests/test_stabilization.cpp
    tests/test_multichannel.cpp
    tests/test_distribution.cpp
    tests/test_platform_capabilities.cpp
    tests/test_text.cpp
    tests/test_clap.cpp
    tests/test_project_document.cpp
    tests/test_project_lifecycle.cpp
    tests/test_autosave_service.cpp
    tests/test_recent_projects.cpp
    tests/test_file_dialog_contract.cpp
    tests/test_authoring_voicebank_session.cpp
    tests/test_authoring_render_coordinator.cpp
    tests/test_technical_edit_controller.cpp
    tests/test_transport_controller.cpp
    tests/test_authoring_runtime.cpp
    tests/test_voicebank_browser.cpp
    tests/test_voicebank_installer_service.cpp
    tests/test_voicebank_relink.cpp
    tests/test_voicebank_coverage.cpp
    tests/test_wav_export_formats.cpp
    tests/test_bank_reference_registry.cpp
    tests/test_diagnostic_registry.cpp
    tests/test_log_export_policy.cpp
    tests/test_production_configuration.cpp
  )
  if(SEAM_BUILD_CLAP_EDITOR_PLUGIN)
    list(APPEND SEAM_TEST_SOURCES
      tests/test_authoring_characterization.cpp)
  endif()
  if(SEAM_BUILD_NATIVE_DESKTOP)
    list(APPEND SEAM_TEST_SOURCES
      tests/test_native_ui.cpp
      tests/test_native_runtime.cpp
      tests/test_standalone_authoring_integration.cpp
      tests/test_standalone_project_lifecycle.cpp
      tests/test_standalone_voicebank_workflow.cpp)
  endif()

  if(SEAM_BUILD_CLAP_EDITOR_PLUGIN)
    add_executable(seam_authoring_characterization_tests
      tests/test_main.cpp
      tests/test_authoring_characterization.cpp)
    target_include_directories(seam_authoring_characterization_tests PRIVATE tests)
    target_compile_definitions(seam_authoring_characterization_tests PRIVATE
      SEAM_SOURCE_PRODUCTION_VOICEBANK="${CMAKE_CURRENT_SOURCE_DIR}/assets/demo-human-voicebank-public-domain/production-bank")
    target_link_libraries(seam_authoring_characterization_tests PRIVATE
      seam_clap_editor seam_authoring_runtime seam_rendering seam_synthesis
      seam_voicebank seam_native_ui seam_editor_ui seam_application
      seam_domain seam_core)
    seam_apply_compiler_options(seam_authoring_characterization_tests)
    add_test(NAME seam_authoring_characterization_tests
      COMMAND seam_authoring_characterization_tests)
  endif()

  add_executable(seam_authoring_render_coordinator_tests
    tests/test_main.cpp
    tests/test_authoring_render_coordinator.cpp)
  target_include_directories(seam_authoring_render_coordinator_tests PRIVATE tests)
  target_compile_definitions(seam_authoring_render_coordinator_tests PRIVATE
    SEAM_SOURCE_PRODUCTION_VOICEBANK="${CMAKE_CURRENT_SOURCE_DIR}/assets/demo-human-voicebank-public-domain/production-bank")
  target_link_libraries(seam_authoring_render_coordinator_tests PRIVATE
    seam_authoring_runtime seam_rendering seam_synthesis seam_voicebank
    seam_application seam_domain seam_core)
  seam_apply_compiler_options(seam_authoring_render_coordinator_tests)
  add_test(NAME seam_authoring_render_coordinator_tests
    COMMAND seam_authoring_render_coordinator_tests)

  add_executable(seam_u2_tests
    tests/test_main.cpp
    tests/test_project_lifecycle.cpp
    tests/test_autosave_service.cpp
    tests/test_recent_projects.cpp
    tests/test_file_dialog_contract.cpp
    tests/test_standalone_project_lifecycle.cpp)
  target_include_directories(seam_u2_tests PRIVATE tests)
  target_compile_definitions(seam_u2_tests PRIVATE
    SEAM_SOURCE_PRODUCTION_VOICEBANK="${CMAKE_CURRENT_SOURCE_DIR}/assets/demo-human-voicebank-public-domain/production-bank")
  target_link_libraries(seam_u2_tests PRIVATE
    seam_standalone_authoring seam_native_ui seam_authoring_runtime
    seam_playback_platform seam_platform seam_rendering seam_synthesis
    seam_voicebank seam_editor_ui seam_phonemizer seam_formats
    seam_application seam_domain seam_text seam_core)
  seam_apply_compiler_options(seam_u2_tests)
  add_test(NAME seam_u2_tests COMMAND seam_u2_tests)

  add_executable(seam_u3_tests
    tests/test_main.cpp
    tests/test_voicebank_browser.cpp
    tests/test_voicebank_installer_service.cpp
    tests/test_voicebank_relink.cpp
    tests/test_voicebank_coverage.cpp)
  target_include_directories(seam_u3_tests PRIVATE tests)
  target_link_libraries(seam_u3_tests PRIVATE
    seam_authoring_runtime seam_distribution seam_rendering seam_synthesis
    seam_phonemizer seam_voicebank seam_formats seam_application seam_domain
    seam_core)
  seam_apply_compiler_options(seam_u3_tests)
  add_test(NAME seam_u3_tests COMMAND seam_u3_tests)

  if(SEAM_BUILD_NATIVE_DESKTOP)
    add_executable(seam_u3_standalone_tests
      tests/test_main.cpp
      tests/test_standalone_voicebank_workflow.cpp)
    target_include_directories(seam_u3_standalone_tests PRIVATE tests)
    target_link_libraries(seam_u3_standalone_tests PRIVATE
      seam_standalone_authoring seam_native_ui seam_authoring_runtime
      seam_distribution seam_rendering seam_synthesis seam_phonemizer
      seam_voicebank seam_formats seam_application seam_domain seam_core)
    seam_apply_compiler_options(seam_u3_standalone_tests)
    add_test(NAME seam_u3_standalone_tests COMMAND seam_u3_standalone_tests)
  endif()

  add_executable(seam_tests ${SEAM_TEST_SOURCES})
  target_include_directories(seam_tests PRIVATE tests)
  target_compile_definitions(seam_tests PRIVATE
    SEAM_SOURCE_PRODUCTION_VOICEBANK="${CMAKE_CURRENT_SOURCE_DIR}/assets/demo-human-voicebank-public-domain/production-bank")
  target_link_libraries(seam_tests PRIVATE
    seam_distribution seam_editor_inspection seam_rendering seam_synthesis seam_voicebank seam_editor_ui seam_phonemizer seam_formats
    seam_playback_platform seam_platform seam_authoring_runtime seam_application seam_domain seam_text seam_clap seam_core)
  if(SEAM_BUILD_NATIVE_DESKTOP)
    target_link_libraries(seam_tests PRIVATE seam_native_ui seam_standalone_authoring)
  endif()
  if(SEAM_BUILD_CLAP_EDITOR_PLUGIN)
    target_link_libraries(seam_tests PRIVATE seam_clap_editor)
  endif()
  seam_apply_compiler_options(seam_tests)
  add_test(NAME seam_tests COMMAND seam_tests)
  add_test(NAME seam_phase2_demo_smoke
    COMMAND seam_phase2_demo --output ${CMAKE_BINARY_DIR}/phase2-smoke)
  add_test(NAME seam_phase3_demo_smoke
    COMMAND seam_phase3_demo --output ${CMAKE_BINARY_DIR}/phase3-smoke)
  add_test(NAME seam_phase4_demo_smoke
    COMMAND seam_phase4_demo --output ${CMAKE_BINARY_DIR}/phase4-smoke)
  add_test(NAME seam_phase6_demo_smoke
    COMMAND seam_phase6_demo --output ${CMAKE_BINARY_DIR}/phase6-smoke)
  add_test(NAME seam_phase7_demo_smoke
    COMMAND seam_phase7_demo --output ${CMAKE_BINARY_DIR}/phase7-smoke)
  add_test(NAME seam_phase8_demo_smoke
    COMMAND seam_phase8_demo --output ${CMAKE_BINARY_DIR}/phase8-smoke)
  if(SEAM_BUILD_NATIVE_DESKTOP)
    add_test(NAME seam_phase9_demo_smoke
      COMMAND seam_phase9_demo --output ${CMAKE_BINARY_DIR}/phase9-smoke)
  endif()
  if(Python3_Interpreter_FOUND)
    add_test(NAME seam_phase8_platform_source_contract
      COMMAND ${Python3_EXECUTABLE}
              ${CMAKE_CURRENT_SOURCE_DIR}/scripts/verify_phase8_platform_sources.py
              --root ${CMAKE_CURRENT_SOURCE_DIR})
    add_test(NAME seam_standalone_production_path_contract
      COMMAND ${Python3_EXECUTABLE}
              ${CMAKE_CURRENT_SOURCE_DIR}/scripts/verify_standalone_production_path.py)
    add_test(NAME seam_u2_project_lifecycle_contract
      COMMAND ${Python3_EXECUTABLE}
              ${CMAKE_CURRENT_SOURCE_DIR}/scripts/verify_u2_project_lifecycle.py
              --root ${CMAKE_CURRENT_SOURCE_DIR})
    add_test(NAME seam_u3_voicebank_workflow_contract
      COMMAND ${Python3_EXECUTABLE}
              ${CMAKE_CURRENT_SOURCE_DIR}/scripts/verify_u3_voicebank_workflow.py
              --root ${CMAKE_CURRENT_SOURCE_DIR})
  endif()
  add_test(NAME seam_phase10_demo_smoke
    COMMAND seam_phase10_demo --output ${CMAKE_BINARY_DIR}/phase10-smoke)
  add_test(NAME seam_clap_state_pack_smoke
    COMMAND seam_clap_state_tool pack
            ${CMAKE_BINARY_DIR}/phase10-smoke/phase10-diagnostic-4ch.wav
            ${CMAKE_BINARY_DIR}/phase10-smoke/from-wav.seamclapstate
            --title "Project SEAM packed render"
            --gain-db 0.0)
  set_tests_properties(seam_clap_state_pack_smoke PROPERTIES
    DEPENDS seam_phase10_demo_smoke TIMEOUT 30)
  add_test(NAME seam_clap_state_inspect_smoke
    COMMAND seam_clap_state_tool inspect
            ${CMAKE_BINARY_DIR}/phase10-smoke/from-wav.seamclapstate)
  set_tests_properties(seam_clap_state_inspect_smoke PROPERTIES
    DEPENDS seam_clap_state_pack_smoke TIMEOUT 30)
  add_test(NAME seam_clap_state_extract_smoke
    COMMAND seam_clap_state_tool extract
            ${CMAKE_BINARY_DIR}/phase10-smoke/from-wav.seamclapstate
            ${CMAKE_BINARY_DIR}/phase10-smoke/from-state.wav)
  set_tests_properties(seam_clap_state_extract_smoke PROPERTIES
    DEPENDS seam_clap_state_pack_smoke TIMEOUT 30)
  if(SEAM_BUILD_CLAP_PLUGIN)
    add_test(NAME seam_clap_plugin_host_smoke
      COMMAND seam_clap_host
              --plugin $<TARGET_FILE:seam_clap_plugin>
              --state ${CMAKE_BINARY_DIR}/phase10-smoke/from-wav.seamclapstate
              --summary ${CMAKE_BINARY_DIR}/phase10-smoke/clap-host-summary.json)
    set_tests_properties(seam_clap_plugin_host_smoke PROPERTIES
      DEPENDS seam_clap_state_pack_smoke TIMEOUT 30)
  endif()
  add_test(NAME seam_bank_tool_help COMMAND seam_bank_tool --help)
  add_test(NAME seam_voicebank_cli_help COMMAND seam_voicebank_cli --help)
  add_test(NAME seam_voicebank_cli_validate
    COMMAND seam_voicebank_cli validate
            ${CMAKE_BINARY_DIR}/phase2-smoke/synthetic-voicebank/manifest.json)
  set_tests_properties(seam_voicebank_cli_validate PROPERTIES
    DEPENDS seam_phase2_demo_smoke)
  if(SEAM_BUILD_NATIVE_DESKTOP)
    add_test(NAME seam_phase5_demo_smoke
      COMMAND seam_phase5_demo --output ${CMAKE_BINARY_DIR}/phase5-smoke)
    if(SEAM_RUN_NATIVE_GUI_TESTS AND (WIN32 OR APPLE))
      add_test(NAME seam_native_editor_platform_smoke
        COMMAND $<TARGET_FILE:seam_editor_native>
                --force-threaded-audio
                --character-package ${CMAKE_CURRENT_SOURCE_DIR}/assets/character-01
                --auto-close-ms 500
                --screenshot ${CMAKE_BINARY_DIR}/phase8-native-window.ppm)
      add_test(NAME seam_voicebank_studio_platform_smoke
        COMMAND $<TARGET_FILE:seam_voicebank_studio_native>
                --manifest ${CMAKE_BINARY_DIR}/phase2-smoke/synthetic-voicebank/manifest.json
                --force-synthetic-input
                --auto-close-ms 500
                --screenshot ${CMAKE_BINARY_DIR}/phase8-voicebank-studio.ppm)
      set_tests_properties(seam_native_editor_platform_smoke PROPERTIES
        TIMEOUT 30)
      set_tests_properties(seam_voicebank_studio_platform_smoke PROPERTIES
        DEPENDS seam_phase2_demo_smoke TIMEOUT 30)
    endif()
    if(UNIX AND NOT APPLE AND X11_FOUND)
      find_program(SEAM_XVFB_RUN_EXECUTABLE xvfb-run)
      if(SEAM_XVFB_RUN_EXECUTABLE)
        add_test(NAME seam_native_editor_x11_smoke
          COMMAND ${SEAM_XVFB_RUN_EXECUTABLE} -a
                  $<TARGET_FILE:seam_editor_native>
                  --force-threaded-audio
                  --character-package ${CMAKE_CURRENT_SOURCE_DIR}/assets/character-01
                  --auto-close-ms 250
                  --screenshot ${CMAKE_BINARY_DIR}/phase5-native-window.ppm)
        add_test(NAME seam_voicebank_studio_x11_smoke
          COMMAND ${SEAM_XVFB_RUN_EXECUTABLE} -a
                  $<TARGET_FILE:seam_voicebank_studio_native>
                  --manifest ${CMAKE_BINARY_DIR}/phase2-smoke/synthetic-voicebank/manifest.json
                  --force-synthetic-input
                  --auto-close-ms 250
                  --screenshot ${CMAKE_BINARY_DIR}/phase5-voicebank-studio.ppm)
        set_tests_properties(seam_voicebank_studio_x11_smoke PROPERTIES
          DEPENDS seam_phase2_demo_smoke)
      endif()
    endif()
  endif()
endif()

if(SEAM_BUILD_BENCHMARKS)
  add_executable(seam_phase1_benchmark benchmarks/phase1_benchmark.cpp)
  target_link_libraries(seam_phase1_benchmark PRIVATE
    seam_editor_ui seam_application seam_domain seam_core)
  seam_apply_compiler_options(seam_phase1_benchmark)

  add_executable(seam_phase2_benchmark benchmarks/phase2_benchmark.cpp)
  target_link_libraries(seam_phase2_benchmark PRIVATE
    seam_synthesis seam_voicebank seam_domain seam_core)
  seam_apply_compiler_options(seam_phase2_benchmark)

  add_executable(seam_phase3_benchmark benchmarks/phase3_benchmark.cpp)
  target_link_libraries(seam_phase3_benchmark PRIVATE
    seam_rendering seam_synthesis seam_voicebank seam_domain seam_core)
  seam_apply_compiler_options(seam_phase3_benchmark)

  add_executable(seam_phase4_benchmark benchmarks/phase4_benchmark.cpp)
  target_link_libraries(seam_phase4_benchmark PRIVATE
    seam_playback_platform seam_platform seam_rendering seam_synthesis seam_voicebank seam_domain seam_core)
  seam_apply_compiler_options(seam_phase4_benchmark)

  if(SEAM_BUILD_NATIVE_DESKTOP)
    add_executable(seam_phase5_benchmark benchmarks/phase5_benchmark.cpp)
    target_link_libraries(seam_phase5_benchmark PRIVATE
      seam_native_ui seam_playback_platform seam_platform seam_rendering
      seam_editor_ui seam_application seam_domain seam_core)
    seam_apply_compiler_options(seam_phase5_benchmark)

    add_executable(seam_phase9_benchmark benchmarks/phase9_benchmark.cpp)
    target_link_libraries(seam_phase9_benchmark PRIVATE seam_text seam_core)
    seam_apply_compiler_options(seam_phase9_benchmark)
  endif()

  add_executable(seam_phase10_benchmark benchmarks/phase10_benchmark.cpp)
  target_link_libraries(seam_phase10_benchmark PRIVATE seam_clap seam_core)
  seam_apply_compiler_options(seam_phase10_benchmark)

endif()

if(Python3_Interpreter_FOUND)
  add_custom_target(seam_branch_policy
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/scripts/verify_master_branch.py
            --root ${CMAKE_CURRENT_SOURCE_DIR}
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    COMMENT "Verifying the master-only branch policy")

  add_custom_target(seam_license_audit
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/license-auditor/audit.py
            --root ${CMAKE_CURRENT_SOURCE_DIR}
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    COMMENT "Auditing dependency licenses and repository policy")

  if(SEAM_BUILD_TESTS AND SEAM_BUILD_BENCHMARKS)
    add_custom_target(seam_phase1_evidence
      COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/scripts/generate_phase1_evidence.py
              --root ${CMAKE_CURRENT_SOURCE_DIR} --skip-build
      DEPENDS seam_tests seam_phase1_demo seam_phase1_benchmark
      WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
      COMMENT "Generating Phase 1 verification evidence")

    add_custom_target(seam_phase2_evidence
      COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/scripts/generate_phase2_evidence.py
              --root ${CMAKE_CURRENT_SOURCE_DIR} --skip-build
      DEPENDS seam_tests seam_phase2_demo seam_voicebank_cli seam_phase2_benchmark
      WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
      COMMENT "Generating Phase 2 verification evidence")

    add_custom_target(seam_phase3_evidence
      COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/scripts/generate_phase3_evidence.py
              --root ${CMAKE_CURRENT_SOURCE_DIR} --skip-build
      DEPENDS seam_tests seam_phase3_demo seam_voicebank_cli seam_phase3_benchmark
      WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
      COMMENT "Generating Phase 3 verification evidence")

    add_custom_target(seam_phase4_evidence
      COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/scripts/generate_phase4_evidence.py
              --root ${CMAKE_CURRENT_SOURCE_DIR} --skip-build
      DEPENDS seam_tests seam_phase4_demo seam_voicebank_cli seam_phase4_benchmark
      WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
      COMMENT "Generating Phase 4 verification evidence")

    add_custom_target(seam_phase4_1_evidence
      COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/scripts/generate_phase4_1_evidence.py
              --root ${CMAKE_CURRENT_SOURCE_DIR} --skip-build
      DEPENDS seam_tests seam_phase2_demo seam_phase3_demo seam_phase4_demo
              seam_voicebank_cli seam_phase4_benchmark
      WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
      COMMENT "Generating Phase 4.1 stabilization evidence")

    if(SEAM_BUILD_NATIVE_DESKTOP)
      add_custom_target(seam_phase5_evidence
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/scripts/generate_phase5_evidence.py
                --root ${CMAKE_CURRENT_SOURCE_DIR} --skip-build
        DEPENDS seam_tests seam_phase5_demo seam_editor_native seam_phase5_benchmark
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "Generating Phase 5 native-runtime evidence")
      add_custom_target(seam_phase8_evidence
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/scripts/generate_phase8_evidence.py
                --root ${CMAKE_CURRENT_SOURCE_DIR} --skip-build
        DEPENDS seam_tests seam_phase8_demo seam_editor_native seam_voicebank_studio_native
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "Generating Phase 8 cross-platform source and Linux regression evidence")
      add_custom_target(seam_phase9_evidence
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/scripts/generate_phase9_evidence.py
                --root ${CMAKE_CURRENT_SOURCE_DIR} --skip-build
        DEPENDS seam_tests seam_phase9_demo seam_editor_native seam_phase9_benchmark
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "Generating Phase 9 Unicode/CJK rendering evidence")
    endif()
    add_custom_target(seam_phase10_evidence
      COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/scripts/generate_phase10_evidence.py
              --root ${CMAKE_CURRENT_SOURCE_DIR} --skip-build
      DEPENDS seam_tests seam_phase10_demo seam_clap_state_tool seam_phase10_benchmark
              $<$<BOOL:${SEAM_BUILD_CLAP_PLUGIN}>:seam_clap_plugin>
              $<$<BOOL:${SEAM_BUILD_CLAP_PLUGIN}>:seam_clap_host>
      WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
      COMMENT "Generating Phase 10 CLAP plug-in evidence")
  endif()
endif()

# Phase 12C: live articulation and runtime validation
add_subdirectory(phase12c)

# Phase 13A: VST3/AUv2, installer, signing, and mandatory release gates
add_subdirectory(phase13a)

if(TARGET seam_clap_editor_plugin)
  target_link_libraries(seam_clap_editor_plugin PRIVATE seam_live_voice)
endif()

# Phase 13B: Official Voicebank 01 and Character 01 release engineering
add_subdirectory(phase13b)
