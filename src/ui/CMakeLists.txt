add_library(ui STATIC
    MainWindow.h
    MainWindow.cpp
)

target_link_libraries(ui PUBLIC Qt6::Widgets viewer)
target_include_directories(ui PUBLIC ${CMAKE_SOURCE_DIR}/src)
