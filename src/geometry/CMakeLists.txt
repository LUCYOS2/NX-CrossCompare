add_library(geometry STATIC
    IGeometryAdapter.h
    MockGeometryAdapter.h
    MockGeometryAdapter.cpp
    NxJtGeometryAdapter.h
    NxJtGeometryAdapter.cpp
    AlignmentCheck.h
    AlignmentCheck.cpp
)

target_include_directories(geometry PUBLIC ${CMAKE_SOURCE_DIR}/src)

if(NX_OPEN_AVAILABLE)
    target_compile_definitions(geometry PRIVATE NX_CROSSCOMPARE_HAS_NX_BACKEND)
    target_link_libraries(geometry PRIVATE NxBackend NxCadCoreBase)
endif()
