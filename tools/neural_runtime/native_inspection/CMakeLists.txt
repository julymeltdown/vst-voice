cmake_minimum_required(VERSION 3.24)
project(seam_native_onnx_inspection LANGUAGES CXX)
# Isolated development experiment, not linked to shipping targets.
find_package(Protobuf 33.4.0 EXACT CONFIG REQUIRED)
# The shipped helper links a static Protobuf/Abseil closure that carries no protoc,
# so a distribution build supplies the pinned code generator explicitly. The
# generated sources stay matched to the runtime because both come from 33.4.
if(SEAM_PROTOC_EXECUTABLE)
  if(NOT EXISTS "${SEAM_PROTOC_EXECUTABLE}")
    message(FATAL_ERROR "SEAM_PROTOC_EXECUTABLE does not exist: ${SEAM_PROTOC_EXECUTABLE}")
  endif()
  set(seam_onnx_protoc "${SEAM_PROTOC_EXECUTABLE}")
else()
  set(seam_onnx_protoc protobuf::protoc)
endif()
if(SEAM_STATIC_PROTOBUF_ROOT)
  # A distribution build must link the static archive from the pinned SDK. If the
  # imported target points anywhere else, the helper silently goes back to a host
  # runtime that cannot exist on a user's machine, so this is refused at configure
  # time instead of being discovered by the packaging owner later.
  set(seam_protobuf_archive "")
  foreach(configuration RELEASE DEBUG NONE)
    get_target_property(candidate protobuf::libprotobuf IMPORTED_LOCATION_${configuration})
    if(candidate)
      set(seam_protobuf_archive "${candidate}")
      break()
    endif()
  endforeach()
  if(NOT seam_protobuf_archive MATCHES "libprotobuf\\.(a|lib)$")
    message(FATAL_ERROR
      "Distribution builds require the pinned static Protobuf archive; found '${seam_protobuf_archive}'")
  endif()
endif()
set(SEAM_ONNX_SCHEMA "" CACHE FILEPATH "Pinned ONNX 1.19.1 onnx.proto")
file(SHA256 "${SEAM_ONNX_SCHEMA}" schema_digest)
if(NOT schema_digest STREQUAL "1fcbd4d5879b352d891a04656a76755114f1ff0a10de77cf2c53a65310132a13")
  message(FATAL_ERROR "ONNX schema differs from the inspected 1.19.1 bytes")
endif()
get_filename_component(onnx_source "${SEAM_ONNX_SCHEMA}" DIRECTORY)
get_filename_component(onnx_parent "${onnx_source}" DIRECTORY)
set(schema_dir "${CMAKE_CURRENT_BINARY_DIR}/schema")
file(MAKE_DIRECTORY "${schema_dir}/onnx")
set(generated_sources)
foreach(name onnx onnx-data onnx-operators)
  file(READ "${onnx_source}/${name}.proto" schema_text)
  string(REPLACE "option optimize_for = LITE_RUNTIME;" "option optimize_for = SPEED;" schema_text "${schema_text}")
  string(REPLACE "onnx/onnx-ml.proto" "onnx/onnx.proto" schema_text "${schema_text}")
  file(CONFIGURE OUTPUT "${schema_dir}/onnx/${name}.proto" CONTENT "${schema_text}" @ONLY)
  add_custom_command(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/onnx/${name}.pb.cc" "${CMAKE_CURRENT_BINARY_DIR}/onnx/${name}.pb.h"
    COMMAND ${seam_onnx_protoc} --cpp_out=${CMAKE_CURRENT_BINARY_DIR} --proto_path=${schema_dir} "${schema_dir}/onnx/${name}.proto"
    DEPENDS "${schema_dir}/onnx/${name}.proto" "${schema_dir}/onnx/onnx.proto" VERBATIM)
  list(APPEND generated_sources "${CMAKE_CURRENT_BINARY_DIR}/onnx/${name}.pb.cc")
endforeach()
# Official locally installed ONNX sources; still development-only dependencies.
file(GLOB_RECURSE checker_sources CONFIGURE_DEPENDS "${onnx_source}/defs/*.cc" "${onnx_source}/common/*.cc" "${onnx_source}/shape_inference/*.cc" "${onnx_source}/inliner/*.cc" "${onnx_source}/version_converter/*.cc")
add_library(seam_native_onnx_inspection OBJECT main.cpp ${generated_sources} ${checker_sources} "${onnx_source}/checker.cc")
target_compile_definitions(seam_native_onnx_inspection PUBLIC ONNX_NAMESPACE=onnx)
target_compile_features(seam_native_onnx_inspection PUBLIC cxx_std_20)
target_include_directories(seam_native_onnx_inspection SYSTEM PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}" "${onnx_parent}")
target_link_libraries(seam_native_onnx_inspection PUBLIC protobuf::libprotobuf)
add_executable(seam_native_onnx_inspect driver.cpp)
target_link_libraries(seam_native_onnx_inspect PRIVATE seam_native_onnx_inspection)
add_executable(seam_native_onnx_owned_bytes_test test_owned_bytes.cpp)
target_link_libraries(seam_native_onnx_owned_bytes_test PRIVATE seam_native_onnx_inspection)
