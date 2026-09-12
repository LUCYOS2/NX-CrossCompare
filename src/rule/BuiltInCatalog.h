#pragma once

#include "rule/Rule.h"

#include <vector>

namespace rule {

// docs/rule_catalog.md 001~005 전체를 코드로 옮긴 내장 규칙 목록.
// Phase4/4b 데모/테스트용 — 실제 프로젝트별 규칙은 database::Database에 저장해서 관리한다.
std::vector<Rule> BuiltInRules();

// § 기본값 규칙 편집(2026-09-12) - "규칙 관리 목록에 전장 사이즈 기본값을 넣어두고 거기서
// 공차를 수정할 수 있게 해달라"는 요청. BuiltInRules()는 DB에 없는 순수 하드코딩값이라
// 그 자체로는 수정/저장이 안 된다 - 사용자가 공차를 바꿔 저장하면 같은 이름으로 DB에
// 사용자 규칙이 새로 생기는데, 이때 내장 기본값과 사용자 저장본이 이름이 겹쳐서 화면에
// 중복으로 뜨면 안 된다(사용자 확인: "사용자 저장본이 기본값을 덮어쓰기"). 이 함수는
// BuiltInRules() 목록을 기준으로, 같은 이름의 사용자 규칙이 있으면 그걸로 교체하고
// (내장값은 감춰짐), 이름이 겹치지 않는 사용자 규칙은 뒤에 그대로 덧붙인다. MainWindow와
// RuleEditorDialog가 규칙 목록을 만들 때 공통으로 쓴다(BuiltInRules()+userRules를 그냥
// insert하던 예전 방식은 이름 중복을 그대로 방치했었다).
std::vector<Rule> MergeWithBuiltIns(const std::vector<Rule>& userRules);

} // namespace rule
