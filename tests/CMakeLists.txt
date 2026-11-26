include(GoogleTest)

add_executable(logstat_tests
  test_parser.cpp
  test_histogram.cpp
  test_normalize.cpp
  test_aggregator.cpp
  test_report.cpp)
target_link_libraries(logstat_tests PRIVATE logstat_core GTest::gtest_main)
gtest_discover_tests(logstat_tests)

# Сквозные проверки бинарника: коды выхода и форматы вывода.
set(SAMPLE ${PROJECT_SOURCE_DIR}/examples/access.log)

add_test(NAME cli_text COMMAND logstat ${SAMPLE})
set_tests_properties(cli_text PROPERTIES PASS_REGULAR_EXPRESSION "requests: 12 +malformed lines: 1")

add_test(NAME cli_endpoint_normalized COMMAND logstat -n 3 ${SAMPLE})
set_tests_properties(cli_endpoint_normalized PROPERTIES PASS_REGULAR_EXPRESSION "GET /api/users/:id")

add_test(NAME cli_json COMMAND logstat --format json ${SAMPLE})
set_tests_properties(cli_json PROPERTIES PASS_REGULAR_EXPRESSION "\"requests\":12,\"malformed\":1")

add_test(NAME cli_prom COMMAND logstat --format prom ${SAMPLE})
set_tests_properties(cli_prom PROPERTIES PASS_REGULAR_EXPRESSION "logstat_requests_total{status_class=\"5xx\"} 2")

add_test(NAME cli_slo_violated COMMAND logstat --slo-p99-ms 100 ${SAMPLE})
set_tests_properties(cli_slo_violated PROPERTIES WILL_FAIL TRUE)

add_test(NAME cli_slo_ok COMMAND logstat --slo-p99-ms 5000 --slo-error-rate 0.5 ${SAMPLE})

add_test(NAME cli_missing_file COMMAND logstat /nonexistent/access.log)
set_tests_properties(cli_missing_file PROPERTIES WILL_FAIL TRUE)

add_test(NAME cli_bad_option COMMAND logstat --threads abc ${SAMPLE})
set_tests_properties(cli_bad_option PROPERTIES WILL_FAIL TRUE)
