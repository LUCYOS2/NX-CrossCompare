// § 측정 매칭 진단(2026-10-30) - 실제 STEP에서 클릭 좌표 기반 매칭(rule::Matching)이 얼마나
// 정확한지 전수 검사하는 명령줄 도구(CTest에는 안 넣음 - 실제 파일 경로가 필요).
//
//   diag_measure_matching <기준.stp> [다른 인치.stp ...]
//
// 검사 내용은 rule::RunMatchingDiagnostics(MatchingDiagnostics.h) 참고 - 앱의 "검증 → 매칭 진단"
// 메뉴와 같은 코드다(2026-11-04). 앱에서 STEP을 불러와 쓰는 게 기본이고, 이 도구는 여러 파일을
// 한 번에 돌리거나 로그로 남길 때 쓴다(scripts/run_diag_matching.bat).

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif

#include "geometry/StepGeometryAdapter.h"
#include "rule/Matching.h"
#include "rule/MatchingDiagnostics.h"

#include <cstdio>
#include <string>
#include <utility>
#include <vector>

namespace {

void Print(const rule::DiagnosticTally& t) {
    for (const auto& [reason, count] : t.reasons) {
        std::printf("      - %s: %d\n", reason.c_str(), count);
    }
}

int Run(const std::vector<std::string>& args) {
    if (args.size() < 2) {
        std::fprintf(stderr, "사용법: %s <기준.stp> [다른 인치.stp ...]\n", args.empty() ? "diag" : args[0].c_str());
        return 1;
    }
    geometry::StepGeometryAdapter adapter;
    std::vector<geometry::ModelHandle> handles;
    for (size_t i = 1; i < args.size(); ++i) {
        try {
            handles.push_back(adapter.LoadModel(args[i]));
        } catch (const std::exception& e) {
            std::fprintf(stderr, "LoadModel failed: %s\n", e.what());
            return 1;
        }
    }
    const geometry::ModelHandle source = handles.front();
    const std::vector<geometry::ModelHandle> others(handles.begin() + 1, handles.end());
    const geometry::BoundingBox box = adapter.GetBoundingBox(source);

    std::printf("기준 모델: %s\n", args[1].c_str());
    std::printf("bbox: (%.2f, %.2f, %.2f) ~ (%.2f, %.2f, %.2f), 매칭 반경 %.2fmm\n", box.min.x, box.min.y, box.min.z,
                box.max.x, box.max.y, box.max.z, rule::MatchRadiusMm(box));
    for (size_t i = 0; i < others.size(); ++i) {
        std::printf("모델 #%zu: %s\n", i + 1, args[i + 2].c_str());
    }

    const auto report = rule::RunMatchingDiagnostics(adapter, source, others);
    for (const auto& section : report.sections) {
        std::printf("\n%s - 후보 %zu개 중 %zu개 검사\n", section.label.c_str(), section.candidateCount, section.sampled);
        std::printf("  자기 매칭     정답 %5d | 실패 %5d | 오답 %5d\n", section.self.correct, section.self.failed,
                    section.self.wrong);
        Print(section.self);
        for (size_t i = 0; i < section.others.size(); ++i) {
            std::printf("  -> 모델 #%zu  값을 냄 %d | 실패 %d\n", i + 1, section.others[i].correct,
                        section.others[i].failed);
            Print(section.others[i]);
        }
    }
    std::printf("\n소요 시간 %.1f초\n", report.seconds);
    return 0;
}

} // namespace

// § 유니코드 경로(2026-11-04) - main(char**)의 인수는 Windows 기본 코드페이지(한국어 Windows는 CP949)로
// 들어와서, "●"는 깨지고 한글 경로는 UTF-8이 아니게 된다. StepGeometryAdapter::LoadModel은 UTF-8 경로를
// 기대하므로(앱은 Qt가 UTF-8로 넘김) wmain으로 UTF-16 인수를 받아 UTF-8로 바꿔 넘긴다.
#ifdef _WIN32
int wmain(int argc, wchar_t** wargv) {
    std::vector<std::string> args;
    for (int i = 0; i < argc; ++i) {
        const int size = WideCharToMultiByte(CP_UTF8, 0, wargv[i], -1, nullptr, 0, nullptr, nullptr);
        std::string utf8(size > 0 ? static_cast<size_t>(size - 1) : 0, '\0');
        if (size > 1) {
            WideCharToMultiByte(CP_UTF8, 0, wargv[i], -1, utf8.data(), size, nullptr, nullptr);
        }
        args.push_back(std::move(utf8));
    }
    return Run(args);
}
#else
int main(int argc, char** argv) {
    return Run(std::vector<std::string>(argv, argv + argc));
}
#endif
