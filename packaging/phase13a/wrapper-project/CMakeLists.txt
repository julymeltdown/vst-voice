cmake_minimum_required(VERSION 3.25)
project(ProjectSEAMFormatWrappers VERSION 0.13.0 LANGUAGES C CXX)

foreach(required CLAP_WRAPPER_ROOT CLAP_SDK_ROOT VST3_SDK_ROOT SEAM_CLAP_MODULE SEAM_ARTIFACT_DIR)
  if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
    message(FATAL_ERROR "${required} is required")
  endif()
endforeach()
foreach(path_var CLAP_WRAPPER_ROOT CLAP_SDK_ROOT VST3_SDK_ROOT)
  if(NOT EXISTS "${${path_var}}/LICENSE" AND NOT EXISTS "${${path_var}}/LICENSE.txt")
    message(FATAL_ERROR "${path_var} is missing its audited licence file: ${${path_var}}")
  endif()
endforeach()
if(NOT EXISTS "${SEAM_CLAP_MODULE}")
  message(FATAL_ERROR "SEAM_CLAP_MODULE does not exist: ${SEAM_CLAP_MODULE}")
endif()

set(CLAP_SDK_ROOT "${CLAP_SDK_ROOT}" CACHE PATH "Pinned CLAP SDK" FORCE)
set(VST3_SDK_ROOT "${VST3_SDK_ROOT}" CACHE PATH "Pinned VST3 SDK" FORCE)
set(CLAP_WRAPPER_OUTPUT_NAME "ProjectSEAMEditor" CACHE STRING "Wrapper output name" FORCE)
set(CLAP_WRAPPER_BUNDLE_IDENTIFIER "com.project-seam.editor" CACHE STRING "Bundle ID" FORCE)
set(CLAP_WRAPPER_BUNDLE_VERSION "0.13.0" CACHE STRING "Bundle version" FORCE)
set(CLAP_WRAPPER_DOWNLOAD_DEPENDENCIES OFF CACHE BOOL "Network downloads are forbidden" FORCE)
set(CLAP_WRAPPER_COPY_AFTER_BUILD OFF CACHE BOOL "Do not mutate user plug-in folders" FORCE)
set(CLAP_SUPPORTS_ALL_NOTE_EXPRESSIONS ON CACHE BOOL "Forward all CLAP note expressions" FORCE)
set(CLAP_WRAPPER_WINDOWS_SINGLE_FILE ON CACHE BOOL "Use a signable Windows VST3 single file" FORCE)
set(CLAP_WRAPPER_CXX_STANDARD 20 CACHE STRING "C++ standard" FORCE)

if(APPLE)
  enable_language(OBJC)
  enable_language(OBJCXX)
endif()

add_subdirectory("${CLAP_WRAPPER_ROOT}" "${CMAKE_BINARY_DIR}/clap-wrapper" EXCLUDE_FROM_ALL)

add_library(ProjectSEAMEditorVST3 MODULE)
target_add_vst3_wrapper(
  TARGET ProjectSEAMEditorVST3
  OUTPUT_NAME "ProjectSEAMEditor"
  SUPPORTS_ALL_NOTE_EXPRESSIONS TRUE
  SINGLE_PLUGIN_TUID "8A10B64D9F834B1485A805C9A89F1301"
  BUNDLE_IDENTIFIER "com.project-seam.editor.vst3"
  BUNDLE_VERSION "0.13.0"
  WINDOWS_FOLDER_VST3 FALSE
  RESOURCE_DIRECTORY "${SEAM_RESOURCE_DIR}"
  ASSET_OUTPUT_DIRECTORY "${SEAM_ARTIFACT_DIR}"
  MACOS_EMBEDDED_CLAP_LOCATION "${SEAM_CLAP_MODULE}")

if(APPLE AND SEAM_BUILD_AUV2)
  if(NOT DEFINED AUDIOUNIT_SDK_ROOT OR "${AUDIOUNIT_SDK_ROOT}" STREQUAL "")
    message(FATAL_ERROR "AUDIOUNIT_SDK_ROOT is required for AUv2")
  endif()
  if(NOT EXISTS "${AUDIOUNIT_SDK_ROOT}/LICENSE" AND NOT EXISTS "${AUDIOUNIT_SDK_ROOT}/LICENSE.txt")
    message(FATAL_ERROR "AUDIOUNIT_SDK_ROOT is missing its audited licence file")
  endif()
  set(AUDIOUNIT_SDK_ROOT "${AUDIOUNIT_SDK_ROOT}" CACHE PATH "Pinned AudioUnitSDK" FORCE)
  add_library(ProjectSEAMEditorAUv2 MODULE)
  target_add_auv2_wrapper(
    TARGET ProjectSEAMEditorAUv2
    OUTPUT_NAME "ProjectSEAMEditor"
    BUNDLE_IDENTIFIER "com.project-seam.editor.auv2"
    BUNDLE_VERSION "0.13.0"
    MANUFACTURER_NAME "Project SEAM"
    MANUFACTURER_CODE "PSEM"
    SUBTYPE_CODE "SEAM"
    INSTRUMENT_TYPE "aumu"
    MACOS_EMBEDDED_CLAP_LOCATION "${SEAM_CLAP_MODULE}"
    PREFER_CMAKE_AUV2_CONFIGURATION TRUE)
  set_target_properties(ProjectSEAMEditorAUv2 PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${SEAM_ARTIFACT_DIR}")
endif()
