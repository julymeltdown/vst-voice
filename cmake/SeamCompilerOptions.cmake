function(seam_apply_compiler_options target)
  if(SEAM_ENABLE_SANITIZERS AND SEAM_ENABLE_THREAD_SANITIZER)
    message(FATAL_ERROR "Address/Undefined sanitizers cannot be combined with ThreadSanitizer")
  endif()
  if(MSVC)
    # C4324 reports the padding required by explicit cache-line alignas members.
    # The padding is the design of the lock-free queues, not accidental layout
    # waste, so retain the alignment while keeping every other /W4 diagnostic.
    target_compile_options(${target} PRIVATE /W4 /wd4324 /permissive- /utf-8 /Zc:__cplusplus)
    # Test fixtures intentionally inspect and mutate process environment state.
    # Production code uses seam::core::environmentVariable's owned _dupenv_s
    # snapshot; keep MSVC's legacy getenv deprecation local to test binaries.
    if("${target}" MATCHES "_tests$")
      target_compile_definitions(${target} PRIVATE _CRT_SECURE_NO_WARNINGS)
    endif()
    if(SEAM_WARNINGS_AS_ERRORS)
      target_compile_options(${target} PRIVATE /WX)
    endif()
  else()
    target_compile_options(${target} PRIVATE
      -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion
      -Wshadow -Wnon-virtual-dtor -Wold-style-cast -Woverloaded-virtual)
    if(SEAM_WARNINGS_AS_ERRORS)
      target_compile_options(${target} PRIVATE -Werror)
    endif()
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
      # Designated aggregates intentionally rely on default member initializers;
      # GCC promotes this benign pattern through -Wextra as missing-field warnings.
      target_compile_options(${target} PRIVATE -Wno-missing-field-initializers)
    endif()
    if(APPLE AND CMAKE_CXX_COMPILER_ID STREQUAL "AppleClang")
      target_compile_options(${target} PRIVATE
        -include
        "${CMAKE_SOURCE_DIR}/cmake/AppleClangStopTokenCompatibility.hpp")
    endif()
    if(SEAM_ENABLE_SANITIZERS AND CMAKE_BUILD_TYPE MATCHES "Debug|RelWithDebInfo")
      target_compile_options(${target} PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
      target_link_options(${target} PRIVATE -fsanitize=address,undefined)
    endif()
    if(SEAM_ENABLE_THREAD_SANITIZER AND CMAKE_BUILD_TYPE MATCHES "Debug|RelWithDebInfo")
      target_compile_options(${target} PRIVATE -fsanitize=thread -fno-omit-frame-pointer)
      target_link_options(${target} PRIVATE -fsanitize=thread)
    endif()
  endif()

  target_compile_definitions(${target} PRIVATE
    $<$<CONFIG:Debug>:SEAM_DEBUG=1>
    $<$<NOT:$<CONFIG:Debug>>:SEAM_DEBUG=0>)
endfunction()
