add_executable(geometry_tests geometry_tests.cpp)
target_link_libraries(geometry_tests PRIVATE geometry)
add_test(NAME geometry_tests COMMAND geometry_tests)

add_executable(rule_tests rule_tests.cpp)
target_link_libraries(rule_tests PRIVATE rule database)
add_test(NAME rule_tests COMMAND rule_tests)
