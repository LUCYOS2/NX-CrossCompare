add_library(ui STATIC
    MainWindow.h
    MainWindow.cpp
    RuleEditorDialog.h
    RuleEditorDialog.cpp
    ImportInchDialog.h
    ImportInchDialog.cpp
    ShapePresetDialog.h
    ShapePresetDialog.cpp
    PresetDimensionDialog.h
    PresetDimensionDialog.cpp
    MatchingDiagnosticsDialog.h
    MatchingDiagnosticsDialog.cpp
    ViewerDiagnosticsDialog.h
    ViewerDiagnosticsDialog.cpp
    CtpImage.h
    CtpImage.cpp
)

target_link_libraries(ui PUBLIC Qt6::Widgets viewer rule export_lib database)
target_include_directories(ui PUBLIC ${CMAKE_SOURCE_DIR}/src)
