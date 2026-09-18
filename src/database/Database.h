#pragma once

#include "geometry/IGeometryAdapter.h"
#include "rule/Rule.h"

#include <optional>
#include <string>
#include <vector>

struct sqlite3;

namespace database {

// § 형상 프리셋(2026-09-18) - shape_presets 테이블 한 행의 요약(목록 UI용 - 지문 자체는
// 큰 값이라 목록에는 안 담고, 필요할 때 LoadShapePresetDescriptor로 따로 불러온다).
struct ShapePresetSummary {
    int id = 0;
    std::string name;
    std::string imagePath;
    std::optional<std::string> dimFilterKind;   // 'diameter' | 'height' | 'width'
    std::optional<double> dimFilterValueMm;
};

// 프로젝트/규칙/포인트 저장·불러오기. 개발계획_v2.md §7 Rule Schema를 순수 관계형
// 테이블로 매핑한다 (JSON 컬럼 없이 rule_anchors/rule_reference_frames/rule_selectors
// 자식 테이블로 분리 — 외부 JSON 라이브러리 의존성 없이 §7 스키마의 배열 필드를 표현).
class Database {
public:
    explicit Database(const std::string& path);
    ~Database();

    Database(const Database&) = delete;
    Database& operator=(const Database&) = delete;

    void EnsureSchema();

    int CreateProject(const std::string& name);
    int FindOrCreateProject(const std::string& name);

    int SaveRule(int projectId, const rule::Rule& r);
    rule::Rule LoadRule(int ruleId);
    std::vector<rule::Rule> LoadRulesForProject(int projectId);
    // § 규칙 관리 통합(포인트 그룹 폐기, 2026-09-08) - 저장된 사용자 규칙을 목록에서
    // 클릭해 불러와 수정/삭제할 수 있어야 해서 추가. SaveRule은 항상 새 id로 INSERT하는
    // "생성" 전용이고, 이 둘은 이미 있는 규칙을 대상으로 한다.
    void UpdateRule(int ruleId, const rule::Rule& r);
    void DeleteRule(int ruleId);

    void SavePoint(int ruleId, const rule::PointSample& point);
    std::vector<rule::PointSample> LoadPoints(int ruleId);

    // § 형상 프리셋(2026-09-18) - 이름은 프로젝트 내에서 유일해야 한다(RuleEditorDialog가
    // "검색으로 추가"에서 이름으로 골라 anchor_type을 "preset:<이름>"으로 저장하므로).
    // LoadRule/LoadRulesForProject가 anchor_type이 "preset:"으로 시작하는 걸 보면 자동으로
    // 이 테이블에서 지문을 찾아 rule::Anchor.patchDescriptor를 채운다.
    int SaveShapePreset(
        int projectId, const std::string& name, const geometry::FeaturePatchDescriptor& descriptor,
        const std::string& imagePath, const std::optional<std::string>& dimFilterKind,
        const std::optional<double>& dimFilterValueMm);
    std::vector<ShapePresetSummary> LoadShapePresetsForProject(int projectId);
    geometry::FeaturePatchDescriptor LoadShapePresetDescriptor(int presetId);
    void DeleteShapePreset(int presetId);

private:
    sqlite3* db_ = nullptr;
};

} // namespace database
