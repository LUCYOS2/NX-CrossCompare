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

    int SaveRule(int projectId, const rule::Rule& r);
    rule::Rule LoadRule(int ruleId);

    void SavePoint(int ruleId, const rule::PointSample& point);
    std::vector<rule::PointSample> LoadPoints(int ruleId);

private:
    sqlite3* db_ = nullptr;
};

} // namespace database
