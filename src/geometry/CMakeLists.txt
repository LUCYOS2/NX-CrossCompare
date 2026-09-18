# OpenCASCADE(OCCT) - StepGeometryAdapter가 STEP(.stp) 파싱/B-rep 분석에 사용
# (개발계획_v2.md §17, vcpkg.json 매니페스트로 관리).
#
# § VTK 강제 의존성 회피(2026-09-18) - 원격 PC에서 공식 OpenCASCADE prebuilt SDK(8.1)로
# 교체했더니 "VTK::CommonCore, VTK::IOImage를 찾을 수 없다"며 configure가 실패했다.
# 그 SDK는 OpenCASCADE_WITH_VTK=ON으로 빌드되어 있고, 이 값은 OpenCASCADEConfig.cmake
# 내부에서 강제로 세팅돼 있어 -DOpenCASCADE_WITH_VTK=OFF로 덮어써도 안 먹힌다(실제 확인
# 됨). COMPONENTS 없이 find_package를 부르면 OCCT 설정 파일이 Visualization/Draw 타겟
# 파일(VTK::* 임포트 타겟 참조)까지 전부 include해버려서 VTK가 없으면 실패한다.
#
# COMPONENTS 시도는 포기함(2026-09-18) - find_package(... COMPONENTS Visualization
# ModelingData ModelingAlgorithms ...)로 순서를 바꿔가며 여러 번 시도했지만, 이 SDK의
# ModelingAlgorithms 타겟 파일은 Visualization(TKService/TKV3d)이 먼저 정의돼 있어야
# 하고, 반대로 Visualization 타겟 파일은 ModelingData/ModelingAlgorithms(TKBRep,
# TKTopAlgo, TKMesh 등)이 먼저 정의돼 있어야 해서, 어느 순서로 나열해도 실패하는
# 순환 의존이 있다(원격 PC가 양쪽 순서 다 실제로 확인). 즉 이 SDK에서는 COMPONENTS로
# Visualization/Draw(및 그게 끌어오는 VTK)만 쏙 빼는 게 애초에 불가능하다.
#
# 대신 NX_OCCT_DIRECT_LINK=ON이면 find_package(CONFIG)의 임포트 타겟 그래프를 아예
# 타지 않고, 우리가 실제로 쓰는 TK*.lib들만 이름으로 직접 target_link_libraries한다.
# .lib 자체는 VTK 심볼을 참조하지 않으므로(OCCT의 VTK 연동은 별도 브릿지 라이브러리인
# TKIVtk/TKIVtkDraw에만 있고 TKV3d/TKService 본체에는 없음) 이렇게 하면 VTK 없이도
# 링크된다 - Visualization/Draw 타겟 파일(및 그 안의 VTK::* 참조) 자체를 CMake가
# 아예 읽지 않기 때문. 사용하려면 OpenCASCADE_INCLUDE_DIR와 OpenCASCADE_LIBRARY_DIR를
# 반드시 -D로 지정해야 한다(공식 SDK 폴더 구조 기준 보통 <SDK>/inc, <SDK>/win64/vc14/lib
# 형태 - 실제 폴더명은 SDK 압축을 풀어서 확인).
option(NX_OCCT_DIRECT_LINK
    "공식 OpenCASCADE SDK에서 find_package(CONFIG)의 Visualization<->ModelingAlgorithms \
순환 의존(VTK 요구)을 피하려면 ON - OpenCASCADE_INCLUDE_DIR/OpenCASCADE_LIBRARY_DIR를 \
반드시 같이 지정해야 함. vcpkg 환경에서는 OFF(기본값) 유지" OFF)
if(NX_OCCT_DIRECT_LINK)
    if(NOT OpenCASCADE_INCLUDE_DIR OR NOT OpenCASCADE_LIBRARY_DIR)
        message(FATAL_ERROR
            "NX_OCCT_DIRECT_LINK=ON이면 -DOpenCASCADE_INCLUDE_DIR=<SDK>/inc 와 "
            "-DOpenCASCADE_LIBRARY_DIR=<SDK>/win64/vc14/lib (SDK 실제 폴더 구조에 맞게) "
            "를 함께 지정해야 한다.")
    endif()
    set(OpenCASCADE_LIBRARIES
        TKernel TKMath
        TKG2d TKG3d TKGeomBase TKGeomAlgo TKBRep TKTopAlgo TKMesh TKShHealing TKHLR
        TKBool TKBO TKPrim TKFillet TKOffset TKFeat
        TKService TKV3d
        TKCDF TKLCAF TKCAF TKVCAF TKBinL TKBin TKBinTObj TKStdL TKStd TKXmlL TKXml TKXmlTObj TKXCAF
        TKXSBase TKDE TKDECascade TKDESTEP TKDEIGES TKDESTL TKDEOBJ TKDEPLY TKDEVRML
    )
else()
    find_package(OpenCASCADE CONFIG REQUIRED)
endif()

add_library(geometry STATIC
    IGeometryAdapter.h
    MockGeometryAdapter.h
    MockGeometryAdapter.cpp
    NxJtGeometryAdapter.h
    NxJtGeometryAdapter.cpp
    StepGeometryAdapter.h
    StepGeometryAdapter.cpp
    FeaturePatch.h
    FeaturePatch.cpp
    AlignmentCheck.h
    AlignmentCheck.cpp
)

target_include_directories(geometry PUBLIC ${CMAKE_SOURCE_DIR}/src)
# PRIVATE: StepGeometryAdapter는 pImpl로 OCCT 타입을 감춰서 헤더에 노출하지 않으므로,
# OCCT include 경로는 geometry 라이브러리 내부(StepGeometryAdapter.cpp)에서만 필요하다.
target_include_directories(geometry PRIVATE ${OpenCASCADE_INCLUDE_DIR})
if(NX_OCCT_DIRECT_LINK)
    target_link_directories(geometry PRIVATE ${OpenCASCADE_LIBRARY_DIR})
endif()
target_link_libraries(geometry PRIVATE ${OpenCASCADE_LIBRARIES})

if(NX_OPEN_AVAILABLE)
    target_compile_definitions(geometry PRIVATE NX_CROSSCOMPARE_HAS_NX_BACKEND)
    target_link_libraries(geometry PRIVATE NxBackend NxCadCoreBase)
endif()
