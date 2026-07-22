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
add_subdirectory(external/NxCadCore/CadImportModule/Core)

# NxBackend(NX Open 의존)는 UGII_BASE_DIR이 잡혀있을 때만 빌드된다 - NX Command
# Prompt에서 실행한 VS/터미널이 아니면 이 변수가 없어서 여기서 자동으로 건너뛴다.
# geometry::NxJtGeometryAdapter가 이 라이브러리를 실제 북마크(.plmxml) 열기에 사용한다.
if(DEFINED ENV{UGII_BASE_DIR})
  message(STATUS "UGII_BASE_DIR found ($ENV{UGII_BASE_DIR}) - building NxBackend (real NX Open import).")
  add_subdirectory(external/NxCadCore/Shared)
  add_subdirectory(external/NxCadCore/CadImportModule/NxBackend)
  set(NX_OPEN_AVAILABLE ON)
else()
  message(STATUS "UGII_BASE_DIR not set - skipping NxBackend. Launch from NX Command Prompt to enable real bookmark import.")
  set(NX_OPEN_AVAILABLE OFF)
endif()

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
