add_library(export_lib STATIC
    ComparisonExporter.h
    ComparisonExporter.cpp
)

target_link_libraries(export_lib PUBLIC rule)
target_include_directories(export_lib PUBLIC ${CMAKE_SOURCE_DIR}/src)
