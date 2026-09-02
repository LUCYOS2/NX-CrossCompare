# vcpkg 매니페스트(vcpkg.json) 자동 연동 - external/vcpkg submodule을 project() 이전에
# 툴체인으로 지정해야 vcpkg가 관리하는 패키지(OpenCASCADE 등)를 find_package로 찾을 수
# 있다. 다른 툴체인을 이미 지정한 경우(-DCMAKE_TOOLCHAIN_FILE=...)는 그대로 존중한다.
if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
  set(CMAKE_TOOLCHAIN_FILE "${CMAKE_CURRENT_SOURCE_DIR}/external/vcpkg/scripts/buildsystems/vcpkg.cmake"
      CACHE STRING "Vcpkg toolchain file")
endif()

cmake_minimum_required(VERSION 3.20)
project(NXCrossCompare LANGUAGES CXX C) # C: SQLite amalgamation(sqlite3.c) 컴파일용

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 소스 내 한글 주석/문자열이 MSVC 로케일(CP949)과 무관하게 UTF-8로 처리되도록 강제
if(MSVC)
  add_compile_options(/utf-8)
endif()

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets OpenGLWidgets OpenGL)

# NxCadCore(공유 모듈, TV_BitSam과 공유) - 북마크 open + Assembly/Geometry 요약 읽기.
# NxBackend(NX Open 의존)는 회사PC 전용이라 여기서는 NX 비의존 Core만 연결한다.
# geometry::NxJtGeometryAdapter가 회사PC에서 이 라이브러리를 사용하게 될 예정.
add_subdirectory(external/NxCadCore/CadImportModule/Core)

add_subdirectory(src/geometry)
add_subdirectory(src/rule)
add_subdirectory(src/database)
add_subdirectory(src/export)
add_subdirectory(src/viewer)
add_subdirectory(src/ui)
# Phase 진행에 따라 서브모듈 추가 활성화
# add_subdirectory(src/core)

add_executable(nx_crosscompare src/main.cpp)
target_link_libraries(nx_crosscompare PRIVATE ui geometry Qt6::Widgets)

enable_testing()
add_subdirectory(src/tests)
