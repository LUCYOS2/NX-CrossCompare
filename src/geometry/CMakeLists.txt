add_library(geometry STATIC
    IGeometryAdapter.h
    MockGeometryAdapter.h
    MockGeometryAdapter.cpp
)

target_include_directories(geometry PUBLIC ${CMAKE_SOURCE_DIR}/src)
