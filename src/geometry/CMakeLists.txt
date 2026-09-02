# OpenCASCADE(OCCT) - StepGeometryAdapter가 STEP(.stp) 파싱/B-rep 분석에 사용
# (개발계획_v2.md §17, vcpkg.json 매니페스트로 관리).
find_package(OpenCASCADE CONFIG REQUIRED)

add_library(geometry STATIC
    IGeometryAdapter.h
    MockGeometryAdapter.h
    MockGeometryAdapter.cpp
    NxJtGeometryAdapter.h
    NxJtGeometryAdapter.cpp
    StepGeometryAdapter.h
    StepGeometryAdapter.cpp
    AlignmentCheck.h
    AlignmentCheck.cpp
)

target_include_directories(geometry PUBLIC ${CMAKE_SOURCE_DIR}/src)
# PRIVATE: StepGeometryAdapter는 pImpl로 OCCT 타입을 감춰서 헤더에 노출하지 않으므로,
# OCCT include 경로는 geometry 라이브러리 내부(StepGeometryAdapter.cpp)에서만 필요하다.
target_include_directories(geometry PRIVATE ${OpenCASCADE_INCLUDE_DIR})
target_link_libraries(geometry PRIVATE ${OpenCASCADE_LIBRARIES})

if(NX_OPEN_AVAILABLE)
    target_compile_definitions(geometry PRIVATE NX_CROSSCOMPARE_HAS_NX_BACKEND)
    target_link_libraries(geometry PRIVATE NxBackend NxCadCoreBase)
endif()
