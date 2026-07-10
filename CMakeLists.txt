cmake_minimum_required(VERSION 3.20)
project(NXCrossCompare LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 소스 내 한글 주석/문자열이 MSVC 로케일(CP949)과 무관하게 UTF-8로 처리되도록 강제
if(MSVC)
  add_compile_options(/utf-8)
endif()

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
