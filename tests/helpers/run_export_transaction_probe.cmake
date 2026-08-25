if(NOT DEFINED PROBE OR NOT DEFINED WORK_ROOT)
  message(FATAL_ERROR "PROBE and WORK_ROOT are required")
endif()

set(phases
  JournalPrepared
  PreviousMoved
  DestinationPublished
  ReceiptCommitted
  BackupRemoved)
set(expected_revisions 1 1 1 2 2)

list(LENGTH phases phase_count)
math(EXPR last_phase "${phase_count} - 1")
foreach(index RANGE 0 ${last_phase})
  list(GET phases ${index} phase)
  list(GET expected_revisions ${index} expected_revision)
  set(case_root "${WORK_ROOT}/${phase}")
  file(REMOVE_RECURSE "${case_root}")

  execute_process(
    COMMAND "${PROBE}" seed "${case_root}"
    RESULT_VARIABLE seed_result
    OUTPUT_VARIABLE seed_output
    ERROR_VARIABLE seed_error)
  if(NOT seed_result EQUAL 0)
    message(FATAL_ERROR
      "seed failed for ${phase}: ${seed_result}\n${seed_output}\n${seed_error}")
  endif()

  execute_process(
    COMMAND "${PROBE}" interrupt "${case_root}" "${phase}"
    RESULT_VARIABLE interrupt_result
    OUTPUT_VARIABLE interrupt_output
    ERROR_VARIABLE interrupt_error)
  if(NOT interrupt_result EQUAL 86)
    message(FATAL_ERROR
      "interrupt failed for ${phase}: ${interrupt_result}\n${interrupt_output}\n${interrupt_error}")
  endif()

  execute_process(
    COMMAND "${PROBE}" recover "${case_root}" "${expected_revision}"
    RESULT_VARIABLE recover_result
    OUTPUT_VARIABLE recover_output
    ERROR_VARIABLE recover_error)
  if(NOT recover_result EQUAL 0)
    message(FATAL_ERROR
      "recovery failed for ${phase}: ${recover_result}\n${recover_output}\n${recover_error}")
  endif()
  message(STATUS
    "${phase}: process interruption recovered revision ${expected_revision}")
endforeach()
