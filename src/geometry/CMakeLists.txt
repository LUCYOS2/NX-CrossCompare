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
# 해결(옵션으로 분리): 우리가 실제로 쓰는 모듈만 COMPONENTS로 명시하면 Visualization/
# Draw 타겟 파일 자체를 안 건드리게 되는데, 이 COMPONENTS 지정 방식을 vcpkg판
# OpenCASCADEConfig.cmake(이 로컬 PC가 씀)가 지원 안 해서("unsupported component" 전부
# 거부, 실제 확인됨) - vcpkg가 업스트림과 다른 자체 설정 파일을 쓰기 때문이다. 그래서
# 기본값은 예전처럼 COMPONENTS 없이 부르고(vcpkg/로컬 개발 환경 그대로 유지), 공식 OCCT
# SDK를 쓰는 환경에서만 NX_OCCT_COMPONENTS_ONLY=ON을 켜서 COMPONENTS 방식을 쓰게 한다.
#
# COMPONENTS 이름은 TK*(개별 라이브러리) 단위가 아니라 모듈(그룹) 단위다 - 원격 PC가
# 공식 SDK의 OpenCASCADEConfig.cmake에서 직접 확인한 유효 값은 FoundationClasses,
# ModelingData, ModelingAlgorithms, Visualization, ApplicationFramework, DataExchange,
# Draw 7개뿐이다(2026-09-18 확인). 우리는 STEP 파싱/B-rep 분석/테셀레이션만 필요하고
# 자체 렌더링은 Qt/OpenGL로 직접 하므로 VTK를 끌어오는 Draw는 뺐는데, DataExchange
# 단독으로는 STEPCAFControl(XCAF 기반 STEP 리더)이 요구하는 TKCAF/TKCDF/TKLCAF/TKVCAF/
# TKBin*/TKStd*/TKXml*(-> ApplicationFramework 모듈) 및 TKService/TKV3d
# (-> Visualization 모듈) 타겟이 정의되지 않아 configure가 또 실패했다(원격 PC 확인,
# 2026-09-18). 그래서 ApplicationFramework/Visualization도 추가한다 - Visualization을
# 넣으면 애초에 피하려던 VTK 참조가 되살아날 수도 있는데, 그건 실제로 콘피규어해봐야
# 확인 가능하다(로컬 vcpkg 환경에서는 이 COMPONENTS 방식 자체가 지원되지 않아 재현/
# 검증 불가 - 원격 PC에서 직접 시도해서 결과를 알려줘야 다음 단계를 정할 수 있다).
option(NX_OCCT_COMPONENTS_ONLY
    "공식 OpenCASCADE SDK(예: VTK 지원 포함 빌드)에서 Draw(VTK) 타겟을 \
피하려면 ON - vcpkg 환경에서는 OFF(기본값) 유지" OFF)
if(NX_OCCT_COMPONENTS_ONLY)
    find_package(OpenCASCADE CONFIG REQUIRED COMPONENTS
        FoundationClasses ModelingData ModelingAlgorithms
        ApplicationFramework DataExchange Visualization
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
    AlignmentCheck.h
    AlignmentCheck.cpp
)

target_include_directories(geometry PUBLIC ${CMAKE_SOURCE_DIR}/src)
# PRIVATE: StepGeometryAdapter는 pImpl로 OCCT 타입을 감춰서 헤더에 노출하지 않으므로,
# OCCT include 경로는 geometry 라이브러리 내부(StepGeometryAdapter.cpp)에서만 필요하다.
target_include_directories(geometry PRIVATE ${OpenCASCADE_INCLUDE_DIR})
target_link_libraries(geometry PRIVATE ${OpenCASCADE_LIBRARIES})

if(NX_OPEN_AVAILABLE)
    target_compile_definitions(geometry PRIVATE NX_CROSSCOMPARE_HAS_NX_BACKEND)
    target_link_libraries(geometry PRIVATE NxBackend NxCadCoreBase)
endif()
