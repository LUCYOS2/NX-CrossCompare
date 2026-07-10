add_library(sqlite3 STATIC third_party/sqlite/sqlite3.c)
target_include_directories(sqlite3 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/third_party/sqlite)
if(MSVC)
  target_compile_definitions(sqlite3 PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

add_library(database STATIC
    Database.h
    Database.cpp
)

target_link_libraries(database PUBLIC sqlite3 rule)
target_include_directories(database PUBLIC ${CMAKE_SOURCE_DIR}/src)
