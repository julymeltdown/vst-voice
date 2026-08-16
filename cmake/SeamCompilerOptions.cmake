function(seam_apply_compiler_options target)
  if(MSVC)
    target_compile_options(${target} PRIVATE /W4 /permissive- /utf-8 /Zc:__cplusplus)
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
    if(SEAM_ENABLE_SANITIZERS AND CMAKE_BUILD_TYPE MATCHES "Debug|RelWithDebInfo")
      target_compile_options(${target} PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
      target_link_options(${target} PRIVATE -fsanitize=address,undefined)
    endif()
  endif()

  target_compile_definitions(${target} PRIVATE
    $<$<CONFIG:Debug>:SEAM_DEBUG=1>
    $<$<NOT:$<CONFIG:Debug>>:SEAM_DEBUG=0>)
endfunction()
