// § 형상 프리셋 진단 도구(2026-09-18) - CTest에 등록된 정식 유닛 테스트가 아니라, 실제
// STEP 표본으로 FeaturePatch 파이프라인(패치 추출/지문/유사도 검색)이 말이 되는 결과를
// 내는지 눈으로 확인하기 위한 일회성 진단 실행 파일. 계획서(형상 프리셋 기능) 검증
// 단계 - 실제 앞으로 임계값/가중치를 튜닝할 때 이 도구로 반복 확인한다.
//
// 사용법: diag_feature_patch <path.stp>
//
// § 유니코드 경로 테스트(2026-09-18) - 일반 main(argc, char** argv)는 Windows에서 OS가
// 명령줄을 "현재 ANSI 코드페이지"로 narrow 변환해서 넘겨주므로, 코드페이지에 없는 문자
// (예: ●)가 인자에 있으면 여기 도달하기도 전에 이미 깨진다 - StepGeometryAdapter의
// UTF-8 경로 처리 수정과는 별개 문제. 그래서 Windows에서는 wmain으로 원본 UTF-16
// 명령줄을 받아 직접 UTF-8로 변환해 넘긴다(WideCharToMultiByte) - 이래야 "●가 든 경로도
// 실제로 열리는지"를 제대로 테스트할 수 있다.
#include "geometry/StepGeometryAdapter.h"

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

#ifdef _WIN32
#include <windows.h>

namespace {
std::string WideToUtf8(const wchar_t* wide) {
    const int len = WideCharToMultiByte(CP_UTF8, 0, wide, -1, nullptr, 0, nullptr, nullptr);
    if (len <= 0) {
        return std::string();
    }
    std::string utf8(static_cast<size_t>(len - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide, -1, utf8.data(), len, nullptr, nullptr);
    return utf8;
}
} // namespace

int RunDiag(const std::vector<std::string>& args);

int wmain(int argc, wchar_t** argv) {
    std::vector<std::string> args;
    for (int i = 0; i < argc; ++i) {
        args.push_back(WideToUtf8(argv[i]));
    }
    return RunDiag(args);
}

int RunDiag(const std::vector<std::string>& args) {
    if (args.size() < 2) {
#else
int main(int argc, char** argv) {
    std::vector<std::string> args(argv, argv + argc);
    if (args.size() < 2) {
#endif
        std::fprintf(stderr, "usage: diag_feature_patch <path.stp>\n");
        return 1;
    }
    const std::string path = args[1];

    geometry::StepGeometryAdapter adapter;
    geometry::ModelHandle handle;
    try {
        handle = adapter.LoadModel(path);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "LoadModel failed: %s\n", e.what());
        return 1;
    }

    const auto box = adapter.GetBoundingBox(handle);
    std::printf("bbox min=(%.2f,%.2f,%.2f) max=(%.2f,%.2f,%.2f)\n", box.min.x, box.min.y, box.min.z, box.max.x,
                box.max.y, box.max.z);

    // Hole 후보(기존 원통 검색, 이미 검증됨)의 실제 표면 위 좌표를 시드로 써서 "형상
    // 위의 실제 점"에서 패치를 캡처해본다 - GetVertices()의 bbox 8코너는 표면 위 점이
    // 아니라서(대부분 빈 공간) 여기 검증에는 부적합.
    const auto holes = adapter.FindAnchorCandidates(handle, "Hole", "");
    std::printf("FindAnchorCandidates(Hole) -> %zu candidates\n", holes.size());
    if (holes.empty()) {
        std::fprintf(stderr, "no Hole candidates - cannot seed patch capture, aborting diagnostic\n");
        return 1;
    }

    int validCount = 0;
    const size_t seedCount = std::min<size_t>(holes.size(), 5);
    for (size_t i = 0; i < seedCount; ++i) {
        const geometry::Vec3 seedPoint = holes[i].position;
        const auto capture = adapter.CaptureFeaturePatch(handle, seedPoint);
        std::printf(
            "seed[%zu]=(%.2f,%.2f,%.2f) diameter=%.2f valid=%d faceCount=%d "
            "bboxRatio=(%.2f,%.2f,%.2f) boundaryRatio=%.3f radiusRatio=%.3f\n",
            i, seedPoint.x, seedPoint.y, seedPoint.z, holes[i].diameterMm, capture.valid ? 1 : 0,
            capture.descriptor.faceCount, capture.descriptor.bboxRatioX, capture.descriptor.bboxRatioY,
            capture.descriptor.bboxRatioZ, capture.descriptor.boundaryLengthRatio,
            capture.descriptor.dominantRadiusRatio);
        if (!capture.valid) {
            continue;
        }
        ++validCount;

        const auto candidates = adapter.FindPatchCandidates(handle, capture.descriptor, 0.7);
        std::printf("  FindPatchCandidates(threshold=0.7) -> %zu candidates\n", candidates.size());
        const size_t showCount = std::min<size_t>(candidates.size(), 8);
        for (size_t c = 0; c < showCount; ++c) {
            std::printf("    cand[%zu] pos=(%.2f,%.2f,%.2f) sim=%.3f\n", c, candidates[c].position.x,
                        candidates[c].position.y, candidates[c].position.z, candidates[c].similarity);
        }
    }

    std::printf("validCount=%d / %zu seeds\n", validCount, seedCount);

    // § Hook/Flange류 등 비원통 형상 다양성 확인 - FindFaceCandidates는 faceType 문자열로
    // 필터링하지 않고 Plane/Cylinder 면 전체를 반환하므로(StepGeometryAdapter 구현 참고),
    // Hole이 아닌 다른 실제 형상(벽/스냅탭 등)에서도 패치 지문이 의미 있게 갈리는지
    // 넓게 훑어본다. 매 K번째만 샘플링(전체 다 하면 출력이 너무 많다).
    const auto faces = adapter.FindFaceCandidates(handle, "", "");
    std::printf("\nFindFaceCandidates(all Plane/Cylinder) -> %zu candidates\n", faces.size());
    if (!faces.empty()) {
        const size_t stride = std::max<size_t>(1, faces.size() / 12);
        for (size_t i = 0; i < faces.size(); i += stride) {
            const auto capture = adapter.CaptureFeaturePatch(handle, faces[i].center);
            std::printf(
                "face[%zu] pos=(%.2f,%.2f,%.2f) valid=%d faceCount=%d bboxRatio=(%.2f,%.2f,%.2f) "
                "boundaryRatio=%.3f radiusRatio=%.3f\n",
                i, faces[i].center.x, faces[i].center.y, faces[i].center.z, capture.valid ? 1 : 0,
                capture.descriptor.faceCount, capture.descriptor.bboxRatioX, capture.descriptor.bboxRatioY,
                capture.descriptor.bboxRatioZ, capture.descriptor.boundaryLengthRatio,
                capture.descriptor.dominantRadiusRatio);
        }
    }

    return 0;
}
