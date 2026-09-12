foreach(scenario allowed cycle direct transitive link_only alias)
  execute_process(COMMAND "${CMAKE_COMMAND}"
      -S "${SEAM_SOURCE}/tests/cmake/neural_dependencies"
      -B "${SEAM_BINARY}/dependency-direction-tests/${scenario}"
      "-DSCENARIO=${scenario}"
      "-DSEAM_GUARD=${SEAM_SOURCE}/cmake/NeuralDependencyDirection.cmake"
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error TIMEOUT 20)
  if(scenario STREQUAL "allowed" OR scenario STREQUAL "cycle")
    if(NOT result EQUAL 0)
      message(FATAL_ERROR "Allowed graph ${scenario} rejected: ${output}${error}")
    endif()
  elseif(result EQUAL 0 OR NOT "${output}${error}" MATCHES "Neural dependency points upward")
    message(FATAL_ERROR "Forbidden graph ${scenario} did not fail with the dependency diagnostic: ${output}${error}")
  endif()
endforeach()
