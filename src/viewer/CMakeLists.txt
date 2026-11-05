add_library(viewer STATIC
    Camera.h
    DiagnosticsLog.h
    DiagnosticsLog.cpp
    ModelViewport.h
    ModelViewport.cpp
    MultiViewportPanel.h
    MultiViewportPanel.cpp
)

target_link_libraries(viewer PUBLIC Qt6::Widgets Qt6::OpenGLWidgets Qt6::OpenGL geometry)
target_include_directories(viewer PUBLIC ${CMAKE_SOURCE_DIR}/src)
