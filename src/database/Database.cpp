#include "database/Database.h"

#include <sqlite3.h>

#include <stdexcept>
#include <utility>

namespace database {

namespace {

// 준비된 statement를 스코프 종료 시 자동으로 finalize하는 RAII 래퍼.
class Stmt {
public:
    Stmt(sqlite3* db, const std::string& sql) : db_(db) {
        if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt_, nullptr) != SQLITE_OK) {
            throw std::runtime_error(std::string("prepare failed: ") + sqlite3_errmsg(db_));
        }
    }
    ~Stmt() { sqlite3_finalize(stmt_); }

    Stmt(const Stmt&) = delete;
    Stmt& operator=(const Stmt&) = delete;

    void BindInt(int index, int value) { sqlite3_bind_int(stmt_, index, value); }
    void BindDouble(int index, double value) { sqlite3_bind_double(stmt_, index, value); }
    void BindText(int index, const std::string& value) {
        sqlite3_bind_text(stmt_, index, value.c_str(), -1, SQLITE_TRANSIENT);
    }
    void BindNull(int index) { sqlite3_bind_null(stmt_, index); }

    bool Step() {
        const int rc = sqlite3_step(stmt_);
        if (rc == SQLITE_ROW) return true;
        if (rc == SQLITE_DONE) return false;
        throw std::runtime_error(std::string("step failed: ") + sqlite3_errmsg(db_));
    }

    int ColumnInt(int index) const { return sqlite3_column_int(stmt_, index); }
    double ColumnDouble(int index) const { return sqlite3_column_double(stmt_, index); }
    std::string ColumnText(int index) const {
        const unsigned char* text = sqlite3_column_text(stmt_, index);
        return text ? reinterpret_cast<const char*>(text) : std::string();
    }
    bool IsNull(int index) const { return sqlite3_column_type(stmt_, index) == SQLITE_NULL; }

    sqlite3_int64 LastInsertRowId() const { return sqlite3_last_insert_rowid(db_); }

private:
    sqlite3* db_;
    sqlite3_stmt* stmt_ = nullptr;
};

constexpr const char* kSchemaSql = R"SQL(
CREATE TABLE IF NOT EXISTS projects (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    name TEXT NOT NULL,
    created_at TEXT NOT NULL DEFAULT (datetime('now'))
);

CREATE TABLE IF NOT EXISTS rules (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    project_id INTEGER NOT NULL REFERENCES projects(id),
    name TEXT NOT NULL,
    measurement_type TEXT NOT NULL,
    projection TEXT NOT NULL,
    tolerance_plus_mm REAL NOT NULL,
    tolerance_minus_mm REAL NOT NULL,
    plane_type TEXT,      -- point_to_plane 전용, 그 외 측정 타입은 NULL
    plane_part_name TEXT
);

CREATE TABLE IF NOT EXISTS rule_anchors (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    rule_id INTEGER NOT NULL REFERENCES rules(id),
    role TEXT NOT NULL,
    anchor_type TEXT NOT NULL,
    part_name TEXT NOT NULL,
    param_key TEXT,
    param_value REAL
);

CREATE TABLE IF NOT EXISTS rule_reference_frames (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    rule_id INTEGER NOT NULL REFERENCES rules(id),
    priority INTEGER NOT NULL,
    frame_name TEXT NOT NULL
);

CREATE TABLE IF NOT EXISTS rule_selectors (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    rule_id INTEGER NOT NULL REFERENCES rules(id),
    priority INTEGER NOT NULL,
    selector_name TEXT NOT NULL
);

CREATE TABLE IF NOT EXISTS points (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    rule_id INTEGER NOT NULL REFERENCES rules(id),
    inch INTEGER NOT NULL,
    role TEXT NOT NULL,
    x REAL NOT NULL,
    y REAL NOT NULL,
    z REAL NOT NULL
);
)SQL";

} // namespace

Database::Database(const std::string& path) {
    if (sqlite3_open(path.c_str(), &db_) != SQLITE_OK) {
        const std::string msg = sqlite3_errmsg(db_);
        sqlite3_close(db_);
        throw std::runtime_error("failed to open database: " + msg);
    }
    sqlite3_exec(db_, "PRAGMA foreign_keys = ON;", nullptr, nullptr, nullptr);
}

Database::~Database() {
    if (db_) {
        sqlite3_close(db_);
    }
}

