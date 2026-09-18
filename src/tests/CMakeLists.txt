add_executable(geometry_tests geometry_tests.cpp)
target_link_libraries(geometry_tests PRIVATE geometry)
add_test(NAME geometry_tests COMMAND geometry_tests)

add_executable(rule_tests rule_tests.cpp)
target_link_libraries(rule_tests PRIVATE rule database)
add_test(NAME rule_tests COMMAND rule_tests)

add_executable(rule_engine_tests rule_engine_tests.cpp)
target_link_libraries(rule_engine_tests PRIVATE rule geometry)
add_test(NAME rule_engine_tests COMMAND rule_engine_tests)

# § 형상 프리셋 진단(2026-09-18) - CTest에는 안 넣는다(실제 STEP 파일 경로가 필요한
# 일회성 진단 도구라 자동화 스위트에 안 맞음, diag_feature_patch.cpp 상단 주석 참고).
add_executable(diag_feature_patch diag_feature_patch.cpp)
target_link_libraries(diag_feature_patch PRIVATE geometry)

add_executable(diag_shape_preset_pipeline diag_shape_preset_pipeline.cpp)
target_link_libraries(diag_shape_preset_pipeline PRIVATE geometry rule database)
