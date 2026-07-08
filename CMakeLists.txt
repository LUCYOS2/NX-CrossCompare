cmake_minimum_required(VERSION 3.20)
project(NXCrossCompare LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Phase1부터 실제 서브모듈이 채워지면 하나씩 활성화
# add_subdirectory(src/core)
# add_subdirectory(src/geometry)
# add_subdirectory(src/rule)
# add_subdirectory(src/viewer)
# add_subdirectory(src/database)
# add_subdirectory(src/ui)

add_executable(nx_crosscompare src/main.cpp)

enable_testing()
# add_subdirectory(src/tests)
