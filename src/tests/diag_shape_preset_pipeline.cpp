// § 형상 프리셋 파이프라인 종단 검증(2026-09-18) - 실제 STEP 표본으로 "캡처 -> DB 저장
// -> DB에서 다시 불러오기(LoadRule) -> RuleEngine이 자동으로 FindPatchCandidates를
// 태워 측정값을 내는지"까지 전부 실제 코드 경로로 확인한다. Qt UI(ShapePresetDialog/
// RuleEditorDialog)는 다루지 않지만, 그 UI들이 호출하는 백엔드(Database/RuleEngine)는
// 전부 동일 코드 - 알고리즘+저장+자동적용 배선이 실제로 맞물려 돌아가는지가 핵심.
//
// 사용법: diag_shape_preset_pipeline <path.stp>
#include "database/Database.h"
#include "geometry/StepGeometryAdapter.h"
#include "rule/Rule.h"
#include "rule/RuleEngine.h"

#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>
#include <vector>

#ifdef _WIN32
#include <windows.h>

namespace {
std::string WideToUtf8(const wchar_t* wide) {
    const int len = WideCharToMultiByte(CP_UTF8, 0, wide, -1, nullptr, 0, nullptr, nullptr);
    if (len <= 0) return std::string();
    std::string utf8(static_cast<size_t>(len - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide, -1, utf8.data(), len, nullptr, nullptr);
    return utf8;
}
} // namespace

int RunDiag(const std::vector<std::string>& args);

int wmain(int argc, wchar_t** argv) {
    std::vector<std::string> args;
    for (int i = 0; i < argc; ++i) args.push_back(WideToUtf8(argv[i]));
    return RunDiag(args);
}

int RunDiag(const std::vector<std::string>& args) {
    if (args.size() < 2) {
#else
int main(int argc, char** argv) {
    std::vector<std::string> args(argv, argv + argc);
    if (args.size() < 2) {
#endif
        std::fprintf(stderr, "usage: diag_shape_preset_pipeline <path.stp>\n");
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
    std::printf("[1] STEP 로드 성공\n");

    // 시드: 실제 스냅/후크 형상을 정확히 모르니, FindFaceCandidates로 얻은 면들 중
    // "복합 형상"(면 개수가 많은 패치)을 만드는 지점을 하나 찾는다 - diag_feature_patch로
    // 이미 확인한 대로 같은 좌표(faceCount=9)가 여러 곳에서 반복되는 패턴이 있었다.
    const auto faces = adapter.FindFaceCandidates(handle, "", "");
    if (faces.empty()) {
        std::fprintf(stderr, "FindFaceCandidates가 빈 목록을 반환 - 진단 중단\n");
        return 1;
    }
    geometry::Vec3 bestSeed;
    int bestFaceCount = -1;
    for (const auto& face : faces) {
        const auto capture = adapter.CaptureFeaturePatch(handle, face.center);
        if (capture.valid && capture.descriptor.faceCount > bestFaceCount) {
            bestFaceCount = capture.descriptor.faceCount;
            bestSeed = face.center;
        }
    }
    if (bestFaceCount <= 0) {
        std::fprintf(stderr, "유효한 패치를 하나도 못 찾음 - 진단 중단\n");
        return 1;
    }
    std::printf("[2] 시드 선택 완료 - pos=(%.2f,%.2f,%.2f) faceCount=%d\n", bestSeed.x, bestSeed.y, bestSeed.z,
                bestFaceCount);

    const auto capture = adapter.CaptureFeaturePatch(handle, bestSeed);
    if (!capture.valid) {
        std::fprintf(stderr, "CaptureFeaturePatch 실패 - 진단 중단\n");
        return 1;
    }

    // DB에 저장 (임시 파일 DB - 실제 앱과 동일한 Database 클래스 사용).
    const char* tempDir = std::getenv("TEMP");
    if (!tempDir) tempDir = std::getenv("TMP");
    const std::string dbPath = (tempDir ? std::string(tempDir) + "\\" : std::string()) + "diag_shape_preset.db";
    std::remove(dbPath.c_str());
    database::Database db(dbPath);
    db.EnsureSchema();
    const int projectId = db.FindOrCreateProject("diag_project");
    const int presetId =
        db.SaveShapePreset(projectId, "DIAG_SNAP", capture.descriptor, /*imagePath=*/std::string(),
                            /*dimFilterKind=*/std::nullopt, /*dimFilterValueMm=*/std::nullopt);
    std::printf("[3] DB에 프리셋 저장 완료 - preset id=%d\n", presetId);

    // 규칙 작성: anchor_type="preset:DIAG_SNAP", measurementType=InstanceCount(가장
    // 간단 - anchor 1개, 값=매칭된 후보 개수) -> SaveRule -> LoadRule로 다시 읽어서
    // patchDescriptor가 DB 경유로 정확히 복원되는지까지 확인.
    rule::Rule rule;
    rule.name = "DIAG_SNAP 개수";
    rule.measurementType = rule::MeasurementType::InstanceCount;
    rule.projection = "3D";
    rule::Anchor anchor;
    anchor.role = "single";
    anchor.anchorType = "preset:DIAG_SNAP";
    rule.anchors.push_back(anchor);
    const int ruleId = db.SaveRule(projectId, rule);
    std::printf("[4] 규칙 저장 완료 - rule id=%d\n", ruleId);

    const rule::Rule loadedRule = db.LoadRule(ruleId);
    if (loadedRule.anchors.empty() || !loadedRule.anchors.front().patchDescriptor.has_value()) {
        std::fprintf(stderr, "LoadRule이 patchDescriptor를 복원 못 함 - DB 직렬화 버그 의심\n");
        return 1;
    }
    std::printf("[5] LoadRule로 patchDescriptor 복원 확인 (faceCount=%d)\n",
                loadedRule.anchors.front().patchDescriptor->faceCount);

    // RuleEngine::Evaluate - 실제로는 인치별로 다른 handle을 넘기지만, 여기선 같은 모델을
    // "43인치"라고 가정해서 자동 검색+선택 파이프라인이 끝까지 도는지만 확인한다.
    std::map<int, geometry::ModelHandle> handlesByInch;
    handlesByInch[43] = handle;
    try {
        const auto results = rule::RuleEngine::Evaluate(adapter, handlesByInch, loadedRule);
        for (const auto& r : results) {
            std::printf("[6] RuleEngine::Evaluate 성공 - inch=%d value(매칭 개수)=%.0f\n", r.inch, r.value);
        }
    } catch (const std::exception& e) {
        std::fprintf(stderr, "RuleEngine::Evaluate 실패: %s\n", e.what());
        return 1;
    }

    std::printf("모든 단계 통과.\n");
    return 0;
}
