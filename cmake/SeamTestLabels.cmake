# CTest labels (completion plan M0-4).
#
# Every CTest entry carries exactly one kind label:
#   unit      a library or tool suite exercised in-process
#   journey   runs product programs or a multi-step workflow end to end
#   contract  checks repository content: sources, contracts, records, build identity
# and any of these tags:
#   gui               needs a logged-in window server
#   slow              took 5 s or more in the 2026-10-18 serial Release run
#   process-lifetime  spawns, kills or crashes processes on purpose
#
# Each directory labels its own tests at the end of its CMakeLists.txt, after every
# set_tests_properties(... LABELS ...) that would otherwise replace these labels.
# Labels are appended, so domain labels such as neural-native-experiment stay.
# tests/test_ctest_labels.py (CTest seam_ctest_label_coverage) checks the result.

# Appends LABEL to each named test of the current directory. Names that this
# configuration does not define are ignored, so a list may name platform- or
# option-gated tests.
function(seam_label_tests label)
  foreach(test_name IN LISTS ARGN)
    if(TEST ${test_name})
      set_property(TEST ${test_name} APPEND PROPERTY LABELS ${label})
    endif()
  endforeach()
endfunction()

# Labels every test of the current directory that has no kind label yet as unit.
function(seam_label_remaining_tests_as_unit)
  get_property(directory_tests DIRECTORY PROPERTY TESTS)
  foreach(test_name IN LISTS directory_tests)
    get_test_property(${test_name} LABELS test_labels)
    if(NOT test_labels)
      set(test_labels "")
    endif()
    if(NOT "unit" IN_LIST test_labels AND NOT "journey" IN_LIST test_labels
       AND NOT "contract" IN_LIST test_labels)
      set_property(TEST ${test_name} APPEND PROPERTY LABELS unit)
    endif()
  endforeach()
endfunction()
