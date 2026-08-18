find_package(Python3 REQUIRED COMPONENTS Interpreter)

add_custom_target(seam_phase13b_contract
  COMMAND ${Python3_EXECUTABLE} -m unittest discover -s tests/phase13b -v
  COMMAND ${Python3_EXECUTABLE} scripts/verify_phase13b_contracts.py --root ${CMAKE_SOURCE_DIR}
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  COMMENT "Running Phase 13B content/IP release contract tests"
  VERBATIM)

add_custom_target(seam_phase13b_evidence
  COMMAND ${Python3_EXECUTABLE} scripts/generate_phase13b_evidence.py
          --root ${CMAKE_SOURCE_DIR}
          --output ${CMAKE_BINARY_DIR}/phase13b-evidence
  DEPENDS seam_phase13b_contract
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  COMMENT "Generating Phase 13B blocked-release and development-content evidence"
  VERBATIM)

if(SEAM_BUILD_TESTS OR BUILD_TESTING)
  add_test(NAME seam_phase13b_contract_tests
    COMMAND ${Python3_EXECUTABLE} -m unittest discover -s tests/phase13b -v)
  set_tests_properties(seam_phase13b_contract_tests PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

  add_test(NAME seam_phase13b_source_contract
    COMMAND ${Python3_EXECUTABLE} scripts/verify_phase13b_contracts.py --root ${CMAKE_SOURCE_DIR})
  set_tests_properties(seam_phase13b_source_contract PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

  add_test(NAME seam_phase13b_evidence_smoke
    COMMAND ${Python3_EXECUTABLE} scripts/generate_phase13b_evidence.py
            --root ${CMAKE_SOURCE_DIR}
            --output ${CMAKE_CURRENT_BINARY_DIR}/evidence)
  set_tests_properties(seam_phase13b_evidence_smoke PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
endif()
