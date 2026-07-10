add_executable(geometry_tests geometry_tests.cpp)
target_link_libraries(geometry_tests PRIVATE geometry)

add_test(NAME geometry_tests COMMAND geometry_tests)
