if(NOT DEFINED CHRONOSTORE_CLI OR NOT DEFINED CHRONOSTORE_CLI_TEST_DIR)
    message(FATAL_ERROR "CLI smoke test variables are missing")
endif()

file(REMOVE_RECURSE "${CHRONOSTORE_CLI_TEST_DIR}")

execute_process(
        COMMAND "${CHRONOSTORE_CLI}" put "${CHRONOSTORE_CLI_TEST_DIR}" temperature 100 21.5 room=lab
        RESULT_VARIABLE first_put_result
)

execute_process(
        COMMAND "${CHRONOSTORE_CLI}" put "${CHRONOSTORE_CLI_TEST_DIR}" temperature 200 22.75 room=lab
        RESULT_VARIABLE second_put_result
)

execute_process(
        COMMAND "${CHRONOSTORE_CLI}" latest "${CHRONOSTORE_CLI_TEST_DIR}" temperature room=lab
        RESULT_VARIABLE latest_result
        OUTPUT_VARIABLE latest_output
)

execute_process(
        COMMAND "${CHRONOSTORE_CLI}" get "${CHRONOSTORE_CLI_TEST_DIR}" temperature 100 room=lab
        RESULT_VARIABLE get_result
        OUTPUT_VARIABLE get_output
)

execute_process(
        COMMAND "${CHRONOSTORE_CLI}" flush "${CHRONOSTORE_CLI_TEST_DIR}"
        RESULT_VARIABLE flush_result
)

execute_process(
        COMMAND "${CHRONOSTORE_CLI}" sync "${CHRONOSTORE_CLI_TEST_DIR}"
        RESULT_VARIABLE sync_result
)

execute_process(
        COMMAND "${CHRONOSTORE_CLI}" stats "${CHRONOSTORE_CLI_TEST_DIR}"
        RESULT_VARIABLE stats_result
        OUTPUT_VARIABLE stats_output
)

execute_process(
        COMMAND "${CHRONOSTORE_CLI}" series "${CHRONOSTORE_CLI_TEST_DIR}"
        RESULT_VARIABLE series_result
        OUTPUT_VARIABLE series_output
)

execute_process(
        COMMAND "${CHRONOSTORE_CLI}" --help
        RESULT_VARIABLE help_result
        OUTPUT_VARIABLE help_output
)

execute_process(
        COMMAND "${CHRONOSTORE_CLI}" --version
        RESULT_VARIABLE version_result
        OUTPUT_VARIABLE version_output
)

if(NOT first_put_result EQUAL 0 OR NOT second_put_result EQUAL 0 OR
   NOT latest_result EQUAL 0 OR NOT get_result EQUAL 0 OR NOT flush_result EQUAL 0 OR
   NOT sync_result EQUAL 0 OR NOT stats_result EQUAL 0 OR NOT series_result EQUAL 0 OR
   NOT help_result EQUAL 0 OR NOT version_result EQUAL 0)
    message(FATAL_ERROR "CLI command failed during smoke test")
endif()

string(FIND "${latest_output}" "200\t22.75" latest_match)
string(FIND "${get_output}" "100\t21.5" get_match)
string(FIND "${stats_output}" "segments\t1" segment_match)
string(FIND "${stats_output}" "wal_bytes\t0" wal_match)
string(FIND "${stats_output}" "memtable_samples\t0" memtable_match)
string(FIND "${series_output}" "temperature\troom=lab" series_match)
string(FIND "${help_output}" "chronostore put" help_match)
string(FIND "${version_output}" "chronostore 0.1.0" version_match)

if(latest_match EQUAL -1 OR get_match EQUAL -1 OR segment_match EQUAL -1 OR wal_match EQUAL -1 OR
   memtable_match EQUAL -1 OR series_match EQUAL -1 OR help_match EQUAL -1 OR
   version_match EQUAL -1)
    message(FATAL_ERROR "CLI output did not match expected persisted state")
endif()

file(REMOVE_RECURSE "${CHRONOSTORE_CLI_TEST_DIR}")
