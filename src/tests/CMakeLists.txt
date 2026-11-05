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

# § 측정 매칭 진단(2026-10-30) - 실제 STEP으로 클릭 좌표 매칭의 정답/실패/오답 개수를 센다.
add_executable(diag_measure_matching diag_measure_matching.cpp)
target_link_libraries(diag_measure_matching PRIVATE geometry rule)

# § 뷰어 점검(2026-11-04) - 실제 STEP으로 뷰어를 띄워 조작 경로를 한 번씩 거친다(Debug 힙 검사용, 창이 뜸).
add_executable(diag_viewer_smoke diag_viewer_smoke.cpp)
target_link_libraries(diag_viewer_smoke PRIVATE viewer geometry Qt6::Widgets)

# § 뷰어 입력 점검(2026-11-05) - 실제 키/마우스 입력을 흉내 내서 단축키와 조작 속도를 확인.
# 키 입력 흉내(QWindowSystemInterface)에 Qt 비공개 헤더가 필요해서, 그게 없는 설치(회사PC 등)에선 건너뛴다.
set(QT_NO_PRIVATE_MODULE_WARNING ON)
find_package(Qt6 QUIET COMPONENTS GuiPrivate)
if(TARGET Qt6::GuiPrivate)
    add_executable(diag_viewer_input diag_viewer_input.cpp)
    target_link_libraries(diag_viewer_input PRIVATE ui viewer geometry database Qt6::Widgets Qt6::GuiPrivate)
endif()
