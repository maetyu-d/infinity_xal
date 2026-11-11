if(NOT INFINITY_EXE OR NOT OUTPUT)
  message(FATAL_ERROR "INFINITY_EXE and OUTPUT required")
endif()
file(MAKE_DIRECTORY "${OUTPUT}")
file(REMOVE_RECURSE "${OUTPUT}/data" "${OUTPUT}/captures")
find_program(capture_python NAMES python3 REQUIRED)
execute_process(COMMAND "${capture_python}" "${CMAKE_CURRENT_LIST_DIR}/../tools/run_economy_native.py"
  "${INFINITY_EXE}" "${OUTPUT}" --scenario micro-expedition-test
  OUTPUT_FILE "${OUTPUT}/native.log" ERROR_FILE "${OUTPUT}/native.log"
  RESULT_VARIABLE result TIMEOUT 610)
if(NOT result EQUAL 0)
  file(READ "${OUTPUT}/native.log" diagnostics)
  message(FATAL_ERROR "Native flight-controlled micro expedition failed (${result}):\n${diagnostics}")
endif()
