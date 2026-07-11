#pragma once

#include "rule/Rule.h"

#include <vector>

namespace rule {

// docs/rule_catalog.md 001~003(point 기반 3종)을 코드로 옮긴 내장 규칙 목록.
// Phase4 데모/테스트용 — 실제 프로젝트별 규칙은 database::Database에 저장해서 관리한다.
std::vector<Rule> BuiltInPointRules();

} // namespace rule
