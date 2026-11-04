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

# windows.h가 min/max를 매크로로 정의하지 못하게 한다 - 안 그러면 std::max(...)/
# numeric_limits<double>::max()가 "함수 같은 매크로 호출 'max'에 대해 인수가 부족합니다"로 깨진다.
# vcpkg OCCT(find_package)는 임포트 타겟이 NOMINMAX를 같이 넘겨줘서 안 보였지만, 회사PC의
# NX_OCCT_DIRECT_LINK(TK*.lib 직접 링크) 빌드에서는 빠져서 터졌다(2026-11-04) - 연결 방식과
# 무관하게 프로젝트 전체에 직접 건다.
if(WIN32)
  add_compile_definitions(NOMINMAX)
endif()

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets OpenGLWidgets OpenGL)

# NxCadCore(공유 모듈, TV_BitSam과 공유) - 북마크 open + Assembly/Geometry 요약 읽기.
add_subdirectory(external/NxCadCore/CadImportModule/Core)

# NxBackend(NX Open 의존) - geometry::NxJtGeometryAdapter가 실제 북마크(.plmxml) 열기에 쓴다.
# § NX Open 보류(개발계획_v2.md §18) - 예전엔 UGII_BASE_DIR 환경변수만 있으면 자동으로 빌드했는데,
# 회사PC처럼 NX는 설치돼 있지만(변수 있음) NX Open 개발용 헤더가 없는 PC에서 빌드 설정을 갱신하면
# "NXOpen/Assemblies/Component.hxx를 찾을 수 없음"으로 빌드가 깨졌다(2026-11-04). 지금 앱은 STEP
# 업로드(§17)만 쓰므로, NX Open 연동을 재개할 때만 -DNX_ENABLE_NXOPEN=ON으로 명시해서 켠다.
option(NX_ENABLE_NXOPEN "NX Open 연동(NxBackend) 빌드 - §18 보류 중, 재개할 때만 ON + UGII_BASE_DIR 필요" OFF)
if(NX_ENABLE_NXOPEN AND DEFINED ENV{UGII_BASE_DIR})
  message(STATUS "NX_ENABLE_NXOPEN=ON, UGII_BASE_DIR=$ENV{UGII_BASE_DIR} - building NxBackend (real NX Open import).")
  add_subdirectory(external/NxCadCore/Shared)
  add_subdirectory(external/NxCadCore/CadImportModule/NxBackend)
  set(NX_OPEN_AVAILABLE ON)
else()
  if(NX_ENABLE_NXOPEN)
    message(WARNING "NX_ENABLE_NXOPEN=ON but UGII_BASE_DIR is not set - skipping NxBackend. Launch from NX Command Prompt.")
  endif()
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
