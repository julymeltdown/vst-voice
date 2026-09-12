add_executable(seam_phase12c_live_tests src/tests.cpp src/live_fixture.cpp)
target_link_libraries(seam_phase12c_live_tests PRIVATE seam_live_voice)
add_test(NAME seam_phase12c_live_tests COMMAND seam_phase12c_live_tests)
add_executable(seam_phase12c_matrix src/matrix_runner.cpp src/clap_matrix_host.cpp src/canonical_evidence.cpp)
target_link_libraries(seam_phase12c_matrix PRIVATE seam_voicebank seam_formats seam_core Threads::Threads ${CMAKE_DL_LIBS})
target_include_directories(seam_phase12c_matrix PRIVATE include)
target_include_directories(seam_phase12c_matrix SYSTEM PRIVATE "${PROJECT_SOURCE_DIR}/third_party/clap/include")
if(TARGET seam_clap_editor_plugin)
  add_dependencies(seam_phase12c_matrix seam_clap_editor_plugin)
  if(APPLE)
    set(SEAM_PHASE12C_PLUGIN_EVIDENCE "$<TARGET_BUNDLE_DIR:seam_clap_editor_plugin>")
  else()
    set(SEAM_PHASE12C_PLUGIN_EVIDENCE "$<TARGET_FILE:seam_clap_editor_plugin>")
  endif()
  add_test(NAME seam_phase12c_matrix
    COMMAND seam_phase12c_matrix "${CMAKE_CURRENT_BINARY_DIR}/matrix.json"
      --plugin "${SEAM_PHASE12C_PLUGIN_EVIDENCE}"
      --bank "${PROJECT_SOURCE_DIR}/assets/demo-human-voicebank-public-domain/production-bank"
      --development-fixture)
endif()
add_executable(seam_phase12c_allocation_probe src/allocation_probe.cpp src/live_fixture.cpp)
target_link_libraries(seam_phase12c_allocation_probe PRIVATE seam_live_voice)
add_test(NAME seam_phase12c_allocation_probe COMMAND seam_phase12c_allocation_probe)
add_executable(seam_phase12c_cancellation_storm src/cancellation_storm.cpp)
target_compile_features(seam_phase12c_cancellation_storm PRIVATE cxx_std_20)
seam_apply_compiler_options(seam_phase12c_cancellation_storm)
add_test(NAME seam_phase12c_cancellation_storm COMMAND seam_phase12c_cancellation_storm)
add_executable(seam_phase12c_soak src/soak_runner.cpp src/live_fixture.cpp src/canonical_evidence.cpp src/engine_evidence_resource.cpp)
target_link_libraries(seam_phase12c_soak PRIVATE seam_live_voice)
if(TARGET seam_clap_editor_plugin)
  add_test(NAME seam_phase12c_soak_smoke
    COMMAND seam_phase12c_soak --profile smoke
      --output "${CMAKE_CURRENT_BINARY_DIR}/soak-smoke.json"
      --plugin "${SEAM_PHASE12C_PLUGIN_EVIDENCE}"
      --bank "${PROJECT_SOURCE_DIR}/assets/demo-human-voicebank-public-domain/production-bank")
else()
  add_test(NAME seam_phase12c_soak_smoke COMMAND seam_phase12c_soak --profile smoke --output "${CMAKE_CURRENT_BINARY_DIR}/soak-smoke.json")
endif()
if(Python3_Interpreter_FOUND)
  add_test(NAME seam_phase12c_canonical_contract
    COMMAND ${Python3_EXECUTABLE}
      "${PROJECT_SOURCE_DIR}/scripts/verify_phase12c_canonical_contract.py"
      --root "${PROJECT_SOURCE_DIR}")
  set_tests_properties(seam_phase12c_canonical_contract PROPERTIES
    WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}")
  if(TARGET seam_clap_editor_plugin)
    add_test(NAME seam_phase12c_matrix_evidence
      COMMAND ${Python3_EXECUTABLE} "${PROJECT_SOURCE_DIR}/scripts/verify_phase12c_canonical_contract.py"
        --root "${PROJECT_SOURCE_DIR}" --plugin "${SEAM_PHASE12C_PLUGIN_EVIDENCE}"
        --bank "${PROJECT_SOURCE_DIR}/assets/demo-human-voicebank-public-domain/production-bank"
        --matrix "${CMAKE_CURRENT_BINARY_DIR}/matrix.json")
    set_tests_properties(seam_phase12c_matrix_evidence PROPERTIES DEPENDS seam_phase12c_matrix)
    add_test(NAME seam_phase12c_matrix_admission
      COMMAND ${Python3_EXECUTABLE} "${PROJECT_SOURCE_DIR}/tests/test_phase12c_matrix_host.py"
        --runner "$<TARGET_FILE:seam_phase12c_matrix>" --plugin "${SEAM_PHASE12C_PLUGIN_EVIDENCE}"
        --bank "${PROJECT_SOURCE_DIR}/assets/demo-human-voicebank-public-domain/production-bank")
  endif()
endif()
add_executable(seam_phase12c_live_demo
  "${PROJECT_SOURCE_DIR}/apps/seam-phase12c-live-demo/main.cpp")
target_compile_definitions(seam_phase12c_live_demo PRIVATE
  SEAM_SOURCE_PRODUCTION_VOICEBANK="${PROJECT_SOURCE_DIR}/assets/demo-human-voicebank-public-domain/production-bank")
target_link_libraries(seam_phase12c_live_demo PRIVATE seam_live_voice seam_voicebank seam_core)
seam_apply_compiler_options(seam_phase12c_live_demo)
add_test(NAME seam_phase12c_live_demo_smoke
  COMMAND seam_phase12c_live_demo --output "${CMAKE_CURRENT_BINARY_DIR}/live-demo-smoke")
foreach(target IN ITEMS seam_phase12c_live_tests seam_phase12c_matrix
                       seam_phase12c_allocation_probe seam_phase12c_soak)
  target_include_directories(${target} PRIVATE generated)
  seam_apply_compiler_options(${target})
endforeach()
if(UNIX AND NOT APPLE)
 find_package(X11 REQUIRED)
 add_executable(seam_phase12c_gui_lifecycle src/gui_lifecycle_x11.cpp)
 target_link_libraries(seam_phase12c_gui_lifecycle PRIVATE X11::X11)
endif()

if(UNIX AND NOT APPLE AND TARGET ProjectSEAMLive12C)
  add_executable(seam_phase12c_clap_host src/clap_live_host.cpp)
  target_include_directories(seam_phase12c_clap_host PRIVATE "${PROJECT_SOURCE_DIR}/third_party/clap/include")
  target_link_libraries(seam_phase12c_clap_host PRIVATE ${CMAKE_DL_LIBS})
  target_compile_features(seam_phase12c_clap_host PRIVATE cxx_std_20)
  add_test(NAME seam_phase12c_clap_host COMMAND seam_phase12c_clap_host $<TARGET_FILE:ProjectSEAMLive12C> "${CMAKE_CURRENT_BINARY_DIR}/clap-host.json")
endif()
