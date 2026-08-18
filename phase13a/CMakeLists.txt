find_package(Python3 REQUIRED COMPONENTS Interpreter)

add_custom_target(seam_phase13a_contract
  COMMAND ${Python3_EXECUTABLE} -m unittest discover -s tests/phase13a -v
  COMMAND ${Python3_EXECUTABLE} scripts/verify_phase13a_contracts.py --root ${CMAKE_SOURCE_DIR}
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  COMMENT "Running Phase 13A distribution, packaging, and mandatory-gate contract tests"
  VERBATIM)

if(SEAM_BUILD_TESTS OR BUILD_TESTING)
  add_test(NAME seam_phase13a_contract_tests
    COMMAND ${Python3_EXECUTABLE} -m unittest discover -s tests/phase13a -v)
  set_tests_properties(seam_phase13a_contract_tests PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

  add_test(NAME seam_phase13a_source_contract
    COMMAND ${Python3_EXECUTABLE} scripts/verify_phase13a_contracts.py --root ${CMAKE_SOURCE_DIR})
  set_tests_properties(seam_phase13a_source_contract PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

  add_test(NAME seam_phase13a_release_gate_blocked
    COMMAND ${Python3_EXECUTABLE} tools/phase13a/release_gate.py check
            --matrix docs/phase13a/mandatory-validation-matrix.json
            --gate G4 --expect-blocked)
  set_tests_properties(seam_phase13a_release_gate_blocked PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

  if(UNIX AND NOT APPLE AND TARGET seam_clap_editor_plugin)
    set(SEAM_PHASE13A_DEV_PACKAGE
      "${CMAKE_CURRENT_BINARY_DIR}/ProjectSEAM-0.13.0-unsigned-development.zip")
    set(SEAM_PHASE13A_RESOURCE_DIR
      "$<TARGET_FILE_DIR:seam_clap_editor_plugin>/ProjectSEAMEditor.resources")
    add_test(NAME seam_phase13a_linux_developer_package
      COMMAND ${Python3_EXECUTABLE} tools/phase13a/developer_package.py
              --clap $<TARGET_FILE:seam_clap_editor_plugin>
              --resources ${SEAM_PHASE13A_RESOURCE_DIR}
              --output ${SEAM_PHASE13A_DEV_PACKAGE}
              --version 0.13.0)
    set_tests_properties(seam_phase13a_linux_developer_package PROPERTIES
      WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
    add_test(NAME seam_phase13a_linux_install_smoke
      COMMAND ${Python3_EXECUTABLE} tools/phase13a/linux_package_smoke.py
              --package ${SEAM_PHASE13A_DEV_PACKAGE}
              --sandbox ${CMAKE_CURRENT_BINARY_DIR}/linux-install-smoke
              --output ${CMAKE_CURRENT_BINARY_DIR}/linux-install-smoke.json)
    set_tests_properties(seam_phase13a_linux_install_smoke PROPERTIES
      WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
      DEPENDS seam_phase13a_linux_developer_package)
  endif()
endif()
