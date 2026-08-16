cmake_minimum_required(VERSION 3.25)

project(ProjectSEAM VERSION 0.1.0 LANGUAGES CXX)

option(SEAM_BUILD_TESTS "Build Project SEAM tests" ON)
option(SEAM_BUILD_BENCHMARKS "Build Project SEAM benchmarks" ON)
option(SEAM_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" ON)
option(SEAM_ENABLE_SANITIZERS "Enable AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
option(SEAM_ENABLE_IPLUG2_SKIA "Enable the production iPlug2 + Skia shell adapter" OFF)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(SeamCompilerOptions)

add_library(seam_core
  libs/seam-core/src/logger.cpp
)
target_include_directories(seam_core PUBLIC libs/seam-core/include)
seam_apply_compiler_options(seam_core)

add_library(seam_domain
  libs/seam-domain/src/tempo_map.cpp
  libs/seam-domain/src/meter_map.cpp
  libs/seam-domain/src/note.cpp
  libs/seam-domain/src/project.cpp
)
target_include_directories(seam_domain PUBLIC libs/seam-domain/include)
target_link_libraries(seam_domain PUBLIC seam_core)
seam_apply_compiler_options(seam_domain)

add_library(seam_application
  libs/seam-application/src/command.cpp
  libs/seam-application/src/note_commands.cpp
  libs/seam-application/src/editor_session.cpp
  libs/seam-application/src/project_factory.cpp
)
target_include_directories(seam_application PUBLIC libs/seam-application/include)
target_link_libraries(seam_application PUBLIC seam_domain seam_core)
seam_apply_compiler_options(seam_application)

add_library(seam_editor_ui
  libs/seam-editor-ui/src/timeline_transform.cpp
  libs/seam-editor-ui/src/note_spatial_index.cpp
  libs/seam-editor-ui/src/piano_roll_model.cpp
  libs/seam-editor-ui/src/svg_renderer.cpp
)
target_include_directories(seam_editor_ui PUBLIC libs/seam-editor-ui/include)
target_link_libraries(seam_editor_ui PUBLIC seam_application seam_domain seam_core)
seam_apply_compiler_options(seam_editor_ui)

add_library(seam_formats
  libs/seam-formats/src/json_value.cpp
  libs/seam-formats/src/project_json.cpp
)
target_include_directories(seam_formats PUBLIC libs/seam-formats/include)
target_link_libraries(seam_formats PUBLIC seam_domain seam_core)
seam_apply_compiler_options(seam_formats)

add_library(seam_platform
  libs/seam-platform/src/audio_callback.cpp
)
target_include_directories(seam_platform PUBLIC libs/seam-platform/include)
target_link_libraries(seam_platform PUBLIC seam_core)
seam_apply_compiler_options(seam_platform)

add_executable(seam_phase1_demo apps/seam-phase1-demo/main.cpp)
target_link_libraries(seam_phase1_demo PRIVATE
  seam_editor_ui seam_formats seam_platform seam_application seam_domain seam_core)
seam_apply_compiler_options(seam_phase1_demo)

if(SEAM_ENABLE_IPLUG2_SKIA)
  message(FATAL_ERROR
    "The iPlug2 + Skia adapter is intentionally dependency-gated in Phase 1. "
    "Set IPLUG2_ROOT and SKIA_ROOT only after the exact source revisions pass "
    "the repository license audit. See docs/architecture/IPLUG2_SKIA_ADAPTER.md.")
endif()

if(SEAM_BUILD_TESTS)
  enable_testing()
  add_executable(seam_tests
    tests/test_main.cpp
    tests/test_time.cpp
    tests/test_commands.cpp
    tests/test_serialization.cpp
    tests/test_ui.cpp
    tests/test_platform.cpp
  )
  target_include_directories(seam_tests PRIVATE tests)
  target_link_libraries(seam_tests PRIVATE
    seam_editor_ui seam_formats seam_platform seam_application seam_domain seam_core)
  seam_apply_compiler_options(seam_tests)
  add_test(NAME seam_tests COMMAND seam_tests)
endif()

if(SEAM_BUILD_BENCHMARKS)
  add_executable(seam_phase1_benchmark benchmarks/phase1_benchmark.cpp)
  target_link_libraries(seam_phase1_benchmark PRIVATE
    seam_editor_ui seam_application seam_domain seam_core)
  seam_apply_compiler_options(seam_phase1_benchmark)
endif()

find_package(Python3 COMPONENTS Interpreter)
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
  endif()
endif()
