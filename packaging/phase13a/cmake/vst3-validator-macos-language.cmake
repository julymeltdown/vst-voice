# The pinned Steinberg VST3 SDK's own top-level CMakeLists.txt (project "vstsdk")
# never calls enable_language(OBJCXX), relying on CMake's implicit per-source-file
# language detection for its .mm sources (module_mac.mm, used directly by the
# "validator" target we build in CI). That implicit path is not reliable across
# every CMake version/generator combination: our macOS CI runner hit
# "CMake Error: Error required internal CMake variable not set... Missing
# variable is: CMAKE_OBJCXX_COMPILE_OBJECT" at generate time once our own
# from_chars/tempo-parser fixes let the build progress this far.
#
# Injected via CMAKE_PROJECT_vstsdk_INCLUDE so the pinned SDK checkout stays
# byte-for-byte unmodified. This runs immediately after the SDK's own
# project(vstsdk ...) call, before any add_subdirectory() that needs OBJCXX.
if(APPLE)
  enable_language(OBJCXX)
endif()