void Database::EnsureSchema() {
    char* errMsg = nullptr;
    if (sqlite3_exec(db_, kSchemaSql, nullptr, nullptr, &errMsg) != SQLITE_OK) {
        const std::string msg = errMsg ? errMsg : "unknown error";
        sqlite3_free(errMsg);
        throw std::runtime_error("failed to create schema: " + msg);
    }

    // rules.image_path - § 이미지 캡쳐 연동(2026-09-09)에서 추가된 컬럼. 기존에 만들어진
    // DB 파일에는 없을 수 있어 CREATE TABLE IF NOT EXISTS만으로는 채워지지 않으므로
    // 별도 마이그레이션이 필요하다. 이미 컬럼이 있으면 "duplicate column" 에러가 나는데
    // 그건 정상 상황(이미 마이그레이션됨)이라 무시하고, 그 외 에러만 던진다.
    char* migrateErr = nullptr;
    if (sqlite3_exec(db_, "ALTER TABLE rules ADD COLUMN image_path TEXT;", nullptr, nullptr, &migrateErr) !=
        SQLITE_OK) {
        const std::string msg = migrateErr ? migrateErr : "unknown error";
        sqlite3_free(migrateErr);
        if (msg.find("duplicate column") == std::string::npos) {
            throw std::runtime_error("failed to migrate rules.image_path: " + msg);
        }
    }
}

int Database::CreateProject(const std::string& name) {
    Stmt stmt(db_, "INSERT INTO projects (name) VALUES (?);");
    stmt.BindText(1, name);
    stmt.Step();
    return static_cast<int>(stmt.LastInsertRowId());
}

int Database::FindOrCreateProject(const std::string& name) {
    {
        Stmt stmt(db_, "SELECT id FROM projects WHERE name = ?;");
        stmt.BindText(1, name);
        if (stmt.Step()) {
            return stmt.ColumnInt(0);
        }
    }
    return CreateProject(name);
}

int Database::SaveRule(int projectId, const rule::Rule& r) {
    int ruleId = 0;
    {
        Stmt stmt(db_,
            "INSERT INTO rules (project_id, name, measurement_type, projection, "
            "tolerance_plus_mm, tolerance_minus_mm, plane_type, plane_part_name, image_path) "
            "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?);");
        stmt.BindInt(1, projectId);
        stmt.BindText(2, r.name);
        stmt.BindText(3, ToString(r.measurementType));
        stmt.BindText(4, r.projection);
        stmt.BindDouble(5, r.tolerancePlusMm);
        stmt.BindDouble(6, r.toleranceMinusMm);
        if (r.referencePlane.has_value()) {
            stmt.BindText(7, r.referencePlane->planeType);
            stmt.BindText(8, r.referencePlane->partName);
        } else {
            stmt.BindNull(7);
            stmt.BindNull(8);
        }
        if (r.imagePath.has_value() && !r.imagePath->empty()) {
            stmt.BindText(9, *r.imagePath);
        } else {
            stmt.BindNull(9);
        }
        stmt.Step();
        ruleId = static_cast<int>(stmt.LastInsertRowId());
    }

    for (const auto& anchor : r.anchors) {
        Stmt stmt(db_,
            "INSERT INTO rule_anchors (rule_id, role, anchor_type, part_name, param_key, param_value) "
            "VALUES (?, ?, ?, ?, ?, ?);");
        stmt.BindInt(1, ruleId);
        stmt.BindText(2, anchor.role);
        stmt.BindText(3, anchor.anchorType);
        stmt.BindText(4, anchor.partName);
        if (anchor.paramKey.has_value()) {
            stmt.BindText(5, *anchor.paramKey);
            stmt.BindDouble(6, anchor.paramValue.value_or(0.0));
        } else {
            stmt.BindNull(5);
            stmt.BindNull(6);
        }
        stmt.Step();
    }

    for (size_t i = 0; i < r.referenceFrame.size(); ++i) {
        Stmt stmt(db_,
            "INSERT INTO rule_reference_frames (rule_id, priority, frame_name) VALUES (?, ?, ?);");
        stmt.BindInt(1, ruleId);
        stmt.BindInt(2, static_cast<int>(i));
        stmt.BindText(3, r.referenceFrame[i]);
        stmt.Step();
    }

    for (size_t i = 0; i < r.selector.size(); ++i) {
        Stmt stmt(db_,
            "INSERT INTO rule_selectors (rule_id, priority, selector_name) VALUES (?, ?, ?);");
        stmt.BindInt(1, ruleId);
        stmt.BindInt(2, static_cast<int>(i));
        stmt.BindText(3, r.selector[i]);
        stmt.Step();
    }

    return ruleId;
}

