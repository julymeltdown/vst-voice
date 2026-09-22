# Developer-only comparison. No downloads, vendoring, installed targets, or
# production links. Every build rechecks external source/header/license bytes.
set(SEAM_WORLD_REFERENCE_SOURCE_DIR "" CACHE PATH "Explicit pinned upstream WORLD source directory")
set(SEAM_WORLD_REFERENCE_GUARD_SOURCE_DIR "" CACHE PATH "Explicit separately prepared D4C-guard reference source directory")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
  "${CMAKE_CURRENT_SOURCE_DIR}/tools/singing_quality/world_reference.lock.json"
  "${CMAKE_CURRENT_SOURCE_DIR}/tools/singing_quality/prepare_world_reference.py")
function(seam_add_world_reference SEAM_WORLD_VARIANT SEAM_WORLD_DIR SEAM_WORLD_TARGET)
  if(NOT Python3_Interpreter_FOUND OR NOT IS_DIRECTORY "${SEAM_WORLD_DIR}")
    message(FATAL_ERROR "WORLD reference needs Python3 and an explicit prepared source directory")
  endif()
  set(SEAM_WORLD_REFERENCE_VERIFIER "${CMAKE_CURRENT_SOURCE_DIR}/tools/singing_quality/prepare_world_reference.py")
  execute_process(COMMAND "${Python3_EXECUTABLE}" -B "${SEAM_WORLD_REFERENCE_VERIFIER}"
    verify "${SEAM_WORLD_DIR}" --variant "${SEAM_WORLD_VARIANT}"
    RESULT_VARIABLE SEAM_WORLD_VERIFY_RESULT OUTPUT_VARIABLE SEAM_WORLD_VERIFIED_JSON
    ERROR_VARIABLE SEAM_WORLD_VERIFY_ERROR OUTPUT_STRIP_TRAILING_WHITESPACE)
  if(NOT SEAM_WORLD_VERIFY_RESULT EQUAL 0)
    message(FATAL_ERROR "Pinned WORLD reference verification failed: ${SEAM_WORLD_VERIFY_ERROR}")
  endif()
  string(JSON SEAM_WORLD_SOURCE_MANIFEST_SHA256 GET "${SEAM_WORLD_VERIFIED_JSON}" sourceManifestSha256)
  string(JSON SEAM_WORLD_LOCK_SHA256 GET "${SEAM_WORLD_VERIFIED_JSON}" lockSha256)
  string(JSON SEAM_WORLD_REVISION GET "${SEAM_WORLD_VERIFIED_JSON}" revision)
  string(JSON SEAM_WORLD_FILE_COUNT LENGTH "${SEAM_WORLD_VERIFIED_JSON}" files)
  math(EXPR SEAM_WORLD_LAST_FILE "${SEAM_WORLD_FILE_COUNT} - 1")
  set(SEAM_WORLD_CPP_FILES)
  set(SEAM_WORLD_FILE_INITIALIZERS)
  foreach(SEAM_WORLD_INDEX RANGE ${SEAM_WORLD_LAST_FILE})
    string(JSON SEAM_WORLD_PATH GET "${SEAM_WORLD_VERIFIED_JSON}" files ${SEAM_WORLD_INDEX} path)
    string(JSON SEAM_WORLD_HASH GET "${SEAM_WORLD_VERIFIED_JSON}" files ${SEAM_WORLD_INDEX} sha256)
    string(APPEND SEAM_WORLD_FILE_INITIALIZERS "  {\"${SEAM_WORLD_PATH}\", \"${SEAM_WORLD_HASH}\"},\n")
    if(SEAM_WORLD_PATH MATCHES "\\.cpp$")
      list(APPEND SEAM_WORLD_CPP_FILES "${SEAM_WORLD_DIR}/${SEAM_WORLD_PATH}")
    endif()
  endforeach()
  configure_file("${CMAKE_CURRENT_SOURCE_DIR}/cmake/WorldReferenceIdentity.hpp.in"
    "${CMAKE_CURRENT_BINARY_DIR}/generated/seam/${SEAM_WORLD_TARGET}_identity.hpp" @ONLY)
  add_custom_target(${SEAM_WORLD_TARGET}_source_check
    COMMAND "${Python3_EXECUTABLE}" -B "${SEAM_WORLD_REFERENCE_VERIFIER}"
      verify "${SEAM_WORLD_DIR}" --variant "${SEAM_WORLD_VARIANT}" VERBATIM)
  add_library(${SEAM_WORLD_TARGET}_library STATIC EXCLUDE_FROM_ALL ${SEAM_WORLD_CPP_FILES})
  target_include_directories(${SEAM_WORLD_TARGET}_library SYSTEM PUBLIC "${SEAM_WORLD_DIR}/src")
  add_dependencies(${SEAM_WORLD_TARGET}_library ${SEAM_WORLD_TARGET}_source_check)
  add_executable(${SEAM_WORLD_TARGET} EXCLUDE_FROM_ALL tests/test_main.cpp
    tests/test_world_reference_experiment.cpp tools/singing_quality/target_voicing_experiment.cpp)
  target_include_directories(${SEAM_WORLD_TARGET} PRIVATE tests tools/singing_quality)
  target_link_libraries(${SEAM_WORLD_TARGET} PRIVATE seam_application seam_synthesis seam_voicebank
    ${SEAM_WORLD_TARGET}_library)
  target_compile_definitions(${SEAM_WORLD_TARGET} PRIVATE
    SEAM_WORLD_SOURCE_DIR="${SEAM_WORLD_DIR}"
    SEAM_WORLD_IDENTITY_HEADER="seam/${SEAM_WORLD_TARGET}_identity.hpp"
    SEAM_WORLD_EXECUTABLE="$<TARGET_FILE:${SEAM_WORLD_TARGET}>"
    SEAM_WORLD_LF_PROTOCOL="${CMAKE_CURRENT_SOURCE_DIR}/docs/implementation/U16_LF_CONTROL_PANEL_2026-09-22.md"
    SEAM_WORLD_REPORT_DIRECTORY="${CMAKE_CURRENT_BINARY_DIR}/world-reference-experiment-v2/${SEAM_WORLD_VARIANT}"
    SEAM_WORLD_SPEECH_FIXTURE="${CMAKE_CURRENT_SOURCE_DIR}/assets/demo-human-voicebank-public-domain/production-bank/audio/human-vowel-demo.wav")
  seam_apply_compiler_options(${SEAM_WORLD_TARGET})
  add_test(NAME ${SEAM_WORLD_TARGET} COMMAND ${SEAM_WORLD_TARGET})
  set_tests_properties(${SEAM_WORLD_TARGET} PROPERTIES TIMEOUT 180
    LABELS "developer-experiment;reference-comparison;not-singer-qualification")
endfunction()
seam_add_world_reference(upstream "${SEAM_WORLD_REFERENCE_SOURCE_DIR}" seam_world_reference_experiment_tests)
if(SEAM_WORLD_REFERENCE_GUARD_SOURCE_DIR)
  seam_add_world_reference(openutau-d4c-guard "${SEAM_WORLD_REFERENCE_GUARD_SOURCE_DIR}" seam_world_d4c_guard_experiment_tests)
endif()
