#pragma once

#include "rule/Rule.h"

#include <string>
#include <vector>

struct sqlite3;

namespace database {

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

private:
    sqlite3* db_ = nullptr;
};

} // namespace database