rule::Rule Database::LoadRule(int ruleId) {
    rule::Rule r;
    r.id = ruleId;

    {
        Stmt stmt(db_,
            "SELECT name, measurement_type, projection, tolerance_plus_mm, tolerance_minus_mm, "
            "plane_type, plane_part_name, image_path FROM rules WHERE id = ?;");
        stmt.BindInt(1, ruleId);
        if (!stmt.Step()) {
            throw std::runtime_error("rule not found: id=" + std::to_string(ruleId));
        }
        r.name = stmt.ColumnText(0);
        r.measurementType = rule::MeasurementTypeFromString(stmt.ColumnText(1));
        r.projection = stmt.ColumnText(2);
        r.tolerancePlusMm = stmt.ColumnDouble(3);
        r.toleranceMinusMm = stmt.ColumnDouble(4);
        if (!stmt.IsNull(5)) {
            rule::PlaneRef planeRef;
            planeRef.planeType = stmt.ColumnText(5);
            planeRef.partName = stmt.ColumnText(6);
            r.referencePlane = std::move(planeRef);
        }
        if (!stmt.IsNull(7)) {
            r.imagePath = stmt.ColumnText(7);
        }
    }

    {
        Stmt stmt(db_,
            "SELECT role, anchor_type, part_name, param_key, param_value "
            "FROM rule_anchors WHERE rule_id = ? ORDER BY id;");
        stmt.BindInt(1, ruleId);
        while (stmt.Step()) {
            rule::Anchor anchor;
            anchor.role = stmt.ColumnText(0);
            anchor.anchorType = stmt.ColumnText(1);
            anchor.partName = stmt.ColumnText(2);
            if (!stmt.IsNull(3)) {
                anchor.paramKey = stmt.ColumnText(3);
                anchor.paramValue = stmt.ColumnDouble(4);
            }
            r.anchors.push_back(std::move(anchor));
        }
    }

    {
        Stmt stmt(db_,
            "SELECT frame_name FROM rule_reference_frames WHERE rule_id = ? ORDER BY priority;");
        stmt.BindInt(1, ruleId);
        while (stmt.Step()) {
            r.referenceFrame.push_back(stmt.ColumnText(0));
        }
    }

    {
        Stmt stmt(db_,
            "SELECT selector_name FROM rule_selectors WHERE rule_id = ? ORDER BY priority;");
        stmt.BindInt(1, ruleId);
        while (stmt.Step()) {
            r.selector.push_back(stmt.ColumnText(0));
        }
    }

    return r;
}

std::vector<rule::Rule> Database::LoadRulesForProject(int projectId) {
    std::vector<int> ruleIds;
    {
        Stmt stmt(db_, "SELECT id FROM rules WHERE project_id = ? ORDER BY id;");
        stmt.BindInt(1, projectId);
        while (stmt.Step()) {
            ruleIds.push_back(stmt.ColumnInt(0));
        }
    }

    std::vector<rule::Rule> rules;
    rules.reserve(ruleIds.size());
    for (int id : ruleIds) {
        rules.push_back(LoadRule(id));
    }
    return rules;
}

void Database::SavePoint(int ruleId, const rule::PointSample& point) {
    Stmt stmt(db_, "INSERT INTO points (rule_id, inch, role, x, y, z) VALUES (?, ?, ?, ?, ?, ?);");
    stmt.BindInt(1, ruleId);
    stmt.BindInt(2, point.inch);
    stmt.BindText(3, point.role);
    stmt.BindDouble(4, point.x);
    stmt.BindDouble(5, point.y);
    stmt.BindDouble(6, point.z);
    stmt.Step();
}

std::vector<rule::PointSample> Database::LoadPoints(int ruleId) {
    std::vector<rule::PointSample> points;
    Stmt stmt(db_, "SELECT inch, role, x, y, z FROM points WHERE rule_id = ? ORDER BY inch, role;");
    stmt.BindInt(1, ruleId);
    while (stmt.Step()) {
        rule::PointSample p;
        p.inch = stmt.ColumnInt(0);
        p.role = stmt.ColumnText(1);
        p.x = stmt.ColumnDouble(2);
        p.y = stmt.ColumnDouble(3);
        p.z = stmt.ColumnDouble(4);
        points.push_back(p);
    }
    return points;
}

void Database::UpdateRule(int ruleId, const rule::Rule& r) {
    Stmt stmt(db_,
        "UPDATE rules SET name = ?, measurement_type = ?, projection = ?, "
        "tolerance_plus_mm = ?, tolerance_minus_mm = ?, plane_type = ?, plane_part_name = ?, image_path = ? "
        "WHERE id = ?;");
    stmt.BindText(1, r.name);
    stmt.BindText(2, ToString(r.measurementType));
    stmt.BindText(3, r.projection);
    stmt.BindDouble(4, r.tolerancePlusMm);
    stmt.BindDouble(5, r.toleranceMinusMm);
    if (r.referencePlane.has_value()) {
        stmt.BindText(6, r.referencePlane->planeType);
        stmt.BindText(7, r.referencePlane->partName);
    } else {
        stmt.BindNull(6);
        stmt.BindNull(7);
    }
    if (r.imagePath.has_value() && !r.imagePath->empty()) {
        stmt.BindText(8, *r.imagePath);
    } else {
        stmt.BindNull(8);
    }
    stmt.BindInt(9, ruleId);
    stmt.Step();

    // 자식 테이블(anchor/reference_frame/selector)은 개수·순서가 통째로 바뀔 수 있어서
    // "지우고 다시 채우기"가 부분 UPDATE보다 훨씬 단순하고 실수가 적다 - SaveRule의
    // INSERT 로직을 그대로 재사용.
    {
        Stmt del(db_, "DELETE FROM rule_anchors WHERE rule_id = ?;");
        del.BindInt(1, ruleId);
        del.Step();
    }
    for (const auto& anchor : r.anchors) {
        Stmt ins(db_,
            "INSERT INTO rule_anchors (rule_id, role, anchor_type, part_name, param_key, param_value) "
            "VALUES (?, ?, ?, ?, ?, ?);");
        ins.BindInt(1, ruleId);
        ins.BindText(2, anchor.role);
        ins.BindText(3, anchor.anchorType);
        ins.BindText(4, anchor.partName);
        if (anchor.paramKey.has_value()) {
            ins.BindText(5, *anchor.paramKey);
            ins.BindDouble(6, anchor.paramValue.value_or(0.0));
        } else {
            ins.BindNull(5);
            ins.BindNull(6);
        }
        ins.Step();
    }

    {
        Stmt del(db_, "DELETE FROM rule_reference_frames WHERE rule_id = ?;");
        del.BindInt(1, ruleId);
        del.Step();
    }
    for (size_t i = 0; i < r.referenceFrame.size(); ++i) {
        Stmt ins(db_,
            "INSERT INTO rule_reference_frames (rule_id, priority, frame_name) VALUES (?, ?, ?);");
        ins.BindInt(1, ruleId);
        ins.BindInt(2, static_cast<int>(i));
        ins.BindText(3, r.referenceFrame[i]);
        ins.Step();
    }

    {
        Stmt del(db_, "DELETE FROM rule_selectors WHERE rule_id = ?;");
        del.BindInt(1, ruleId);
        del.Step();
    }
    for (size_t i = 0; i < r.selector.size(); ++i) {
        Stmt ins(db_,
            "INSERT INTO rule_selectors (rule_id, priority, selector_name) VALUES (?, ?, ?);");
        ins.BindInt(1, ruleId);
        ins.BindInt(2, static_cast<int>(i));
        ins.BindText(3, r.selector[i]);
        ins.Step();
    }
}

void Database::DeleteRule(int ruleId) {
    // FK 체크가 켜져 있지만(PRAGMA foreign_keys=ON) rule_anchors 등은 ON DELETE 규칙이
    // 없으므로 자식부터 직접 지운다.
    for (const char* table : {"rule_anchors", "rule_reference_frames", "rule_selectors", "points"}) {
        Stmt stmt(db_, std::string("DELETE FROM ") + table + " WHERE rule_id = ?;");
        stmt.BindInt(1, ruleId);
        stmt.Step();
    }
    Stmt stmt(db_, "DELETE FROM rules WHERE id = ?;");
    stmt.BindInt(1, ruleId);
    stmt.Step();
}

} // namespace database
